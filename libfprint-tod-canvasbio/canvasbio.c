#define FP_COMPONENT "canvasbio"

#include <stdio.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
#include <drivers_api.h>
#include "canvasbio.h"

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

/* Bicubic upscale: src (sw x sh) → dst (sw*scale x sh*scale).
 * Samples a 4x4 neighborhood for each output pixel using Catmull-Rom. */
static void
cb_bicubic_upscale (const guint8 *src, int sw, int sh,
                    guint8 *dst, int scale)
{
  int dw = sw * scale;
  int dh = sh * scale;

  for (int dy = 0; dy < dh; dy++)
    {
      gdouble sy = ((gdouble) dy + 0.5) / scale - 0.5;
      int iy = (int) floor (sy);
      gdouble fy = sy - iy;

      for (int dx = 0; dx < dw; dx++)
        {
          gdouble sx = ((gdouble) dx + 0.5) / scale - 0.5;
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

G_DECLARE_FINAL_TYPE (FpiDeviceCanvasbio, fpi_device_canvasbio, FPI, DEVICE_CANVASBIO, FpImageDevice)
G_DEFINE_TYPE (FpiDeviceCanvasbio, fpi_device_canvasbio, FP_TYPE_IMAGE_DEVICE)

static const FpIdEntry id_table[] = {
  { .vid = 0x04E8, .pid = 0x730B },
  { .vid = 0, .pid = 0 },
};

/* ========================================================================
 * USB Helper Functions
 * ======================================================================== */

static void
cb_vendor_write (FpiSsm *ssm, FpDevice *dev,
                 guint8 request, guint16 value, guint16 index)
{
  FpiUsbTransfer *xfer = fpi_usb_transfer_new (dev);

  fpi_usb_transfer_fill_control (xfer,
                                 G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
                                 G_USB_DEVICE_REQUEST_TYPE_VENDOR,
                                 G_USB_DEVICE_RECIPIENT_DEVICE,
                                 request, value, index, 0);
  xfer->ssm = ssm;
  fpi_usb_transfer_submit (xfer, TIMEOUT_CMD, NULL,
                           fpi_ssm_usb_transfer_cb, NULL);
}

static void
cb_vendor_read (FpiSsm *ssm, FpDevice *dev,
                guint8 request, guint16 value,
                gsize len, FpiUsbTransferCallback callback)
{
  FpiUsbTransfer *xfer = fpi_usb_transfer_new (dev);

  fpi_usb_transfer_fill_control (xfer,
                                 G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
                                 G_USB_DEVICE_REQUEST_TYPE_VENDOR,
                                 G_USB_DEVICE_RECIPIENT_DEVICE,
                                 request, value, 0, len);
  xfer->ssm = ssm;
  fpi_usb_transfer_submit (xfer, TIMEOUT_CMD, NULL, callback, NULL);
}

static void
cb_bulk_write (FpiSsm *ssm, FpDevice *dev,
               const guint8 *data, gsize len)
{
  FpiUsbTransfer *xfer = fpi_usb_transfer_new (dev);

  /* Allocate transfer-owned buffer and copy data into it.
   * Cannot use fill_bulk_full with stack-local data because the
   * async transfer outlives the caller's stack frame. */
  fpi_usb_transfer_fill_bulk (xfer, CB_EP_OUT, len);
  memcpy (xfer->buffer, data, len);
  xfer->ssm = ssm;
  fpi_usb_transfer_submit (xfer, TIMEOUT_CMD, NULL,
                           fpi_ssm_usb_transfer_cb, NULL);
}

static void
cb_bulk_read (FpiSsm *ssm, FpDevice *dev,
              gsize len, guint timeout_ms,
              FpiUsbTransferCallback callback)
{
  FpiUsbTransfer *xfer = fpi_usb_transfer_new (dev);

  fpi_usb_transfer_fill_bulk (xfer, CB_EP_IN, len);
  xfer->ssm = ssm;
  fpi_usb_transfer_submit (xfer, timeout_ms, NULL, callback, NULL);
}

/* ========================================================================
 * Generic Register Sequence Sub-SSM
 *
 * Walks a RegEntry table. Each entry takes 3 states:
 *   PREPARE (CA vendor write) -> BULK_OUT (SPI cmd) -> COMPLETE (CC or bulk IN)
 * ======================================================================== */

enum {
  REG_SEQ_PREPARE,
  REG_SEQ_BULK_OUT,
  REG_SEQ_COMPLETE,
  REG_SEQ_NUM_STATES,
};

static void reg_seq_complete_cb (FpiUsbTransfer *xfer, FpDevice *dev,
                                 gpointer user_data, GError *error);

static void
reg_seq_run (FpiSsm *ssm, FpDevice *dev)
{
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);
  int state = fpi_ssm_get_cur_state (ssm);

  if (self->reg_idx >= self->reg_table_len)
    {
      fpi_ssm_mark_completed (ssm);
      return;
    }

  const RegEntry *e = &self->reg_table[self->reg_idx];

  switch (state)
    {
    case REG_SEQ_PREPARE:
      {
        guint16 direction, size;
        if (e->op == REG_READ || e->op == REG_READ_FULL)
          {
            direction = SPI_DIR_READ;
            size = 3;
          }
        else if (e->op == REG_WRITE1)
          {
            direction = SPI_DIR_WRITE;
            size = 3;
          }
        else /* REG_WRITE2 */
          {
            direction = SPI_DIR_WRITE;
            size = 4;
          }
        cb_vendor_write (ssm, dev, CY_SPI_TRANSFER, direction, size);
        break;
      }

    case REG_SEQ_BULK_OUT:
      {
        if (e->op == REG_READ || e->op == REG_READ_FULL)
          {
            guint8 cmd[3] = { SPI_READ_CMD, e->reg, 0x00 };
            cb_bulk_write (ssm, dev, cmd, 3);
          }
        else if (e->op == REG_WRITE1)
          {
            guint8 cmd[3] = { SPI_WRITE_CMD, e->reg, (guint8)(e->val & 0xFF) };
            cb_bulk_write (ssm, dev, cmd, 3);
          }
        else /* REG_WRITE2 */
          {
            guint8 cmd[4] = { SPI_WRITE_CMD, e->reg,
                              (guint8)(e->val >> 8), (guint8)(e->val & 0xFF) };
            cb_bulk_write (ssm, dev, cmd, 4);
          }
        break;
      }

    case REG_SEQ_COMPLETE:
      {
        if (e->op == REG_READ || e->op == REG_READ_FULL)
          {
            /* Read response via bulk IN */
            cb_bulk_read (ssm, dev, 64, TIMEOUT_CMD, reg_seq_complete_cb);
          }
        else
          {
            /* Check SPI ready via CC vendor read */
            cb_vendor_read (ssm, dev, CY_READY_CHECK, 0, 4, reg_seq_complete_cb);
          }
        break;
      }
    }
}

static void
reg_seq_complete_cb (FpiUsbTransfer *xfer, FpDevice *dev,
                     gpointer user_data, GError *error)
{
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);

  if (error)
    {
      const RegEntry *e = &self->reg_table[self->reg_idx];
      fp_warn ("reg_seq error at idx=%d op=%d reg=0x%02X: %s",
               self->reg_idx, e->op, e->reg, error->message);
      fpi_ssm_mark_failed (xfer->ssm, error);
      return;
    }

  const RegEntry *e = &self->reg_table[self->reg_idx];

  if (e->op == REG_READ || e->op == REG_READ_FULL)
    {
      self->spi_result_len = MIN ((gsize) xfer->actual_length, sizeof (self->spi_result));
      memcpy (self->spi_result, xfer->buffer, self->spi_result_len);
    }

  self->reg_idx++;
  if (self->reg_idx < self->reg_table_len)
    fpi_ssm_jump_to_state (xfer->ssm, REG_SEQ_PREPARE);
  else
    fpi_ssm_mark_completed (xfer->ssm);
}

static FpiSsm *
reg_seq_ssm_new (FpDevice *dev, const RegEntry *table, gint len)
{
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);

  self->reg_idx = 0;
  self->reg_table = table;
  self->reg_table_len = len;

  FpiSsm *ssm = fpi_ssm_new (dev, reg_seq_run, REG_SEQ_NUM_STATES);
  return ssm;
}

/* ========================================================================
 * Init (dev_open) SSM
 *
 * Power-on and SPI enable are done synchronously in dev_open (matching
 * the Rust driver). Only the 17 register writes use the async SSM.
 * ======================================================================== */

enum {
  INIT_WRITE_REGS,
  INIT_NUM_STATES,
};


/* ========================================================================
 * Raw USB transfer helpers — ALL USB I/O via USBDEVFS ioctls
 *
 * GUsb 0.4+ uses a nested GLib main loop for sync transfers, which
 * causes reentrancy problems when called from GLib callbacks or threads.
 * We bypass GUsb entirely and use raw Linux USBDEVFS ioctls for:
 *   - Control transfers (vendor commands to Cypress MCU)
 *   - Bulk transfers (SPI data and image blocks)
 *
 * Buffer size MUST match expected transfer size for bulk IN (no oversized
 * buffers) because Full Speed USB (64B max packet) has no ZLP to signal
 * end-of-transfer when the response exactly fills the requested buffer.
 * ======================================================================== */

static gboolean
raw_ctrl_write (int fd, guint8 request, guint16 value,
                guint16 index, GError **error)
{
  struct usbdevfs_ctrltransfer ct = {
    .bRequestType = 0x40,  /* host-to-device, vendor, device */
    .bRequest = request,
    .wValue = value,
    .wIndex = index,
    .wLength = 0,
    .timeout = TIMEOUT_CMD,
    .data = NULL
  };
  if (ioctl (fd, USBDEVFS_CONTROL, &ct) < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "ctrl write (req=0x%02X) failed: %s",
                   request, g_strerror (errno));
      return FALSE;
    }
  return TRUE;
}

static int
raw_ctrl_read (int fd, guint8 request, guint16 value,
               guint8 *buf, gsize len, GError **error)
{
  struct usbdevfs_ctrltransfer ct = {
    .bRequestType = 0xC0,  /* device-to-host, vendor, device */
    .bRequest = request,
    .wValue = value,
    .wIndex = 0,
    .wLength = (guint16) len,
    .timeout = TIMEOUT_CMD,
    .data = buf
  };
  int ret = ioctl (fd, USBDEVFS_CONTROL, &ct);
  if (ret < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "ctrl read (req=0x%02X) failed: %s",
                   request, g_strerror (errno));
      return -1;
    }
  return ret;
}

static gboolean
raw_bulk_out (int fd, guint8 *data, gsize len, GError **error)
{
  struct usbdevfs_bulktransfer bt = {
    .ep = CB_EP_OUT, .len = (unsigned int) len,
    .timeout = TIMEOUT_CMD, .data = data
  };
  if (ioctl (fd, USBDEVFS_BULK, &bt) < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "bulk OUT failed: %s", g_strerror (errno));
      return FALSE;
    }
  return TRUE;
}

static int
raw_bulk_in (int fd, guint8 *data, gsize len, guint timeout_ms, GError **error)
{
  struct usbdevfs_bulktransfer bt = {
    .ep = CB_EP_IN, .len = (unsigned int) len,
    .timeout = timeout_ms, .data = data
  };
  int ret = ioctl (fd, USBDEVFS_BULK, &bt);
  if (ret < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "bulk IN failed: %s", g_strerror (errno));
      return -1;
    }
  return ret;
}

/* Synchronous SPI helpers — all use raw fd for both control and bulk */

static gboolean
sync_spi_write_reg (int fd, guint8 reg, guint8 val, GError **error)
{
  guint8 cmd[3] = { SPI_WRITE_CMD, reg, val };
  guint8 cc[4] = {0};

  if (!raw_ctrl_write (fd, CY_SPI_TRANSFER, SPI_DIR_WRITE, 3, error))
    return FALSE;
  if (!raw_bulk_out (fd, cmd, 3, error))
    return FALSE;
  if (raw_ctrl_read (fd, CY_READY_CHECK, 0, cc, 4, error) < 0)
    return FALSE;
  return TRUE;
}

static guint8
sync_spi_read_reg (int fd, guint8 reg, GError **error)
{
  guint8 cmd[3] = { SPI_READ_CMD, reg, 0x00 };
  guint8 buf[64];  /* Must be >= max packet size (64B for Full Speed USB) */

  if (!raw_ctrl_write (fd, CY_SPI_TRANSFER, SPI_DIR_READ, 3, error))
    return 0;
  if (!raw_bulk_out (fd, cmd, 3, error))
    return 0;
  int n = raw_bulk_in (fd, buf, 64, TIMEOUT_CMD, error);
  if (n < 0)
    return 0;
  return (n >= 3) ? buf[2] : 0;
}

static gboolean
sync_spi_write_reg2 (int fd, guint8 reg, guint16 val, GError **error)
{
  guint8 cmd[4] = { SPI_WRITE_CMD, reg, (guint8)(val >> 8), (guint8)(val & 0xFF) };
  guint8 cc[4] = {0};

  if (!raw_ctrl_write (fd, CY_SPI_TRANSFER, SPI_DIR_WRITE, 4, error))
    return FALSE;
  if (!raw_bulk_out (fd, cmd, 4, error))
    return FALSE;
  if (raw_ctrl_read (fd, CY_READY_CHECK, 0, cc, 4, error) < 0)
    return FALSE;
  return TRUE;
}

static gboolean
sync_spi_read_reg_full (int fd, guint8 reg,
                        guint8 *out, gsize *out_len, GError **error)
{
  guint8 cmd[3] = { SPI_READ_CMD, reg, 0x00 };
  guint8 buf[64];  /* Must be >= max packet size (64B for Full Speed USB) */

  if (!raw_ctrl_write (fd, CY_SPI_TRANSFER, SPI_DIR_READ, 3, error))
    return FALSE;
  if (!raw_bulk_out (fd, cmd, 3, error))
    return FALSE;
  int n = raw_bulk_in (fd, buf, 64, TIMEOUT_CMD, error);
  if (n < 0)
    return FALSE;
  if (out && out_len)
    {
      memcpy (out, buf, MIN ((gsize) n, *out_len));
      *out_len = (gsize) n;
    }
  return TRUE;
}

/* Quick re-init: power cycle + SPI enable + HW ID check only.
 * Skips the 17 register writes — sensor IC retains them across USB reset.
 * Used for reinit after capture (not first-time cold init). */
static gboolean
sync_init_quick (int fd, GError **error)
{
  guint8 cc[4];
  guint8 spi_en[2] = { 0x4F, 0x80 };

  /* Power cycle: ON → OFF → ON */
  if (!raw_ctrl_write (fd, CY_POWER, 0x0006, 1, error))
    return FALSE;
  if (!raw_ctrl_write (fd, CY_POWER, 0x0006, 0, error))
    return FALSE;
  if (!raw_ctrl_write (fd, CY_POWER, 0x0006, 1, error))
    return FALSE;
  g_usleep (100000);  /* 100ms after power cycle */

  /* SPI enable */
  if (!raw_ctrl_write (fd, CY_SPI_TRANSFER, SPI_DIR_WRITE, 2, error))
    return FALSE;
  if (!raw_bulk_out (fd, spi_en, 2, error))
    return FALSE;
  if (raw_ctrl_read (fd, CY_READY_CHECK, 0, cc, 4, error) < 0)
    return FALSE;

  /* SPI register enable */
  if (!sync_spi_write_reg (fd, 0x4F, 0x80, error))
    return FALSE;
  g_usleep (100000);  /* 100ms after SPI enable */

  /* Verify HW ID */
  guint8 hw_id = 0;
  for (int attempt = 0; attempt < 3; attempt++)
    {
      hw_id = sync_spi_read_reg (fd, 0xB9, error);
      if (*error)
        return FALSE;
      if (hw_id == 0x34 || hw_id == 0x68)
        break;
      g_usleep (50000);
    }
  if (hw_id != 0x34 && hw_id != 0x68)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Quick init: bad HW ID 0x%02X", hw_id);
      return FALSE;
    }

  fp_dbg ("Quick init OK (HW ID=0x%02X)", hw_id);
  return TRUE;
}

/* Full init: power on, SPI enable, read HW ID, write 17 config registers.
 * All I/O via raw USB fd. Returns TRUE on success. */
static gboolean
sync_init_early (int fd, GError **error)
{
  guint8 cc[4];
  guint8 spi_en[2] = { 0x4F, 0x80 };

  /* Power cycle: ON → OFF → ON */
  fp_dbg ("INIT: power cycle");
  if (!raw_ctrl_write (fd, CY_POWER, 0x0006, 1, error))
    return FALSE;
  if (!raw_ctrl_write (fd, CY_POWER, 0x0006, 0, error))
    return FALSE;
  if (!raw_ctrl_write (fd, CY_POWER, 0x0006, 1, error))
    return FALSE;
  g_usleep (100000);  /* 100ms — MCU needs time after power cycle */

  /* SPI enable (raw 2-byte command: 0x4F 0x80) */
  fp_dbg ("INIT: SPI enable");
  if (!raw_ctrl_write (fd, CY_SPI_TRANSFER, SPI_DIR_WRITE, 2, error))
    return FALSE;
  if (!raw_bulk_out (fd, spi_en, 2, error))
    return FALSE;
  if (raw_ctrl_read (fd, CY_READY_CHECK, 0, cc, 4, error) < 0)
    return FALSE;

  /* SPI register enable */
  fp_dbg ("INIT: register enable");
  if (!sync_spi_write_reg (fd, 0x4F, 0x80, error))
    return FALSE;
  g_usleep (100000);  /* 100ms — sensor needs time after SPI enable */

  /* Read Hardware ID (retry up to 5 times) */
  guint8 hw_id = 0;
  for (int attempt = 0; attempt < 5; attempt++)
    {
      hw_id = sync_spi_read_reg (fd, 0xB9, error);
      if (*error)
        return FALSE;
      if (hw_id == 0x34 || hw_id == 0x68)
        break;
      g_usleep (100000);
    }

  fp_dbg ("Hardware ID: 0x%02X", hw_id);
  if (hw_id != 0x34 && hw_id != 0x68)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Bad hardware ID 0x%02X (expected 0x34/0x68)", hw_id);
      return FALSE;
    }

  /* Write all 17 init registers */
  fp_dbg ("INIT: writing %d config registers", (int) G_N_ELEMENTS (init_registers));
  for (gsize i = 0; i < G_N_ELEMENTS (init_registers); i++)
    {
      const RegEntry *e = &init_registers[i];
      if (e->op == REG_WRITE2)
        {
          if (!sync_spi_write_reg2 (fd, e->reg, e->val, error))
            return FALSE;
        }
      else if (e->op == REG_WRITE1)
        {
          if (!sync_spi_write_reg (fd, e->reg,
                                    (guint8)(e->val & 0xFF), error))
            return FALSE;
        }
    }
  fp_dbg ("INIT: all registers written");

  guint8 verify = sync_spi_read_reg (fd, 0x60, error);
  if (*error)
    return FALSE;
  fp_dbg ("INIT: verify reg 0x60 = 0x%02X (expected 0x1B)", verify);

  return TRUE;
}

static void
init_run (FpiSsm *ssm, FpDevice *dev)
{
  switch (fpi_ssm_get_cur_state (ssm))
    {
    case INIT_WRITE_REGS:
      {
        /* All init done synchronously in sync_init_early now */
        fpi_ssm_mark_completed (ssm);
        break;
      }
    }
}

static void
init_done (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);
  FpImageDevice *img_dev = FP_IMAGE_DEVICE (dev);

  if (!error)
    {
      self->initialized = TRUE;
      fp_dbg ("Sensor initialized successfully");
    }

  fpi_image_device_open_complete (img_dev, error);
}

static void
dev_open (FpImageDevice *dev)
{
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);
  GUsbDevice *usb_dev = fpi_device_get_usb_device (FP_DEVICE (dev));
  GError *error = NULL;

  /* USB reset to ensure clean device state (previous session may have
   * left the Cypress MCU in a bad state from USB resets during capture) */
  {
    guint8 bus_num = g_usb_device_get_bus (usb_dev);
    guint8 dev_addr = g_usb_device_get_address (usb_dev);
    char reset_path[64];
    snprintf (reset_path, sizeof (reset_path), "/dev/bus/usb/%03u/%03u",
              bus_num, dev_addr);
    int reset_fd = open (reset_path, O_RDWR);
    if (reset_fd >= 0)
      {
        fp_dbg ("Resetting USB device at %s for clean state", reset_path);
        ioctl (reset_fd, USBDEVFS_RESET, NULL);
        close (reset_fd);
        g_usleep (200000);  /* Wait 200ms for re-enumeration */
      }
  }

  /* Claim interface via GUsb first (unbinds kernel driver) */
  if (!g_usb_device_claim_interface (usb_dev, 0,
                                     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
                                     &error))
    {
      fpi_image_device_open_complete (dev, error);
      return;
    }

  /* Release from GUsb (without rebinding kernel driver) and open raw fd.
   * We keep the raw fd for the entire session to avoid per-capture
   * release/claim dance that breaks second captures. */
  if (!g_usb_device_release_interface (usb_dev, 0, 0, &error))
    {
      fpi_image_device_open_complete (dev, error);
      return;
    }

  guint8 bus_num = g_usb_device_get_bus (usb_dev);
  guint8 dev_addr = g_usb_device_get_address (usb_dev);
  char usb_path[64];
  snprintf (usb_path, sizeof (usb_path), "/dev/bus/usb/%03u/%03u",
            bus_num, dev_addr);

  self->raw_usb_fd = open (usb_path, O_RDWR);
  if (self->raw_usb_fd < 0)
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Failed to open %s: %s", usb_path, g_strerror (errno));
      g_usb_device_claim_interface (usb_dev, 0, 0, NULL);
      fpi_image_device_open_complete (dev, error);
      return;
    }

  int iface = 0;
  if (ioctl (self->raw_usb_fd, USBDEVFS_CLAIMINTERFACE, &iface) < 0)
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Failed to claim interface on raw fd: %s", g_strerror (errno));
      close (self->raw_usb_fd);
      self->raw_usb_fd = -1;
      g_usb_device_claim_interface (usb_dev, 0, 0, NULL);
      fpi_image_device_open_complete (dev, error);
      return;
    }

  fp_dbg ("Raw USB fd=%d for %s", self->raw_usb_fd, usb_path);

  /* Synchronous init: power on, SPI enable, HW ID check */
  if (!sync_init_early (self->raw_usb_fd, &error))
    {
      int i = 0;
      ioctl (self->raw_usb_fd, USBDEVFS_RELEASEINTERFACE, &i);
      close (self->raw_usb_fd);
      self->raw_usb_fd = -1;
      g_usb_device_claim_interface (usb_dev, 0,
                                     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
                                     NULL);
      fpi_image_device_open_complete (dev, error);
      return;
    }

  /* Async SSM for register writes (currently a no-op, init done above) */
  FpiSsm *ssm = fpi_ssm_new (FP_DEVICE (dev), init_run, INIT_NUM_STATES);
  fpi_ssm_start (ssm, init_done);
}

static void
dev_close (FpImageDevice *dev)
{
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);
  GUsbDevice *usb_dev = fpi_device_get_usb_device (FP_DEVICE (dev));

  self->initialized = FALSE;

  /* Release raw fd and restore GUsb interface ownership */
  if (self->raw_usb_fd >= 0)
    {
      int iface = 0;
      ioctl (self->raw_usb_fd, USBDEVFS_RELEASEINTERFACE, &iface);
      close (self->raw_usb_fd);
      self->raw_usb_fd = -1;
    }

  /* Re-claim via GUsb so kernel driver gets rebound on close */
  g_usb_device_claim_interface (usb_dev, 0,
                                G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
                                NULL);
  g_usb_device_release_interface (usb_dev, 0,
                                  G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
                                  NULL);
  fpi_image_device_close_complete (dev, NULL);
}

/* ========================================================================
 * Activate / Deactivate / Change State
 * ======================================================================== */

static void detect_start (FpImageDevice *dev);
static void capture_start (FpImageDevice *dev);

static void
dev_activate (FpImageDevice *dev)
{
  fpi_image_device_activate_complete (dev, NULL);
}

static void
dev_deactivate (FpImageDevice *dev)
{
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);

  if (self->poll_source)
    {
      g_source_destroy (self->poll_source);
      g_source_unref (self->poll_source);
      self->poll_source = NULL;
    }

  fpi_image_device_deactivate_complete (dev, NULL);
}

static void
dev_change_state (FpImageDevice *dev, FpiImageDeviceState state)
{
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);

  switch (state)
    {
    case FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON:
      detect_start (dev);
      break;

    case FPI_IMAGE_DEVICE_STATE_CAPTURE:
      capture_start (dev);
      break;

    case FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_OFF:
      /* Press sensor — finger is already off after capture */
      fpi_image_device_report_finger_status (dev, FALSE);
      break;

    default:
      break;
    }

  (void) self;
}

/* Forward declaration — defined below in capture section */
static gboolean sync_readout_and_transfer (FpDevice *dev, GError **error);

/* ========================================================================
 * Finger Detection — Software-based
 *
 * The CanvasBio CB2000 has no hardware finger detection. GPIO status (0xDA)
 * is a scan-complete flag, not a finger-presence indicator — it always
 * returns [00 01] almost immediately after scan trigger.
 *
 * Flow:
 * 1. Trigger scan → DA poll (completes in ~10ms)
 * 2. Read image
 * 3. Check image variance: high variance = finger present
 * 4. If no finger → wait 500ms → power cycle → re-scan
 * 5. If finger → report finger_on with pre-captured image
 * ======================================================================== */

/* Minimum image variance to consider a finger present.
 * Empirically: blank sensor ~0-10, fingerprint ~200-2000+ */
#define FINGER_VARIANCE_THRESHOLD 100.0

/* Synchronous scan trigger — writes all scan_registers[] back-to-back */
static gboolean
sync_trigger_scan (int fd, GError **error)
{
  for (gsize i = 0; i < G_N_ELEMENTS (scan_registers); i++)
    {
      const RegEntry *e = &scan_registers[i];

      if (e->op == REG_WRITE2)
        {
          if (!sync_spi_write_reg2 (fd, e->reg, e->val, error))
            return FALSE;
        }
      else if (e->op == REG_WRITE1)
        {
          if (!sync_spi_write_reg (fd, e->reg,
                                    (guint8)(e->val & 0xFF), error))
            return FALSE;
        }
      else if (e->op == REG_READ || e->op == REG_READ_FULL)
        {
          guint8 out[3];
          gsize out_len = sizeof (out);
          if (!sync_spi_read_reg_full (fd, e->reg,
                                        out, &out_len, error))
            return FALSE;
        }
    }
  return TRUE;
}

/* Synchronous: re-init sensor after capture.
 * Two tiers:
 *   Tier 1: clear_halt + SPI test (~50ms if SPI still alive)
 *   Tier 2: USB reset + quick init (~300ms — skips 17 register writes)
 * Updates self->raw_usb_fd. Returns TRUE on success. */
static gboolean
sync_reinit_sensor (FpiDeviceCanvasbio *self, GError **error)
{
  int fd = self->raw_usb_fd;

  /* Tier 1: Just clear halted endpoints and test SPI */
  {
    unsigned int ep_out = CB_EP_OUT;
    unsigned int ep_in = CB_EP_IN;
    ioctl (fd, USBDEVFS_CLEAR_HALT, &ep_out);
    ioctl (fd, USBDEVFS_CLEAR_HALT, &ep_in);

    GError *test_err = NULL;
    guint8 test_val = sync_spi_read_reg (fd, 0xB9, &test_err);
    if (!test_err && (test_val == 0x68 || test_val == 0x34))
      {
        fp_dbg ("Reinit tier1: SPI alive (0x%02X)", test_val);
        return TRUE;
      }
    g_clear_error (&test_err);
  }

  /* Tier 2: USB reset + quick init (no 17 register writes) */
  fp_dbg ("Reinit tier2: USB reset");
  {
    GUsbDevice *usb = fpi_device_get_usb_device (FP_DEVICE (self));
    int iface = 0;
    ioctl (fd, USBDEVFS_RELEASEINTERFACE, &iface);
    close (fd);
    self->raw_usb_fd = -1;

    guint8 bus_num = g_usb_device_get_bus (usb);
    guint8 dev_addr = g_usb_device_get_address (usb);
    char usb_path[64];
    snprintf (usb_path, sizeof (usb_path), "/dev/bus/usb/%03u/%03u",
              bus_num, dev_addr);

    int reset_fd = open (usb_path, O_RDWR);
    if (reset_fd >= 0)
      {
        ioctl (reset_fd, USBDEVFS_RESET, NULL);
        close (reset_fd);
      }
    g_usleep (200000);  /* 200ms for re-enumeration */

    fd = open (usb_path, O_RDWR);
    if (fd < 0)
      {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "Failed to reopen %s: %s", usb_path, g_strerror (errno));
        return FALSE;
      }
    if (ioctl (fd, USBDEVFS_CLAIMINTERFACE, &iface) < 0)
      {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "Failed to re-claim: %s", g_strerror (errno));
        close (fd);
        return FALSE;
      }
    self->raw_usb_fd = fd;

    /* Quick init — sensor IC retains register config across USB reset */
    if (!sync_init_quick (fd, error))
      {
        /* Fall back to full init if quick fails */
        fp_dbg ("Quick init failed, trying full init");
        g_clear_error (error);
        if (!sync_init_early (fd, error))
          return FALSE;
      }

    fp_dbg ("Reinit tier2 done (fd=%d)", fd);
    return TRUE;
  }
}

/* Synchronous: full capture cycle (scan → DA → readout → transfer).
 * Returns TRUE if image captured, FALSE on error. */
static gboolean
sync_full_capture (FpDevice *dev, GError **error)
{
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);
  int fd = self->raw_usb_fd;

  /* Trigger scan */
  if (!sync_trigger_scan (fd, error))
    return FALSE;

  /* DA poll — scan completes quickly (~10ms), poll up to 1 second */
  for (int i = 0; i < 100; i++)
    {
      guint8 da[2] = {0, 0};
      if (raw_ctrl_read (fd, CY_GPIO_STATUS, 0x0007, da, 2, error) < 0)
        return FALSE;
      if (da[1] == 0x01)
        break;
      g_usleep (10000);
    }

  /* Readout + image transfer */
  if (!sync_readout_and_transfer (dev, error))
    return FALSE;

  return TRUE;
}

/* Check if captured image has a finger (software detection via variance) */
static gboolean
image_has_finger (const guint8 *buf, gsize len)
{
  if (len < 100)
    return FALSE;

  gdouble sum = 0, sum_sq = 0;
  for (gsize i = 0; i < len; i++)
    {
      sum += buf[i];
      sum_sq += (gdouble) buf[i] * buf[i];
    }
  gdouble mean = sum / len;
  gdouble variance = (sum_sq / len) - (mean * mean);

  fp_dbg ("Image stats: mean=%.1f, variance=%.1f (threshold=%.1f)",
          mean, variance, FINGER_VARIANCE_THRESHOLD);

  return (variance > FINGER_VARIANCE_THRESHOLD);
}

/* Thread function: single capture + check.
 * Returns TRUE if finger detected, FALSE (with no error) if no finger.
 * Caller reschedules if no finger. */
static void
detect_thread_func (GTask *task, gpointer source_object,
                    gpointer task_data, GCancellable *cancellable)
{
  FpDevice *dev = FP_DEVICE (source_object);
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);
  GError *error = NULL;

  (void) task_data;

  if (g_cancellable_is_cancelled (cancellable))
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled");
      g_task_return_error (task, error);
      return;
    }

  if (!sync_full_capture (dev, &error))
    {
      fp_warn ("Detect capture failed: %s", error->message);
      g_clear_error (&error);
      /* Try re-init for next attempt */
      if (!sync_reinit_sensor (self, &error))
        {
          g_task_return_error (task, error);
          return;
        }
      /* Return FALSE (no finger) — will be retried */
      g_task_return_boolean (task, FALSE);
      return;
    }

  if (image_has_finger (self->img_buf, self->img_offset))
    {
      fp_dbg ("Finger detected! Reinitializing for next capture...");
      /* Reinit now so SPI is ready for the next detect cycle.
       * After image transfer the Cypress MCU SPI bus is corrupted. */
      GError *reinit_err = NULL;
      if (!sync_reinit_sensor (self, &reinit_err))
        fp_warn ("Post-capture reinit failed: %s (non-fatal)", reinit_err->message);
      g_clear_error (&reinit_err);
      g_task_return_boolean (task, TRUE);
      return;
    }

  /* No finger — reinit SPI after bulk transfer before next attempt */
  fp_dbg ("No finger detected, reinitializing...");
  GError *reinit_err = NULL;
  if (!sync_reinit_sensor (self, &reinit_err))
    fp_warn ("No-finger reinit failed: %s (non-fatal)", reinit_err->message);
  g_clear_error (&reinit_err);
  g_task_return_boolean (task, FALSE);
}

/* Forward declarations */
static gboolean detect_retry_cb (gpointer user_data);

/* Completion callback: runs on main loop after detect thread finishes */
static void
detect_thread_done (GObject *source_object, GAsyncResult *res,
                    gpointer user_data)
{
  FpDevice *dev = FP_DEVICE (source_object);
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);
  FpImageDevice *img_dev = FP_IMAGE_DEVICE (dev);
  GError *error = NULL;
  gboolean finger_found;

  (void) user_data;

  finger_found = g_task_propagate_boolean (G_TASK (res), &error);

  if (error)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_error_free (error);
          return;
        }
      fp_warn ("Detect failed: %s", error->message);
      fpi_image_device_session_error (img_dev, error);
      return;
    }

  if (finger_found)
    {
      self->image_ready = TRUE;
      fp_dbg ("Finger detected, image ready (%zd bytes)", self->img_offset);
      fpi_image_device_report_finger_status (img_dev, TRUE);
      return;
    }

  /* No finger — schedule retry after 100ms */
  g_timeout_add (100, detect_retry_cb, dev);
}

static gboolean
detect_retry_cb (gpointer user_data)
{
  FpImageDevice *dev = FP_IMAGE_DEVICE (user_data);
  detect_start (dev);
  return G_SOURCE_REMOVE;
}

static void
detect_start (FpImageDevice *dev)
{
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);

  self->image_ready = FALSE;

  GCancellable *cancellable = NULL;
  if (fpi_device_get_cancellable (FP_DEVICE (dev)))
    cancellable = fpi_device_get_cancellable (FP_DEVICE (dev));

  GTask *task = g_task_new (FP_DEVICE (dev), cancellable,
                            detect_thread_done, NULL);
  g_task_run_in_thread (task, detect_thread_func);
  g_object_unref (task);
}

/* ========================================================================
 * Image Capture SSM (4 phases)
 * ======================================================================== */

enum {
  CAP_DO_EVERYTHING,
  CAP_NUM_STATES,
};

/* Synchronous readout config + image transfer.
 * Must be done without async gaps — Cypress MCU is timing-sensitive.
 * Uses the persistent raw_usb_fd for all bulk transfers. */
static gboolean
sync_readout_and_transfer (FpDevice *dev, GError **error)
{
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);
  int fd = self->raw_usb_fd;

  /* Readout config — matching Rust driver set_mode_readout() exactly */
  fp_dbg ("Readout config start");

  sync_spi_read_reg (fd, 0x08, error);
  if (*error) return FALSE;

  if (!sync_spi_write_reg2 (fd, 0x09, 0x0000, error)) return FALSE;

  { guint8 out[3]; gsize out_len = sizeof (out);
    if (!sync_spi_read_reg_full (fd, 0x3E, out, &out_len, error)) return FALSE;
  }

  if (!sync_spi_write_reg2 (fd, 0x03, 0x0000, error)) return FALSE;

  { guint8 out[3]; gsize out_len = sizeof (out);
    if (!sync_spi_read_reg_full (fd, 0x20, out, &out_len, error)) return FALSE;
  }

  if (!sync_spi_write_reg2 (fd, 0x10, 0x0001, error)) return FALSE;
  if (!sync_spi_write_reg2 (fd, 0x2F, 0xEF00, error)) return FALSE;
  if (!sync_spi_write_reg2 (fd, 0x09, 0x0000, error)) return FALSE;
  if (!sync_spi_write_reg2 (fd, 0x5D, 0x4D00, error)) return FALSE;
  if (!sync_spi_write_reg2 (fd, 0x51, 0x3A25, error)) return FALSE;
  if (!sync_spi_write_reg  (fd, 0x0C, 0x00, error)) return FALSE;

  { guint8 out[3]; gsize out_len = sizeof (out);
    if (!sync_spi_read_reg_full (fd, 0x20, out, &out_len, error)) return FALSE;
  }

  if (!sync_spi_write_reg2 (fd, 0x04, 0x0000, error)) return FALSE;
  if (!sync_spi_write_reg2 (fd, 0x09, 0x0000, error)) return FALSE;
  fp_dbg ("Readout config done");

  /* SPI prepare: read 10755 bytes (3B cmd + 10752B image) */
  guint16 total = IMG_SIZE + 3;
  if (!raw_ctrl_write (fd, CY_SPI_TRANSFER, SPI_DIR_READ, total, error))
    return FALSE;

  /* Block 0: [A8 06 00] + 256 zeros = 259 bytes OUT, then read 256B IN */
  guint8 block0[259] = { SPI_READ_CMD, 0x06, 0x00 };
  memset (block0 + 3, 0, 256);

  if (!raw_bulk_out (fd, block0, 259, error))
    return FALSE;

  guint8 buf[256];
  int ret = raw_bulk_in (fd, buf, 256, TIMEOUT_IMG, error);
  if (ret < 0)
    return FALSE;

  gsize n = MIN ((gsize) ret, (gsize) IMG_SIZE);
  memcpy (self->img_buf, buf, n);
  self->img_offset = n;
  fp_dbg ("Block 0: %d bytes", ret);

  /* Blocks 1-41: 256 zeros OUT, read 256B IN */
  guint8 zeros[256] = { 0 };
  for (gint block = 1; block <= 41; block++)
    {
      if (!raw_bulk_out (fd, zeros, 256, error))
        return FALSE;

      ret = raw_bulk_in (fd, buf, 256, TIMEOUT_IMG, error);
      if (ret < 0)
        return FALSE;

      gsize count = (gsize) ret;
      gsize remaining = IMG_SIZE - self->img_offset;
      gsize copy = MIN (count, remaining);
      if (copy > 0)
        {
          memcpy (self->img_buf + self->img_offset, buf, copy);
          self->img_offset += copy;
        }

      fp_dbg ("Block %d: %d bytes (total: %zd)", block, ret, self->img_offset);
    }

  fp_dbg ("Image transfer complete: %zd bytes", self->img_offset);
  return TRUE;
}

static void
cap_run (FpiSsm *ssm, FpDevice *dev)
{
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case CAP_DO_EVERYTHING:
      {
        FpImageDevice *img_dev = FP_IMAGE_DEVICE (dev);

        if (!self->image_ready)
          {
            /* Image should have been captured in detect phase.
             * If not, something went wrong — retry detect. */
            fp_warn ("No pre-captured image available!");
            GError *err = g_error_new_literal (G_IO_ERROR,
                                                G_IO_ERROR_FAILED,
                                                "No image captured");
            fpi_ssm_mark_failed (ssm, err);
            break;
          }

        /* Process and submit the pre-captured image */
        gsize copy_len = MIN (self->img_offset, (gsize) IMG_SIZE);

        fp_dbg ("Submitting pre-captured image: %zd bytes", copy_len);

        /* CLAHE enhancement on raw image */
        cb_clahe (self->img_buf, IMG_WIDTH, IMG_HEIGHT, 8, 8, 2.5);

#ifdef CB_DEBUG_IMAGES
        {
          static int dump_count = 0;
          char path[256];
          snprintf (path, sizeof (path), "/tmp/canvasbio_%03d.pgm", dump_count++);
          FILE *f = fopen (path, "wb");
          if (f)
            {
              fprintf (f, "P5\n%d %d\n255\n", IMG_WIDTH, IMG_HEIGHT);
              fwrite (self->img_buf, 1, copy_len, f);
              fclose (f);
              fp_dbg ("Dumped image to %s", path);
            }
        }
#endif

        /* 3x bicubic upscale: 112x96 → 336x288 */
        #define UPSCALE 3
        int dw = IMG_WIDTH * UPSCALE;
        int dh = IMG_HEIGHT * UPSCALE;

        FpImage *img = fp_image_new (dw, dh);
        img->ppmm = 19.685;
        img->flags = FPI_IMAGE_COLORS_INVERTED;

        cb_bicubic_upscale (self->img_buf, IMG_WIDTH, IMG_HEIGHT,
                            img->data, UPSCALE);

        fp_dbg ("Submitting image: %dx%d, ppmm=%.2f, flags=0x%x",
                img->width, img->height, img->ppmm, img->flags);

        self->image_ready = FALSE;
        fpi_image_device_image_captured (img_dev, img);
        fpi_image_device_report_finger_status (img_dev, FALSE);
        break;
      }
    }
}

static void
cap_done (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  if (error)
    {
      FpImageDevice *img_dev = FP_IMAGE_DEVICE (dev);
      fpi_image_device_session_error (img_dev, error);
    }
}

static void
capture_start (FpImageDevice *dev)
{
  /* Image was already captured in detect phase — just submit it */
  FpiSsm *ssm = fpi_ssm_new (FP_DEVICE (dev), cap_run, CAP_NUM_STATES);
  fpi_ssm_start (ssm, cap_done);
}

/* ========================================================================
 * GObject / Class Init / TOD Entry Point
 * ======================================================================== */

static void
fpi_device_canvasbio_init (FpiDeviceCanvasbio *self)
{
  self->initialized = FALSE;
  self->poll_source = NULL;
  self->raw_usb_fd = -1;
}

static void
fpi_device_canvasbio_class_init (FpiDeviceCanvasbioClass *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);
  FpImageDeviceClass *img_class = FP_IMAGE_DEVICE_CLASS (klass);

  dev_class->id = "canvasbio";
  dev_class->full_name = "CanvasBio CB2000 Fingerprint Sensor";
  dev_class->type = FP_DEVICE_TYPE_USB;
  dev_class->id_table = id_table;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;
  dev_class->nr_enroll_stages = 5;

  img_class->img_width = IMG_WIDTH;
  img_class->img_height = IMG_HEIGHT;
  img_class->bz3_threshold = 6;

  img_class->img_open = dev_open;
  img_class->img_close = dev_close;
  img_class->activate = dev_activate;
  img_class->deactivate = dev_deactivate;
  img_class->change_state = dev_change_state;
}

/* TOD entry point — libfprint loads this symbol from the .so */
GType
fpi_tod_shared_driver_get_type (void)
{
  return fpi_device_canvasbio_get_type ();
}
