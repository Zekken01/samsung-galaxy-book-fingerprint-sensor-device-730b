use anyhow::Result;

use crate::domain::sensor::{CanvasBioDevice, SensorInit};
use crate::platform::i18n;
use crate::platform::i18n::Key;

use super::super::runtime::{print_sensor_info, with_dev_mut};

pub fn run_init(dev: &mut Option<CanvasBioDevice>) -> Result<()> {
    let info = with_dev_mut(dev)?.read_info()?;
    print_sensor_info(&info);
    println!("{}", i18n::text(Key::InitSensor));
    with_dev_mut(dev)?.initialize()?;
    println!(
        "{} 0x{:02X}",
        i18n::text(Key::HardwareIdLabel),
        with_dev_mut(dev)?.spi_read_reg(0xB9)?
    );
    println!("{}", i18n::text(Key::SensorInitialized));
    println!("{}", i18n::text(Key::SensorInitializedReady));
    Ok(())
}
