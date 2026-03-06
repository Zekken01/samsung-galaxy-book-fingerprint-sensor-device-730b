use anyhow::{Context, Result};
use std::path::Path;

use crate::sensor::capture::{IMG_WIDTH, IMG_HEIGHT};

pub fn save_image(data: &[u8], path: &Path) -> Result<()> {
    let img = image::GrayImage::from_raw(IMG_WIDTH, IMG_HEIGHT, data.to_vec())
        .context("Failed to create image from raw data")?;
    img.save(path)
        .with_context(|| format!("Failed to save image to {}", path.display()))?;
    Ok(())
}
