//! Extract fingerprint images from USB capture data.
//!
//! Reads tshark-exported TSV bulk data, finds image read sequences
//! (triggered by RD 0x06 command), and saves as PNG files.
//!
//! Image format: 112x96 @ 8-bit grayscale (10,752 bytes per image)
//!
//! Generate input with:
//!   tshark -r capture.pcapng -Y "usb.transfer_type==0x03" \
//!     -T fields -e frame.number -e frame.time_relative \
//!     -e usb.endpoint_address -e usb.data_len \
//!     -e usb.capdata -E separator=$'\t' > /tmp/bulk_data.tsv

use anyhow::{Context, Result};
use std::env;
use std::fs;
use std::path::Path;

const EP_OUT: u8 = 0x01;
const EP_IN: u8 = 0x82;
const WIDTH: u32 = 112;
const HEIGHT: u32 = 96;
const IMG_SIZE: usize = (WIDTH * HEIGHT) as usize; // 10752

struct Packet {
    frame: u32,
    time: f64,
    ep: u8,
    len: usize,
    data: Vec<u8>,
}

struct CapturedImage {
    frame: u32,
    time: f64,
    data: Vec<u8>,
}

fn parse_hex(s: &str) -> Vec<u8> {
    let clean: String = s.chars().filter(|c| c.is_ascii_hexdigit()).collect();
    (0..clean.len())
        .step_by(2)
        .filter_map(|i| {
            if i + 2 <= clean.len() {
                u8::from_str_radix(&clean[i..i + 2], 16).ok()
            } else {
                None
            }
        })
        .collect()
}

fn parse_tsv(path: &str) -> Result<Vec<Packet>> {
    let content = fs::read_to_string(path).context("Failed to read TSV file")?;
    let mut packets = Vec::new();

    for line in content.lines() {
        let fields: Vec<&str> = line.split('\t').collect();
        if fields.len() < 4 {
            continue;
        }
        let frame = fields[0].parse::<u32>().unwrap_or(0);
        let time = fields[1].parse::<f64>().unwrap_or(0.0);
        let ep = u8::from_str_radix(fields[2].trim_start_matches("0x"), 16).unwrap_or(0);
        let len = fields[3].parse::<usize>().unwrap_or(0);
        let data = if fields.len() > 4 {
            parse_hex(fields[4])
        } else {
            vec![]
        };

        packets.push(Packet {
            frame,
            time,
            ep,
            len,
            data,
        });
    }
    Ok(packets)
}

fn extract_images(packets: &[Packet]) -> Vec<CapturedImage> {
    let mut images = Vec::new();

    for i in 0..packets.len() {
        let p = &packets[i];
        // Look for RD 0x06 command (image read trigger)
        if p.ep == EP_OUT && p.data.len() >= 2 && p.data[0] == 0xA8 && p.data[1] == 0x06 {
            let mut img_data = Vec::new();
            let mut j = i + 1;

            while j < packets.len() {
                let pp = &packets[j];
                if pp.ep == EP_IN && (pp.len == 256 || pp.len == 259) {
                    if pp.len == 259 && pp.data.len() >= 3 {
                        // Skip 3-byte SPI header for 259B packets
                        img_data.extend_from_slice(&pp.data[3..]);
                    } else {
                        img_data.extend_from_slice(&pp.data);
                    }
                } else if pp.ep == EP_OUT && (pp.len == 256 || pp.len == 259) {
                    // Dummy SPI clock-out, skip
                } else {
                    break;
                }
                j += 1;
            }

            images.push(CapturedImage {
                frame: p.frame,
                time: p.time,
                data: img_data,
            });
        }
    }
    images
}

fn main() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    let tsv_path = args
        .get(1)
        .map(|s| s.as_str())
        .unwrap_or("/tmp/bulk_data.tsv");
    let out_dir = args.get(2).map(|s| s.as_str()).unwrap_or("captures/images");

    fs::create_dir_all(out_dir).context("Failed to create output directory")?;

    let packets = parse_tsv(tsv_path)?;
    let images = extract_images(&packets);

    println!("Found {} fingerprint images", images.len());

    for (idx, img) in images.iter().enumerate() {
        println!(
            "\n  Image #{}: frame={}, t={:.3}s, {}B",
            idx + 1,
            img.frame,
            img.time,
            img.data.len()
        );

        // Save raw binary
        let raw_path = Path::new(out_dir).join(format!("fp_raw_{:02}.bin", idx));
        fs::write(&raw_path, &img.data)?;
        println!("    Raw saved: {}", raw_path.display());

        // Save as PNG (112x96 8-bit grayscale)
        if img.data.len() >= IMG_SIZE {
            let pixel_data = img.data[..IMG_SIZE].to_vec();
            if let Some(gray_img) = image::GrayImage::from_raw(WIDTH, HEIGHT, pixel_data) {
                let png_path =
                    Path::new(out_dir).join(format!("fp_{:02}_{}x{}.png", idx, WIDTH, HEIGHT));
                gray_img
                    .save(&png_path)
                    .with_context(|| format!("Failed to save {}", png_path.display()))?;
                println!(
                    "    PNG saved: {} ({}x{} 8-bit)",
                    png_path.display(),
                    WIDTH,
                    HEIGHT
                );
            }

            // Also try with 3-byte offset (first transfer may have SPI header)
            if img.data.len() >= IMG_SIZE + 3 {
                let offset_data = img.data[3..IMG_SIZE + 3].to_vec();
                if let Some(gray_img) = image::GrayImage::from_raw(WIDTH, HEIGHT, offset_data) {
                    let png_path = Path::new(out_dir)
                        .join(format!("fp_{:02}_{}x{}_off3.png", idx, WIDTH, HEIGHT));
                    gray_img
                        .save(&png_path)
                        .with_context(|| format!("Failed to save {}", png_path.display()))?;
                    println!("    PNG saved: {} (3B offset)", png_path.display());
                }
            }
        }
    }

    println!("\n{}", "=".repeat(60));
    println!("Total images extracted: {}", images.len());
    if !images.is_empty() {
        let sizes: std::collections::BTreeSet<usize> =
            images.iter().map(|img| img.data.len()).collect();
        println!("Image sizes: {:?}", sizes);
        println!("Expected for {}x{}@8bpp: {}B", WIDTH, HEIGHT, IMG_SIZE);
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::path::PathBuf;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn write_temp_file(contents: &str) -> PathBuf {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system time before UNIX_EPOCH")
            .as_nanos();
        let path = std::env::temp_dir().join(format!("canvasbio_extract_{stamp}.tsv"));
        fs::write(&path, contents).expect("failed to write temp tsv");
        path
    }

    #[test]
    fn parse_tsv_reads_packets() {
        let path = write_temp_file("10\t1.200\t0x01\t3\ta80600\n11\t1.201\t0x82\t3\t000001\n");
        let packets = parse_tsv(path.to_str().expect("utf8 path")).expect("parse_tsv failed");
        fs::remove_file(path).ok();

        assert_eq!(packets.len(), 2);
        assert_eq!(packets[0].frame, 10);
        assert_eq!(packets[0].ep, EP_OUT);
        assert_eq!(packets[1].ep, EP_IN);
    }

    #[test]
    fn extract_images_collects_full_image_stream() {
        let mut packets = Vec::new();
        packets.push(Packet {
            frame: 1,
            time: 0.0,
            ep: EP_OUT,
            len: 3,
            data: vec![0xA8, 0x06, 0x00],
        });

        for i in 0..42 {
            packets.push(Packet {
                frame: 2 + i,
                time: 0.001 + (i as f64 * 0.001),
                ep: EP_IN,
                len: 256,
                data: vec![i as u8; 256],
            });
        }

        let images = extract_images(&packets);
        assert_eq!(images.len(), 1);
        assert_eq!(images[0].data.len(), IMG_SIZE);
        assert_eq!(images[0].data[0], 0);
        assert_eq!(images[0].data[256], 1);
    }

    #[test]
    fn extract_images_strips_three_byte_header_for_259b_packets() {
        let mut packets = Vec::new();
        packets.push(Packet {
            frame: 1,
            time: 0.0,
            ep: EP_OUT,
            len: 3,
            data: vec![0xA8, 0x06, 0x00],
        });

        let mut first = vec![0xDE, 0xAD, 0xBE];
        first.extend_from_slice(&vec![7u8; 256]);
        packets.push(Packet {
            frame: 2,
            time: 0.001,
            ep: EP_IN,
            len: 259,
            data: first,
        });

        for i in 0..41 {
            packets.push(Packet {
                frame: 3 + i,
                time: 0.002 + (i as f64 * 0.001),
                ep: EP_IN,
                len: 256,
                data: vec![0u8; 256],
            });
        }

        let images = extract_images(&packets);
        assert_eq!(images.len(), 1);
        assert_eq!(images[0].data.len(), IMG_SIZE);
        assert_eq!(images[0].data[0], 7);
    }
}
