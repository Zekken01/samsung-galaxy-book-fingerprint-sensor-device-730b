use anyhow::Result;
use std::time::Duration;

use super::CanvasBioDevice;
use crate::domain::usb::transfer::*;
use crate::domain::usb::{BulkTransfer, CypressControl};

// Sensor configuration registers (from Windows driver capture)
const INIT_REGISTERS: &[(u8, u16)] = &[
    (0x60, 0x1B00), // Bias/timing
    (0x50, 0x2100), // Scan config
    (0x61, 0x0000), // ADC offset
    (0x62, 0x001A), // Row start
    (0x63, 0x001A), // Row end
    (0x64, 0x040A), // Column config
    (0x66, 0x0F80), // Scan timing
    (0x67, 0x1B00), // Amplifier gain
    (0x68, 0x000F), // Integration time
    (0x69, 0x0014), // SNR threshold
    (0x6A, 0x0019), // ADC reference
    (0x6C, 0x0019), // ADC reference 2
    (0x40, 0x4300), // Finger-down threshold high
    (0x41, 0x6F00), // Finger-down threshold low
    (0x55, 0x2000), // Image buffer config
    (0x5F, 0x0000), // Image mode
    (0x52, 0x2700), // Image parameter
];

pub struct SensorInfo {
    pub cypress_magic: [u8; 4],
    pub firmware_major: u8,
    pub firmware_minor: u8,
    pub firmware_patch: u8,
    pub firmware_mode: char,
    pub build: u32,
    pub ready: bool,
}

/// SPI register write/read helpers and initialization.
pub trait SensorInit {
    fn read_info(&self) -> Result<SensorInfo>;
    fn initialize(&mut self) -> Result<()>;
    fn spi_write_reg(&self, reg: u8, val: u8) -> Result<()>;
    fn spi_write_reg2(&self, reg: u8, val: u16) -> Result<()>;
    fn spi_read_reg(&self, reg: u8) -> Result<u8>;
    fn spi_read_reg_full(&self, reg: u8) -> Result<Vec<u8>>;
}

impl SensorInit for CanvasBioDevice {
    fn spi_write_reg(&self, reg: u8, val: u8) -> Result<()> {
        let data = [SPI_WRITE, reg, val];
        self.usb.spi_prepare(SPI_DIR_WRITE, data.len() as u16)?;
        self.usb.bulk_write(&data)?;
        self.usb.spi_check_ready()?;
        Ok(())
    }

    fn spi_write_reg2(&self, reg: u8, val: u16) -> Result<()> {
        let data = [SPI_WRITE, reg, (val >> 8) as u8, val as u8];
        self.usb.spi_prepare(SPI_DIR_WRITE, data.len() as u16)?;
        self.usb.bulk_write(&data)?;
        self.usb.spi_check_ready()?;
        Ok(())
    }

    fn spi_read_reg(&self, reg: u8) -> Result<u8> {
        let cmd = [SPI_READ, reg, 0x00];
        self.usb.spi_prepare(SPI_DIR_READ, cmd.len() as u16)?;
        self.usb.bulk_write(&cmd)?;
        let mut buf = [0u8; 64];
        let n = self.usb.bulk_read(&mut buf, Duration::from_secs(1))?;
        Ok(if n >= 3 { buf[2] } else { 0 })
    }

    fn spi_read_reg_full(&self, reg: u8) -> Result<Vec<u8>> {
        let cmd = [SPI_READ, reg, 0x00];
        self.usb.spi_prepare(SPI_DIR_READ, cmd.len() as u16)?;
        self.usb.bulk_write(&cmd)?;
        let mut buf = [0u8; 64];
        let n = self.usb.bulk_read(&mut buf, Duration::from_secs(1))?;
        Ok(buf[..n].to_vec())
    }

    fn read_info(&self) -> Result<SensorInfo> {
        let mut cypress_magic = [0u8; 4];
        self.usb.cy_vendor_read(0xBD, &mut cypress_magic)?;
        let mut ver = [0u8; 8];
        self.usb.cy_vendor_read(0xB0, &mut ver)?;
        let mut build = [0u8; 4];
        self.usb.cy_vendor_read(0xB1, &mut build)?;
        Ok(SensorInfo {
            cypress_magic,
            firmware_major: ver[0],
            firmware_minor: ver[1],
            firmware_patch: ver[2],
            firmware_mode: ver[4] as char,
            build: u32::from_le_bytes(build),
            ready: self.usb.spi_check_ready()? != 0,
        })
    }

    fn initialize(&mut self) -> Result<()> {
        // Step 1: Cypress power init
        log::debug!("Power init");
        for &idx in &[1u16, 0, 1] {
            self.usb.cy_vendor_write(CY_POWER, 0x0006, idx)?;
        }
        std::thread::sleep(Duration::from_millis(100));

        // Step 2: SPI enable (special 2-byte command)
        log::debug!("SPI enable");
        self.usb.spi_prepare(SPI_DIR_WRITE, 2)?;
        self.usb.bulk_write(&[0x4F, 0x80])?;
        self.usb.spi_check_ready()?;
        std::thread::sleep(Duration::from_millis(50));

        // Step 3: SPI register enable
        self.spi_write_reg(0x4F, 0x80)?;
        std::thread::sleep(Duration::from_millis(100));

        // Step 4: Read Hardware ID
        let hw_id = self.spi_read_reg(0xB9)?;
        log::debug!("Hardware ID: 0x{:02X}", hw_id);

        // Step 5: Sensor configuration
        log::debug!("Writing init registers");
        for &(reg, val) in INIT_REGISTERS {
            self.spi_write_reg2(reg, val)?;
        }

        self.initialized = true;
        Ok(())
    }
}
