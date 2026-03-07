use anyhow::Result;
use std::path::PathBuf;
use std::time::Duration;

use crate::domain::sensor::{CanvasBioDevice, SensorInit};
use crate::platform::feedback;
use crate::platform::i18n;
use crate::platform::i18n::Key;

use super::super::runtime::{
    capture_with_usb_recovery, numbered_output_path, wait_with_usb_recovery, with_dev_mut,
};

pub fn run_capture(
    dev: &mut Option<CanvasBioDevice>,
    output: PathBuf,
    count: u32,
    wait: bool,
    timeout: u64,
) -> Result<()> {
    with_dev_mut(dev)?.initialize()?;
    let timeout_duration = Duration::from_secs(timeout);

    for index in 0..count {
        let path = numbered_output_path(&output, count, index);

        if wait {
            println!("{}", i18n::text(Key::WaitingFinger));
            let detected = wait_with_usb_recovery(dev, timeout_duration)?;
            if !detected {
                let user_feedback = feedback::for_timeout_no_finger(timeout);
                feedback::print_feedback("No finger detected", &user_feedback);
                println!(
                    "{}",
                    i18n::format(Key::TimeoutNoFinger, &[("seconds", timeout.to_string())])
                );
                return Ok(());
            }
            println!("{}", i18n::text(Key::FingerDetected));
        }

        println!(
            "{}",
            i18n::format(Key::CaptureImage, &[("index", (index + 1).to_string())])
        );
        let image_data = capture_with_usb_recovery(dev)?;
        crate::shared::image::save_image(&image_data, &path)?;
        println!(
            "{}",
            i18n::format(Key::SavedPath, &[("path", path.display().to_string())])
        );
    }

    Ok(())
}
