# CanvasBio CB2000 SPI-over-USB Protocol Specification

Decoded from Windows driver binary analysis and USB traffic capture.

## 1. Protocol Layers

The sensor communicates through three nested protocol layers:

```
┌──────────────────────────────────────────────────────────┐
│  Layer 3: Sensor SPI Registers                           │
│  Register read/write via 0xA8/0xA9 command prefixes      │
├──────────────────────────────────────────────────────────┤
│  Layer 2: SPI-over-USB (Bulk Endpoints)                  │
│  Bulk OUT (EP 0x01) → commands / SPI clock               │
│  Bulk IN  (EP 0x82) → SPI read responses                 │
├──────────────────────────────────────────────────────────┤
│  Layer 1: Cypress MCU Control (Vendor Control Transfers)  │
│  0xCA prepare, 0xCC ready, 0xDA scan status, 0xDB power  │
└──────────────────────────────────────────────────────────┘
```

## 2. Layer 1: Cypress MCU Vendor Control Transfers

### Write Registers (reqType=0x40, Host-to-Device)

| Request | wValue | wIndex | Description |
|---------|--------|--------|-------------|
| `0xCA`  | direction | size | Prepare SPI transfer. direction: 0x0002=write, 0x0003=read. size: byte count. |
| `0xDB`  | 0x0006 | 0/1 | Power control. Sequence: index=1, 0, 1 for power-on. |

### Read Registers (reqType=0xC0, Device-to-Host)

| Request | Size | Value | Description |
|---------|------|-------|-------------|
| `0xB0`  | 8B   | `01 00 03 00 4E 00 00 00` | Firmware version v1.0.3, 'N'=Normal mode |
| `0xB1`  | 4B   | `A3 08 00 00` | Build number: 2211 (little-endian) |
| `0xB4`  | 8B   | (unique) | Device unique ID |
| `0xBB`  | 64B  | (calibration) | Factory calibration data |
| `0xBD`  | 4B   | `43 59 55 53` | Magic: "CYUS" (Cypress USB System) |
| `0xC0`  | 16B  | (config) | Configuration register A |
| `0xC8`  | 3B   | `20 00 00` | Buffer size: 32 |
| `0xCC`  | 4B   | `01 00 00 00` | Ready flag: 1=ready, 0=busy after bulk write |
| `0xD4`  | 1B   | `02` | Device mode: 2=idle |
| `0xDA`  | 2B   | varies | GPIO/scan status (see scan completion polling) |
| `0xDD`  | 7B   | `00 00 00 00 64 14 00` | Sensor dimensions: 100x20 |
| `0xDE`  | 16B  | (extended) | Extended sensor spec |

### Scan Completion Polling (0xDA)

Read with `wValue=0x0007`, returns 2 bytes:
- `[0x00, 0x00]` — scan in progress
- `[0x00, 0x01]` — scan complete, image ready in sensor buffer

**Important:** The completion flag is in `buf[1]`, not `buf[0]`.

The Windows driver polls every ~15ms. Typical scan duration with finger: ~3-4 seconds.
Without a finger on the sensor, the scan never completes.

## 3. Layer 2: SPI Commands (Bulk Endpoints)

### Command Prefixes

| Byte | Name | Format | Description |
|------|------|--------|-------------|
| `0xA9` | SPI_WRITE | `A9 [reg] [val]` (3B) or `A9 [reg] [hi] [lo]` (4B) | Write sensor register |
| `0xA8` | SPI_READ  | `A8 [reg] [00]` (3B) | Read sensor register |
| `0x4F` | SPECIAL   | `4F [arg]` (2B) | Pre-SPI power/mode command |

### SPI Transaction Protocol

Every SPI transaction follows this sequence:

1. **Prepare** — `0xCA` vendor control write (direction + byte count)
2. **Command** — Bulk OUT with the SPI command bytes
3. **Response** — For reads: Bulk IN returns `[00] [00] [val]` (3 bytes)
4. **Ready check** — For writes: `0xCC` vendor read confirms completion

### SPI Register Write (single byte)

```
Host: CA write, wValue=0x0002, wIndex=3      (prepare write, 3 bytes)
Host: Bulk OUT → [A9] [reg] [val]            (SPI write command)
Host: CC read → [01 00 00 00]                (confirm ready)
```

### SPI Register Write (two bytes)

```
Host: CA write, wValue=0x0002, wIndex=4      (prepare write, 4 bytes)
Host: Bulk OUT → [A9] [reg] [hi] [lo]       (SPI write command)
Host: CC read → [01 00 00 00]                (confirm ready)
```

### SPI Register Read

```
Host: CA write, wValue=0x0003, wIndex=3      (prepare read, 3 bytes)
Host: Bulk OUT → [A8] [reg] [00]             (SPI read command)
Host: Bulk IN  ← [00] [00] [val]             (SPI response, value in byte 2)
```

## 4. Layer 3: Sensor Register Map

### Initialization Registers (written during `InitParam`)

| Register | Value  | Function |
|----------|--------|----------|
| `0x4F`   | `0x80` | SPI/sensor enable |
| `0xB9`   | (read) | Hardware ID (expected: 0x34 or 0x68) |
| `0x60`   | `0x1B00` | Bias/timing configuration |
| `0x50`   | `0x2100` | Scan configuration |
| `0x61`   | `0x0000` | ADC offset |
| `0x62`   | `0x001A` | Row start address |
| `0x63`   | `0x001A` | Row end address |
| `0x64`   | `0x040A` | Column configuration |
| `0x66`   | `0x0F80` | Scan timing |
| `0x67`   | `0x1B00` | Amplifier gain |
| `0x68`   | `0x000F` | Integration time |
| `0x69`   | `0x0014` | SNR threshold |
| `0x6A`   | `0x0019` | ADC reference voltage |
| `0x6C`   | `0x0019` | ADC reference voltage 2 |
| `0x40`   | `0x4300` | Finger-down detection threshold (high) |
| `0x41`   | `0x6F00` | Finger-down detection threshold (low) |
| `0x55`   | `0x2000` | Image buffer configuration |
| `0x5F`   | `0x0000` | Image mode |
| `0x52`   | `0x2700` | Image parameter |

### Scan-Mode vs Readout-Mode Registers

Several registers take different values depending on the operating phase:

| Register | Scan Mode | Readout Mode | Notes |
|----------|-----------|--------------|-------|
| `0x51`   | `0xA825`  | `0x3A25`     | Scan parameters |
| `0x10`   | `0x6000`  | `0x0001`     | Operating mode |
| `0x2F`   | `0xF6FF`  | `0xEF00`     | Reference configuration |
| `0x38`   | `0x0100`  | (not set)    | Scan-only: IRQ enable |
| `0x3D`   | `0xFF0F`  | (not set)    | Scan-only: IRQ mask |
| `0x3B`   | `0x1400`  | (not set)    | Scan-only: Detect timing |

### Status Register (0x20)

Read via `spi_read_reg_full(0x20)` — returns 4 bytes, status in bytes [2:3] (big-endian u16):

| Value    | Meaning |
|----------|---------|
| `0x0002` | Idle / pre-trigger |
| `0x001A` | Scan in progress (no finger) |
| `0x0048` | Transitional state |
| `0x0102` | Finger-down detection mode, no finger |
| `0x0107` | **Finger detected** |

## 5. Complete Protocol Sequences

### 5.1 Power-On Initialization

```
1. Cypress power cycle:
   CA vendor write: DB, wValue=0x0006, wIndex=1
   CA vendor write: DB, wValue=0x0006, wIndex=0
   CA vendor write: DB, wValue=0x0006, wIndex=1
   Sleep 100ms

2. SPI enable (special 2-byte command):
   CA prepare: wValue=0x0002, wIndex=2
   Bulk OUT: [4F 80]
   CC ready check

3. SPI register enable:
   SPI write reg 0x4F = 0x80
   Sleep 100ms

4. Hardware ID check:
   SPI read reg 0xB9 → expect 0x34 or 0x68

5. Write 17 initialization registers (see table above)
```

### 5.2 Image Capture (4-Phase Protocol)

```
Phase 1 — Trigger Scan:
   SPI write2 0x5F = 0x0000
   SPI write2 0x52 = 0x2700
   SPI write2 0x09 = 0x0000
   SPI write2 0x5D = 0x4D00
   SPI write2 0x51 = 0xA825    ← scan mode
   SPI write  0x03 = 0x00
   SPI write2 0x38 = 0x0100    ← scan-only
   SPI write2 0x3D = 0xFF0F    ← scan-only
   SPI write2 0x10 = 0x6000    ← scan mode
   SPI write2 0x3B = 0x1400    ← scan-only
   SPI write2 0x2F = 0xF6FF    ← scan mode
   SPI write2 0x09 = 0x0000
   SPI write  0x0C = 0x00
   SPI read_full 0x20          (status check)
   SPI write  0x04 = 0x00      ← TRIGGERS SCAN

Phase 2 — Poll Scan Completion:
   Loop: vendor read 0xDA, wValue=0x0007
   Wait until response[1] == 0x01
   Poll interval: 10ms, typical duration: 3-4s

Phase 3 — Reconfigure for Readout:
   SPI read  0x08              (interrupt status)
   SPI write2 0x09 = 0x0000
   SPI read_full 0x3E
   SPI write2 0x03 = 0x0000
   SPI read_full 0x20
   SPI write2 0x10 = 0x0001    ← readout mode
   SPI write2 0x2F = 0xEF00    ← readout mode
   SPI write2 0x09 = 0x0000
   SPI write2 0x5D = 0x4D00
   SPI write2 0x51 = 0x3A25    ← readout mode
   SPI write  0x0C = 0x00
   SPI read_full 0x20
   SPI write2 0x04 = 0x0000
   SPI write2 0x09 = 0x0000

Phase 4 — Bulk Image Transfer:
   CA prepare: wValue=0x0003, wIndex=10755  (3B cmd + 10752B image)

   Block 0:
     Bulk OUT: [A8 06 00] + 256 zero bytes (259B total)
     Bulk IN:  read 256B → image[0..256]

   Blocks 1-41:
     Bulk OUT: 256 zero bytes (SPI clock padding)
     Bulk IN:  read 256B → append to image
     (Last block may return 259B with 3B MCU framing header — strip it)

   Result: 10,752 bytes = 112 x 96 grayscale image
```

### 5.3 Finger-Down Detection

```
1. Enter detect mode:
   SPI write2 0x5D = 0x4D00
   SPI write2 0x51 = 0xA825
   SPI write2 0x03 = 0x0000
   SPI write2 0x38 = 0x0100
   SPI write2 0x3D = 0xFF0F
   SPI write2 0x10 = 0x6000
   SPI write2 0x3B = 0x1400
   SPI write2 0x2F = 0xF6FF
   SPI write2 0x09 = 0x0000
   SPI write  0x0C = 0x00
   SPI read_full 0x20          (prime sensor)
   SPI write  0x04 = 0x00      (clear trigger)

2. Poll loop (every 15ms):
   SPI read   0x08             (interrupt status)
   SPI write2 0x09 = 0x0000   (clear pending)
   SPI write2 0x03 = 0x0000
   SPI write  0x0C = 0x00     (retrigger detect scan)
   SPI read_full 0x20         → check bytes [2:3]
   SPI write  0x04 = 0x00     (clear trigger)

   If status == 0x0107 → finger detected, proceed to capture
```

## 6. Communication Flow Diagram

```
┌──────────┐     Control Transfer (EP0)      ┌────────────┐
│          │ ──────────────────────────────→  │            │
│          │  0xCA: prepare SPI transfer      │  Cypress   │
│          │  0xCC: check ready flag          │  PSoC MCU  │
│   Host   │  0xDA: poll scan completion      │  "CYUS"    │
│  (Linux) │  0xDB: power control             │            │
│          │                                  │  USB↔SPI   │
│          │     Bulk OUT (EP 0x01)           │  Bridge    │
│          │ ──────────────────────────────→  │            │
│          │  A9/A8 + reg + data (SPI cmds)   │            │
│          │                                  │            │
│          │     Bulk IN  (EP 0x82)           │            │
│          │ ←──────────────────────────────  │            │
│          │  SPI read responses / image data │            │
└──────────┘                                  └────────────┘
                                                    │ SPI bus
                                              ┌────────────┐
                                              │ CanvasBio  │
                                              │  CB2000    │
                                              │ Sensor IC  │
                                              │ 112x96 8bpp│
                                              └────────────┘
```

## 7. Image Format

- **Resolution**: 112 x 96 pixels
- **Bit depth**: 8-bit grayscale (1 byte per pixel)
- **Total size**: 10,752 bytes
- **DPI**: ~340 (estimated from sensor physical dimensions)
- **Orientation**: Top-to-bottom, left-to-right raster scan
- **Buffer register**: 0x06 (image data read via `A8 06 00`)
