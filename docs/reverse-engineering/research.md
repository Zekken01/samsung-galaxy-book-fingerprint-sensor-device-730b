# Hardware Research Report

Initial investigation of the Samsung Galaxy Book fingerprint sensor (USB 04e8:730b) on Linux.

## Document Context

- Type: historical research record
- Scope: early probing, instability observations, and hypothesis-building phase
- Notes:
  - Some sections capture pre-stabilization behavior and should be read as historical context.
  - For current validated workflows, prefer:
    - `docs/operations/ubuntu-login-integration.md`
    - `docs/operations/fingerprint-operations.md`
    - `docs/index.md`

## 1. Device Identification

| Property | Value |
|----------|-------|
| USB Vendor ID | 0x04E8 (Samsung Electronics) |
| USB Product ID | 0x730B |
| Product Name | Fingerprint Sensor Device - 730B |
| USB Controller | Cypress (now Infineon) — "CYUS" magic confirmed |
| Sensor Manufacturer | CanvasBio (now LaserSemicon Co., Ltd) |
| Interface Class | 0xFF (Vendor Specific), SubClass 0x02 |
| USB Version | 2.0 (Full Speed, 64-byte max packet) |
| Architecture | Host-side matching (not Match-on-Chip) |

### USB Endpoint Layout

| Endpoint | Direction | Transfer Type | Purpose |
|----------|-----------|---------------|---------|
| EP 1 (0x01) | OUT | Bulk | Host → sensor SPI commands |
| EP 2 (0x82) | IN  | Bulk | Sensor → host SPI responses / image data |
| EP 3 (0x83) | IN  | Interrupt | Event notifications (unused in practice) |

All endpoints have a 64-byte max packet size (Full Speed USB).

## 2. Phase 1: Blind Probing Results

### 2.1 Vendor Control Transfer Register Map (reqType=0xC0)

Discovered via systematic scanning of requests 0x00-0xFF:

| bRequest | Size | Value (Hex) | Interpretation |
|----------|------|-------------|----------------|
| 0xB0 | 8B | `01 00 03 00 4E 00 00 00` | **Firmware version**: v1.0.3, 'N'=Normal mode |
| 0xB1 | 4B | `A3 08 00 00` | **Build number**: 2211 (0x08A3, little-endian) |
| 0xB4 | 8B | `0B D3 4A 06 B0 16 05 00` | **Device unique ID** (serial substitute) |
| 0xBB | 64B | `00 10 00 20 9B 00 00 10...` | **Calibration data** (factory settings) |
| 0xBD | 4B | `43 59 55 53` | **Magic: "CYUS"** (Cypress USB System) |
| 0xC0 | 16B | `40 0D 03 00 08 00 00 01...` | **Config register A** |
| 0xC2 | 16B | (same as 0xC0) | **Config register B** |
| 0xC4 | 16B | (same as 0xC0) | **Config register C** |
| 0xC8 | 3B | `20 00 00` | **Buffer size**: 0x20 = 32 |
| 0xCC | 4B | `01 00 00 00` / `00 00 00 00` | **Ready flag** (1=ready, changes after bulk writes) |
| 0xD4 | 1B | `02` | **Device mode**: 2=idle |
| 0xDA | 2B | `00 01` | **GPIO/scan status** |
| 0xDD | 7B | `00 00 00 00 64 14 00` | **Sensor spec**: 100(0x64) x 20(0x14) |
| 0xDE | 16B | `00 00 00 00 64 14 00 01 01 01...` | **Extended spec** (0xDD + extra config) |

### 2.2 Bulk Endpoint Behavior (Pre-Initialization)

Before the proper initialization sequence, the device operates in an "echo mode":

| Bytes Sent | Response Size | Response Content | Notes |
|------------|---------------|------------------|-------|
| 1B | 1B | 0x00 | Same-length zero response |
| 2B | 2B | 0x0000 | Same-length zero response |
| 3B | 3B | 0x000000 | Same-length zero response |
| 8B | **4B** | 0x00000000 | **Capped at 4B** |
| 64B | — | (crash) | **Firmware crash!** |

Key observations:
- 1-3B writes: response length = send length
- 4B+ writes: response capped at 4 bytes
- Response content: always zeros (no data processing without init)
- 64B+ or rapid sequential writes: **firmware crash**

### 2.3 Control Write Rejection

- **All vendor WRITE commands rejected** (reqType=0x40, 0x41)
- Full 0x00-0xFF range scanned with various wValue/wIndex: no response
- **Conclusion**: Device initialization must happen through bulk endpoints, not control writes

### 2.4 Interrupt Endpoint

- Pre-initialization: no events generated
- Finger contact: no response
- **Conclusion**: Interrupt endpoint only activates after proper initialization
- **Later finding**: The Windows driver doesn't actually use the interrupt endpoint for finger detection — it polls SPI register 0x20 instead

## 3. Device Stability

### Crash Triggers

1. Single bulk write of 64+ bytes → firmware crash
2. Rapid sequential control transfer scans → crash
3. Process kill during active USB session → crash
4. Aggressive probing (256-byte sequential scan) → crash

### Recovery Methods

| Method | Result |
|--------|--------|
| USB reset (`usbreset`) | **Failed** — error -71 persists |
| xHCI controller rebind | **Failed** |
| Soft reboot | **Failed** |
| `usbreset` after reboot | **Sometimes works** |
| **Full power off (30+ seconds)** | **Only reliable recovery** |

The error -71 (protocol error) on re-enumeration after crash indicates the Cypress MCU firmware enters an unrecoverable state that only a full power cycle can clear.

## 4. Protocol Architecture (Pre-Capture Understanding)

```
┌─────────────────────────────────────────────┐
│           Host (Linux/Windows)              │
├─────────────────────────────────────────────┤
│  Control Transfer (EP0)                     │
│  ├─ Standard USB commands (GET_STATUS)      │
│  └─ Vendor READ (0xC0): register reads      │
│     - Device info, status, config queries   │
│     - WRITE (0x40) not supported            │
├─────────────────────────────────────────────┤
│  Bulk Transfer (EP1 OUT → EP2 IN)           │
│  ├─ Initialization sequence (proprietary)   │
│  ├─ SPI register read/write commands        │
│  └─ Image data transfer                     │
├─────────────────────────────────────────────┤
│  Interrupt Transfer (EP3 IN)                │
│  └─ Async events (only after init)          │
│     - Not used for finger detection (!)     │
└─────────────────────────────────────────────┘
         │
         │ USB 2.0 Full Speed (64B packets)
         │
┌─────────────────────────────────────────────┐
│     Cypress PSoC MCU ("CYUS")               │
│  ├─ USB communication management            │
│  ├─ Vendor registers (0xB0-0xDE)            │
│  └─ USB ↔ SPI bridge (bulk data routing)    │
├─────────────────────────────────────────────┤
│     CanvasBio CB2000 Sensor Module          │
│  ├─ Fingerprint image capture (112x96)      │
│  ├─ SPI register interface                  │
│  └─ Capacitive finger detection             │
└─────────────────────────────────────────────┘
```

## 5. Ready Flag (0xCC) State Transitions

- **Initial probe**: 0xCC = `01 00 00 00` (ready)
- **After bulk echo test**: 0xCC = `00 00 00 00` (not ready)
- This confirms bulk writes affect device state — the MCU processes them but transitions to "not ready" when the protocol isn't followed correctly

## 6. Research Methodology

### Tools Used

| Tool | Purpose |
|------|---------|
| `lsusb -v` | USB descriptor analysis |
| `python3 + pyusb` | Custom USB probing scripts |
| `tshark` / `wireshark` | USB traffic capture (usbmon) |
| `QEMU/KVM` | Windows VM for driver traffic capture |
| Ghidra | Windows driver binary disassembly |

### Safety Measures

- Signal handlers in probe scripts (SIGINT/SIGTERM)
- Incremental probing (small transfers first)
- Logging all transfers for analysis
- Device state monitoring via 0xCC ready flag
- 30-second power-off recovery procedure documented

## 7. References

- [libfprint](https://gitlab.freedesktop.org/libfprint/libfprint) — Target upstream for Linux integration
- [Galaxy Book2 Pro Linux](https://github.com/joshuagrisham/galaxy-book2-pro-linux) — EgisTec reverse engineering reference
- [libfprint egismoc SDCP](https://github.com/TenSeventy7/libfprint-egismoc-sdcp) — MOC driver implementation reference
- [Cypress PSoC USB Application Note](https://www.infineon.com/an56377) — Cypress USB implementation
- [Microsoft Update Catalog: CanvasBio](https://www.catalog.update.microsoft.com/Search.aspx?q=CanvasBio) — Windows driver download
- [linux-hardware.org 04e8:730b](https://linux-hardware.org/?id=usb:04e8-730b) — Linux compatibility reports
