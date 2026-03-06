pub fn usb_operation_error(op: &str, err: rusb::Error) -> anyhow::Error {
    let hint = match err {
        rusb::Error::Timeout => {
            "Timeout. Retry once, then reinitialize the sensor (`canvasbio-fp init`)."
        }
        rusb::Error::Pipe => {
            "Endpoint stalled. Reconnect USB, then retry. If this repeats, power off for 30+ seconds."
        }
        rusb::Error::NoDevice => {
            "Device disconnected or re-enumerated. Reconnect and retry."
        }
        rusb::Error::Access => {
            "Permission denied. Run as root or install the udev rule."
        }
        rusb::Error::Busy => {
            "Interface busy. Stop other processes using the fingerprint sensor (for example fprintd)."
        }
        rusb::Error::Io | rusb::Error::Other => {
            "Low-level USB I/O failure. If USB reset does not recover it, perform full power-off for 30+ seconds."
        }
        _ => "Check cable/connection and retry with `RUST_LOG=debug` for details.",
    };

    anyhow::Error::new(err).context(format!("USB {op} failed. Recovery: {hint}"))
}

pub fn classify_usb_error(err: &anyhow::Error) -> Option<rusb::Error> {
    err.chain()
        .find_map(|cause| cause.downcast_ref::<rusb::Error>().copied())
}

pub fn is_retryable_usb_error(kind: rusb::Error) -> bool {
    matches!(
        kind,
        rusb::Error::Timeout
            | rusb::Error::Pipe
            | rusb::Error::Interrupted
            | rusb::Error::Io
            | rusb::Error::Other
    )
}

pub fn should_suggest_power_cycle(kind: rusb::Error) -> bool {
    matches!(
        kind,
        rusb::Error::Pipe | rusb::Error::Io | rusb::Error::Other
    )
}
