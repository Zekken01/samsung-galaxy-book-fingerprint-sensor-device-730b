# Windows Driver Binary Analysis

Reverse engineering results from the CanvasBio fingerprint Windows driver binaries.

## Document Context

- Type: reverse engineering reference
- Scope: Windows driver binaries, architecture, recovered symbols/flows
- Notes:
  - This is not an operational Ubuntu setup guide.
  - For current Linux usage and maintenance, use:
    - `docs/operations/ubuntu-login-integration.md`
    - `docs/operations/fingerprint-operations.md`
    - `docs/index.md`

## 1. Driver Files

### 04E8:730B Driver (v2.4.5.882, 2023-07-25)

| File | Size | Purpose |
|------|------|---------|
| `CanvasBioFingerprintDriver.dll` | 163KB | **UMDF sensor driver** (USB communication, sensor control) |
| `CanvasBioFingerprintEngine.dll` | 677KB | **WinBio engine adapter** (fingerprint matching, template management) |
| `CanvasBioFingerprint.inf` | 5.5KB | Driver INF configuration |

### Newer VID_2DF0 Driver (v5.1.155.6134, 2024-10-09)

| File | Size | Purpose |
|------|------|---------|
| `CanvasBioFingerprintDriver.dll` | 1MB | **UMDF MOC driver** (Realtek-based) |
| `CanvasBioFingerprintAdapter.dll` | 489KB | **WinBio unified adapter** (Sensor+Engine+Storage) |

> **Important**: The VID 2DF0:0007 version uses MOC (Match-on-Chip) + SDCP + TLS.
> The 04E8:730B version uses **host-side matching** (SensorMode=1, Basic) — a much simpler protocol.

## 2. Software Architecture

### Build Environment

```
Project path: C:\workspace\btp_sdk\WBF_UMDF2_TEST\umdf2_fx2\driver\
PDB path:     ...\x64\Release\osrusbfx2um\CanvasBioFingerprintDriver.pdb
Engine path:  ...\adapters\engine_adapter\x64\Release\
Framework:    Microsoft WDF UMDF 2.15 + osrusbfx2 (based on MS USB sample!)
```

### Layer Diagram

```
┌─────────────────────────────────────────────┐
│  Windows Biometric Framework (WBF/WinBio)   │
├─────────────────────────────────────────────┤
│  CanvasBioFingerprintEngine.dll             │
│  ├─ EngineAdapterVerifyFeatureSet()         │
│  ├─ EngineAdapterCreateEnrollment()         │
│  ├─ EngineAdapterIdentifyFeatureSet()       │
│  ├─ ExtractRawImg() / ExtractInfoFromSample()│
│  └─ Host-side fingerprint matching (BCrypt) │
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
│  └─ cfs_* (sensor SPI communication layer)  │
│      ├─ cfs_writeRegister(reg, val)         │
│      ├─ cfs_write2ByteRegister(reg, val)    │
│      ├─ cfs_writeRegisterEx(reg, data, len) │
│      ├─ cfs_readRegister(reg)               │
│      └─ cfs_readImage()                     │
├─────────────────────────────────────────────┤
│  WinUSB (NativeUSB UMDF Dispatcher)         │
│  ├─ Bulk OUT (EP 0x01) → SPI commands       │
│  ├─ Bulk IN  (EP 0x82) → SPI responses      │
│  └─ Interrupt IN (EP 0x83) → IRQ events     │
├─────────────────────────────────────────────┤
│  Cypress PSoC MCU ("CYUS")                  │
│  ├─ USB ↔ SPI bridge                        │
│  ├─ Vendor control transfer registers       │
│  └─ IRQ routing                             │
├─────────────────────────────────────────────┤
│  CanvasBio Sensor IC (CB2000)               │
│  ├─ SPI register interface                  │
│  ├─ Image capture (112x96 or 80x64)         │
│  ├─ ADC / SNR configuration                 │
│  └─ IRQ (finger-down detection)             │
└─────────────────────────────────────────────┘
```

## 3. Key Function Signatures (from Ghidra)

### SPI Communication Layer (`cfs_*`)

```c
// Write single-byte SPI register
// Protocol: CA(0x0002, 3) → Bulk OUT [A9 reg val] → CC check
void cfs_writeRegister(uint8_t reg, uint8_t val);

// Write two-byte SPI register
// Protocol: CA(0x0002, 4) → Bulk OUT [A9 reg hi lo] → CC check
void cfs_write2ByteRegister(uint8_t reg, uint16_t val);

// Read SPI register
// Protocol: CA(0x0003, 3) → Bulk OUT [A8 reg 00] → Bulk IN [00 00 val]
uint8_t cfs_readRegister(uint8_t reg);

// Read image data from sensor buffer
// Protocol: CA(0x0003, 10755) → 42 bulk OUT/IN block pairs
void cfs_readImage(uint8_t* buffer, size_t size);
```

### Sensor Control

```c
// Initialization sequence
void PrepareHardwareForUSBDriver();
void ConfigCypressPower();            // DB vendor writes
void ReadHardwareID();                // SPI read reg 0xB9, logs "Hardware id : 0x%02x"
void SoftResetSensor();
void cb_power_on();
void InitParam();                     // 17 register writes
void SetFingerDownAdcValues();        // Regs 0x40, 0x41

// Operating modes
void SetMode(int mode);              // FINGER_DOWN or CAPTURE
void WaitFingerDownCB();             // Poll register 0x20 for 0x0107
void CaptureImageCB();              // 4-phase capture protocol
void ReadImage();                    // Bulk image transfer
```

### Discovered String Literals

```
"CYUS"                               // Cypress MCU magic (reg 0xBD)
"Hardware id : 0x%02x"               // ReadHardwareID() log
"Hardware id mismatch"               // ID validation failure
"occurred fingerdown irq"            // OnIrqSignal() log
```

## 4. Sensor Specifications

| Property | Value |
|----------|-------|
| Sensor model | CanvasBio CB2000 |
| Sensor type | Capacitive area sensor |
| Resolution variants | 112x96 @ ~340dpi or 80x64 @ ~508dpi |
| Sensor dimensions (reg 0xDD) | 100x20 (register-reported) |
| Communication | SPI (via USB-SPI bridge) |
| Matching mode | **Host-side matching** (Basic SensorMode=1) |
| Firmware | v1.0.3, Build 2211 |
| MCU magic | "CYUS" (Cypress USB System) |

## 5. Key Insights for Linux Driver Development

1. **The Cypress MCU is just a bridge** — All sensor logic is accessed via SPI register read/write through bulk endpoints.

2. **Host-side matching simplifies everything** — No need to implement TLS, SDCP, or on-chip template management. Just capture the image and process it on the host.

3. **The init sequence is critical** — The sensor won't respond until the exact power-on and register configuration sequence is replicated.

4. **Two capture variants exist** — `CaptureImageCB` (CanvasBio native) and `CaptureImageIX` (extended). The 04E8:730B device uses the CB variant.

5. **Finger detection is SPI-based** — Despite having an interrupt endpoint (EP 0x83), the Windows driver polls SPI register 0x20 for finger-down detection, not the interrupt endpoint.
