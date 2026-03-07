# Documentation Map

This map defines what each document is for and which order to read.

## 1) Start Here (Operations)

- `operations/ubuntu-login-integration.md`
  - install, enroll, verify, PAM flow, healthcheck/repair/rollback
- `operations/fingerprint-operations.md`
  - enrollment lifecycle, delete/re-enroll, user guidance

## 2) Start Here (Development)

- `architecture/domain-feature-structure.md`
  - source layout and dependency direction rules
- `engineering/naming-conventions.md`
  - naming policy and automated checks
- `development/i18n-feedback.md`
  - i18n key mapper and feedback model
- `development/gap-analysis.md`
  - production readiness gaps and priorities

## 3) Reverse Engineering Knowledge Base

- `reverse-engineering/protocol.md`
  - decoded SPI-over-USB protocol used by the Linux implementation
- `reverse-engineering/driver-analysis.md`
  - Windows binary reverse engineering notes
- `reverse-engineering/research.md`
  - initial hardware probing and early-stage findings

## 4) Suggested Read Order

For contributors:

1. `architecture/domain-feature-structure.md`
2. `engineering/naming-conventions.md`
3. `operations/ubuntu-login-integration.md`
4. `reverse-engineering/protocol.md`

For reverse engineering detail:

1. `reverse-engineering/research.md`
2. `reverse-engineering/driver-analysis.md`
3. `reverse-engineering/protocol.md`

## 5) Document Ownership Rule

- Operational runbooks should reflect current validated behavior.
- Reverse engineering documents may include historical findings.
- If current implementation differs from legacy notes, prefer:
  - source code under `src/`
  - runbooks in `docs/operations/ubuntu-login-integration.md`
  - architecture rules in `docs/architecture/domain-feature-structure.md`

## 6) Domain/Feature Coverage Status

- Already aligned with domain/feature architecture:
  - `architecture/domain-feature-structure.md`
  - `engineering/naming-conventions.md`
  - `development/i18n-feedback.md`
  - `operations/ubuntu-login-integration.md`
- Maintained as reverse engineering references:
  - `reverse-engineering/protocol.md`
  - `reverse-engineering/driver-analysis.md`
  - `reverse-engineering/research.md`
