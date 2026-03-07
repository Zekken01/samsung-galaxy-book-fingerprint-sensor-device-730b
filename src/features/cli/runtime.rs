use anyhow::{anyhow, Result};
use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::time::Duration;

use crate::domain::sensor::{CanvasBioDevice, FingerDetect, SensorCapture, SensorInfo, SensorInit};
use crate::domain::usb::error::{
    classify_usb_error, is_retryable_usb_error, should_suggest_power_cycle,
};
use crate::platform::feedback;
use crate::platform::i18n;
use crate::platform::i18n::Key;

use super::args::Command;
use super::commands;

pub(super) fn numbered_output_path(output: &Path, count: u32, index: u32) -> PathBuf {
    if count <= 1 {
        return output.to_path_buf();
    }

    let parent = output.parent().unwrap_or_else(|| Path::new(""));
    let stem = output.file_stem().unwrap_or(output.as_os_str());
    let mut name = OsString::from(stem);
    name.push(format!("_{:03}", index));

    if let Some(ext) = output.extension() {
        name.push(".");
        name.push(ext);
    }

    parent.join(name)
}

pub(super) fn print_sensor_info(info: &SensorInfo) {
    println!(
        "  Cypress MCU: {} ({:02x?})",
        String::from_utf8_lossy(&info.cypress_magic),
        info.cypress_magic
    );
    println!(
        "{} v{}.{}.{}, mode='{}'",
        i18n::text(Key::FirmwareLabel),
        info.firmware_major,
        info.firmware_minor,
        info.firmware_patch,
        info.firmware_mode
    );
    println!("{} {}", i18n::text(Key::BuildLabel), info.build);
    println!("{} {}", i18n::text(Key::ReadyLabel), info.ready);
}

fn reopen_and_initialize() -> Result<CanvasBioDevice> {
    let mut dev = CanvasBioDevice::open()?;
    dev.initialize()?;
    Ok(dev)
}

fn usb_recovery_enabled() -> bool {
    match std::env::var("CANVASBIO_USB_RECOVERY") {
        Ok(v) => !matches!(v.as_str(), "0" | "false" | "FALSE" | "off" | "OFF"),
        Err(_) => true,
    }
}

pub(super) fn with_dev_mut(dev: &mut Option<CanvasBioDevice>) -> Result<&mut CanvasBioDevice> {
    dev.as_mut()
        .ok_or_else(|| anyhow!("device handle missing during recovery"))
}

fn reopen_in_place(dev: &mut Option<CanvasBioDevice>) -> Result<()> {
    let _ = dev.take();
    *dev = Some(reopen_and_initialize()?);
    Ok(())
}

pub(super) fn capture_with_usb_recovery(dev: &mut Option<CanvasBioDevice>) -> Result<Vec<u8>> {
    let first = with_dev_mut(dev)?.capture_image();
    match first {
        Ok(image) => Ok(image),
        Err(err) => {
            let fb = feedback::for_error(&err);
            feedback::print_feedback(
                i18n::text(Key::CaptureRetrying)
                    .split('.')
                    .next()
                    .unwrap_or("Capture issue"),
                &fb,
            );
            if !usb_recovery_enabled() {
                return Err(err);
            }
            let Some(kind) = classify_usb_error(&err) else {
                return Err(err);
            };
            if !is_retryable_usb_error(kind) {
                return Err(err);
            }

            eprintln!(
                "{}",
                i18n::format(Key::CaptureRetrying, &[("kind", format!("{kind:?}"))])
            );
            if should_suggest_power_cycle(kind) {
                eprintln!("{}", i18n::text(Key::PowerOffHint));
            }

            reopen_in_place(dev)?;
            with_dev_mut(dev)?.capture_image()
        }
    }
}

pub(super) fn wait_with_usb_recovery(
    dev: &mut Option<CanvasBioDevice>,
    timeout: Duration,
) -> Result<bool> {
    let first = with_dev_mut(dev)?.wait_finger_down(timeout);
    match first {
        Ok(detected) => Ok(detected),
        Err(err) => {
            let fb = feedback::for_error(&err);
            feedback::print_feedback(
                i18n::text(Key::WaitRetrying)
                    .split('.')
                    .next()
                    .unwrap_or("Wait issue"),
                &fb,
            );
            if !usb_recovery_enabled() {
                return Err(err);
            }
            let Some(kind) = classify_usb_error(&err) else {
                return Err(err);
            };
            if !is_retryable_usb_error(kind) {
                return Err(err);
            }

            eprintln!(
                "{}",
                i18n::format(Key::WaitRetrying, &[("kind", format!("{kind:?}"))])
            );
            reopen_in_place(dev)?;
            with_dev_mut(dev)?.wait_finger_down(timeout)
        }
    }
}

pub fn execute(command: Command) -> Result<()> {
    let mut dev = Some(CanvasBioDevice::open()?);
    println!(
        "{}",
        i18n::format(
            Key::DeviceOpened,
            &[("vid", "04e8".to_string()), ("pid", "730b".to_string())]
        )
    );

    match command {
        Command::Info => commands::info::run_info(&mut dev),
        Command::Init => commands::init::run_init(&mut dev),
        Command::Capture {
            output,
            count,
            wait,
            timeout,
        } => commands::capture::run_capture(&mut dev, output, count, wait, timeout),
        Command::Wait { timeout } => commands::wait::run_wait(&mut dev, timeout),
    }
}
