pub(crate) mod capture;
mod detect;
mod init;

pub use capture::SensorCapture;
pub use detect::{FingerDetect, WAIT_TIMEOUT};
pub use init::SensorInit;

use crate::usb::UsbDevice;

pub struct CanvasBioDevice {
    pub(crate) usb: UsbDevice,
    pub(crate) initialized: bool,
}

impl CanvasBioDevice {
    pub fn open() -> anyhow::Result<Self> {
        let usb = UsbDevice::open()?;
        Ok(Self {
            usb,
            initialized: false,
        })
    }

    pub fn prepare_for_reopen(&mut self) {
        self.usb.release_interface_if_claimed();
        self.initialized = false;
    }
}
