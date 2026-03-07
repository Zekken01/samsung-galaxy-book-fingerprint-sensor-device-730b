mod parser;
mod report;

use anyhow::Result;
use std::env;

use crate::features::tools::tsv;

pub fn run() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    let tsv_path = args
        .get(1)
        .map(|arg| arg.as_str())
        .unwrap_or("/tmp/bulk_data.tsv");

    let packets = tsv::parse_tsv(tsv_path)?;
    report::print_analysis(&packets);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::parser::decode_cmd;
    use crate::features::tools::tsv::{parse_hex, parse_tsv, EP_IN, EP_OUT};
    use std::fs;
    use std::path::PathBuf;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn write_temp_file(contents: &str) -> PathBuf {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system time before UNIX_EPOCH")
            .as_nanos();
        let path = std::env::temp_dir().join(format!("canvasbio_analyze_{stamp}.tsv"));
        fs::write(&path, contents).expect("failed to write temp tsv");
        path
    }

    #[test]
    fn parse_hex_ignores_separators() {
        assert_eq!(parse_hex("a8:06:00"), vec![0xA8, 0x06, 0x00]);
        assert_eq!(parse_hex("43 59 55 53"), vec![0x43, 0x59, 0x55, 0x53]);
    }

    #[test]
    fn parse_tsv_reads_basic_packets() {
        let path = write_temp_file("1\t0.001\t0x01\t3\ta80600\n2\t0.002\t0x82\t3\t000001\n");
        let packets = parse_tsv(path.to_str().expect("utf8 path")).expect("parse_tsv failed");
        fs::remove_file(path).ok();

        assert_eq!(packets.len(), 2);
        assert_eq!(packets[0].frame, 1);
        assert_eq!(packets[0].ep, EP_OUT);
        assert_eq!(packets[0].data, vec![0xA8, 0x06, 0x00]);
        assert_eq!(packets[1].ep, EP_IN);
    }

    #[test]
    fn decode_cmd_formats_spi_commands() {
        assert_eq!(decode_cmd(&[0xA9, 0x4F, 0x80]), "W1 0x4F=0x80");
        assert_eq!(decode_cmd(&[0xA9, 0x51, 0xA8, 0x25]), "W2 0x51=0xA825");
        assert_eq!(decode_cmd(&[0xA8, 0x20, 0x00]), "RD 0x20");
    }
}
