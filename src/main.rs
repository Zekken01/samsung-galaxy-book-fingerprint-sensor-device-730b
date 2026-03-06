use anyhow::{anyhow, Result};
use clap::Parser;
use std::ffi::OsString;
use std::path::Path;
use std::path::PathBuf;
use std::time::Duration;

use canvasbio_fp::sensor::{CanvasBioDevice, FingerDetect, SensorCapture, SensorInit};
use canvasbio_fp::usb::error::{
    classify_usb_error, is_retryable_usb_error, should_suggest_power_cycle,
};

#[derive(Parser)]
#[command(name = "canvasbio-fp")]
#[command(about = "CanvasBio CB2000 Fingerprint Sensor Driver (04e8:730b)")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(clap::Subcommand)]
enum Command {
    /// Show sensor information
    Info,
    /// Check sensor status and initialize
    Init,
    /// Capture a fingerprint image
    Capture {
        /// Output file path
        #[arg(short, long, default_value = "fingerprint.png")]
        output: PathBuf,
        /// Number of images to capture
        #[arg(short, long, default_value = "1")]
        count: u32,
        /// Wait for finger-down before capturing
        #[arg(short, long)]
        wait: bool,
        /// Timeout in seconds for finger-down wait (default: 30)
        #[arg(short, long, default_value = "30")]
        timeout: u64,
    },
    /// Wait for a finger-down event on the sensor
    Wait {
        /// Timeout in seconds (default: 30)
        #[arg(short, long, default_value = "30")]
        timeout: u64,
    },
}

fn numbered_output_path(output: &Path, count: u32, index: u32) -> PathBuf {
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

fn with_dev_mut(dev: &mut Option<CanvasBioDevice>) -> Result<&mut CanvasBioDevice> {
    dev.as_mut()
        .ok_or_else(|| anyhow!("device handle missing during recovery"))
}

fn reopen_in_place(dev: &mut Option<CanvasBioDevice>) -> Result<()> {
    let _ = dev.take();
    *dev = Some(reopen_and_initialize()?);
    Ok(())
}

fn capture_with_usb_recovery(dev: &mut Option<CanvasBioDevice>) -> Result<Vec<u8>> {
    let first = with_dev_mut(dev)?.capture_image();
    match first {
        Ok(image) => Ok(image),
        Err(err) => {
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
                "[!] USB error during capture ({kind:?}). Reopening device and retrying once..."
            );
            if should_suggest_power_cycle(kind) {
                eprintln!("[!] If retry fails again, fully power off the laptop for 30+ seconds.");
            }

            reopen_in_place(dev)?;
            with_dev_mut(dev)?.capture_image()
        }
    }
}

fn wait_with_usb_recovery(dev: &mut Option<CanvasBioDevice>, timeout: Duration) -> Result<bool> {
    let first = with_dev_mut(dev)?.wait_finger_down(timeout);
    match first {
        Ok(detected) => Ok(detected),
        Err(err) => {
            if !usb_recovery_enabled() {
                return Err(err);
            }
            let Some(kind) = classify_usb_error(&err) else {
                return Err(err);
            };
            if !is_retryable_usb_error(kind) {
                return Err(err);
            }

            eprintln!("[!] USB error while waiting for finger ({kind:?}). Retrying once...");
            reopen_in_place(dev)?;
            with_dev_mut(dev)?.wait_finger_down(timeout)
        }
    }
}

fn main() -> Result<()> {
    env_logger::init();
    let cli = Cli::parse();

    let mut dev = Some(CanvasBioDevice::open()?);

    match cli.command {
        Command::Info => {
            with_dev_mut(&mut dev)?.print_info()?;
        }
        Command::Init => {
            with_dev_mut(&mut dev)?.print_info()?;
            with_dev_mut(&mut dev)?.initialize()?;
            println!("[OK] Sensor initialized and ready.");
        }
        Command::Capture {
            output,
            count,
            wait,
            timeout,
        } => {
            with_dev_mut(&mut dev)?.initialize()?;
            let timeout_dur = Duration::from_secs(timeout);
            for i in 0..count {
                let path = numbered_output_path(&output, count, i);

                if wait {
                    let detected = wait_with_usb_recovery(&mut dev, timeout_dur)?;
                    if !detected {
                        println!("[!] Timeout: no finger detected after {}s", timeout);
                        return Ok(());
                    }
                }

                println!("[*] Capturing image #{}...", i + 1);
                let image_data = capture_with_usb_recovery(&mut dev)?;
                canvasbio_fp::image::save_image(&image_data, &path)?;
                println!("[OK] Saved: {}", path.display());
            }
        }
        Command::Wait { timeout } => {
            with_dev_mut(&mut dev)?.initialize()?;
            let timeout_dur = Duration::from_secs(timeout);
            let detected = wait_with_usb_recovery(&mut dev, timeout_dur)?;
            if !detected {
                println!("[!] Timeout: no finger detected after {}s", timeout);
                std::process::exit(1);
            }
        }
    }

    Ok(())
}
