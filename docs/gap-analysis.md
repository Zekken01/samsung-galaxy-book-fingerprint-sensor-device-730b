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

## 2. Remaining Gaps

### A. Reliability Hardening

- Need broader soak testing across suspend/resume and long uptime cycles
- Need repeatability metrics for false reject/accept over many sessions
- Need explicit regression suite for USB fault-recovery paths

### B. Packaging and Upgrade Safety

- No distro package currently (manual build/install flow)
- No post-install health check script
- No automated rollback path when driver update fails

### C. Test Automation

- Limited automated coverage for protocol + i18n key completeness + feedback mapping
- No CI pipeline for TOD build + CLI tests on clean Ubuntu containers

### D. User Experience

- CLI feedback exists, but no dedicated GUI enrollment coach
- No structured quality score surfaced to user per enrollment attempt
- Operational guidance is docs-based, not in-app workflow

### E. Upstream/Distribution Path

- No upstream submission workflow documented for libfprint/mainline adoption
- ABI/API drift risk across libfprint versions needs compatibility matrix

## 3. Priority Order

1. Reliability test matrix (suspend/resume, reboot cycles, repeated unlock)
2. CI + regression tests (protocol/i18n/feedback/TOD build)
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

