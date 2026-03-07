use anyhow::Result;

use crate::domain::sensor::{CanvasBioDevice, SensorInit};

use super::super::runtime::{print_sensor_info, with_dev_mut};

pub fn run_info(dev: &mut Option<CanvasBioDevice>) -> Result<()> {
    let info = with_dev_mut(dev)?.read_info()?;
    print_sensor_info(&info);
    Ok(())
}
