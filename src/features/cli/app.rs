use anyhow::Result;
use clap::Parser;

use super::args::Cli;
use super::runtime;

pub fn run() -> Result<()> {
    env_logger::init();
    let cli = Cli::parse();
    runtime::execute(cli.command)
}
