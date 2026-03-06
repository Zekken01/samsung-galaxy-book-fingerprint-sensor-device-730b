use anyhow::{Context, Result};
use rusb::{DeviceHandle, GlobalContext};

const VID: u16 = 0x04E8;
const PID: u16 = 0x730B;

pub struct UsbDevice {
    pub(crate) handle: DeviceHandle<GlobalContext>,
}

impl UsbDevice {
    pub fn open() -> Result<Self> {
        let handle = rusb::open_device_with_vid_pid(VID, PID)
            .context("Device 04e8:730b not found. Is it connected?")?;

        if handle.kernel_driver_active(0).unwrap_or(false) {
            handle.detach_kernel_driver(0)
                .context("Failed to detach kernel driver")?;
        }

        handle.set_active_configuration(1)
            .or_else(|_| Ok::<_, rusb::Error>(()))?;
        handle.claim_interface(0)
            .context("Failed to claim interface 0")?;

        println!("[OK] Device opened: {:04x}:{:04x}", VID, PID);

        Ok(Self { handle })
    }
}

impl Drop for UsbDevice {
    fn drop(&mut self) {
        let _ = self.handle.release_interface(0);
        log::debug!("Device closed");
    }
}
