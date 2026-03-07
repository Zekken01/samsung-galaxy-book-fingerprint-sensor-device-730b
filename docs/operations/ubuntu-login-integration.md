# Ubuntu Login Integration Guide

This guide covers how to run the CanvasBio (`04e8:730b`) sensor as a real Ubuntu
fingerprint login path using the TOD driver in this repository.

## 1. Prerequisites

- Ubuntu with `fprintd` installed
- Sensor visible in USB:

```bash
lsusb -d 04e8:730b
```

- Build dependencies:

```bash
sudo apt install -y meson ninja-build pkg-config libfprint-2-dev libusb-1.0-0-dev
```

## 2. Build and Install TOD Driver

Preferred (automated):

```bash
./scripts/ops/install-ubuntu-login.sh --configure-pam
```

Manual:

```bash
cd libfprint-tod-canvasbio
meson setup build --reconfigure
ninja -C build
sudo ninja -C build install
sudo udevadm control --reload-rules
sudo udevadm trigger
sudo systemctl restart fprintd || sudo pkill -9 fprintd
```

## 3. Enrollment and Verification

List device:

```bash
fprintd-list "$USER"
```

Enroll a finger:

```bash
fprintd-enroll -f right-index-finger
```

Verify:

```bash
fprintd-verify -f right-index-finger
```

If verify succeeds repeatedly, the biometric path is functional.

## 4. PAM Ordering for Login UX

When multiple biometric modules are present (for example `pam_fprintd` + `pam_howdy`),
ensure fingerprint comes first for faster unlock.

File: `/etc/pam.d/common-auth`

- `pam_fprintd.so` should be evaluated before secondary biometric modules
- success jump counts must be adjusted so control flow remains correct

After editing:

```bash
sudo pam-auth-update
```

Then lock screen and validate fingerprint unlock.

## 5. Operational Checks

Run these checks after updates:

1. `lsusb -d 04e8:730b`
2. `fprintd-list "$USER"`
3. `fprintd-verify -f <finger>`
4. Lock screen and test real unlock path

## 6. Troubleshooting

- `Access` / permission:
  - run with proper privileges or install udev rule
- `Busy`:
  - stop conflicting processes (`fprintd`, capture tools)
- repeated `Pipe`/`Io`:
  - retry once, then full power-off for 30+ seconds if persistent

For CLI-level diagnostics:

```bash
RUST_LOG=debug ./target/release/canvasbio-fp wait --timeout 5
```

## 7. Health Check and Rollback

Health check:

```bash
./scripts/ops/healthcheck-ubuntu-login.sh --user "$USER"
```

CI/sandbox environment check:

```bash
./scripts/ops/healthcheck-ubuntu-login.sh --user "$USER" --ci
```

Optional interactive verify during health check:

```bash
./scripts/ops/healthcheck-ubuntu-login.sh --user "$USER" --finger right-index-finger
```

Rollback:

```bash
./scripts/ops/rollback-ubuntu-login.sh
```

If you previously changed `/etc/pam.d/common-auth` and kept a backup:

```bash
./scripts/ops/rollback-ubuntu-login.sh --restore-common-auth /path/to/common-auth.backup
```

## 8. Automated Repair Flow

For common failures (service state drift, stale install, PAM mismatch), run:

```bash
./scripts/ops/repair-ubuntu-login.sh --reinstall --configure-pam
```

Optional interactive verify at the end:

```bash
./scripts/ops/repair-ubuntu-login.sh --reinstall --configure-pam --verify-finger right-index-finger
```
