# CanvasBio Fingerprint Driver 바이너리 분석 결과

## 1. 드라이버 파일 구성

### 04E8:730B 전용 드라이버 (v2.4.5.882, 2023-07-25)

| 파일 | 크기 | 용도 |
|------|------|------|
| `CanvasBioFingerprintDriver.dll` | 163KB | **UMDF 센서 드라이버** (USB 통신, 센서 제어) |
| `CanvasBioFingerprintEngine.dll` | 677KB | **WinBio 엔진 어댑터** (지문 매칭, 템플릿 관리) |
| `CanvasBioFingerprint.inf` | 5.5KB | 드라이버 설정 |

### 최신 VID_2DF0 드라이버 (v5.1.155.6134, 2024-10-09)

| 파일 | 크기 | 용도 |
|------|------|------|
| `CanvasBioFingerprintDriver.dll` | 1MB | **UMDF MOC 드라이버** (Realtek 기반) |
| `CanvasBioFingerprintAdapter.dll` | 489KB | **WinBio 통합 어댑터** (Sensor+Engine+Storage) |

> **중요**: 2DF0:0007 버전은 MOC(Match-on-Chip) + SDCP + TLS 사용.
> 04E8:730B 버전은 **호스트 매칭** (SensorMode=1, Basic) — 더 단순한 프로토콜!

## 2. 소프트웨어 아키텍처

### 빌드 환경
```
프로젝트 경로: C:\workspace\btp_sdk\WBF_UMDF2_TEST\umdf2_fx2\driver\
PDB 경로:      ...\x64\Release\osrusbfx2um\CanvasBioFingerprintDriver.pdb
엔진 경로:     ...\adapters\engine_adapter\x64\Release\
기반 프레임워크: Microsoft WDF UMDF 2.15 + osrusbfx2 (MS USB 샘플 기반!)
```

### 계층 구조
```
┌─────────────────────────────────────────────┐
│  Windows Biometric Framework (WBF/WinBio)   │
├─────────────────────────────────────────────┤
│  CanvasBioFingerprintEngine.dll             │
│  ├─ EngineAdapterVerifyFeatureSet()         │
│  ├─ EngineAdapterCreateEnrollment()         │
│  ├─ EngineAdapterIdentifyFeatureSet()       │
│  ├─ ExtractRawImg() / ExtractInfoFromSample()│
│  └─ 호스트 측 지문 매칭 (BCrypt 사용)       │
├─────────────────────────────────────────────┤
│  CanvasBioFingerprintDriver.dll (UMDF)      │
│  ├─ FxEvtDevicePrepareHardware()            │
│  │   └─ PrepareHardwareForUSBDriver()       │
│  │       ├─ Vendor_CheckPipe()              │
│  │       ├─ ConfigCypressPower()            │
│  │       ├─ Vendor_InitializeSensor()       │
│  │       │   ├─ ReadHardwareID()            │
│  │       │   ├─ SoftResetSensor()           │
│  │       │   ├─ cb_power_on()              │
│  │       │   ├─ InitParam()                │
│  │       │   └─ InitializeWBF()            │
│  │       └─ SetFingerDownAdcValues()        │
│  ├─ CommanderThread()                       │
│  │   ├─ SetMode(SENSOR_MODE_FINGER_DOWN)    │
│  │   ├─ WaitFingerDown[CB/IX]()             │
│  │   ├─ SetMode(SENSOR_MODE_CAPTURE)        │
│  │   ├─ CaptureImage[CB/IX]()              │
│  │   └─ ReadImage()                         │
│  └─ cfs_* (센서 SPI 통신 레이어)            │
│      ├─ cfs_writeRegister(reg, val)         │
│      ├─ cfs_write2ByteRegister(reg, val)    │
│      ├─ cfs_writeRegisterEx(reg, data, len) │
│      ├─ cfs_readRegister(reg)               │
│      └─ cfs_readImage()                     │
├─────────────────────────────────────────────┤
│  WinUSB (NativeUSB UMDF Dispatcher)         │
│  ├─ Bulk OUT (EP 0x01) → SPI 명령 전달      │
│  ├─ Bulk IN  (EP 0x82) → SPI 응답 수신      │
│  └─ Interrupt IN (EP 0x83) → IRQ 이벤트     │
├─────────────────────────────────────────────┤
│  Cypress PSoC MCU ("CYUS")                  │
│  ├─ USB ↔ SPI 브릿지                        │
│  ├─ 벤더 Control Transfer 레지스터          │
│  └─ IRQ 라우팅                              │
├─────────────────────────────────────────────┤
│  CanvasBio 센서 IC (CB2000)                 │
│  ├─ SPI 레지스터 인터페이스                  │
│  ├─ 이미지 캡처 (112x96 또는 80x64)         │
│  ├─ ADC / SNR 설정                          │
│  └─ IRQ (fingerdown 감지)                   │
└─────────────────────────────────────────────┘
```

## 3. USB 통신 프로토콜 (핵심!)

### 3.1 Cypress MCU는 USB↔SPI 브릿지

Bulk 엔드포인트를 통해 **SPI 명령을 센서에 전달**하는 구조:

```
Host → Bulk OUT → Cypress MCU → SPI → CanvasBio Sensor
Host ← Bulk IN  ← Cypress MCU ← SPI ← CanvasBio Sensor
Host ← Int  IN  ← Cypress MCU ← IRQ ← CanvasBio Sensor
```

### 3.2 cfs_ 레이어 (SPI over USB Bulk)

핵심 통신 함수:
- `cfs_writeRegister(reg, val)` — 센서 SPI 레지스터 쓰기
- `cfs_write2ByteRegister(reg, val)` — 2바이트 레지스터 쓰기
- `cfs_writeRegisterEx(reg, data, len)` — 확장 레지스터 쓰기
- `cfs_readRegister(reg)` — 센서 SPI 레지스터 읽기
- `cfs_readImage()` — 이미지 데이터 읽기

### 3.3 센서 SPI 레지스터 (CB_ 접두사)

발견된 레지스터:
- `CB_REG_BIAS_SET` — 바이어스 전압 설정
- `CB_REG_CELL_AMP_SET` — 셀 앰프 설정
- `CB_CMD_SCAN` — 스캔 명령
- `CB_MODE_CAPTURE` — 캡처 모드

### 3.4 Bulk 패킷 구조 (추정)

`cfs_writeRegister`가 USB Bulk를 통해 SPI 명령을 보내는 방식:

```
Bulk OUT 패킷 (추정):
[CMD_TYPE: 1B] [REG_ADDR: 1B] [DATA: 1-2B]

예: cfs_writeRegister(0x10, 0x01) → Bulk OUT: [WRITE_CMD] [0x10] [0x01]
    cfs_readRegister(0x10)       → Bulk OUT: [READ_CMD] [0x10]
                                   Bulk IN:  [0x10] [VALUE]
    cfs_readImage()              → Bulk OUT: [READ_IMG_CMD]
                                   Bulk IN:  [IMAGE_DATA...]
```

## 4. 초기화 시퀀스 (추정)

DLL의 함수 호출 순서로 추론:

```
1. PrepareHardwareForUSBDriver()
   ├─ WinUSB 핸들 열기
   ├─ Vendor_CheckPipe() — EP 확인
   └─ ConfigCypressPower() — Cypress MCU 전원 설정
      → Control Transfer 0xC0으로 레지스터 확인?

2. Vendor_InitializeSensor()
   ├─ ReadHardwareID() — 하드웨어 ID 확인
   │   → "Hardware id : 0x%02x"
   │   → "Hardware id mismatch" 시 에러
   ├─ SoftResetSensor() — 소프트 리셋
   ├─ cb_power_on() — 센서 전원 켜기
   ├─ InitParam() — 파라미터 초기화
   │   ├─ CB_SetSNRAdcValue() — SNR ADC 설정
   │   ├─ SetAdcValue() — ADC 값 설정
   │   └─ SetFingerDownAdcValues() — 손가락 감지 임계값
   └─ InitializeWBF() — WBF 프레임워크 연결

3. SetMode(SENSOR_MODE_FINGER_DOWN) — 손가락 대기 모드
   └─ OnIrqSignal() — "occurred fingerdown irq"

4. SetMode(SENSOR_MODE_CAPTURE) — 캡처 모드
   └─ CaptureImageCB() / CaptureImageIX()
      └─ cfs_readImage() → ReadImage()
```

## 5. 센서 사양

| 항목 | 값 |
|------|-----|
| 센서 모델 | CanvasBio CB2000 |
| 센서 타입 | Capacitive area sensor |
| 해상도 변형 | 112×96 @ 340dpi 또는 80×64 @ 508dpi |
| 센서 크기 (0xDD) | 100×20 (레지스터 보고) |
| 통신 | SPI (USB-SPI 브릿지 via Cypress) |
| 매칭 방식 | **호스트 매칭** (Basic SensorMode=1) |
| 펌웨어 | v1.0.3, Build 2211 |

## 6. 다음 단계: Linux 드라이버 개발

### 6.1 Bulk 패킷 구조 확인 (Windows VM 캡처)

**아직 필요한 핵심 정보:**
1. `cfs_writeRegister()` → Bulk OUT 패킷 형식
2. `cfs_readRegister()` → Bulk OUT/IN 패킷 형식
3. `cfs_readImage()` → 이미지 전송 프로토콜
4. 초기화 시퀀스의 정확한 레지스터 값

### 6.2 Windows VM 캡처 시 집중할 것

```
캡처 시나리오:
1. 드라이버 로드 → PrepareHardware → InitializeSensor 패킷
2. 손가락 대기 → SetMode(FINGER_DOWN) → IRQ 패턴
3. 이미지 캡처 → SetMode(CAPTURE) → ReadImage 데이터
```

### 6.3 libfprint 드라이버 구조 (계획)

```
libfprint/drivers/canvasbio/
├─ fpi-device-canvasbio.c    # 메인 드라이버
├─ canvasbio.h               # 상수 정의
├─ canvasbio-usb.c           # cfs_* USB-SPI 통신 레이어
└─ canvasbio-sensor.c        # CB_ 센서 제어
```

- USB-SPI 브릿지 프로토콜은 Cypress MCU 전용
- 센서 레지스터는 SPI를 통해 접근
- 이미지는 호스트에서 처리 (Basic 모드)
- libfprint의 기존 이미지 처리 파이프라인 활용 가능
