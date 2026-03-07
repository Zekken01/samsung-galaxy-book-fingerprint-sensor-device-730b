# i18n and Feedback Architecture

This project separates user-visible UX text from technical logs and protocol diagnostics.

## 1. Policy

- User-visible CLI messages: localized via key-based mapper
- Log/debug messages: fixed English for analysis consistency
- Fallback language: English

This matches the requirement: "logs stay English, only UI-visible text uses i18n."

## 2. i18n Mapper

Location:

- `src/i18n.rs`

Core model:

- `Locale` enum: `En`, `Ko`, `Ja`, `Zh`, `Es`
- `Key` enum: stable message identifiers
- `text(Key)` for static strings
- `format(Key, pairs)` for placeholder interpolation

Locale resolution:

- reads `LANG`
- maps by prefix (`ko*`, `ja*`, `zh*`, `es*`, otherwise `en`)

## 3. Why Key-Based Mapping

Compared to direct string branching:

- stable code identifiers for message reuse
- easier expansion to many languages
- lower risk of inconsistent translation updates
- easier grep/refactor and testability

## 4. Feedback System

Location:

- `src/feedback.rs`

Model:

- `FeedbackCode` classifies failures (`UsbBusy`, `UsbPermission`, `UsbPipeStall`, etc.)
- `UserFeedback` carries:
  - summary
  - actionable steps list

Entry points:

- `for_timeout_no_finger(seconds)`
- `for_error(err)`
- `print_feedback(prefix, feedback)`

## 5. Integration Points

Main CLI path:

- `src/main.rs`
- wait/capture flows call feedback handlers on timeout/error
- USB recovery retry messages are i18n-based for user display

## 6. Extending to More Languages

For each new locale:

1. add locale enum variant
2. add locale detection in `locale()`
3. fill translation for every `Key`
4. keep fallback English for missing entries

Suggested guardrail:

- add a unit test asserting every `Key` resolves non-empty text for all locales

