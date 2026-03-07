use anyhow::{Context, Result};
use std::fs;

pub const EP_OUT: u8 = 0x01;
pub const EP_IN: u8 = 0x82;

#[derive(Clone, Debug)]
pub struct Packet {
    pub frame: u32,
    pub time: f64,
    pub ep: u8,
    pub len: usize,
    pub data: Vec<u8>,
}

pub fn parse_hex(input: &str) -> Vec<u8> {
    let clean: String = input.chars().filter(|ch| ch.is_ascii_hexdigit()).collect();
    (0..clean.len())
        .step_by(2)
        .filter_map(|index| {
            if index + 2 <= clean.len() {
                u8::from_str_radix(&clean[index..index + 2], 16).ok()
            } else {
                None
            }
        })
        .collect()
}

pub fn parse_tsv(path: &str) -> Result<Vec<Packet>> {
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
