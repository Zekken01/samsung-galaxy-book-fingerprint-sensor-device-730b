# Gap Analysis: Toward Production Ubuntu Fingerprint Driver

This analysis compares current implementation state vs a production-ready Ubuntu
login driver experience.

## 1. Current Achievements

- Device protocol reverse-engineered and documented
- Stable init/wait/capture path in Rust CLI
- TOD driver integrated with libfprint/fprintd
- Real enrollment + verify + lock-screen unlock validated
- User-facing i18n mapper introduced
- Actionable CLI feedback for major runtime errors
- Domain/feature/platform/shared source boundaries applied
- Naming and architecture lint scripts implemented
- CI quality gates (`cargo check`, naming lint, architecture lint, fmt check, clippy) added
- Install/healthcheck/repair/rollback automation scripts added

## 2. Remaining Gaps

### A. Reliability Hardening

- Need broader soak testing across suspend/resume and long uptime cycles
- Need repeatability metrics for false reject/accept over many sessions
- Need explicit regression suite for USB fault-recovery paths

### B. Packaging and Upgrade Safety

- No distro package currently (manual/script install flow)
- No automated rollback path when driver update fails

### C. Test Automation

- Limited automated coverage for protocol + i18n key completeness + feedback mapping
- CI exists, but hardware-in-the-loop regression is not automated yet

### D. User Experience

- CLI feedback exists, but no dedicated GUI enrollment coach
- No structured quality score surfaced to user per enrollment attempt
- Operational guidance is mostly docs/script-based, not in-app workflow

### E. Upstream/Distribution Path

- No upstream submission workflow documented for libfprint/mainline adoption
- ABI/API drift risk across libfprint versions needs compatibility matrix

## 3. Priority Order

1. Reliability test matrix (suspend/resume, reboot cycles, repeated unlock)
2. Hardware-in-the-loop regression tests (real sensor + fprintd scenarios)
3. Packaging (`.deb`) + install/rollback scripts
4. UX enhancements (enrollment quality coaching)
5. Upstream strategy and long-term maintenance plan

## 4. Definition of Done (Suggested)

Driver can be considered production-grade when:

- unlock success rate target is met in repeated real-user scenarios
- update/install is one-command and reversible
- regression suite passes in CI for supported Ubuntu versions
- localization quality is complete for supported locales
- troubleshooting steps are deterministic and documented
