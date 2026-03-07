mod core;
mod output;

use anyhow::Result;
use std::env;

use crate::features::tools::tsv;

pub fn run() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    let tsv_path = args
        .get(1)
        .map(|arg| arg.as_str())
        .unwrap_or("/tmp/bulk_data.tsv");
    let out_dir = args
        .get(2)
        .map(|arg| arg.as_str())
        .unwrap_or("captures/images");

    let packets = tsv::parse_tsv(tsv_path)?;
    let images = core::extract_images(&packets);
    output::write_images(out_dir, &images)
}

#[cfg(test)]
mod tests {
    use super::core::{extract_images, IMG_SIZE};
    use crate::features::tools::tsv::{parse_tsv, Packet, EP_IN, EP_OUT};
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
