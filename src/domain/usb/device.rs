use anyhow::{Context, Result};
use rusb::{DeviceHandle, GlobalContext};

use super::error::usb_operation_error;

const VID: u16 = 0x04E8;
const PID: u16 = 0x730B;

pub struct UsbDevice {
    pub(crate) handle: DeviceHandle<GlobalContext>,
}

impl UsbDevice {
    pub fn open() -> Result<Self> {
        let handle = rusb::open_device_with_vid_pid(VID, PID)
            .context("Device 04e8:730b not found. Is it connected?")?;

        if handle
            .kernel_driver_active(0)
            .map_err(|e| usb_operation_error("kernel_driver_active(interface 0)", e))?
        {
            handle
                .detach_kernel_driver(0)
                .map_err(|e| usb_operation_error("detach_kernel_driver(interface 0)", e))?;
        }

        handle
            .set_active_configuration(1)
            .or(Ok::<_, rusb::Error>(()))?;
        handle
            .claim_interface(0)
            .map_err(|e| usb_operation_error("claim_interface(0)", e))?;

        Ok(Self { handle })
    }

    pub(crate) fn release_interface_if_claimed(&mut self) {
        let _ = self.handle.release_interface(0);
    }
}

impl Drop for UsbDevice {
    fn drop(&mut self) {
        let _ = self.handle.release_interface(0);
        log::debug!("Device closed");
    }
}
