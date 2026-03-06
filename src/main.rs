use anyhow::Result;
use clap::Parser;
use std::path::PathBuf;
use std::time::Duration;

use canvasbio_fp::sensor::{CanvasBioDevice, SensorInit, SensorCapture, FingerDetect};

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

fn main() -> Result<()> {
    env_logger::init();
    let cli = Cli::parse();

    let mut dev = CanvasBioDevice::open()?;

    match cli.command {
        Command::Info => {
            dev.print_info()?;
        }
        Command::Init => {
            dev.print_info()?;
            dev.initialize()?;
            println!("[OK] Sensor initialized and ready.");
        }
        Command::Capture { output, count, wait, timeout } => {
            dev.initialize()?;
            let timeout_dur = Duration::from_secs(timeout);
            for i in 0..count {
                let path = if count > 1 {
                    let stem = output.file_stem().unwrap().to_str().unwrap();
                    let ext = output.extension().unwrap_or_default().to_str().unwrap();
                    output.with_file_name(format!("{}_{:03}.{}", stem, i, ext))
                } else {
                    output.clone()
                };

                if wait {
                    let detected = dev.wait_finger_down(timeout_dur)?;
                    if !detected {
                        println!("[!] Timeout: no finger detected after {}s", timeout);
                        return Ok(());
                    }
                }

                println!("[*] Capturing image #{}...", i + 1);
                let image_data = dev.capture_image()?;
                canvasbio_fp::image::save_image(&image_data, &path)?;
                println!("[OK] Saved: {}", path.display());
            }
        }
        Command::Wait { timeout } => {
            dev.initialize()?;
            let timeout_dur = Duration::from_secs(timeout);
            let detected = dev.wait_finger_down(timeout_dur)?;
            if !detected {
                println!("[!] Timeout: no finger detected after {}s", timeout);
                std::process::exit(1);
            }
        }
    }

    Ok(())
}
