mod init;
pub(crate) mod capture;

pub use init::SensorInit;
pub use capture::SensorCapture;

use crate::usb::UsbDevice;

pub struct CanvasBioDevice {
    pub(crate) usb: UsbDevice,
    pub(crate) initialized: bool,
}

impl CanvasBioDevice {
    pub fn open() -> anyhow::Result<Self> {
        let usb = UsbDevice::open()?;
        Ok(Self { usb, initialized: false })
    }
}
