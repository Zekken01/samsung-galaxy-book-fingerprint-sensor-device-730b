use anyhow::Result;
use std::time::Duration;

use super::UsbDevice;

const EP_OUT: u8 = 0x01;
const EP_IN: u8 = 0x82;
const TIMEOUT_CMD: Duration = Duration::from_secs(1);

// Cypress MCU vendor requests
const CY_SPI_TRANSFER: u8 = 0xCA;
const CY_READY_CHECK: u8 = 0xCC;
pub(crate) const CY_POWER: u8 = 0xDB;

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
    fn spi_prepare(&self, direction: u16, size: u16) -> Result<()>;
    fn spi_check_ready(&self) -> Result<u8>;
}

/// USB bulk transfer operations.
pub trait BulkTransfer {
    fn bulk_write(&self, data: &[u8]) -> Result<()>;
    fn bulk_read(&self, buf: &mut [u8], timeout: Duration) -> Result<usize>;
    fn clear_halt_out(&self) -> Result<()>;
}

impl CypressControl for UsbDevice {
    fn cy_vendor_write(&self, request: u8, value: u16, index: u16) -> Result<()> {
        self.handle.write_control(0x40, request, value, index, &[], TIMEOUT_CMD)?;
        Ok(())
    }

    fn cy_vendor_read(&self, request: u8, buf: &mut [u8]) -> Result<usize> {
        let n = self.handle.read_control(0xC0, request, 0, 0, buf, TIMEOUT_CMD)?;
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
}

impl BulkTransfer for UsbDevice {
    fn bulk_write(&self, data: &[u8]) -> Result<()> {
        self.handle.write_bulk(EP_OUT, data, TIMEOUT_CMD)?;
        Ok(())
    }

    fn bulk_read(&self, buf: &mut [u8], timeout: Duration) -> Result<usize> {
        let n = self.handle.read_bulk(EP_IN, buf, timeout)?;
        Ok(n)
    }

    fn clear_halt_out(&self) -> Result<()> {
        self.handle.clear_halt(EP_OUT)?;
        Ok(())
    }
}
