use anyhow::{Context, Result};
use std::collections::BTreeSet;
use std::fs;
use std::path::Path;

use crate::platform::i18n;
use crate::platform::i18n::Key;

use super::core::{CapturedImage, HEIGHT, IMG_SIZE, WIDTH};

pub fn write_images(out_dir: &str, images: &[CapturedImage]) -> Result<()> {
    fs::create_dir_all(out_dir).context("Failed to create output directory")?;

    println!(
        "{}",
        i18n::format(
            Key::ExtractFoundImages,
            &[("count", images.len().to_string())]
        )
    );

    for (index, image) in images.iter().enumerate() {
        println!(
            "\n{}",
            i18n::format(
                Key::ExtractImageHeader,
                &[
                    ("index", (index + 1).to_string()),
                    ("frame", image.frame.to_string()),
                    ("time", format!("{:.3}", image.time)),
                    ("bytes", image.data.len().to_string()),
                ],
            )
        );

        let raw_path = Path::new(out_dir).join(format!("fp_raw_{:02}.bin", index));
        fs::write(&raw_path, &image.data)?;
        println!(
            "{}",
            i18n::format(
                Key::ExtractRawSaved,
                &[("path", raw_path.display().to_string())]
            )
        );

        if image.data.len() >= IMG_SIZE {
            save_png_variants(out_dir, index, image)?;
        }
    }

    println!("\n{}", "=".repeat(60));
    println!(
        "{}",
        i18n::format(
            Key::ExtractTotalImages,
            &[("count", images.len().to_string())]
        )
    );
    if !images.is_empty() {
        let sizes: BTreeSet<usize> = images.iter().map(|image| image.data.len()).collect();
        println!(
            "{}",
            i18n::format(Key::ExtractImageSizes, &[("sizes", format!("{sizes:?}"))])
        );
        println!(
            "{}",
            i18n::format(
                Key::ExtractExpectedSize,
                &[
                    ("width", WIDTH.to_string()),
                    ("height", HEIGHT.to_string()),
                    ("bytes", IMG_SIZE.to_string()),
                ],
            )
        );
    }

    Ok(())
}

fn save_png_variants(out_dir: &str, index: usize, image: &CapturedImage) -> Result<()> {
    let pixel_data = image.data[..IMG_SIZE].to_vec();
    if let Some(gray_image) = image::GrayImage::from_raw(WIDTH, HEIGHT, pixel_data) {
        let png_path = Path::new(out_dir).join(format!("fp_{:02}_{}x{}.png", index, WIDTH, HEIGHT));
        gray_image
            .save(&png_path)
            .with_context(|| format!("Failed to save {}", png_path.display()))?;
        println!(
            "{}",
            i18n::format(
                Key::ExtractPngSaved,
                &[
                    ("path", png_path.display().to_string()),
                    ("width", WIDTH.to_string()),
                    ("height", HEIGHT.to_string()),
                ],
            )
        );
    }

    if image.data.len() >= IMG_SIZE + 3 {
        let offset_data = image.data[3..IMG_SIZE + 3].to_vec();
        if let Some(gray_image) = image::GrayImage::from_raw(WIDTH, HEIGHT, offset_data) {
            let png_path =
                Path::new(out_dir).join(format!("fp_{:02}_{}x{}_off3.png", index, WIDTH, HEIGHT));
            gray_image
                .save(&png_path)
                .with_context(|| format!("Failed to save {}", png_path.display()))?;
            println!(
                "{}",
                i18n::format(
                    Key::ExtractPngSavedOff3,
                    &[("path", png_path.display().to_string())],
                )
            );
        }
    }

    Ok(())
}
