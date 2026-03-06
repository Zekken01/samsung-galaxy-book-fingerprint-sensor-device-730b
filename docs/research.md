# Samsung Galaxy Book 지문인식 센서 (04e8:730b) Linux 드라이버 심층 조사 보고서

## 1. 하드웨어 식별 정보

| 항목 | 값 |
|------|-----|
| USB Vendor ID | 0x04e8 (Samsung Electronics) |
| USB Product ID | 0x730b |
| 제품명 | Fingerprint Sensor Device - 730B |
| USB 컨트롤러 | Cypress (현 Infineon) - "CYUS" 매직 확인 |
| 센서 제조사 | CanvasBio (현 LaserSemicon Co., Ltd) |
| 인터페이스 클래스 | 0xFF (Vendor Specific), SubClass 0x02 |
| USB 버전 | 2.0 (Full Speed, 64 byte packets) |
| 아키텍처 | Match-on-Chip (MOC) |

### USB 엔드포인트 구성

| 엔드포인트 | 방향 | 전송 타입 | 용도 (추정) |
|------------|------|-----------|-------------|
| EP 1 (0x01) | OUT | Bulk | 호스트 → 센서 명령 전송 |
| EP 2 (0x82) | IN | Bulk | 센서 → 호스트 응답/데이터 수신 |
| EP 3 (0x83) | IN | Interrupt | 이벤트/상태 알림 |

## 2. 프로토콜 분석 결과 (Phase 1)

### 2.1 벤더 Control Transfer 레지스터 맵

**발견된 읽기 가능 레지스터 (reqType=0xC0, Device-to-Host, Vendor):**

| bRequest | 크기 | 값 (Hex) | 해석 |
|----------|------|----------|------|
| 0xB0 | 8B | `01 00 03 00 4E 00 00 00` | **버전 정보**: v1.0.3, 'N'=Normal mode |
| 0xB1 | 4B | `A3 08 00 00` | **펌웨어 빌드**: 2211 (0x8A3) |
| 0xB4 | 8B | `0B D3 4A 06 B0 16 05 00` | **장치 고유 ID** (시리얼 대용) |
| 0xBB | 64B | `00 10 00 20 9B 00 00 10...` | **캘리브레이션 데이터** (센서 설정) |
| 0xBD | 4B | `43 59 55 53` | **매직: "CYUS"** (Cypress USB System) |
| 0xC0 | 16B | `40 0D 03 00 08 00 00 01...` | **설정 레지스터 A** |
| 0xC2 | 16B | (0xC0과 동일) | **설정 레지스터 B** |
| 0xC4 | 16B | (0xC0과 동일) | **설정 레지스터 C** |
| 0xC8 | 3B | `20 00 00` | **버퍼 크기**: 0x20 = 32 |
| 0xCC | 4B | `01 00 00 00` / `00 00 00 00` | **Ready 플래그** (1=ready, 0=not ready) |
| 0xD4 | 1B | `02` | **장치 모드** (2=idle/대기) |
| 0xDA | 2B | `00 01` | **상태** (big-endian: 1) |
| 0xDD | 7B | `00 00 00 00 64 14 00` | **센서 규격**: 100(0x64) x 20(0x14) |
| 0xDE | 16B | `00 00 00 00 64 14 00 01 01 01...` | **확장 규격** (0xDD + 추가 설정) |

### 2.2 Bulk 엔드포인트 동작

**장치 초기화 전 동작 (에코 모드):**

| 전송 크기 | 응답 크기 | 응답 내용 | 분석 |
|-----------|-----------|-----------|------|
| 1B | 1B | 0x00 | 동일 길이 제로 응답 |
| 2B | 2B | 0x0000 | 동일 길이 제로 응답 |
| 3B | 3B | 0x000000 | 동일 길이 제로 응답 |
| 8B | **4B** | 0x00000000 | **절반 길이!** |
| 64B | (크래시) | - | 장치 펌웨어 크래시 |

**핵심 발견:**
- 1-3B 전송: 응답 길이 = 전송 길이
- 4B+ 전송: 응답 길이 = 4B 고정 (최대 응답 크기 4B)
- 응답 내용: 항상 0x00 (데이터 불포함)
- 64B 이상 또는 빠른 연속 전송: **장치 펌웨어 크래시 유발**

### 2.3 Control WRITE (0x40) 동작

- **모든 벤더 WRITE 명령이 거부됨** (reqType=0x40, 0x41 모두)
- 0x00-0xFF 범위 전수 스캔: 응답 없음
- wValue/wIndex 변형: 효과 없음
- **결론: 장치 초기화는 Bulk EP를 통해서만 가능**

### 2.4 인터럽트 엔드포인트

- 초기화 전: 이벤트 발생 없음
- 손가락 접촉: 반응 없음
- **결론: 초기화 시퀀스 없이는 센서가 비활성 상태**

### 2.5 장치 안정성

**크래시 트리거 조건:**
1. ~~256바이트 단일 바이트 전수 스캔~~ → 크래시 확인
2. ~~64바이트 Bulk 전송~~ → 크래시 확인
3. ~~빠른 연속 Control WRITE 스캔~~ → 크래시 추정
4. ~~프로세스 kill 중 USB 세션 비정상 종료~~ → 크래시 확인

**복구 방법:**
- USB Reset: **불가** (error -71 지속)
- xHCI 컨트롤러 Rebind: **불가**
- 소프트 리부팅: **불가** (30초 전원차단 필요)
- **완전 전원 차단 (30초+)**: 유일한 복구 방법

## 3. 프로토콜 아키텍처 분석

### 3.1 통신 계층

```
┌─────────────────────────────────────────────┐
│           Host (Linux/Windows)              │
├─────────────────────────────────────────────┤
│  Control Transfer (EP0)                     │
│  ├─ 표준 USB 명령 (GET_STATUS 등)           │
│  └─ 벤더 READ (0xC0): 레지스터 읽기        │
│     - 장치 정보, 상태, 설정 조회            │
│     - WRITE (0x40)는 미지원                 │
├─────────────────────────────────────────────┤
│  Bulk Transfer (EP1 OUT → EP2 IN)           │
│  ├─ 초기화 시퀀스 (Windows 드라이버 필요)   │
│  ├─ 지문 등록/인증/삭제 명령               │
│  └─ 암호화된 통신 (TLS-PSK 가능)           │
├─────────────────────────────────────────────┤
│  Interrupt Transfer (EP3 IN)                │
│  └─ 비동기 이벤트 (손가락 감지 등)          │
│     - 초기화 후에만 활성화                  │
└─────────────────────────────────────────────┘
         │
         │ USB 2.0 Full Speed (64B packets)
         │
┌─────────────────────────────────────────────┐
│     Cypress PSoC MCU ("CYUS")               │
│  ├─ USB 통신 관리                           │
│  ├─ 벤더 레지스터 (0xB0-0xDE)              │
│  └─ Bulk 데이터 라우팅                     │
├─────────────────────────────────────────────┤
│     CanvasBio 센서 모듈                     │
│  ├─ 지문 이미지 캡처 (100x20?)             │
│  ├─ 템플릿 저장 (on-chip)                  │
│  └─ 매칭 엔진 (MOC)                        │
└─────────────────────────────────────────────┘
```

### 3.2 0xCC (Ready 플래그) 상태 변화 관찰

- **첫 프로브 시**: 0xCC = `01 00 00 00` (ready)
- **Bulk 에코 테스트 후**: 0xCC = `00 00 00 00` (not ready)
- **이는 Bulk 전송이 장치 상태에 영향을 줌을 의미**
- **Bulk 명령이 처리되고 있으나, 올바른 프로토콜이 아니어서 ready→not ready 전환**

## 4. 다음 단계: Windows 드라이버 분석

### 4.1 필요한 이유

장치의 Bulk 초기화 프로토콜을 알아내는 유일한 방법은:
1. **Windows에서 실제 드라이버 동작을 USB 캡처**하거나
2. **CanvasBio UMDF DLL을 리버스 엔지니어링**하는 것

### 4.2 방법 A: USB 트래픽 캡처 (권장)

```
┌──────────────┐     USB Passthrough     ┌──────────────┐
│  Linux Host  │ ◄═══════════════════╦══ │  Windows VM  │
│  (usbmon)    │                     ║   │  (QEMU/KVM)  │
│              │                     ║   │  CanvasBio   │
│  Wireshark   │ ◄── usbmon3 ──────╝   │  Driver      │
└──────────────┘                         └──────────────┘
```

**준비 사항:**
1. Windows 10/11 ISO 이미지
2. QEMU/KVM + virt-manager 설치
3. USB passthrough 설정 (04e8:730b)
4. CanvasBio 드라이버 설치 (Samsung Update)
5. usbmon으로 캡처

**캡처할 시나리오:**
- VM 시작 → 드라이버 로드 (초기화 시퀀스!)
- 지문 등록 (Enroll)
- 지문 인증 (Verify)
- 지문 삭제 (Delete)

### 4.3 방법 B: 드라이버 바이너리 분석

**대상 파일:**
- CanvasBio UMDF DLL (DriverStore에서 추출)
- 버전: 5.1.155.6134 (최신)
- Microsoft Update Catalog에서 다운로드 가능

**분석 도구:**
- Ghidra (무료, NSA 제공)
- IDA Pro
- Binary Ninja

**찾아야 할 것:**
- `WinUsb_WritePipe()` 호출 → Bulk OUT 명령
- `WinUsb_ReadPipe()` 호출 → Bulk IN 응답
- `WinUsb_ControlTransfer()` 호출 → 초기화 Control 명령
- 명령 바이트 상수, 상태 머신 로직

### 4.4 방법 C: 유사 장치 프로토콜 참고

**04e8:730a** (이전 리비전)도 동일한 드라이버 사용 → 같은 프로토콜 가능성 높음

## 5. 프로젝트 구조

```
samsung-galaxy-book-fingerprint-sensor-device-730b/
├── RESEARCH_REPORT.md          ← 이 보고서
├── tools/
│   ├── safe_probe.py           ← 안전한 단계별 프로브 (signal handler, 로깅)
│   ├── ctrl_probe.py           ← Control Transfer 전용 프로브
│   ├── init_probe.py           ← 초기화 프로토콜 탐색
│   ├── analyze_probe.py        ← 프로브 데이터 분석
│   ├── usb_capture.sh          ← tshark 기반 USB 트래픽 캡처
│   ├── usb_probe.py            ← 초기 프로브 (참고용)
│   └── usb_deep_probe.py       ← 심층 프로브 (주의: 크래시 유발)
├── captures/
│   ├── probe_20260306_120053.json    ← safe_probe 결과
│   └── ctrl_probe_20260306_120532.json ← ctrl_probe 결과
└── driver_analysis/            ← Windows 드라이버 분석 공간
```

## 6. 주요 참고 자료

- [libfprint 공식](https://gitlab.freedesktop.org/libfprint/libfprint) — 드라이버 업스트림 대상
- [Galaxy Book2 Pro Linux](https://github.com/joshuagrisham/galaxy-book2-pro-linux) — EgisTec 리버스 엔지니어링 참고
- [libfprint egismoc SDCP](https://github.com/TenSeventy7/libfprint-egismoc-sdcp) — MOC 드라이버 구현 참고
- [Cypress PSoC USB Application Note](https://www.infineon.com/assets/row/public/documents/cross-divisions/42/infineon-an56377-psoc-3-and-psoc-5lp-introduction-to-implementing-usb-data-transfers-applicationnotes-en.pdf) — Cypress USB 구현
- [Microsoft Update Catalog - CanvasBio](https://www.catalog.update.microsoft.com/Search.aspx?q=CanvasBio) — Windows 드라이버 다운로드
- [linux-hardware.org 04e8:730b](https://linux-hardware.org/?id=usb:04e8-730b) — Linux 호환성 보고
