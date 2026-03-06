use anyhow::{Result, bail};
use std::time::{Duration, Instant};

use super::CanvasBioDevice;
use super::init::SensorInit;

/// Polling interval for finger-down detection loop.
const POLL_INTERVAL: Duration = Duration::from_millis(15);

/// Default timeout for waiting for finger-down event.
const DEFAULT_TIMEOUT: Duration = Duration::from_secs(30);

/// Register 0x20 status value indicating finger is present on sensor.
const STATUS_FINGER_DOWN: u16 = 0x0107;

/// Finger-down detection mode registers.
/// These configure the sensor for low-power finger-detect scanning.
const FINGER_DOWN_MODE_REGS: &[(u8, u16)] = &[
    (0x5D, 0x4D00), // Detection threshold
    (0x51, 0xA825), // Scan parameters for detect mode
    (0x03, 0x0000), // Clear status
    (0x38, 0x0100), // IRQ enable
    (0x3D, 0xFF0F), // IRQ mask
    (0x10, 0x6000), // Detect scan mode
    (0x3B, 0x1400), // Detect timing
    (0x2F, 0xF6FF), // Detect reference
    (0x09, 0x0000), // Clear pending
    (0x0C, 0x0000), // Trigger detect scan
];

/// Finger-down detection operations.
pub trait FingerDetect {
    /// Configure the sensor for finger-down detection mode.
    fn set_mode_finger_down(&self) -> Result<()>;

    /// Poll the sensor for a finger-down event.
    /// Returns `Ok(true)` when a finger is detected, `Ok(false)` on timeout.
    fn wait_finger_down(&self, timeout: Duration) -> Result<bool>;

    /// Poll once: returns `true` if finger is currently on sensor.
    fn poll_finger_status(&self) -> Result<bool>;
}

impl CanvasBioDevice {
    /// Enter finger-down detection mode by writing the detect-mode register set.
    fn write_finger_down_regs(&self) -> Result<()> {
        // Write detect mode registers via SPI
        for &(reg, val) in FINGER_DOWN_MODE_REGS {
            if val <= 0xFF {
                self.spi_write_reg(reg, val as u8)?;
            } else {
                self.spi_write_reg2(reg, val)?;
            }
        }
        Ok(())
    }

    /// Read the initial status after entering finger-down mode.
    fn read_initial_status(&self) -> Result<u16> {
        let data = self.spi_read_reg_full(0x20)?;
        let status = if data.len() >= 4 {
            u16::from_be_bytes([data[2], data[3]])
        } else {
            0
        };
        log::debug!("Initial detect status: 0x{:04X}", status);
        Ok(status)
    }

    /// One finger-down poll cycle: read status register and check for finger.
    /// Returns the 16-bit status from register 0x20.
    fn poll_detect_cycle(&self) -> Result<u16> {
        // Read reg 0x08 (interrupt status)
        let _irq = self.spi_read_reg(0x08)?;

        // Clear pending and start next detect scan
        self.spi_write_reg2(0x09, 0x0000)?;
        self.spi_write_reg2(0x03, 0x0000)?;
        self.spi_write_reg(0x0C, 0x00)?;

        // Read status register 0x20
        let data = self.spi_read_reg_full(0x20)?;
        let status = if data.len() >= 4 {
            u16::from_be_bytes([data[2], data[3]])
        } else {
            0
        };

        // Clear scan trigger
        self.spi_write_reg(0x04, 0x00)?;

        log::trace!("Detect poll: reg0x20=0x{:04X}", status);
        Ok(status)
    }
}

impl FingerDetect for CanvasBioDevice {
    fn set_mode_finger_down(&self) -> Result<()> {
        if !self.initialized {
            bail!("Sensor not initialized");
        }

        log::debug!("Entering finger-down detection mode");

        // Write the finger-down detection register set
        self.write_finger_down_regs()?;

        // Read initial status (discarded, just primes the sensor)
        self.read_initial_status()?;

        // Clear scan trigger
        self.spi_write_reg(0x04, 0x00)?;

        log::debug!("Finger-down detection mode active");
        Ok(())
    }

    fn wait_finger_down(&self, timeout: Duration) -> Result<bool> {
        self.set_mode_finger_down()?;

        println!("[*] Waiting for finger...");
        let start = Instant::now();

        loop {
            if start.elapsed() >= timeout {
                log::debug!("Finger-down wait timed out after {:?}", timeout);
                return Ok(false);
            }

            let status = self.poll_detect_cycle()?;

            if status == STATUS_FINGER_DOWN {
                println!("[OK] Finger detected!");
                return Ok(true);
            }

            // Small sleep to avoid hammering the SPI bus
            std::thread::sleep(POLL_INTERVAL);
        }
    }

    fn poll_finger_status(&self) -> Result<bool> {
        let status = self.poll_detect_cycle()?;
        Ok(status == STATUS_FINGER_DOWN)
    }
}

/// Default timeout for the CLI `wait` command.
pub const WAIT_TIMEOUT: Duration = DEFAULT_TIMEOUT;
