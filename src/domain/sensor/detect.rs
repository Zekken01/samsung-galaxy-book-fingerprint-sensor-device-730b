use anyhow::{bail, Result};
use std::time::{Duration, Instant};

use super::init::SensorInit;
use super::CanvasBioDevice;
use crate::domain::usb::error::{classify_usb_error, is_retryable_usb_error};

/// Polling interval for finger-down detection loop.
const POLL_INTERVAL: Duration = Duration::from_millis(15);

/// Default timeout for waiting for finger-down event.
const DEFAULT_TIMEOUT: Duration = Duration::from_secs(30);

/// Register 0x20 status value indicating finger is present on sensor.
const STATUS_FINGER_DOWN: u16 = 0x0107;
/// Maximum duration of one fallback scan-complete probe.
const FALLBACK_PROBE_MAX: Duration = Duration::from_secs(4);
/// Minimum remaining time needed to attempt another fallback probe.
const FALLBACK_PROBE_MIN: Duration = Duration::from_millis(700);

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
        // These writes must match the reverse-engineered protocol exactly.
        // Several 0x0000 values are still 2-byte register writes.
        self.spi_write_reg2(0x5D, 0x4D00)?;
        self.spi_write_reg2(0x51, 0xA825)?;
        self.spi_write_reg2(0x03, 0x0000)?;
        self.spi_write_reg2(0x38, 0x0100)?;
        self.spi_write_reg2(0x3D, 0xFF0F)?;
        self.spi_write_reg2(0x10, 0x6000)?;
        self.spi_write_reg2(0x3B, 0x1400)?;
        self.spi_write_reg2(0x2F, 0xF6FF)?;
        self.spi_write_reg2(0x09, 0x0000)?;
        self.spi_write_reg(0x0C, 0x00)?;
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

    fn wait_finger_down_via_scan_probe(&self, timeout: Duration) -> Result<bool> {
        log::debug!("Falling back to scan-complete finger detection");
        let start = Instant::now();
        loop {
            let elapsed = start.elapsed();
            if elapsed >= timeout {
                return Ok(false);
            }
            let remaining = timeout.saturating_sub(elapsed);
            if remaining < FALLBACK_PROBE_MIN {
                log::debug!(
                    "Skipping fallback probe: remaining {:?} is below minimum {:?}",
                    remaining,
                    FALLBACK_PROBE_MIN
                );
                return Ok(false);
            }

            let probe_timeout = std::cmp::min(remaining, FALLBACK_PROBE_MAX);
            if self.probe_finger_via_scan_complete(probe_timeout)? {
                return Ok(true);
            }

            std::thread::sleep(POLL_INTERVAL);
        }
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
        let start = Instant::now();

        loop {
            if start.elapsed() >= timeout {
                log::debug!("Finger-down wait timed out after {:?}", timeout);
                return Ok(false);
            }

            let status = match self.poll_detect_cycle() {
                Ok(status) => status,
                Err(err) => {
                    if let Some(kind) = classify_usb_error(&err) {
                        if is_retryable_usb_error(kind) {
                            log::warn!(
                                "Register-poll finger detect failed with {:?}; switching to scan-complete fallback",
                                kind
                            );
                            let remaining = timeout.saturating_sub(start.elapsed());
                            return self.wait_finger_down_via_scan_probe(remaining);
                        }
                    }
                    return Err(err);
                }
            };

            if status == STATUS_FINGER_DOWN {
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
