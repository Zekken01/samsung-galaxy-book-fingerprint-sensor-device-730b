# canvasbio-fp

**Linux userspace driver for the CanvasBio CB2000 fingerprint sensor (Samsung Galaxy Book, USB `04e8:730b`)**

> I opened my laptop after a while. Windows 11 had an update waiting. I installed it.
> It was unusable. So I decided to switch to Ubuntu. Installed it. And then — oh no.
> The fingerprint sensor doesn't work. No driver. No support. Nothing on the internet.
>
> So I fired up [Claude Code](https://claude.ai/claude-code). We reverse-engineered the
> Windows driver binary, captured USB traffic from a Windows VM, decoded the proprietary
> SPI-over-USB protocol, and wrote a working Linux driver from scratch.
>
> It actually works.
>
> *— A stunned developer*

## Status

**Working.** The driver can initialize the sensor, detect finger placement, capture fingerprint images, and save them as PNG files. This was built entirely through reverse engineering with no official documentation.

| Feature | Status |
|---------|--------|
| Sensor initialization | Working |
| Image capture (112x96 8bpp) | Working |
| Finger-down detection | Working |
| CLI tool | Working |
| fprintd/libfprint integration | Planned |

## Hardware

This driver targets the fingerprint sensor embedded in Samsung Galaxy Book laptops:

```
Bus 003 Device 002: ID 04e8:730b Samsung Electronics Co., Ltd Fingerprint Sensor Device - 730B
```

```
┌──────────┐     USB 2.0 Full Speed      ┌────────────┐   SPI   ┌────────────┐
│          │  Control Transfer (EP0)      │            │         │            │
│   Host   │  Bulk OUT (EP 0x01)    ───>  │  Cypress   │  ───>   │ CanvasBio  │
│  (Linux) │  Bulk IN  (EP 0x82)   <───  │  PSoC MCU  │  <───   │  CB2000    │
│          │  Interrupt IN (EP 0x83) <─── │  "CYUS"    │         │  Sensor IC │
└──────────┘                              └────────────┘         └────────────┘
                                           USB-SPI Bridge         112x96 @ 8bpp
```

- **Cypress PSoC MCU** — USB-to-SPI bridge controller (firmware magic: `"CYUS"`)
- **CanvasBio CB2000** — Capacitive area fingerprint sensor IC
- **Image** — 112 x 96 pixels, 8-bit grayscale (10,752 bytes)
- **Matching** — Host-side (SensorMode=1, Basic) — not Match-on-Chip

## Building

### Prerequisites

```bash
# Debian/Ubuntu
sudo apt install libusb-1.0-0-dev pkg-config

# Fedora
sudo dnf install libusb1-devel

# Rust toolchain
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

### Compile

```bash
cargo build --release
```

## Usage

The sensor is accessed via raw USB, so **root privileges** (or a udev rule) are required.

```bash
# Show sensor info (Cypress MCU version, firmware, ready state)
sudo ./target/release/canvasbio-fp info

# Initialize the sensor
sudo ./target/release/canvasbio-fp init

# Capture a fingerprint image
sudo ./target/release/canvasbio-fp capture -o fingerprint.png

# Wait for finger, then capture
sudo ./target/release/canvasbio-fp capture -o fingerprint.png --wait

# Wait for finger, then capture 5 images
sudo ./target/release/canvasbio-fp capture -o scan.png --count 5 --wait

# Just wait for finger-down event (useful for scripting)
sudo ./target/release/canvasbio-fp wait --timeout 30

# Enable debug logging
sudo RUST_LOG=debug ./target/release/canvasbio-fp capture -o fingerprint.png
```

### Optional: udev rule (run without sudo)

```bash
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="04e8", ATTR{idProduct}=="730b", MODE="0666"' \
  | sudo tee /etc/udev/rules.d/99-canvasbio-fp.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### Analysis Tools

For protocol development and debugging:

```bash
# Export USB bulk traffic from a pcapng capture
tshark -r capture.pcapng -Y "usb.transfer_type==0x03" \
  -T fields -e frame.number -e frame.time_relative \
  -e usb.endpoint_address -e usb.data_len \
  -e usb.capdata -E separator=$'\t' > /tmp/bulk_data.tsv

# Analyze the traffic
./target/release/analyze-capture /tmp/bulk_data.tsv

# Extract fingerprint images from a capture
./target/release/extract-image /tmp/bulk_data.tsv output_dir/
```

## How It Works

The sensor uses a proprietary layered protocol. See [docs/protocol.md](docs/protocol.md) for the full specification.

### Capture Protocol (4 phases)

```
Phase 1: Trigger Scan
  Write scan-mode registers (0x51=0xA825, 0x10=0x6000, 0x2F=0xF6FF, ...)
  Write reg 0x04=0x00 → hardware scan begins

Phase 2: Wait for Scan Completion
  Poll vendor request 0xDA (wValue=0x0007) every 10ms
  Returns [0x00, 0x00] while scanning, [0x00, 0x01] when ready
  Typical scan time: ~3-4 seconds with finger on sensor

Phase 3: Reconfigure for Readout
  Switch registers to readout mode (0x51=0x3A25, 0x10=0x0001, 0x2F=0xEF00)

Phase 4: Bulk Image Transfer
  Single SPI read transaction: 3B command + 10,752B image data
  42 blocks of 256 bytes via Bulk OUT/IN pairs
  Transfer time: ~70ms (with completed scan)
```

### Finger-Down Detection

```
Configure detect-mode registers → Poll SPI register 0x20
  0x0102 = no finger
  0x0107 = finger detected
```

## Project Structure

```
.
├── Cargo.toml
├── src/
│   ├── main.rs                  # CLI (clap): info, init, capture, wait
│   ├── lib.rs                   # Library root
│   ├── image.rs                 # PNG image output
│   ├── usb/
│   │   ├── mod.rs
│   │   ├── device.rs            # USB device open/close/claim (rusb)
│   │   └── transfer.rs          # Cypress MCU vendor control + bulk I/O
│   ├── sensor/
│   │   ├── mod.rs               # CanvasBioDevice struct
│   │   ├── init.rs              # Power-on, SPI enable, register config
│   │   ├── capture.rs           # 4-phase image capture protocol
│   │   └── detect.rs            # Finger-down detection via reg 0x20 polling
│   └── bin/
│       ├── analyze_capture.rs   # USB traffic analyzer
│       └── extract_image.rs     # Fingerprint image extractor from pcapng
├── docs/
│   ├── protocol.md              # Complete SPI-over-USB protocol specification
│   ├── driver-analysis.md       # Windows driver binary reverse engineering
│   └── research.md              # Initial hardware research report
└── scripts/
    ├── setup-vm.sh              # Windows VM setup for USB capture
    ├── usb-capture.sh           # tshark USB traffic capture helper
    └── usb-passthrough.xml      # QEMU USB passthrough config
```

## Reverse Engineering Methodology

This driver was built without any official documentation. The entire protocol was reverse-engineered through:

1. **Hardware probing** — USB descriptor analysis, vendor control transfer register scan (`0xB0`-`0xDE`)
2. **Windows driver binary analysis** — Ghidra disassembly of `CanvasBioFingerprintDriver.dll` (v2.4.5.882)
3. **USB traffic capture** — Windows 10 VM (QEMU/KVM) with USB passthrough, captured via usbmon/tshark
4. **Protocol decoding** — Matched captured USB traffic to driver function signatures (`cfs_writeRegister`, `cfs_readImage`, etc.)
5. **Iterative implementation** — Python prototype for validation, then Rust for the production driver

See [docs/driver-analysis.md](docs/driver-analysis.md) and [docs/research.md](docs/research.md) for detailed findings.

## Known Limitations

- **No fprintd integration yet** — Currently a standalone CLI tool. libfprint driver is planned.
- **Last block 1 byte short** — Block 41 occasionally returns 255B instead of 256B; the missing pixel is zero-padded.
- **Without finger** — Capture still works but produces a blank/dark image (scan timeout is non-fatal).

## Device Recovery

If the device firmware crashes (USB protocol error -71), a USB reset is **not sufficient**. Full power off for 30+ seconds is required.

## References

- [libfprint](https://gitlab.freedesktop.org/libfprint/libfprint) — Target upstream for Linux integration
- [Cypress PSoC USB Application Note](https://www.infineon.com/an56377) — Cypress MCU USB protocol reference
- [Microsoft Update Catalog: CanvasBio](https://www.catalog.update.microsoft.com/Search.aspx?q=CanvasBio) — Windows driver source
- [linux-hardware.org 04e8:730b](https://linux-hardware.org/?id=usb:04e8-730b) — Linux compatibility reports

## License

This project is the result of independent reverse engineering for Linux hardware compatibility.
The protocol documentation and driver code are original work.
