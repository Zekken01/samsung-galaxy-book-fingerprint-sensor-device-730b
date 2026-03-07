# Domain/Feature Directory Structure

This document defines the repository structure based on domain boundaries and feature flows.

## 1. Top-Level Principles

- `domain`: hardware protocol and device control logic
- `features`: user-facing workflows (CLI commands, tooling commands)
- `docs`: architecture, operations, protocol knowledge
- `scripts`: installation, healthcheck, repair, rollback automation

## 2. Current Source Layout (Phase 4 Applied)

```text
src/
  domain/
    sensor/     # fingerprint sensor state/config/capture/detect
    usb/        # USB transport, control/bulk transfer, USB error classification
  features/
    cli/        # user CLI application flow (args/runtime/commands split)
    tools/      # protocol analysis/extraction tools (parser/core/output split)
  platform/
    i18n.rs     # user-facing i18n mapper
    feedback.rs # user feedback model and remediation hints
  shared/
    image.rs    # shared image output utility
  bin/          # thin wrappers to feature tools
  main.rs       # thin wrapper to features::cli
```

## 3. Next Target Layout (Phase 5)

```text
src/
  domain/
    sensor/
    transport/usb/
  features/
    cli/
      commands/ (info/init/capture/wait)
    tools/
      analyze_capture/ (parser + report)
      extract_image/ (core + output)
  platform/
    i18n/
    feedback/
  shared/
    image/
```

## 4. Dependency Direction Rules

Allowed direction:

1. `features` -> `domain`, `platform`, `shared`
2. `platform` -> `domain`, `shared`
3. `domain` -> `shared` only

Not allowed:

- `domain` must not depend on `features`
- `shared` must not depend on `features` or `platform`

Enforced by script:

```bash
./scripts/quality/lint-architecture.sh
```

Current policy detail:

- `domain -> platform` is fully blocked.

## 5. Migration Rule

- New code should import from `crate::domain::*` directly.
- Prefer `crate::platform::*`, `crate::features::*`, `crate::shared::*` in new modules.
- Feature CLI can be further split to `commands/*` as needed.
