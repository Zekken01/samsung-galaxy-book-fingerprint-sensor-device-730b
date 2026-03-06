use anyhow::{bail, Result};
use std::time::{Duration, Instant};

use super::init::SensorInit;
use super::CanvasBioDevice;
use crate::usb::transfer::*;
use crate::usb::{BulkTransfer, CypressControl};

pub const IMG_WIDTH: u32 = 112;
pub const IMG_HEIGHT: u32 = 96;
pub const IMG_SIZE: usize = (IMG_WIDTH * IMG_HEIGHT) as usize; // 10752

/// Timeout per bulk read during image transfer.
/// Windows capture shows each 256B block completes in ~1.7ms on Full Speed USB.
/// 500ms is a generous upper bound — if a block takes longer, the SPI transaction
/// has likely been disrupted.  The previous 5s timeout masked the real failure by
/// making every timed-out block look like it simply took 5 seconds.
const TIMEOUT_IMG: Duration = Duration::from_millis(500);

/// Maximum time to wait for the sensor to complete a scan.
/// The Windows capture shows ~4 seconds of DA polling.  We allow up to 10s
/// to account for variation in finger placement timing.
const TIMEOUT_SCAN: Duration = Duration::from_secs(10);

/// Image capture operations.
pub trait SensorCapture {
    fn capture_image(&self) -> Result<Vec<u8>>;
}

impl CanvasBioDevice {
    /// Configure the sensor to begin a scan.
    ///
    /// This writes the scan-mode registers observed in the Windows USB capture.
    /// These differ from the post-scan readout registers (set_mode_readout):
    ///   - reg 0x51: 0xA825 (scan) vs 0x3A25 (readout)
    ///   - reg 0x10: 0x6000 (scan) vs 0x0001 (readout)
    ///   - reg 0x2F: 0xF6FF (scan) vs 0xEF00 (readout)
    ///   - regs 0x38, 0x3D, 0x3B: only present in scan mode
    ///
    /// Writing reg 0x04=0x00 at the end triggers the actual scan.
    fn trigger_scan(&self) -> Result<()> {
        log::debug!("Configuring scan-mode registers");
        self.spi_write_reg2(0x5F, 0x0000)?;
        self.spi_write_reg2(0x52, 0x2700)?;
        self.spi_write_reg2(0x09, 0x0000)?;
        self.spi_write_reg2(0x5D, 0x4D00)?;
        self.spi_write_reg2(0x51, 0xA825)?; // scan mode (not 0x3A25)
        self.spi_write_reg(0x03, 0x00)?;
        self.spi_write_reg2(0x38, 0x0100)?; // scan-only register
        self.spi_write_reg2(0x3D, 0xFF0F)?; // scan-only register
        self.spi_write_reg2(0x10, 0x6000)?; // scan mode (not 0x0001)
        self.spi_write_reg2(0x3B, 0x1400)?; // scan-only register
        self.spi_write_reg2(0x2F, 0xF6FF)?; // scan mode (not 0xEF00)
        self.spi_write_reg2(0x09, 0x0000)?;
        self.spi_write_reg(0x0C, 0x00)?;
        self.spi_read_reg_full(0x20)?;
        // Write reg 0x04 = 0x00: this triggers the sensor scan
        self.spi_write_reg(0x04, 0x00)?;
        log::debug!("Scan triggered");
        Ok(())
    }

    /// Post-scan readout configuration.
    ///
    /// After the scan completes (DA poll returns 0x01), these registers
    /// reconfigure the sensor for image data readout over SPI.
    fn set_mode_readout(&self) -> Result<()> {
        log::debug!("Configuring readout-mode registers");
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

    /// Fast finger-presence probe using only scan trigger + DA completion.
    ///
    /// Returns `Ok(true)` when the sensor reports scan completion within
    /// `scan_timeout`, which typically indicates finger contact.
    /// Returns `Ok(false)` on scan timeout (no finger).
    pub(crate) fn probe_finger_via_scan_complete(&self, scan_timeout: Duration) -> Result<bool> {
        if !self.initialized {
            bail!("Sensor not initialized");
        }

        self.trigger_scan()?;
        match self.usb.poll_scan_complete(scan_timeout) {
            Ok(()) => Ok(true),
            Err(err) => {
                if err.to_string().contains("Scan timeout") {
                    Ok(false)
                } else {
                    Err(err)
                }
            }
        }
    }
}

impl SensorCapture for CanvasBioDevice {
    fn capture_image(&self) -> Result<Vec<u8>> {
        if !self.initialized {
            bail!("Sensor not initialized");
        }

        // Phase 1: Trigger the sensor scan.
        // This configures scan-mode registers and writes reg 0x04=0x00 to
        // start the hardware scan on the CanvasBio CB2000.
        self.trigger_scan()?;

        // Phase 2: Wait for scan completion.
        // The Cypress MCU exposes scan status via vendor request 0xDA
        // (wValue=0x0007).  It returns [0x00, 0x00] while scanning and
        // [0x00, 0x01] when the image is ready in the sensor's buffer.
        // The Windows driver polls every ~15ms; typical scan takes ~3s.
        // NOTE: Without a finger on the sensor, the scan never completes
        // (DA stays [0x00, 0x00]).  In that case we proceed to readout
        // anyway — the sensor buffer will contain a blank/dark image.
        println!("  Waiting for sensor scan...");
        match self.usb.poll_scan_complete(TIMEOUT_SCAN) {
            Ok(()) => {}
            Err(e) => {
                log::warn!("Scan poll: {} — proceeding with readout", e);
                println!("  Scan timeout (no finger?), reading anyway...");
            }
        }

        // Phase 3: Reconfigure for image readout.
        // After the scan, several registers must be switched from scan-mode
        // values to readout-mode values before the image data can be read.
        self.set_mode_readout()?;

        // Phase 4: Transfer the image over USB bulk endpoints.
        let transfer_start = Instant::now();

        // Tell the Cypress MCU to set up a single SPI read transaction
        // spanning the entire image (3B command + 10752B data = 10755B).
        // All subsequent bulk OUT/IN pairs clock data through that
        // transaction — no per-block CA/CC is needed, and no clear_halt
        // should be issued (Windows driver never does).
        const TOTAL_SPI: u16 = (IMG_SIZE + 3) as u16;
        self.usb.spi_prepare(SPI_DIR_READ, TOTAL_SPI)?;

        // Block 0: send [A8 06 00] + 256 zeros (259B OUT), read 256B IN.
        // The 3-byte prefix is the SPI read command for register 0x06
        // (image data buffer).  The 256 zero bytes are SPI clock padding.
        let mut first_block = vec![SPI_READ, 0x06, 0x00];
        first_block.extend_from_slice(&[0u8; 256]);
        self.usb.bulk_write(&first_block)?;

        let mut image = Vec::with_capacity(IMG_SIZE);
        let mut buf = [0u8; 512];
        let n = self.usb.bulk_read(&mut buf, TIMEOUT_IMG)?;
        image.extend_from_slice(&buf[..n]);
        log::debug!("Block 0: {} bytes", n);

        // Blocks 1-41: send 256 zeros (SPI clock), read 256B each.
        // The last block may return up to 259B (with 3B MCU framing).
        let zeros = [0u8; 256];
        for i in 0..41 {
            self.usb.bulk_write(&zeros)?;
            let n = self.usb.bulk_read(&mut buf, TIMEOUT_IMG)?;
            let block_num = i + 1;

            if n > 256 {
                // Last block: first 3 bytes are MCU framing, skip them
                log::debug!("Block {}: {} bytes (stripping 3B header)", block_num, n);
                image.extend_from_slice(&buf[3..n]);
            } else {
                image.extend_from_slice(&buf[..n]);
                log::debug!("Block {}: {} bytes (total: {})", block_num, n, image.len());
            }
        }

        let transfer_ms = transfer_start.elapsed().as_secs_f64() * 1000.0;

        image.truncate(IMG_SIZE);

        if image.len() < IMG_SIZE {
            log::warn!(
                "Short image: {} bytes (expected {}), padding with zeros",
                image.len(),
                IMG_SIZE
            );
            image.resize(IMG_SIZE, 0);
        }

        println!(
            "  Image captured: {}x{} ({} bytes), transfer: {:.0}ms",
            IMG_WIDTH,
            IMG_HEIGHT,
            image.len(),
            transfer_ms
        );
        Ok(image)
    }
}
