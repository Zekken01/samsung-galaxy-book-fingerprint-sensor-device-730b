use anyhow::Error;

use crate::domain::usb::error::classify_usb_error;
use crate::platform::i18n;
use crate::platform::i18n::Locale;

#[derive(Clone, Copy, Debug)]
pub enum FeedbackCode {
    NoFingerDetected,
    UsbBusy,
    UsbPermission,
    UsbTimeout,
    UsbPipeStall,
    UsbDisconnected,
    UsbIo,
    Unknown,
}

pub struct UserFeedback {
    pub code: FeedbackCode,
    pub summary: String,
    pub actions: Vec<String>,
}

pub fn for_timeout_no_finger(seconds: u64) -> UserFeedback {
    match i18n::locale() {
        Locale::Ko => UserFeedback {
            code: FeedbackCode::NoFingerDetected,
            summary: format!("{seconds}초 동안 손가락이 감지되지 않았습니다."),
            actions: vec![
                "손가락 전체 면적을 센서 중앙에 평평하게 올려주세요.".to_string(),
                "센서를 닦은 뒤 1~2초 유지한 상태로 다시 시도하세요.".to_string(),
                "지속 실패 시 `fprintd-verify`로 등록 품질을 점검하세요.".to_string(),
            ],
        },
        _ => UserFeedback {
            code: FeedbackCode::NoFingerDetected,
            summary: format!("No finger was detected for {seconds} seconds."),
            actions: vec![
                "Place the full pad of your finger flat on the sensor center.".to_string(),
                "Clean the sensor and hold for 1-2 seconds before lifting.".to_string(),
                "If failures continue, run `fprintd-verify` to check enrollment quality."
                    .to_string(),
            ],
        },
    }
}

pub fn for_error(err: &Error) -> UserFeedback {
    let code = match classify_usb_error(err) {
        Some(rusb::Error::Busy) => FeedbackCode::UsbBusy,
        Some(rusb::Error::Access) => FeedbackCode::UsbPermission,
        Some(rusb::Error::Timeout) => FeedbackCode::UsbTimeout,
        Some(rusb::Error::Pipe) => FeedbackCode::UsbPipeStall,
        Some(rusb::Error::NoDevice) => FeedbackCode::UsbDisconnected,
        Some(rusb::Error::Io | rusb::Error::Other) => FeedbackCode::UsbIo,
        _ => FeedbackCode::Unknown,
    };

    match (i18n::locale(), code) {
        (Locale::Ko, FeedbackCode::UsbBusy) => UserFeedback {
            code,
            summary: "센서 인터페이스가 다른 프로세스에 의해 사용 중입니다.".to_string(),
            actions: vec![
                "`fprintd` 또는 다른 캡처 도구를 중지한 뒤 다시 시도하세요.".to_string(),
                "동시 실행 중인 지문/생체인식 프로그램이 없는지 확인하세요.".to_string(),
            ],
        },
        (Locale::Ko, FeedbackCode::UsbPermission) => UserFeedback {
            code,
            summary: "센서 접근 권한이 없습니다.".to_string(),
            actions: vec![
                "root 권한으로 실행하거나 udev 규칙을 설치하세요.".to_string(),
                "규칙 적용 후 `udevadm control --reload-rules && udevadm trigger`를 실행하세요."
                    .to_string(),
            ],
        },
        (Locale::Ko, FeedbackCode::UsbPipeStall) => UserFeedback {
            code,
            summary: "USB 엔드포인트가 stall 상태입니다.".to_string(),
            actions: vec![
                "USB를 다시 연결하고 재시도하세요.".to_string(),
                "반복되면 노트북 전원을 30초 이상 완전히 끄고 재부팅하세요.".to_string(),
            ],
        },
        (Locale::Ko, FeedbackCode::UsbTimeout) => UserFeedback {
            code,
            summary: "USB 통신이 시간 초과되었습니다.".to_string(),
            actions: vec![
                "잠시 후 한 번 더 시도하세요.".to_string(),
                "계속 실패하면 `canvasbio-fp init`으로 재초기화하세요.".to_string(),
            ],
        },
        (Locale::Ko, FeedbackCode::UsbDisconnected) => UserFeedback {
            code,
            summary: "장치가 분리되었거나 재열거되었습니다.".to_string(),
            actions: vec![
                "장치 연결 상태를 확인하고 다시 시도하세요.".to_string(),
                "`lsusb -d 04e8:730b`로 인식 여부를 확인하세요.".to_string(),
            ],
        },
        (Locale::Ko, FeedbackCode::UsbIo) => UserFeedback {
            code,
            summary: "저수준 USB I/O 오류가 발생했습니다.".to_string(),
            actions: vec![
                "재시도 후에도 실패하면 전원을 30초 이상 완전히 끄세요.".to_string(),
                "문제 지속 시 `RUST_LOG=debug` 로그를 수집하세요.".to_string(),
            ],
        },
        (Locale::Ko, _) => UserFeedback {
            code,
            summary: "알 수 없는 오류가 발생했습니다.".to_string(),
            actions: vec![
                "다시 시도하고 동일하면 디버그 로그를 확인하세요.".to_string(),
                "`RUST_LOG=debug`로 실행 후 오류 코드를 공유하세요.".to_string(),
            ],
        },
        (_, FeedbackCode::UsbBusy) => UserFeedback {
            code,
            summary: "Sensor interface is busy by another process.".to_string(),
            actions: vec![
                "Stop `fprintd` or other capture tools, then retry.".to_string(),
                "Ensure only one biometric process is using the sensor.".to_string(),
            ],
        },
        (_, FeedbackCode::UsbPermission) => UserFeedback {
            code,
            summary: "Permission denied when accessing the sensor.".to_string(),
            actions: vec![
                "Run as root or install the udev rule.".to_string(),
                "Reload rules with `udevadm control --reload-rules && udevadm trigger`."
                    .to_string(),
            ],
        },
        (_, FeedbackCode::UsbPipeStall) => UserFeedback {
            code,
            summary: "USB endpoint entered a stalled state.".to_string(),
            actions: vec![
                "Reconnect USB and retry.".to_string(),
                "If repeated, fully power off the laptop for 30+ seconds.".to_string(),
            ],
        },
        (_, FeedbackCode::UsbTimeout) => UserFeedback {
            code,
            summary: "USB communication timed out.".to_string(),
            actions: vec![
                "Retry once after a short pause.".to_string(),
                "If it keeps failing, run `canvasbio-fp init` first.".to_string(),
            ],
        },
        (_, FeedbackCode::UsbDisconnected) => UserFeedback {
            code,
            summary: "Device disconnected or re-enumerated.".to_string(),
            actions: vec![
                "Check connection and retry.".to_string(),
                "Verify with `lsusb -d 04e8:730b`.".to_string(),
            ],
        },
        (_, FeedbackCode::UsbIo) => UserFeedback {
            code,
            summary: "Low-level USB I/O error occurred.".to_string(),
            actions: vec![
                "Retry once; if it persists, power off for 30+ seconds.".to_string(),
                "Collect logs using `RUST_LOG=debug`.".to_string(),
            ],
        },
        (_, _) => UserFeedback {
            code,
            summary: "Unknown error occurred.".to_string(),
            actions: vec![
                "Retry and collect debug logs if it repeats.".to_string(),
                "Run with `RUST_LOG=debug` and share the output.".to_string(),
            ],
        },
    }
}

pub fn print_feedback(prefix: &str, feedback: &UserFeedback) {
    eprintln!("[!] {prefix} [{:?}]", feedback.code);
    eprintln!("    {}", feedback.summary);
    for action in &feedback.actions {
        eprintln!("    - {}", action);
    }
}
