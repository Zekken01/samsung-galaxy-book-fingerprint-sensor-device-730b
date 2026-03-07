# canvasbio-fp

Linux userspace driver and libfprint TOD integration for the CanvasBio CB2000 fingerprint
sensor used in Samsung Galaxy Book devices (`USB 04e8:730b`).

## Current Status

Working end-to-end on Ubuntu:

- Rust CLI (`canvasbio-fp`): init, wait, capture
- libfprint TOD plugin: enrollment + verification via `fprintd`
- PAM login path: fingerprint unlock confirmed
- i18n mapper for user-facing CLI output (multi-language, English fallback)
- Actionable runtime feedback for timeout/USB error classes

## Quick Start

### 1) Build CLI

```bash
sudo apt install -y libusb-1.0-0-dev pkg-config
cargo build --release
```

### 2) Basic Sensor Test

```bash
sudo ./target/release/canvasbio-fp info
sudo ./target/release/canvasbio-fp init
sudo ./target/release/canvasbio-fp capture --wait -o fingerprint.png
```

### 3) Build/Install libfprint TOD Driver

```bash
cd libfprint-tod-canvasbio
meson setup build
ninja -C build
sudo ninja -C build install
sudo pkill -9 fprintd
```

### 4) Enroll + Verify

```bash
fprintd-enroll -f right-index-finger
fprintd-verify -f right-index-finger
```

For complete Ubuntu login setup and operational runbook, see:
[docs/operations/ubuntu-login-integration.md](docs/operations/ubuntu-login-integration.md)

## Automation Scripts

Use the following helper scripts for repeatable install/validation/rollback:

```bash
# Install CLI + TOD driver (+ optional PAM enable)
./scripts/ops/install-ubuntu-login.sh --configure-pam

# Non-interactive/CI-safe PAM update
./scripts/ops/install-ubuntu-login.sh --configure-pam-force

# Health check (device/module/fprintd/user path)
./scripts/ops/healthcheck-ubuntu-login.sh --user "$USER"

# CI/sandbox-friendly check (runtime failures downgraded to WARN)
./scripts/ops/healthcheck-ubuntu-login.sh --user "$USER" --ci

# Roll back installed TOD artifacts
./scripts/ops/rollback-ubuntu-login.sh

# Attempt automated repair for common runtime failures
./scripts/ops/repair-ubuntu-login.sh --reinstall --configure-pam
```

## Quality Gates

```bash
# Install local pre-commit hook
./scripts/quality/install-git-hooks.sh

# Run all gates together
./scripts/quality/run-quality-gates.sh

# Naming conventions
./scripts/quality/lint-naming.sh

# Layer dependency rules
./scripts/quality/lint-architecture.sh
```

CI workflow: `.github/workflows/quality-gates.yml`

## Release Packaging

For Ubuntu distribution, build a `.deb` package:

```bash
./scripts/release/build-deb.sh
```

Note: `.dmg` is a macOS format and is not used for Ubuntu deployment.

## Documentation Index

- [docs/index.md](docs/index.md)
  - full documentation map and recommended read order

### Operations

- [docs/operations/ubuntu-login-integration.md](docs/operations/ubuntu-login-integration.md)
  - install, enroll, verify, PAM ordering, login checks
- [docs/operations/fingerprint-operations.md](docs/operations/fingerprint-operations.md)
  - enrollment lifecycle, template management, user guidance and feedback

### Development

- [docs/development/i18n-feedback.md](docs/development/i18n-feedback.md)
  - key-based i18n mapper, English-fixed logs, feedback system design
- [docs/development/gap-analysis.md](docs/development/gap-analysis.md)
  - current gaps to "production-grade Ubuntu login driver" and next milestones
- [docs/architecture/domain-feature-structure.md](docs/architecture/domain-feature-structure.md)
  - domain/feature structure and migration rules
- [docs/engineering/naming-conventions.md](docs/engineering/naming-conventions.md)
  - file/directory/variable/function/code naming standard

### Reverse Engineering / Protocol

- [docs/reverse-engineering/protocol.md](docs/reverse-engineering/protocol.md) - SPI-over-USB protocol specification
- [docs/reverse-engineering/driver-analysis.md](docs/reverse-engineering/driver-analysis.md) - Windows driver reverse engineering
- [docs/reverse-engineering/research.md](docs/reverse-engineering/research.md) - early hardware and USB probing report

## Hardware Summary

- USB ID: `04e8:730b`
- Bridge MCU: Cypress/Infineon (`"CYUS"`)
- Sensor: CanvasBio CB2000
- Frame format: `112x96`, 8bpp (`10,752 bytes`)
- Architecture: host-side matching (not match-on-chip)

## Key Runtime Notes

- The sensor can enter unstable state after USB-level faults.
- CLI includes one-shot reopen/reinitialize retry for recoverable USB errors.
- If repeated `Pipe/Io/Other` errors occur, a full laptop power-off (30+ sec) may still be required.

## License

Independent reverse-engineering project for Linux hardware compatibility.
Protocol docs and driver code are original work.
