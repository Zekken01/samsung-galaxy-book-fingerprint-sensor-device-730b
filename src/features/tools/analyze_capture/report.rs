use std::collections::BTreeMap;

use crate::platform::i18n;
use crate::platform::i18n::Key;

use super::parser::decode_cmd;
use crate::features::tools::tsv::{Packet, EP_IN, EP_OUT};

pub fn print_analysis(packets: &[Packet]) {
    let out_count = packets.iter().filter(|packet| packet.ep == EP_OUT).count();
    let in_count = packets.iter().filter(|packet| packet.ep == EP_IN).count();
    println!(
        "{}",
        i18n::format(
            Key::AnalyzeTotalPackets,
            &[("count", packets.len().to_string())]
        )
    );
    println!(
        "{}",
        i18n::format(Key::AnalyzeOutPackets, &[("count", out_count.to_string())])
    );
    println!(
        "{}",
        i18n::format(Key::AnalyzeInPackets, &[("count", in_count.to_string())])
    );

    let mut out_sizes: BTreeMap<usize, usize> = BTreeMap::new();
    let mut in_sizes: BTreeMap<usize, usize> = BTreeMap::new();
    for packet in packets {
        let sizes = if packet.ep == EP_OUT {
            &mut out_sizes
        } else {
            &mut in_sizes
        };
        *sizes.entry(packet.len).or_insert(0) += 1;
    }

    println!(
        "\n{}",
        i18n::format(Key::AnalyzeOutSizes, &[("sizes", format!("{out_sizes:?}"))])
    );
    println!(
        "{}",
        i18n::format(Key::AnalyzeInSizes, &[("sizes", format!("{in_sizes:?}"))])
    );

    let large_in_packets: Vec<&Packet> = packets
        .iter()
        .filter(|packet| packet.ep == EP_IN && packet.len > 10)
        .collect();
    println!(
        "\n{}",
        i18n::format(
            Key::AnalyzeLargeInPackets,
            &[("count", large_in_packets.len().to_string())]
        )
    );

    for packet in large_in_packets.iter().take(20) {
        let preview: String = packet
            .data
            .iter()
            .take(20)
            .map(|byte| format!("{:02x}", byte))
            .collect::<Vec<_>>()
            .join("");
        let ellipsis = if packet.data.len() > 20 { "..." } else { "" };
        println!(
            "  [{:>5}] t={:.3}s  {:>5}B  {}{}",
            packet.frame, packet.time, packet.len, preview, ellipsis
        );
    }

    print_phases(packets);
    print_unique_writes(packets);
    print_unique_reads(packets);
    print_image_events(packets);
}

fn print_phases(packets: &[Packet]) {
    println!("\n{}", "=".repeat(80));
    println!("{}", i18n::text(Key::AnalyzeTrafficPhasesTitle));
    println!("{}", "=".repeat(80));

    let mut phases: Vec<Vec<usize>> = Vec::new();
    let mut current_phase: Vec<usize> = Vec::new();
    let mut last_time = 0.0;

    for (index, packet) in packets.iter().enumerate() {
        if packet.time - last_time > 0.5 && !current_phase.is_empty() {
            phases.push(current_phase.clone());
            current_phase.clear();
        }
        current_phase.push(index);
        last_time = packet.time;
    }
    if !current_phase.is_empty() {
        phases.push(current_phase);
    }

    for (phase_index, indices) in phases.iter().enumerate() {
        let first = &packets[indices[0]];
        let last = &packets[*indices.last().expect("phase should be non-empty")];
        let duration = last.time - first.time;
        let phase_out_count = indices.iter().filter(|&&i| packets[i].ep == EP_OUT).count();
        let phase_in_count = indices.iter().filter(|&&i| packets[i].ep == EP_IN).count();
        let total_in_bytes: usize = indices
            .iter()
            .filter(|&&i| packets[i].ep == EP_IN)
            .map(|&i| packets[i].len)
            .sum();

        let phase_type = if total_in_bytes > 5000 {
            "IMAGE_CAPTURE"
        } else if phase_out_count > 10 && total_in_bytes < 100 {
            "CONFIG/INIT"
        } else if phase_out_count < 5 && phase_in_count < 5 {
            "BRIEF"
        } else {
            "unknown"
        };

        println!(
            "\nPhase {}: t={:.3}-{:.3}s ({:.3}s) OUT={} IN={} IN_bytes={} [{}]",
            phase_index,
            first.time,
            last.time,
            duration,
            phase_out_count,
            phase_in_count,
            total_in_bytes,
            phase_type
        );

        let mut shown = 0;
        for &packet_index in indices {
            let packet = &packets[packet_index];
            if packet.ep == EP_OUT && !packet.data.is_empty() {
                println!("    [{:>5}] OUT {}", packet.frame, decode_cmd(&packet.data));
                shown += 1;
                if shown >= 8 {
                    let remaining = indices
                        .iter()
                        .filter(|&&idx| packets[idx].ep == EP_OUT && !packets[idx].data.is_empty())
                        .count()
                        - shown;
                    if remaining > 0 {
                        println!("    ... +{} more OUT commands", remaining);
                    }
                    break;
                }
            } else if packet.ep == EP_IN && packet.len > 10 {
                let preview: String = packet
                    .data
                    .iter()
                    .take(30)
                    .map(|byte| format!("{:02x}", byte))
                    .collect::<Vec<_>>()
                    .join("");
                let ellipsis = if packet.data.len() > 30 { "..." } else { "" };
                println!(
                    "    [{:>5}] IN  {}B: {}{}",
                    packet.frame, packet.len, preview, ellipsis
                );
                shown += 1;
                if shown >= 8 {
                    break;
                }
            }
        }
    }
}

fn print_unique_writes(packets: &[Packet]) {
    println!("\n{}", "=".repeat(80));
    println!("{}", i18n::text(Key::AnalyzeUniqueWritesTitle));
    println!("{}", "=".repeat(80));

    let mut writes: BTreeMap<u8, Vec<String>> = BTreeMap::new();
    for packet in packets.iter().filter(|packet| packet.ep == EP_OUT) {
        if packet.data.len() >= 3 && packet.data[0] == 0xA9 {
            let reg = packet.data[1];
            let value = if packet.data.len() == 3 {
                format!("0x{:02X}", packet.data[2])
            } else if packet.data.len() == 4 {
                format!(
                    "0x{:04X}",
                    ((packet.data[2] as u16) << 8) | packet.data[3] as u16
                )
            } else {
                format!("[{}B]", packet.data.len() - 2)
            };
            let values = writes.entry(reg).or_default();
            if !values.contains(&value) {
                values.push(value);
            }
        }
    }

    for (reg, values) in &writes {
        if values.len() <= 5 {
            println!("  reg 0x{:02X}: {}", reg, values.join(", "));
        } else {
            println!(
                "  reg 0x{:02X}: {} ... ({} unique values)",
                reg,
                values[..5].join(", "),
                values.len()
            );
        }
    }
}

fn print_unique_reads(packets: &[Packet]) {
    println!("\n{}", "=".repeat(80));
    println!("{}", i18n::text(Key::AnalyzeUniqueReadsTitle));
    println!("{}", "=".repeat(80));

    let mut reads: BTreeMap<u8, Vec<String>> = BTreeMap::new();
    for i in 0..packets.len() {
        let packet = &packets[i];
        if packet.ep == EP_OUT && packet.data.len() >= 2 && packet.data[0] == 0xA8 {
            let reg = packet.data[1];
            for next_packet in packets
                .iter()
                .take(std::cmp::min(i + 5, packets.len()))
                .skip(i + 1)
            {
                if next_packet.ep == EP_IN && !next_packet.data.is_empty() {
                    let response = &next_packet.data;
                    let value = if response.len() >= 3 {
                        format!("0x{:02X}", response[2])
                    } else {
                        response
                            .iter()
                            .map(|byte| format!("{:02x}", byte))
                            .collect::<Vec<_>>()
                            .join("")
                    };
                    let values = reads.entry(reg).or_default();
                    if !values.contains(&value) {
                        values.push(value);
                    }
                    break;
                }
            }
        }
    }

    for (reg, values) in &reads {
        if values.len() <= 10 {
            println!("  reg 0x{:02X}: {}", reg, values.join(", "));
        } else {
            println!(
                "  reg 0x{:02X}: {} ... ({} unique values)",
                reg,
                values[..5].join(", "),
                values.len()
            );
        }
    }
}

fn print_image_events(packets: &[Packet]) {
    println!("\n{}", "=".repeat(80));
    println!("{}", i18n::text(Key::AnalyzeImageEventsTitle));
    println!("{}", "=".repeat(80));

    let mut capture_count = 0;
    let mut index = 0;
    while index < packets.len() {
        let packet = &packets[index];
        if packet.ep == EP_IN && packet.len >= 64 {
            let mut total_bytes = packet.len;
            let start_frame = packet.frame;
            let start_time = packet.time;
            let mut next = index + 1;
            while next < packets.len() && packets[next].ep == EP_IN && packets[next].len >= 64 {
                total_bytes += packets[next].len;
                next += 1;
            }
            let transfer_count = next - index;

            if total_bytes >= 256 {
                capture_count += 1;
                let end_frame = packets[next - 1].frame;

                let mut preceding = Vec::new();
                for previous_packet in packets.iter().take(index).skip(index.saturating_sub(10)) {
                    if previous_packet.ep == EP_OUT && !previous_packet.data.is_empty() {
                        preceding.push(decode_cmd(&previous_packet.data));
                    }
                }

                println!(
                    "\n  Capture #{}: frames {}-{}, t={:.3}s, {} transfers, {} bytes",
                    capture_count, start_frame, end_frame, start_time, transfer_count, total_bytes
                );
                if !preceding.is_empty() {
                    let show: Vec<&String> = preceding
                        .iter()
                        .rev()
                        .take(5)
                        .collect::<Vec<_>>()
                        .into_iter()
                        .rev()
                        .collect();
                    println!(
                        "    Preceding: {}",
                        show.iter()
                            .map(|s| s.as_str())
                            .collect::<Vec<_>>()
                            .join(", ")
                    );
                }
                let first_data: String = packets[index]
                    .data
                    .iter()
                    .take(40)
                    .map(|byte| format!("{:02x}", byte))
                    .collect::<Vec<_>>()
                    .join("");
                println!("    First data: {}...", first_data);
            }
            index = next;
        } else {
            index += 1;
        }
    }

    println!(
        "\n{}",
        i18n::format(
            Key::AnalyzeTotalCaptures,
            &[("count", capture_count.to_string())]
        )
    );
}
