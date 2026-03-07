use clap::Parser;
use std::path::PathBuf;

#[derive(Parser)]
#[command(name = "canvasbio-fp")]
#[command(about = "CanvasBio CB2000 Fingerprint Sensor Driver (04e8:730b)")]
pub struct Cli {
    #[command(subcommand)]
    pub command: Command,
}

#[derive(clap::Subcommand)]
pub enum Command {
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
