use anyhow::Result;
use std::time::Duration;

use crate::domain::sensor::{CanvasBioDevice, SensorInit};
use crate::platform::feedback;
use crate::platform::i18n;
use crate::platform::i18n::Key;

use super::super::runtime::{wait_with_usb_recovery, with_dev_mut};

pub fn run_wait(dev: &mut Option<CanvasBioDevice>, timeout: u64) -> Result<()> {
    with_dev_mut(dev)?.initialize()?;
    let timeout_duration = Duration::from_secs(timeout);
    println!("{}", i18n::text(Key::WaitingFinger));
    let detected = wait_with_usb_recovery(dev, timeout_duration)?;
    if !detected {
        let user_feedback = feedback::for_timeout_no_finger(timeout);
        feedback::print_feedback("No finger detected", &user_feedback);
        println!(
            "{}",
            i18n::format(Key::TimeoutNoFinger, &[("seconds", timeout.to_string())])
        );
        std::process::exit(1);
    }
    println!("{}", i18n::text(Key::FingerDetected));
    Ok(())
}
