#pragma once
/* Image processing shared by the driver and tools/cb-eval.c. */

#include <math.h>
#include <glib.h>

/* CLAHE (Contrast Limited Adaptive Histogram Equalization).
 * Divides image into tiles, computes clipped histograms per tile,
 * then bilinear-interpolates CDF mappings to avoid block artifacts.
 * Operates in-place on buf. */
static void
cb_clahe (guint8 *buf, int width, int height,
          int tile_w, int tile_h, gdouble clip_limit)
{
  int nx = width / tile_w;   /* number of tiles horizontally */
  int ny = height / tile_h;  /* number of tiles vertically */

  if (nx < 1 || ny < 1)
    return;

  int tile_pixels = tile_w * tile_h;
  int clip_count = (int)(clip_limit * tile_pixels / 256);
  if (clip_count < 1)
    clip_count = 1;

  /* Allocate CDF LUTs for all tiles: nx * ny * 256 bytes */
  guint8 *cdfs = g_malloc0 ((gsize) nx * ny * 256);

  /* Build per-tile CDF */
  for (int ty = 0; ty < ny; ty++)
    {
      for (int tx = 0; tx < nx; tx++)
        {
          int hist[256] = { 0 };
          int y0 = ty * tile_h;
          int x0 = tx * tile_w;

          /* Compute histogram for this tile */
          for (int y = y0; y < y0 + tile_h; y++)
            for (int x = x0; x < x0 + tile_w; x++)
              hist[buf[y * width + x]]++;

          /* Clip histogram and redistribute excess */
          int excess = 0;
          for (int i = 0; i < 256; i++)
            {
              if (hist[i] > clip_count)
                {
                  excess += hist[i] - clip_count;
                  hist[i] = clip_count;
                }
            }
          int bonus = excess / 256;
          int residual = excess - bonus * 256;
          for (int i = 0; i < 256; i++)
            hist[i] += bonus;
          /* Spread residual evenly across bins */
          for (int i = 0; i < residual; i++)
            hist[(int)((gint64) i * 256 / residual)]++;

          /* Compute CDF and normalize to 0-255 */
          int cdf = 0;
          guint8 *lut = cdfs + ((gsize) ty * nx + tx) * 256;
          for (int i = 0; i < 256; i++)
            {
              cdf += hist[i];
              lut[i] = (guint8) CLAMP ((int)((gdouble) cdf * 255 / tile_pixels + 0.5), 0, 255);
            }
        }
    }

  /* Apply CLAHE with bilinear interpolation between tile CDFs */
  for (int y = 0; y < height; y++)
    {
      /* Map pixel to tile-center coordinates */
      gdouble gy = ((gdouble) y - (gdouble) tile_h / 2.0) / tile_h;
      int ty0 = (int) gy;
      int ty1 = ty0 + 1;
      gdouble fy = gy - ty0;

      if (ty0 < 0) { ty0 = 0; fy = 0.0; }
      if (ty1 >= ny) { ty1 = ny - 1; fy = 1.0; if (ty0 >= ny) ty0 = ny - 1; }
      if (ty0 == ty1) fy = 0.0;

      for (int x = 0; x < width; x++)
        {
          gdouble gx = ((gdouble) x - (gdouble) tile_w / 2.0) / tile_w;
          int tx0 = (int) gx;
          int tx1 = tx0 + 1;
          gdouble fx = gx - tx0;

          if (tx0 < 0) { tx0 = 0; fx = 0.0; }
          if (tx1 >= nx) { tx1 = nx - 1; fx = 1.0; if (tx0 >= nx) tx0 = nx - 1; }
          if (tx0 == tx1) fx = 0.0;

          guint8 val = buf[y * width + x];

          /* Bilinear interpolation of 4 surrounding tile CDFs */
          guint8 *lut_tl = cdfs + ((gsize) ty0 * nx + tx0) * 256;
          guint8 *lut_tr = cdfs + ((gsize) ty0 * nx + tx1) * 256;
          guint8 *lut_bl = cdfs + ((gsize) ty1 * nx + tx0) * 256;
          guint8 *lut_br = cdfs + ((gsize) ty1 * nx + tx1) * 256;

          gdouble v = (1.0 - fx) * (1.0 - fy) * lut_tl[val]
                    + fx         * (1.0 - fy) * lut_tr[val]
                    + (1.0 - fx) * fy         * lut_bl[val]
                    + fx         * fy         * lut_br[val];

          buf[y * width + x] = (guint8) CLAMP ((int)(v + 0.5), 0, 255);
        }
    }

  g_free (cdfs);
}

/* Bicubic interpolation kernel (Catmull-Rom, a = -0.5).
 * w(t) = (a+2)|t|^3 - (a+3)|t|^2 + 1          for |t| <= 1
 *       = a|t|^3 - 5a|t|^2 + 8a|t| - 4a        for 1 < |t| < 2
 *       = 0                                      otherwise */
static inline gdouble
bicubic_weight (gdouble t)
{
  const gdouble a = -0.5;
  gdouble at = (t < 0) ? -t : t;

  if (at <= 1.0)
    return (a + 2.0) * at * at * at - (a + 3.0) * at * at + 1.0;
  else if (at < 2.0)
    return a * at * at * at - 5.0 * a * at * at + 8.0 * a * at - 4.0 * a;
  else
    return 0.0;
}

/* Bicubic resample: src (sw x sh) -> dst (dw x dh), any scale factor.
 * Samples a 4x4 neighborhood for each output pixel using Catmull-Rom. */
static void
cb_bicubic_resample (const guint8 *src, int sw, int sh,
                     guint8 *dst, int dw, int dh)
{
  gdouble rx = (gdouble) sw / dw;
  gdouble ry = (gdouble) sh / dh;

  for (int dy = 0; dy < dh; dy++)
    {
      gdouble sy = ((gdouble) dy + 0.5) * ry - 0.5;
      int iy = (int) floor (sy);
      gdouble fy = sy - iy;

      for (int dx = 0; dx < dw; dx++)
        {
          gdouble sx = ((gdouble) dx + 0.5) * rx - 0.5;
          int ix = (int) floor (sx);
          gdouble fx = sx - ix;

          gdouble val = 0.0;

          for (int m = -1; m <= 2; m++)
            {
              gdouble wy = bicubic_weight (fy - m);
              int yy = iy + m;
              if (yy < 0) yy = 0;
              if (yy >= sh) yy = sh - 1;

              for (int n = -1; n <= 2; n++)
                {
                  gdouble wx = bicubic_weight (fx - n);
                  int xx = ix + n;
                  if (xx < 0) xx = 0;
                  if (xx >= sw) xx = sw - 1;

                  val += wy * wx * src[yy * sw + xx];
                }
            }

          dst[dy * dw + dx] = (guint8) CLAMP ((int)(val + 0.5), 0, 255);
        }
    }
}
