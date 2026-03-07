pub fn decode_cmd(data: &[u8]) -> String {
    if data.is_empty() {
        return "EMPTY".to_string();
    }

    match data[0] {
        0xA9 => {
            if data.len() == 3 {
                format!("W1 0x{:02X}=0x{:02X}", data[1], data[2])
            } else if data.len() == 4 {
                format!(
                    "W2 0x{:02X}=0x{:04X}",
                    data[1],
                    ((data[2] as u16) << 8) | data[3] as u16
                )
            } else {
                format!("WX 0x{:02X} [{}B]", data[1], data.len() - 2)
            }
        }
        0xA8 => format!("RD 0x{:02X}", data[1]),
        command => format!("?? 0x{:02X} [{}B]", command, data.len()),
    }
}
