use anyhow::Result;
use clap::Parser;
use std::path::PathBuf;

use canvasbio_fp::sensor::{CanvasBioDevice, SensorInit, SensorCapture};

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
        Command::Capture { output, count } => {
            dev.initialize()?;
            for i in 0..count {
                let path = if count > 1 {
                    let stem = output.file_stem().unwrap().to_str().unwrap();
                    let ext = output.extension().unwrap_or_default().to_str().unwrap();
                    output.with_file_name(format!("{}_{:03}.{}", stem, i, ext))
                } else {
                    output.clone()
                };
                println!("[*] Capturing image #{}...", i + 1);
                let image_data = dev.capture_image()?;
                canvasbio_fp::image::save_image(&image_data, &path)?;
                println!("[OK] Saved: {}", path.display());
            }
        }
    }

    Ok(())
}
