# CanvasBio CB2000 SPI-over-USB Protocol - Decoded from Windows Capture

## 1. Bulk EP Protocol Format

### Command Types (first byte)
| Cmd | Name | Format | Description |
|-----|------|--------|-------------|
| `0xA9` | SPI_WRITE | `A9 [reg] [val]` (3B) or `A9 [reg] [hi] [lo]` (4B) | Write sensor SPI register |
| `0xA8` | SPI_READ | `A8 [reg] [00]` (3B) | Read sensor SPI register |
| `0x4F` | SPECIAL | `4F [arg]` (2B) | Power/mode command (pre-SPI) |

### Read Response Format
```
Bulk IN (EP 0x82): [00] [00] [val]  (3 bytes)
```

## 2. Complete Initialization Sequence (from Windows capture)

### Phase 0: Pre-SPI Setup
```
bulk_write(0x4F, 0x80)     // Special command - likely MCU power/SPI enable
```

### Phase 1: SPI Enable & Hardware ID Check
```
cfs_writeRegister(0x4F, 0x80)     // Enable SPI? (same as special cmd)
val = cfs_readRegister(0xB9)      // Read Hardware ID → 0x34
```

### Phase 2: Sensor Configuration (InitParam)
```
// ADC / Amplifier settings
cfs_write2ByteRegister(0x60, 0x1B00)   // CB_REG_BIAS_SET or timing
cfs_write2ByteRegister(0x50, 0x2100)   // Scan config
cfs_write2ByteRegister(0x61, 0x0000)   // ADC offset
cfs_write2ByteRegister(0x62, 0x001A)   // Row start
cfs_write2ByteRegister(0x63, 0x001A)   // Row end
cfs_write2ByteRegister(0x64, 0x040A)   // Column config (4 cols x 10?)
cfs_write2ByteRegister(0x66, 0x0F80)   // Scan timing
cfs_write2ByteRegister(0x67, 0x1B00)   // Amplifier gain
cfs_write2ByteRegister(0x68, 0x000F)   // Integration time
cfs_write2ByteRegister(0x69, 0x0014)   // SNR threshold
cfs_write2ByteRegister(0x6A, 0x0019)   // ADC reference
cfs_write2ByteRegister(0x6C, 0x0019)   // ADC reference 2

// Finger detection thresholds
cfs_write2ByteRegister(0x40, 0x4300)   // Finger-down threshold high
cfs_write2ByteRegister(0x41, 0x6F00)   // Finger-down threshold low

// Image configuration
cfs_write2ByteRegister(0x55, 0x2000)   // Image buffer config
cfs_write2ByteRegister(0x5F, 0x0000)   // Image mode
cfs_write2ByteRegister(0x52, 0x2700)   // Image parameter

// Final
cfs_write2ByteRegister(0x09, 0x0000)   // Soft reset / apply config
```

## 3. Register Map (Decoded)

### Vendor Control Registers (reqType=0xC0, via Control Transfer)
| Register | Size | Value | Description |
|----------|------|-------|-------------|
| 0xB0 | 8B | 01 00 03 00 4E 00 00 00 | Firmware version v1.0.3, 'N'=Normal |
| 0xB1 | 4B | A3 08 00 00 | Build number 2211 |
| 0xB4 | 8B | (unique) | Device unique ID |
| 0xBB | 64B | (calibration) | Factory calibration data |
| 0xBD | 4B | 43 59 55 53 | "CYUS" magic (Cypress USB System) |
| 0xC0 | 16B | (config) | Configuration register |
| 0xC8 | 3B | 20 00 00 | Buffer size = 32 |
| 0xCC | 4B | 01 00 00 00 | Ready flag (1=ready) |
| 0xD4 | 1B | 02 | Device mode (2=idle) |
| 0xDD | 7B | (sensor spec) | 100x20 sensor dimensions |
| 0xDE | 16B | (extended) | Extended sensor spec |

### SPI Sensor Registers (via Bulk EP, 0xA8/0xA9 commands)
| Register | Init Value | Probable Function |
|----------|-----------|-------------------|
| 0x09 | 0x0000 | Soft reset / config apply |
| 0x40 | 0x4300 | Finger-down detect threshold (high) |
| 0x41 | 0x6F00 | Finger-down detect threshold (low) |
| 0x4F | 0x80 | SPI/sensor enable |
| 0x50 | 0x2100 | Scan configuration |
| 0x52 | 0x2700 | Image parameter |
| 0x55 | 0x2000 | Image buffer config |
| 0x5F | 0x0000 | Image mode |
| 0x60 | 0x1B00 | Bias/timing set |
| 0x61 | 0x0000 | ADC offset |
| 0x62 | 0x001A | Row start address |
| 0x63 | 0x001A | Row end address |
| 0x64 | 0x040A | Column configuration |
| 0x66 | 0x0F80 | Scan timing |
| 0x67 | 0x1B00 | Amplifier gain |
| 0x68 | 0x000F | Integration time |
| 0x69 | 0x0014 | SNR threshold |
| 0x6A | 0x0019 | ADC reference voltage |
| 0x6C | 0x0019 | ADC reference voltage 2 |
| 0xB9 | (read) 0x34 | Hardware ID |

## 4. Communication Flow Summary

```
┌──────────┐     Control Transfer (0xC0)     ┌────────────┐
│          │ ──────────────────────────────→  │            │
│          │     (vendor register read)       │  Cypress   │
│          │                                  │  PSoC MCU  │
│   Host   │     Bulk OUT (EP 0x01)          │  "CYUS"    │
│  (Linux) │ ──────────────────────────────→  │            │
│          │     A9/A8 + reg + data           │   USB↔SPI  │
│          │                                  │   Bridge   │
│          │     Bulk IN  (EP 0x82)           │            │
│          │ ←──────────────────────────────  │            │
│          │     SPI read response            │            │
│          │                                  │            │
│          │     Interrupt IN (EP 0x83)       │            │
│          │ ←──────────────────────────────  │            │
│          │     IRQ events (finger-down)     │            │
└──────────┘                                  └────────────┘
                                                    │ SPI
                                              ┌────────────┐
                                              │ CanvasBio  │
                                              │  CB2000    │
                                              │  Sensor IC │
                                              └────────────┘
```

## 5. Missing: Image Capture & Finger Detection

Still need to capture:
1. SetMode(FINGER_DOWN) → what registers are written
2. IRQ on EP 0x83 when finger touches
3. SetMode(CAPTURE) → what registers are written
4. cfs_readImage() → Bulk IN image data format
5. Image dimensions and pixel format
