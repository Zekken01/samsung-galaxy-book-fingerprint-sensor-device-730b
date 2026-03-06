# Samsung Galaxy Book Fingerprint Sensor Linux Driver

Linux userspace driver for the **CanvasBio CB2000** fingerprint sensor found in Samsung Galaxy Book laptops.

**USB ID**: `04e8:730b` (Samsung Electronics)

## Hardware Architecture

```
┌──────────┐     USB 2.0 Full Speed      ┌────────────┐   SPI   ┌────────────┐
│          │  Control Transfer (EP0)      │            │         │            │
│  Host    │  Bulk OUT (EP 0x01)    ───>  │  Cypress   │  ───>   │ CanvasBio  │
│  (Linux) │  Bulk IN  (EP 0x82)   <───  │  PSoC MCU  │  <───   │  CB2000    │
│          │  Interrupt IN (EP 0x83) <─── │  "CYUS"    │  <───   │  Sensor IC │
└──────────┘                              └────────────┘         └────────────┘
                                           USB-SPI Bridge         112x96 @ 8bpp
```

- **Cypress PSoC MCU**: USB-to-SPI bridge controller ("CYUS" magic)
- **CanvasBio CB2000**: Capacitive area fingerprint sensor
- **Image**: 112 x 96 pixels, 8-bit grayscale (10,752 bytes)
- **Matching**: Host-side (SensorMode=1, Basic) — not Match-on-Chip

## Project Structure

```
.
├── Cargo.toml
├── src/
│   ├── main.rs                  # CLI entry point
│   ├── lib.rs                   # Library root
│   ├── image.rs                 # Image save/format
│   ├── usb/                     # USB communication domain
│   │   ├── device.rs            # Device open/close/claim
│   │   └── transfer.rs          # Cypress MCU control & bulk transfers
│   ├── sensor/                  # Sensor domain
│   │   ├── init.rs              # Power-on, register configuration
│   │   └── capture.rs           # Image capture protocol
│   └── bin/
│       ├── analyze_capture.rs   # USB traffic analysis tool
│       └── extract_image.rs     # Fingerprint image extractor from pcapng
├── docs/
│   ├── protocol.md              # Complete SPI-over-USB protocol spec
│   ├── driver-analysis.md       # Windows driver binary analysis
│   └── research.md              # Initial hardware research report
└── scripts/
    ├── setup-vm.sh              # Windows VM setup for USB capture
    ├── usb-capture.sh           # tshark USB traffic capture
    └── usb-passthrough.xml      # QEMU USB passthrough config
```

## Building

```bash
# Prerequisites
sudo apt install libusb-1.0-0-dev
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# Build
cargo build --release
```

## Usage

```bash
# Show sensor info (Cypress MCU version, firmware, ready state)
sudo ./target/release/canvasbio-fp info

# Initialize sensor (power on, configure registers)
sudo ./target/release/canvasbio-fp init

# Capture a fingerprint image
sudo ./target/release/canvasbio-fp capture -o fingerprint.png

# Capture multiple images
sudo ./target/release/canvasbio-fp capture -o scan.png -c 5

# Enable debug logging
sudo RUST_LOG=debug ./target/release/canvasbio-fp capture -o fingerprint.png
```

### Analysis Tools

```bash
# Analyze USB traffic from pcapng capture
# First export bulk data with tshark:
tshark -r capture.pcapng -Y "usb.transfer_type==0x03" \
  -T fields -e frame.number -e frame.time_relative \
  -e usb.endpoint_address -e usb.data_len \
  -e usb.capdata -E separator=$'\t' > /tmp/bulk_data.tsv

# Then analyze:
sudo ./target/release/analyze-capture /tmp/bulk_data.tsv

# Extract fingerprint images from capture:
sudo ./target/release/extract-image /tmp/bulk_data.tsv captures/images
```

## Protocol Overview

The sensor communicates via a layered protocol:

### Layer 1: Cypress MCU Control (USB Control Transfers)

| Request | Direction | Purpose |
|---------|-----------|---------|
| `0xCA`  | Host→Dev  | Prepare SPI transfer (wValue=direction, wIndex=size) |
| `0xCC`  | Dev→Host  | Read ready flag (1=ready, 0=busy) |
| `0xDB`  | Host→Dev  | Power control (wValue=0x0006) |
| `0xBD`  | Dev→Host  | Read magic ("CYUS") |
| `0xB0`  | Dev→Host  | Firmware version |

### Layer 2: SPI Commands (USB Bulk Transfers)

| Prefix | Format | Direction |
|--------|--------|-----------|
| `0xA9` | `A9 [reg] [val...]` | Write SPI register |
| `0xA8` | `A8 [reg] 0x00` | Read SPI register (response on Bulk IN) |

Every SPI transaction requires:
1. `0xCA` control transfer to prepare the MCU (direction + size)
2. Bulk OUT write with the SPI command
3. `0xCC` control transfer to confirm completion (for writes)

### Layer 3: Sensor Registers

Key registers configured during initialization:

| Register | Value | Function |
|----------|-------|----------|
| `0x4F`   | `0x80` | SPI/sensor enable |
| `0xB9`   | (read) | Hardware ID |
| `0x60`   | `0x1B00` | Bias/timing |
| `0x67`   | `0x1B00` | Amplifier gain |
| `0x68`   | `0x000F` | Integration time |
| `0x40`   | `0x4300` | Finger-down threshold (high) |
| `0x41`   | `0x6F00` | Finger-down threshold (low) |

### Image Capture Protocol

1. Write capture-mode registers (14 register operations)
2. Single `0xCA` prepare for 10,755 bytes (3B header + 10,752B image)
3. Block 0: Send `[A8 06 00]` + 256 zero bytes, read 256B response
4. Clear endpoint halt (Linux-specific quirk)
5. Blocks 1-41: Send 256 zero bytes, read 256B response each
6. Concatenate all response data → 112x96 grayscale image

## Reverse Engineering Methodology

1. **Hardware probing** — USB descriptor analysis, control transfer register scan
2. **Windows driver binary analysis** — Ghidra disassembly of `CanvasBioFingerprintDriver.dll`
3. **USB traffic capture** — Windows 10 VM (QEMU/KVM) with USB passthrough + usbmon/tshark
4. **Protocol decoding** — Matched captured traffic to driver function signatures
5. **Linux PoC** — Python prototype, then Rust implementation

## Known Issues

- **Slow image capture (~3 min/frame)**: The Cypress MCU halts EP_OUT after the first block's size mismatch (259B out → 256B in). Each subsequent `clear_halt` adds ~5s delay on Linux. Windows WinUSB doesn't exhibit this behavior.
- **Last block short by 4 bytes**: Block 41 returns 252-255 bytes instead of 256. The missing pixels are zero-padded.
- **No finger-down detection**: Interrupt endpoint (EP 0x83) polling for finger-down events is not yet implemented.
- **No fprintd integration**: Currently a standalone CLI tool. libfprint/fprintd integration is planned.

## Device Recovery

If the device firmware crashes (protocol error -71):
- USB reset is **not sufficient**
- Full power off (30+ seconds) is required for recovery

## References

- [libfprint](https://gitlab.freedesktop.org/libfprint/libfprint) — Target upstream for Linux integration
- [Cypress PSoC USB Application Note](https://www.infineon.com/an56377) — Cypress MCU USB protocol reference
- [Microsoft Update Catalog - CanvasBio](https://www.catalog.update.microsoft.com/Search.aspx?q=CanvasBio) — Windows driver source
- [linux-hardware.org 04e8:730b](https://linux-hardware.org/?id=usb:04e8-730b) — Linux compatibility reports

## License

This project is the result of independent reverse engineering for Linux hardware compatibility.
The protocol documentation and driver code are original work.
