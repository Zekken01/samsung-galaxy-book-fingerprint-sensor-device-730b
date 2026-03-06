use anyhow::Result;
use std::time::Duration;

use super::error::usb_operation_error;
use super::UsbDevice;

const EP_OUT: u8 = 0x01;
const EP_IN: u8 = 0x82;
const TIMEOUT_CMD: Duration = Duration::from_secs(1);

// Cypress MCU vendor requests
const CY_SPI_TRANSFER: u8 = 0xCA;
const CY_READY_CHECK: u8 = 0xCC;
pub(crate) const CY_POWER: u8 = 0xDB;
/// GPIO/interrupt status register — used to poll scan completion.
/// Read with wValue=0x0007; returns 2 bytes, [0x00, 0x01] when scan is done.
pub(crate) const CY_GPIO_STATUS: u8 = 0xDA;

// SPI direction flags (wValue for 0xCA)
pub(crate) const SPI_DIR_WRITE: u16 = 0x0002;
pub(crate) const SPI_DIR_READ: u16 = 0x0003;

// SPI command prefixes
pub(crate) const SPI_WRITE: u8 = 0xA9;
pub(crate) const SPI_READ: u8 = 0xA8;

/// Cypress MCU control transfer operations.
pub trait CypressControl {
    fn cy_vendor_write(&self, request: u8, value: u16, index: u16) -> Result<()>;
    fn cy_vendor_read(&self, request: u8, buf: &mut [u8]) -> Result<usize>;
    fn cy_vendor_read_wval(&self, request: u8, value: u16, buf: &mut [u8]) -> Result<usize>;
    fn spi_prepare(&self, direction: u16, size: u16) -> Result<()>;
    fn spi_check_ready(&self) -> Result<u8>;
    fn poll_scan_complete(&self, timeout: Duration) -> Result<()>;
}

/// USB bulk transfer operations.
pub trait BulkTransfer {
    fn bulk_write(&self, data: &[u8]) -> Result<()>;
    fn bulk_read(&self, buf: &mut [u8], timeout: Duration) -> Result<usize>;
    fn clear_halt_out(&self) -> Result<()>;
}

impl CypressControl for UsbDevice {
    fn cy_vendor_write(&self, request: u8, value: u16, index: u16) -> Result<()> {
        self.handle
            .write_control(0x40, request, value, index, &[], TIMEOUT_CMD)
            .map_err(|e| {
                usb_operation_error(
                    &format!(
                        "control write (req=0x{request:02X}, wValue=0x{value:04X}, wIndex=0x{index:04X})"
                    ),
                    e,
                )
            })?;
        Ok(())
    }

    fn cy_vendor_read(&self, request: u8, buf: &mut [u8]) -> Result<usize> {
        let n = self
            .handle
            .read_control(0xC0, request, 0, 0, buf, TIMEOUT_CMD)
            .map_err(|e| usb_operation_error(&format!("control read (req=0x{request:02X})"), e))?;
        Ok(n)
    }

    fn cy_vendor_read_wval(&self, request: u8, value: u16, buf: &mut [u8]) -> Result<usize> {
        let n = self
            .handle
            .read_control(0xC0, request, value, 0, buf, TIMEOUT_CMD)
            .map_err(|e| {
                usb_operation_error(
                    &format!("control read (req=0x{request:02X}, wValue=0x{value:04X})"),
                    e,
                )
            })?;
        Ok(n)
    }

    fn spi_prepare(&self, direction: u16, size: u16) -> Result<()> {
        self.cy_vendor_write(CY_SPI_TRANSFER, direction, size)
    }

    fn spi_check_ready(&self) -> Result<u8> {
        let mut buf = [0u8; 4];
        self.cy_vendor_read(CY_READY_CHECK, &mut buf)?;
        Ok(buf[0])
    }

    /// Poll the Cypress MCU GPIO/interrupt status register (0xDA, wValue=0x0007)
    /// until it returns [0x00, 0x01], indicating the sensor scan is complete.
    /// The Windows driver polls every ~15ms; we use 10ms intervals.
    /// Typical scan time observed in Windows capture: ~4 seconds.
    fn poll_scan_complete(&self, timeout: Duration) -> Result<()> {
        let start = std::time::Instant::now();
        loop {
            let mut buf = [0u8; 2];
            self.cy_vendor_read_wval(CY_GPIO_STATUS, 0x0007, &mut buf)?;
            if buf[1] == 0x01 {
                log::debug!(
                    "Scan complete after {:.0}ms",
                    start.elapsed().as_secs_f64() * 1000.0
                );
                return Ok(());
            }
            if start.elapsed() > timeout {
                anyhow::bail!(
                    "Scan timeout after {:.1}s (sensor never signaled ready)",
                    timeout.as_secs_f64()
                );
            }
            std::thread::sleep(Duration::from_millis(10));
        }
    }
}

impl BulkTransfer for UsbDevice {
    fn bulk_write(&self, data: &[u8]) -> Result<()> {
        self.handle
            .write_bulk(EP_OUT, data, TIMEOUT_CMD)
            .map_err(|e| {
                usb_operation_error(
                    &format!("bulk write (ep=0x{EP_OUT:02X}, len={})", data.len()),
                    e,
                )
            })?;
        Ok(())
    }

    fn bulk_read(&self, buf: &mut [u8], timeout: Duration) -> Result<usize> {
        let n = self.handle.read_bulk(EP_IN, buf, timeout).map_err(|e| {
            usb_operation_error(
                &format!("bulk read (ep=0x{EP_IN:02X}, timeout={:?})", timeout),
                e,
            )
        })?;
        Ok(n)
    }

    fn clear_halt_out(&self) -> Result<()> {
        self.handle
            .clear_halt(EP_OUT)
            .map_err(|e| usb_operation_error(&format!("clear_halt (ep=0x{EP_OUT:02X})"), e))?;
        Ok(())
    }
}
