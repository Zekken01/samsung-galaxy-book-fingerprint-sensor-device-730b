//! USB capture traffic analyzer for CanvasBio CB2000 fingerprint sensor.
//!
//! Reads tshark-exported TSV bulk data and identifies protocol phases,
//! register writes/reads, and image capture events.
//!
//! Generate input with:
//!   tshark -r capture.pcapng -Y "usb.transfer_type==0x03" \
//!     -T fields -e frame.number -e frame.time_relative \
//!     -e usb.endpoint_address -e usb.data_len \
//!     -e usb.capdata -E separator=$'\t' > /tmp/bulk_data.tsv

use anyhow::{Context, Result};
use std::collections::BTreeMap;
use std::env;
use std::fs;

const EP_OUT: u8 = 0x01;
const EP_IN: u8 = 0x82;

struct Packet {
    frame: u32,
    time: f64,
    ep: u8,
    len: usize,
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
        let data = if fields.len() > 4 { parse_hex(fields[4]) } else { vec![] };

        packets.push(Packet { frame, time, ep, len, data });
    }
    Ok(packets)
}

fn decode_cmd(data: &[u8]) -> String {
    if data.is_empty() {
        return "EMPTY".to_string();
    }
    match data[0] {
        0xA9 => {
            if data.len() == 3 {
                format!("W1 0x{:02X}=0x{:02X}", data[1], data[2])
            } else if data.len() == 4 {
                format!("W2 0x{:02X}=0x{:04X}", data[1], ((data[2] as u16) << 8) | data[3] as u16)
            } else {
                format!("WX 0x{:02X} [{}B]", data[1], data.len() - 2)
            }
        }
        0xA8 => format!("RD 0x{:02X}", data[1]),
        cmd => format!("?? 0x{:02X} [{}B]", cmd, data.len()),
    }
}

fn main() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    let tsv_path = args.get(1).map(|s| s.as_str()).unwrap_or("/tmp/bulk_data.tsv");

    let packets = parse_tsv(tsv_path)?;

    let n_out = packets.iter().filter(|p| p.ep == EP_OUT).count();
    let n_in = packets.iter().filter(|p| p.ep == EP_IN).count();
    println!("Total Bulk data packets: {}", packets.len());
    println!("  OUT (EP 0x01): {}", n_out);
    println!("  IN  (EP 0x82): {}", n_in);

    // Packet size distribution
    let mut out_sizes: BTreeMap<usize, usize> = BTreeMap::new();
    let mut in_sizes: BTreeMap<usize, usize> = BTreeMap::new();
    for p in &packets {
        let map = if p.ep == EP_OUT { &mut out_sizes } else { &mut in_sizes };
        *map.entry(p.len).or_insert(0) += 1;
    }
    println!("\nOUT packet sizes: {:?}", out_sizes);
    println!("IN  packet sizes: {:?}", in_sizes);

    // Large IN transfers
    let large_in: Vec<&Packet> = packets.iter().filter(|p| p.ep == EP_IN && p.len > 10).collect();
    println!("\nLarge IN packets (>10B): {}", large_in.len());
    for p in large_in.iter().take(20) {
        let preview: String = p.data.iter().take(20).map(|b| format!("{:02x}", b)).collect::<Vec<_>>().join("");
        let ellipsis = if p.data.len() > 20 { "..." } else { "" };
        println!("  [{:>5}] t={:.3}s  {:>5}B  {}{}", p.frame, p.time, p.len, preview, ellipsis);
    }

    // Group by time gaps (>0.5s = new phase)
    println!("\n{}", "=".repeat(80));
    println!("TRAFFIC PHASES (separated by >0.5s gaps)");
    println!("{}", "=".repeat(80));

    let mut phases: Vec<Vec<usize>> = Vec::new();
    let mut current: Vec<usize> = Vec::new();
    let mut last_time = 0.0;

    for (i, p) in packets.iter().enumerate() {
        if p.time - last_time > 0.5 && !current.is_empty() {
            phases.push(current.clone());
            current.clear();
        }
        current.push(i);
        last_time = p.time;
    }
    if !current.is_empty() {
        phases.push(current);
    }

    for (phase_idx, indices) in phases.iter().enumerate() {
        let first = &packets[indices[0]];
        let last = &packets[*indices.last().unwrap()];
        let duration = last.time - first.time;
        let phase_n_out = indices.iter().filter(|&&i| packets[i].ep == EP_OUT).count();
        let phase_n_in = indices.iter().filter(|&&i| packets[i].ep == EP_IN).count();
        let total_in_bytes: usize = indices.iter()
            .filter(|&&i| packets[i].ep == EP_IN)
            .map(|&i| packets[i].len)
            .sum();

        let phase_type = if total_in_bytes > 5000 {
            "IMAGE_CAPTURE"
        } else if phase_n_out > 10 && total_in_bytes < 100 {
            "CONFIG/INIT"
        } else if phase_n_out < 5 && phase_n_in < 5 {
            "BRIEF"
        } else {
            "unknown"
        };

        println!("\nPhase {}: t={:.3}-{:.3}s ({:.3}s) OUT={} IN={} IN_bytes={} [{}]",
            phase_idx, first.time, last.time, duration,
            phase_n_out, phase_n_in, total_in_bytes, phase_type);

        let mut cmds_shown = 0;
        for &i in indices {
            let p = &packets[i];
            if p.ep == EP_OUT && !p.data.is_empty() {
                println!("    [{:>5}] OUT {}", p.frame, decode_cmd(&p.data));
                cmds_shown += 1;
                if cmds_shown >= 8 {
                    let remaining = indices.iter()
                        .filter(|&&j| packets[j].ep == EP_OUT && !packets[j].data.is_empty())
                        .count() - cmds_shown;
                    if remaining > 0 {
                        println!("    ... +{} more OUT commands", remaining);
                    }
                    break;
                }
            } else if p.ep == EP_IN && p.len > 10 {
                let preview: String = p.data.iter().take(30).map(|b| format!("{:02x}", b)).collect::<Vec<_>>().join("");
                let ellipsis = if p.data.len() > 30 { "..." } else { "" };
                println!("    [{:>5}] IN  {}B: {}{}", p.frame, p.len, preview, ellipsis);
                cmds_shown += 1;
                if cmds_shown >= 8 { break; }
            }
        }
    }

    // Register writes
    println!("\n{}", "=".repeat(80));
    println!("UNIQUE REGISTER WRITES");
    println!("{}", "=".repeat(80));

    let mut reg_writes: BTreeMap<u8, Vec<String>> = BTreeMap::new();
    for p in packets.iter().filter(|p| p.ep == EP_OUT) {
        if p.data.len() >= 3 && p.data[0] == 0xA9 {
            let reg = p.data[1];
            let val = if p.data.len() == 3 {
                format!("0x{:02X}", p.data[2])
            } else if p.data.len() == 4 {
                format!("0x{:04X}", ((p.data[2] as u16) << 8) | p.data[3] as u16)
            } else {
                format!("[{}B]", p.data.len() - 2)
            };
            let vals = reg_writes.entry(reg).or_default();
            if !vals.contains(&val) {
                vals.push(val);
            }
        }
    }
    for (reg, vals) in &reg_writes {
        if vals.len() <= 5 {
            println!("  reg 0x{:02X}: {}", reg, vals.join(", "));
        } else {
            println!("  reg 0x{:02X}: {} ... ({} unique values)", reg, vals[..5].join(", "), vals.len());
        }
    }

    // Register reads
    println!("\n{}", "=".repeat(80));
    println!("UNIQUE REGISTER READS");
    println!("{}", "=".repeat(80));

    let mut reg_reads: BTreeMap<u8, Vec<String>> = BTreeMap::new();
    for i in 0..packets.len() {
        let p = &packets[i];
        if p.ep == EP_OUT && p.data.len() >= 2 && p.data[0] == 0xA8 {
            let reg = p.data[1];
            for j in (i + 1)..std::cmp::min(i + 5, packets.len()) {
                if packets[j].ep == EP_IN && !packets[j].data.is_empty() {
                    let resp = &packets[j].data;
                    let val = if resp.len() >= 3 {
                        format!("0x{:02X}", resp[2])
                    } else {
                        resp.iter().map(|b| format!("{:02x}", b)).collect::<Vec<_>>().join("")
                    };
                    let vals = reg_reads.entry(reg).or_default();
                    if !vals.contains(&val) {
                        vals.push(val);
                    }
                    break;
                }
            }
        }
    }
    for (reg, vals) in &reg_reads {
        if vals.len() <= 10 {
            println!("  reg 0x{:02X}: {}", reg, vals.join(", "));
        } else {
            println!("  reg 0x{:02X}: {} ... ({} unique values)", reg, vals[..5].join(", "), vals.len());
        }
    }

    // Image capture events
    println!("\n{}", "=".repeat(80));
    println!("IMAGE CAPTURE EVENTS");
    println!("{}", "=".repeat(80));

    let mut capture_count = 0;
    let mut i = 0;
    while i < packets.len() {
        let p = &packets[i];
        if p.ep == EP_IN && p.len >= 64 {
            let mut total_bytes = p.len;
            let start_frame = p.frame;
            let start_time = p.time;
            let mut j = i + 1;
            while j < packets.len() && packets[j].ep == EP_IN && packets[j].len >= 64 {
                total_bytes += packets[j].len;
                j += 1;
            }
            let n_transfers = j - i;

            if total_bytes >= 256 {
                capture_count += 1;
                let end_frame = packets[j - 1].frame;

                // Find preceding OUT commands
                let mut pre_cmds = Vec::new();
                for k in (i.saturating_sub(10))..i {
                    if packets[k].ep == EP_OUT && !packets[k].data.is_empty() {
                        pre_cmds.push(decode_cmd(&packets[k].data));
                    }
                }

                println!("\n  Capture #{}: frames {}-{}, t={:.3}s, {} transfers, {} bytes",
                    capture_count, start_frame, end_frame, start_time, n_transfers, total_bytes);
                if !pre_cmds.is_empty() {
                    let show: Vec<&String> = pre_cmds.iter().rev().take(5).collect::<Vec<_>>().into_iter().rev().collect();
                    println!("    Preceding: {}", show.iter().map(|s| s.as_str()).collect::<Vec<_>>().join(", "));
                }
                let first_data: String = packets[i].data.iter().take(40).map(|b| format!("{:02x}", b)).collect::<Vec<_>>().join("");
                println!("    First data: {}...", first_data);
            }
            i = j;
        } else {
            i += 1;
        }
    }
    println!("\n  Total image captures: {}", capture_count);

    Ok(())
}
