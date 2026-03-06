mod init;
pub(crate) mod capture;
mod detect;

pub use init::SensorInit;
pub use capture::SensorCapture;
pub use detect::{FingerDetect, WAIT_TIMEOUT};

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
