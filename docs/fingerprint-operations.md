# Fingerprint Operations Runbook

This document explains practical fingerprint lifecycle operations for users and admins.

## 1. Enrollment

Recommended flow:

1. Clean sensor surface.
2. Enroll one primary finger:
   - `fprintd-enroll -f right-index-finger`
3. Enroll one backup finger:
   - `fprintd-enroll -f right-thumb`
4. Verify both:
   - `fprintd-verify -f right-index-finger`
   - `fprintd-verify -f right-thumb`

## 2. Management (List / Delete / Re-enroll)

List enrolled fingers:

```bash
fprintd-list "$USER"
```

Delete one finger:

```bash
fprintd-delete "$USER" -f right-index-finger
```

Delete all enrolled fingers:

```bash
fprintd-delete "$USER"
```

Re-enrollment is recommended after repeated false rejects.

## 3. Quality Guidelines

- Place the full finger pad flat at the sensor center.
- Hold for 1-2 seconds; avoid quick tap-and-release.
- Re-enroll if fingers are enrolled with inconsistent angle/pressure.

## 4. User Feedback System (Implemented)

The Rust CLI includes actionable feedback for common failures:

- No finger detected (timeout)
- USB busy/permission/timeout/pipe/disconnect/low-level I/O classes

Location:

- `src/feedback.rs`

Behavior:

- provides summary + concrete next actions
- localizes user-facing feedback by locale
- keeps technical logs/diagnostics in English where needed for supportability

## 5. What Exists vs What Does Not

Exists now:

- enrollment and verification via `fprintd`
- delete/list management via `fprintd-*` commands
- localized user-facing CLI guidance

Not implemented in this repo:

- custom GUI for enrollment management
- adaptive coaching loop (for example per-attempt placement scoring)
- centralized telemetry dashboard for fleet/admin usage

## 6. Recommended Routine for End Users

1. Enroll two fingers.
2. Verify both once per boot after major driver updates.
3. If unlock quality drops:
   - clean sensor
   - re-enroll affected finger
   - test on lock screen before relying on login-only path

