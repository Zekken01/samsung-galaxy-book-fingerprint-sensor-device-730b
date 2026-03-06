use anyhow::{Result, bail};
use std::time::Duration;

use crate::usb::transfer::*;
use crate::usb::{BulkTransfer, CypressControl};
use super::CanvasBioDevice;
use super::init::SensorInit;

pub const IMG_WIDTH: u32 = 112;
pub const IMG_HEIGHT: u32 = 96;
pub const IMG_SIZE: usize = (IMG_WIDTH * IMG_HEIGHT) as usize; // 10752

const TIMEOUT_IMG: Duration = Duration::from_secs(5);

/// Image capture operations.
pub trait SensorCapture {
    fn capture_image(&self) -> Result<Vec<u8>>;
}

impl CanvasBioDevice {
    fn set_mode_capture(&self) -> Result<()> {
        self.spi_read_reg(0x08)?;
        self.spi_write_reg2(0x09, 0x0000)?;
        self.spi_read_reg_full(0x3E)?;
        self.spi_write_reg2(0x03, 0x0000)?;
        self.spi_read_reg_full(0x20)?;
        self.spi_write_reg2(0x10, 0x0001)?;
        self.spi_write_reg2(0x2F, 0xEF00)?;
        self.spi_write_reg2(0x09, 0x0000)?;
        self.spi_write_reg2(0x5D, 0x4D00)?;
        self.spi_write_reg2(0x51, 0x3A25)?;
        self.spi_write_reg(0x0C, 0x00)?;
        self.spi_read_reg_full(0x20)?;
        self.spi_write_reg2(0x04, 0x0000)?;
        self.spi_write_reg2(0x09, 0x0000)?;
        Ok(())
    }
}

impl SensorCapture for CanvasBioDevice {
    fn capture_image(&self) -> Result<Vec<u8>> {
        if !self.initialized {
            bail!("Sensor not initialized");
        }

        self.set_mode_capture()?;

        // Prepare 10755-byte SPI read (3B cmd + 10752B image)
        const TOTAL_SPI: u16 = (IMG_SIZE + 3) as u16;
        self.usb.spi_prepare(SPI_DIR_READ, TOTAL_SPI)?;

        // Block 0: send [A8 06 00] + 256 zeros (259B), read 256B
        let mut first_block = vec![SPI_READ, 0x06, 0x00];
        first_block.extend_from_slice(&[0u8; 256]);
        self.usb.bulk_write(&first_block)?;

        let mut image = Vec::with_capacity(IMG_SIZE);
        let mut buf = [0u8; 512];
        let n = self.usb.bulk_read(&mut buf, TIMEOUT_IMG)?;
        image.extend_from_slice(&buf[..n]);
        log::debug!("Block 0: {} bytes", n);

        // Clear halt once after Block 0 (Cypress MCU halts EP_OUT after 259B->256B mismatch)
        self.usb.clear_halt_out()?;

        // Blocks 1-41: send 256 zeros, read 256B
        let zeros = [0u8; 256];
        for i in 0..41 {
            self.usb.bulk_write(&zeros)?;
            let n = self.usb.bulk_read(&mut buf, TIMEOUT_IMG)?;
            image.extend_from_slice(&buf[..n]);
            log::debug!("Block {}: {} bytes (total: {})", i + 1, n, image.len());
        }

        image.truncate(IMG_SIZE);

        if image.len() < IMG_SIZE {
            log::warn!("Short image: {} bytes (expected {})", image.len(), IMG_SIZE);
            image.resize(IMG_SIZE, 0);
        }

        println!("  Image captured: {}x{} ({} bytes)", IMG_WIDTH, IMG_HEIGHT, image.len());
        Ok(image)
    }
}
