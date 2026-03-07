use crate::features::tools::tsv::{Packet, EP_IN, EP_OUT};

pub const WIDTH: u32 = 112;
pub const HEIGHT: u32 = 96;
pub const IMG_SIZE: usize = (WIDTH * HEIGHT) as usize;

#[derive(Clone, Debug)]
pub struct CapturedImage {
    pub frame: u32,
    pub time: f64,
    pub data: Vec<u8>,
}

pub fn extract_images(packets: &[Packet]) -> Vec<CapturedImage> {
    let mut images = Vec::new();

    for i in 0..packets.len() {
        let packet = &packets[i];
        if packet.ep == EP_OUT
            && packet.data.len() >= 2
            && packet.data[0] == 0xA8
            && packet.data[1] == 0x06
        {
            let mut image_data = Vec::new();
            let mut j = i + 1;

            while j < packets.len() {
                let current = &packets[j];
                if current.ep == EP_IN && (current.len == 256 || current.len == 259) {
                    if current.len == 259 && current.data.len() >= 3 {
                        image_data.extend_from_slice(&current.data[3..]);
                    } else {
                        image_data.extend_from_slice(&current.data);
                    }
                } else if current.ep == EP_OUT && (current.len == 256 || current.len == 259) {
                    // Dummy clock packets.
                } else {
                    break;
                }
                j += 1;
            }

            images.push(CapturedImage {
                frame: packet.frame,
                time: packet.time,
                data: image_data,
            });
        }
    }

    images
}
