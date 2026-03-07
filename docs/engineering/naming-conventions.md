# Naming Conventions

This document standardizes naming for files, directories, variables, functions, and code identifiers.

## 1. General

- Use English for identifiers and comments.
- Use descriptive names; avoid abbreviations unless protocol-defined (`spi`, `usb`, `irq`).
- Keep naming stable; prefer extending enums/structs over ad-hoc string literals.

## 2. Files and Directories

- Rust module files/directories: `snake_case`
  - examples: `capture.rs`, `driver_analysis.rs`
- Script files: `kebab-case.sh`
  - examples: `install-ubuntu-login.sh`
- Markdown docs: `kebab-case.md`
  - examples: `gap-analysis.md`

## 3. Rust Code

- Modules/files: `snake_case`
- Functions/methods: `snake_case`
- Variables/parameters: `snake_case`
- Types (`struct`, `enum`, `trait`): `UpperCamelCase`
- Constants/statics: `UPPER_SNAKE_CASE`
- Enum variants: `UpperCamelCase`

Examples:

- good: `wait_finger_down`, `CanvasBioDevice`, `TIMEOUT_SCAN`
- avoid: `waitFingerDown`, `canvasbio_device`, `timeoutScan`

## 4. C Code (TOD driver)

- Functions: `snake_case` with domain prefix where needed
  - examples: `sync_probe_scan_ready`, `canvasbio_capture_frame`
- Macros/constants: `UPPER_SNAKE_CASE`
- Struct typedef names: `snake_case_t` or project-consistent existing style

## 5. i18n and Feedback Names

- i18n keys: `UpperCamelCase` enum variants in `Key`
  - examples: `WaitingFinger`, `TimeoutNoFinger`
- Feedback code: `UpperCamelCase` enum variants in `FeedbackCode`
  - examples: `UsbPipeStall`, `NoFingerDetected`
- Do not embed user-visible strings directly in business logic.
  - use `i18n::text(Key::...)` or `i18n::format(Key::..., ...)`

## 6. Command/Option Names

- CLI commands/options use lowercase and kebab-case by default (`clap` standard).
- Script options use long-form kebab-case.
  - examples: `--configure-pam`, `--verify-finger`

## 7. Logging and Message Policy

- Debug/error logs for diagnostics: English fixed.
- User-facing UI/CLI output: i18n-managed.
- Keep error category names stable (`UsbBusy`, `UsbTimeout`) for automation.

## 8. Forbidden Patterns

- Single-letter variable names outside tiny local loops (`i`, `j` allowed for loop index only)
- Ambiguous suffixes like `_data2`, `_tmp2`
- Mixed naming styles in the same module

## 9. Automated Check

Run:

```bash
./scripts/lint-naming.sh
```
