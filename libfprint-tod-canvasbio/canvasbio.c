#define FP_COMPONENT "canvasbio"

#include <stdio.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
#include <drivers_api.h>
#include "canvasbio.h"
#include "cb-match.h"

G_DECLARE_FINAL_TYPE (FpiDeviceCanvasbio, fpi_device_canvasbio, FPI, DEVICE_CANVASBIO, FpDevice)
G_DEFINE_TYPE (FpiDeviceCanvasbio, fpi_device_canvasbio, FP_TYPE_DEVICE)

static const FpIdEntry id_table[] = {
  { .vid = 0x04E8, .pid = 0x730B },
  { .vid = 0, .pid = 0 },
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
dev_open (FpDevice *dev)
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
      fpi_device_open_complete (dev, error);
      return;
    }

  /* Release from GUsb (without rebinding kernel driver) and open raw fd.
   * We keep the raw fd for the entire session to avoid per-capture
   * release/claim dance that breaks second captures. */
  if (!g_usb_device_release_interface (usb_dev, 0, 0, &error))
    {
      fpi_device_open_complete (dev, error);
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
      fpi_device_open_complete (dev, error);
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
      fpi_device_open_complete (dev, error);
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
      fpi_device_open_complete (dev, error);
      return;
    }

  self->initialized = TRUE;
  fpi_device_open_complete (dev, NULL);
}

static void
dev_close (FpDevice *dev)
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
  fpi_device_close_complete (dev, NULL);
}

/* Forward declaration — defined below in capture section */
static gboolean sync_readout_and_transfer (int fd, guint8 *img, GError **error);

/* ========================================================================
 * Finger Detection — Hardware SPI register 0x20 polling
 *
 * Uses the same approach as the Windows driver:
 * 1. Enter detect mode (write detect_enter_registers)
 * 2. Poll SPI reg 0x20 every 15ms — status 0x0107 = finger detected
 * 3. Every 18 cycles, clear_halt on bulk endpoints to prevent MCU stall
 * 4. When finger detected → trigger capture + read image
 *
 * This avoids doing a full image capture just to check finger presence.
 * ======================================================================== */

/* Max poll cycles before giving up (18 cycles × batches) */
#define DETECT_CYCLES_PER_BATCH  18   /* clear_halt every 18 cycles (~270ms) */
#define DETECT_MAX_BATCHES       200  /* ~54 seconds total timeout */
#define DETECT_POLL_INTERVAL_US  15000  /* 15ms between polls (matches Windows) */
#define DETECT_FAST_PROBE_TIMEOUT_MS_DEFAULT 180
#define DETECT_FAST_PROBE_TIMEOUT_MS_MIN 80
#define DETECT_FAST_PROBE_TIMEOUT_MS_MAX 800
#define DETECT_RETRY_DELAY_MS_DEFAULT 35
#define DETECT_RETRY_DELAY_MS_MIN 10
#define DETECT_RETRY_DELAY_MS_MAX 250

static guint
cb_env_uint (const char *key, guint fallback, guint min, guint max)
{
  const gchar *val = g_getenv (key);
  if (!val || !*val)
    return fallback;

  gchar *end = NULL;
  guint64 parsed = g_ascii_strtoull (val, &end, 10);
  if (end == val || *end != '\0')
    {
      fp_warn ("Invalid %s='%s'; using default %u", key, val, fallback);
      return fallback;
    }

  if (parsed < min || parsed > max)
    {
      fp_warn ("Out-of-range %s='%s' (valid %u-%u); using default %u",
               key, val, min, max, fallback);
      return fallback;
    }

  return (guint) parsed;
}

static guint
cb_detect_fast_probe_timeout_ms (void)
{
  static gsize inited = 0;
  static guint cached = DETECT_FAST_PROBE_TIMEOUT_MS_DEFAULT;

  if (g_once_init_enter (&inited))
    {
      cached = cb_env_uint ("CANVASBIO_DETECT_PROBE_MS",
                            DETECT_FAST_PROBE_TIMEOUT_MS_DEFAULT,
                            DETECT_FAST_PROBE_TIMEOUT_MS_MIN,
                            DETECT_FAST_PROBE_TIMEOUT_MS_MAX);
      g_once_init_leave (&inited, 1);
    }

  return cached;
}

static guint
cb_detect_retry_delay_ms (void)
{
  static gsize inited = 0;
  static guint cached = DETECT_RETRY_DELAY_MS_DEFAULT;

  if (g_once_init_enter (&inited))
    {
      cached = cb_env_uint ("CANVASBIO_DETECT_RETRY_MS",
                            DETECT_RETRY_DELAY_MS_DEFAULT,
                            DETECT_RETRY_DELAY_MS_MIN,
                            DETECT_RETRY_DELAY_MS_MAX);
      g_once_init_leave (&inited, 1);
    }

  return cached;
}

/* Execute a register table synchronously */
static gboolean
sync_exec_reg_table (int fd, const RegEntry *table, gsize len,
                     guint8 *last_read_out, gsize *last_read_len,
                     GError **error)
{
  for (gsize i = 0; i < len; i++)
    {
      const RegEntry *e = &table[i];

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
          guint8 out[4];
          gsize out_len = sizeof (out);
          if (!sync_spi_read_reg_full (fd, e->reg,
                                        out, &out_len, error))
            return FALSE;
          /* Store last read result for caller to inspect */
          if (last_read_out && last_read_len)
            {
              memcpy (last_read_out, out, MIN (out_len, *last_read_len));
              *last_read_len = out_len;
            }
        }
    }
  return TRUE;
}

/* Synchronous scan trigger — writes all scan_registers[] back-to-back */
static gboolean
sync_trigger_scan (int fd, GError **error)
{
  return sync_exec_reg_table (fd, scan_registers,
                               G_N_ELEMENTS (scan_registers),
                               NULL, NULL, error);
}

/* Fast finger probe: trigger scan and wait briefly for DA completion.
 * Returns:
 *   TRUE  -> scan completed quickly (finger likely present)
 *   FALSE -> no quick completion (likely no finger)
 * On transport error, returns FALSE and sets *error. */
static gboolean
sync_probe_scan_ready (int fd, guint timeout_ms, gboolean *ready, GError **error)
{
  if (!sync_trigger_scan (fd, error))
    return FALSE;

  guint polls = (timeout_ms / 10u);
  if (polls == 0)
    polls = 1;
  for (guint i = 0; i < polls; i++)
    {
      guint8 da[2] = { 0, 0 };
      if (raw_ctrl_read (fd, CY_GPIO_STATUS, 0x0007, da, 2, error) < 0)
        return FALSE;
      if (da[1] == 0x01)
        {
          *ready = TRUE;
          return TRUE;
        }
      g_usleep (10000); /* 10ms */
    }

  *ready = FALSE;
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
    g_usleep (500000);  /* 500ms for re-enumeration */

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
sync_full_capture (int fd, guint8 *img, GError **error)
{
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
  if (!sync_readout_and_transfer (fd, img, error))
    return FALSE;

  return TRUE;
}

/* Synchronous readout config + image transfer.
 * Must be done without async gaps — Cypress MCU is timing-sensitive.
 * Uses the persistent raw_usb_fd for all bulk transfers. */
static gboolean
sync_readout_and_transfer (int fd, guint8 *img, GError **error)
{
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

  /* Drop anything still queued on bulk IN from an earlier, interrupted
   * transfer; it would otherwise be read as the start of this image. */
  {
    guint8 junk[64];
    int drained = 0;
    for (int i = 0; i < 64; i++)
      {
        GError *derr = NULL;
        int r = raw_bulk_in (fd, junk, sizeof (junk), 10, &derr);
        g_clear_error (&derr);
        if (r <= 0)
          break;
        drained += r;
      }
    if (drained)
      fp_dbg ("Drained %d stale bytes before image read", drained);
  }

  /* SPI prepare: read 10755 bytes (3B cmd + 10752B image) */
  guint16 total = IMG_SIZE + 3;
  if (!raw_ctrl_write (fd, CY_SPI_TRANSFER, SPI_DIR_READ, total, error))
    return FALSE;

  /* The MCU returns one IN byte per OUT byte clocked over SPI: 3 bytes that
   * echo the read command, then the 10752 pixels. Block 0 is 259 bytes; its
   * last 3 bytes only arrive (as a short packet) after the next block is
   * clocked, so a 256-byte read can return just those 3 and every later
   * read lags one packet behind, leaving the image tail unread. The old
   * code copied reads into fixed slots, which shifted rows sideways and
   * dropped the tail. Waiting for a block's full count doesn't work either
   * (the MCU holds the tail back, so the read times out). So: keep the
   * per-block 256-byte reads the MCU expects, append whatever arrives in
   * order, then collect the lagging tail with short reads. */
  guint8 stream[3 + IMG_SIZE];
  gsize got = 0;
  guint8 out[259] = { SPI_READ_CMD, 0x06, 0x00 };  /* + 256 zeros */
  guint8 buf[256];

  for (gint block = 0; block < 42; block++)
    {
      if (!raw_bulk_out (fd, block == 0 ? out : out + 3,
                         block == 0 ? 259 : 256, error))
        return FALSE;

      int r = raw_bulk_in (fd, buf, sizeof (buf), TIMEOUT_IMG, error);
      if (r < 0)
        return FALSE;
      gsize take = MIN ((gsize) r, sizeof (stream) - got);
      memcpy (stream + got, buf, take);
      got += take;
    }

  for (int tries = 0; got < sizeof (stream) && tries < 8; tries++)
    {
      GError *rerr = NULL;
      int r = raw_bulk_in (fd, buf, sizeof (buf), 100, &rerr);
      if (r <= 0)
        {
          g_clear_error (&rerr);
          break;
        }
      gsize take = MIN ((gsize) r, sizeof (stream) - got);
      memcpy (stream + got, buf, take);
      got += take;
    }

  /* A handful of missing tail bytes is tolerable (padded below); a frame
   * missing rows is not, since those rows would be fabricated. */
  if (got < 3 + IMG_SIZE - IMG_WIDTH / 2)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Short image transfer: %zu of %d bytes", got, 3 + IMG_SIZE);
      return FALSE;
    }
  if (got < sizeof (stream))
    {
      fp_dbg ("Image %zu bytes short, padding tail",
              sizeof (stream) - got);
      memset (stream + got, stream[got - 1], sizeof (stream) - got);
    }

  /* SPI reads come back shifted left one bit: each received byte holds the
   * low 7 bits of the true byte plus the top bit of the next one (register
   * reads show it too: HW ID 0x34 reads as 0x68, reg 0x60 = 0x1B as 0x36).
   * Untreated, pixels >= 128 wrap and contrast doubles, which drew false
   * edges inside every ridge. The lost top bit is the previous byte's LSB,
   * so the shift is undone exactly. */
  for (int i = 0; i < IMG_SIZE; i++)
    img[i] = (stream[3 + i] >> 1) | ((stream[2 + i] & 1) << 7);
  fp_dbg ("Image transfer complete: %zu bytes", got);
  return TRUE;
}

/* ========================================================================
 * Touch capture worker
 *
 * Runs on a GTask thread: optionally waits for the finger to lift, then for
 * a touch; captures it and extracts match features. Sensor I/O is the same
 * synchronous raw-usbdevfs path the rest of the driver uses.
 * ======================================================================== */

#define ENROLL_STAGES            12
#define MATCH_THRESHOLD_DEFAULT  12   /* RANSAC inliers; impostors scored <= 6 */
#define FINGER_MIN_STD           25.0 /* frame contrast below this = no finger */
#define MAX_POLL_ERRORS          10

typedef struct {
  gboolean  wait_lift;
  GBytes   *features;   /* out; NULL when the touch had too little detail */
} TouchTask;

static void
touch_task_free (gpointer data)
{
  TouchTask *t = data;
  g_clear_pointer (&t->features, g_bytes_unref);
  g_free (t);
}

static gdouble
img_std (const guint8 *img)
{
  gdouble s = 0, s2 = 0;
  for (int i = 0; i < IMG_SIZE; i++)
    {
      s += img[i];
      s2 += (gdouble) img[i] * img[i];
    }
  gdouble mean = s / IMG_SIZE;
  return sqrt (MAX (0.0, s2 / IMG_SIZE - mean * mean));
}

/* Opt-in raw dump for building test sets (tools/cb-eval, cb-tool). */
static void
dump_raw (const guint8 *img)
{
  const gchar *dir = g_getenv ("CANVASBIO_DUMP_DIR");
  static int count = 0;

  if (!dir || !*dir)
    return;
  g_autofree gchar *path = g_strdup_printf ("%s/raw_%04d.pgm", dir, count++);
  FILE *f = fopen (path, "wb");
  if (!f)
    return;
  fprintf (f, "P5\n%d %d\n255\n", IMG_WIDTH, IMG_HEIGHT);
  fwrite (img, 1, IMG_SIZE, f);
  fclose (f);
}

/* One detection cycle: quick scan-complete probe, then a full frame if the
 * probe fired. *captured says a frame was read; *present is TRUE only for a
 * frame with ridge contrast. */
static gboolean
touch_poll (FpiDeviceCanvasbio *self, gboolean *captured, gboolean *present,
            GError **error)
{
  gboolean ready = FALSE;

  *captured = FALSE;
  *present = FALSE;
  if (!sync_probe_scan_ready (self->raw_usb_fd, cb_detect_fast_probe_timeout_ms (),
                              &ready, error))
    return FALSE;
  if (!ready)
    return TRUE;

  gboolean ok = sync_full_capture (self->raw_usb_fd, self->img_buf, error);
  GError *re = NULL;
  if (!sync_reinit_sensor (self, &re))
    {
      if (ok)
        {
          g_propagate_error (error, re);
          return FALSE;
        }
      g_clear_error (&re);
    }
  if (!ok)
    return FALSE;

  *captured = TRUE;
  *present = img_std (self->img_buf) > FINGER_MIN_STD;
  return TRUE;
}

static void
touch_thread (GTask *task, gpointer source_object, gpointer task_data,
              GCancellable *cancellable)
{
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (source_object);
  TouchTask *t = task_data;
  gboolean want_present = !t->wait_lift;
  int absent = 0, errors = 0;

  for (;;)
    {
      GError *err = NULL;
      gboolean captured, present;

      if (g_cancellable_is_cancelled (cancellable))
        {
          g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                   "Cancelled");
          return;
        }

      if (!touch_poll (self, &captured, &present, &err))
        {
          fp_warn ("Sensor poll failed: %s", err->message);
          g_clear_error (&err);
          /* Only a successful capture resets this, so a sensor that keeps
           * failing reports an error instead of polling forever. */
          if (++errors >= MAX_POLL_ERRORS)
            {
              g_task_return_error (task, fpi_device_error_new_msg (
                                     FP_DEVICE_ERROR_GENERAL,
                                     "Sensor stopped responding"));
              return;
            }
          if (!sync_reinit_sensor (self, &err))
            g_clear_error (&err);
          continue;
        }
      if (captured)
        errors = 0;

      if (!want_present)
        {
          /* Two clear polls in a row before accepting the next touch. */
          absent = present ? 0 : absent + 1;
          if (absent >= 2)
            want_present = TRUE;
          else
            g_usleep (cb_detect_retry_delay_ms () * 1000);
          continue;
        }
      if (present)
        break;
      g_usleep (cb_detect_retry_delay_ms () * 1000);
    }

  dump_raw (self->img_buf);

  int n_kp = 0;
  t->features = cb_match_extract (self->img_buf, IMG_WIDTH, IMG_HEIGHT, &n_kp);
  fp_dbg ("Touch captured: %d keypoints%s", n_kp,
          t->features ? "" : " (too few, will retry)");
  g_task_return_boolean (task, TRUE);
}

static void touch_done (GObject *source_object, GAsyncResult *res,
                        gpointer user_data);

static void
touch_start (FpiDeviceCanvasbio *self, gboolean wait_lift)
{
  FpDevice *dev = FP_DEVICE (self);
  TouchTask *t = g_new0 (TouchTask, 1);

  t->wait_lift = wait_lift;
  fpi_device_report_finger_status (dev, FP_FINGER_STATUS_NEEDED);

  GTask *task = g_task_new (dev, fpi_device_get_cancellable (dev), touch_done, NULL);
  g_task_set_task_data (task, t, touch_task_free);
  g_task_run_in_thread (task, touch_thread);
  g_object_unref (task);
}

/* ========================================================================
 * Templates: enrolled prints hold the SIFT feature blobs of every enroll
 * touch (no images), as GVariant "(qaay)" = (format version, blobs).
 * ======================================================================== */

#define TEMPLATE_VERSION 1

static guint
match_threshold (void)
{
  static gsize inited = 0;
  static guint cached = MATCH_THRESHOLD_DEFAULT;

  if (g_once_init_enter (&inited))
    {
      cached = cb_env_uint ("CANVASBIO_MATCH_THRESHOLD",
                            MATCH_THRESHOLD_DEFAULT, 4, 200);
      g_once_init_leave (&inited, 1);
    }
  return cached;
}

static GVariant *
template_to_variant (GPtrArray *feats)
{
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("aay"));
  for (guint i = 0; i < feats->len; i++)
    g_variant_builder_add_value (&b, g_variant_new_from_bytes (
                                   G_VARIANT_TYPE_BYTESTRING,
                                   g_ptr_array_index (feats, i), TRUE));
  return g_variant_new ("(qaay)", (guint16) TEMPLATE_VERSION, &b);
}

/* Best score of a probe against every enroll touch of print, or -1 when
 * the print isn't one of ours (e.g. enrolled by the old NBIS driver). */
static int
template_score (FpPrint *print, GBytes *probe)
{
  g_autoptr(GVariant) data = NULL;
  g_autoptr(GVariant) blobs = NULL;
  guint16 version;
  int best = 0;

  g_object_get (print, "fpi-data", &data, NULL);
  if (!data || !g_variant_is_of_type (data, G_VARIANT_TYPE ("(qaay)")))
    return -1;
  g_variant_get (data, "(q@aay)", &version, &blobs);
  if (version != TEMPLATE_VERSION)
    return -1;

  for (gsize i = 0; i < g_variant_n_children (blobs); i++)
    {
      g_autoptr(GVariant) child = g_variant_get_child_value (blobs, i);
      g_autoptr(GBytes) tmpl = g_variant_get_data_as_bytes (child);
      best = MAX (best, cb_match_score (tmpl, probe));
    }
  return best;
}

/* ========================================================================
 * Enroll / Verify / Identify
 * ======================================================================== */

static void
action_fail (FpDevice *dev, GError *error)
{
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);

  g_ptr_array_set_size (self->enroll_feats, 0);
  fpi_device_report_finger_status (dev, FP_FINGER_STATUS_NONE);
  switch (fpi_device_get_current_action (dev))
    {
    case FPI_DEVICE_ACTION_ENROLL:
      fpi_device_enroll_complete (dev, NULL, error);
      break;
    case FPI_DEVICE_ACTION_VERIFY:
      fpi_device_verify_complete (dev, error);
      break;
    case FPI_DEVICE_ACTION_IDENTIFY:
      fpi_device_identify_complete (dev, error);
      break;
    default:
      fpi_device_action_error (dev, error);
    }
}

static void
enroll_touch (FpiDeviceCanvasbio *self, GBytes *features)
{
  FpDevice *dev = FP_DEVICE (self);
  FpPrint *print = NULL;

  if (!features)
    {
      fpi_device_enroll_progress (dev, self->enroll_feats->len, NULL,
                                  fpi_device_retry_new (FP_DEVICE_RETRY_CENTER_FINGER));
      touch_start (self, TRUE);
      return;
    }

  g_ptr_array_add (self->enroll_feats, g_bytes_ref (features));
  fpi_device_enroll_progress (dev, self->enroll_feats->len, NULL, NULL);
  if (self->enroll_feats->len < ENROLL_STAGES)
    {
      touch_start (self, TRUE);
      return;
    }

  fpi_device_get_enroll_data (dev, &print);
  fpi_print_set_type (print, FPI_PRINT_RAW);
  g_object_set (print, "fpi-data", template_to_variant (self->enroll_feats), NULL);
  g_ptr_array_set_size (self->enroll_feats, 0);

  fpi_device_report_finger_status (dev, FP_FINGER_STATUS_NONE);
  fpi_device_enroll_complete (dev, g_object_ref (print), NULL);
}

static void
verify_touch (FpiDeviceCanvasbio *self, GBytes *features)
{
  FpDevice *dev = FP_DEVICE (self);
  FpPrint *print = NULL;

  fpi_device_report_finger_status (dev, FP_FINGER_STATUS_NONE);
  if (!features)
    {
      fpi_device_verify_report (dev, FPI_MATCH_ERROR, NULL,
                                fpi_device_retry_new (FP_DEVICE_RETRY_CENTER_FINGER));
      fpi_device_verify_complete (dev, NULL);
      return;
    }

  fpi_device_get_verify_data (dev, &print);
  int score = template_score (print, features);
  fp_dbg ("Verify score %d (threshold %u)", score, match_threshold ());
  if (score < 0)
    {
      fpi_device_verify_complete (dev, fpi_device_error_new_msg (
                                    FP_DEVICE_ERROR_DATA_INVALID,
                                    "Print enrolled by an older driver version; please re-enroll"));
      return;
    }
  fpi_device_verify_report (dev, score >= (int) match_threshold () ?
                            FPI_MATCH_SUCCESS : FPI_MATCH_FAIL, NULL, NULL);
  fpi_device_verify_complete (dev, NULL);
}

static void
identify_touch (FpiDeviceCanvasbio *self, GBytes *features)
{
  FpDevice *dev = FP_DEVICE (self);
  GPtrArray *prints = NULL;
  FpPrint *best = NULL;
  int best_score = -1;

  fpi_device_report_finger_status (dev, FP_FINGER_STATUS_NONE);
  if (!features)
    {
      fpi_device_identify_report (dev, NULL, NULL,
                                  fpi_device_retry_new (FP_DEVICE_RETRY_CENTER_FINGER));
      fpi_device_identify_complete (dev, NULL);
      return;
    }

  fpi_device_get_identify_data (dev, &prints);
  for (guint i = 0; i < prints->len; i++)
    {
      FpPrint *p = g_ptr_array_index (prints, i);
      int s = template_score (p, features);
      if (s > best_score)
        {
          best_score = s;
          best = p;
        }
    }
  fp_dbg ("Identify best score %d (threshold %u)", best_score, match_threshold ());
  fpi_device_identify_report (dev, best_score >= (int) match_threshold () ? best : NULL,
                              NULL, NULL);
  fpi_device_identify_complete (dev, NULL);
}

static void
touch_done (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  FpDevice *dev = FP_DEVICE (source_object);
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);
  TouchTask *t = g_task_get_task_data (G_TASK (res));
  GError *error = NULL;

  if (!g_task_propagate_boolean (G_TASK (res), &error))
    {
      action_fail (dev, error);
      return;
    }
  fpi_device_report_finger_status (dev, FP_FINGER_STATUS_PRESENT);

  switch (fpi_device_get_current_action (dev))
    {
    case FPI_DEVICE_ACTION_ENROLL:
      enroll_touch (self, t->features);
      break;
    case FPI_DEVICE_ACTION_VERIFY:
      verify_touch (self, t->features);
      break;
    case FPI_DEVICE_ACTION_IDENTIFY:
      identify_touch (self, t->features);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
dev_enroll (FpDevice *dev)
{
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (dev);

  g_ptr_array_set_size (self->enroll_feats, 0);
  touch_start (self, FALSE);
}

static void
dev_verify (FpDevice *dev)
{
  touch_start (FPI_DEVICE_CANVASBIO (dev), FALSE);
}

static void
dev_identify (FpDevice *dev)
{
  touch_start (FPI_DEVICE_CANVASBIO (dev), FALSE);
}

/* ========================================================================
 * GObject / Class Init / TOD Entry Point
 * ======================================================================== */

static void
fpi_device_canvasbio_finalize (GObject *object)
{
  FpiDeviceCanvasbio *self = FPI_DEVICE_CANVASBIO (object);

  g_clear_pointer (&self->enroll_feats, g_ptr_array_unref);
  G_OBJECT_CLASS (fpi_device_canvasbio_parent_class)->finalize (object);
}

static void
fpi_device_canvasbio_init (FpiDeviceCanvasbio *self)
{
  self->initialized = FALSE;
  self->raw_usb_fd = -1;
  self->enroll_feats = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
}

static void
fpi_device_canvasbio_class_init (FpiDeviceCanvasbioClass *klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);

  obj_class->finalize = fpi_device_canvasbio_finalize;

  dev_class->id = "canvasbio";
  dev_class->full_name = "CanvasBio CB2000 Fingerprint Sensor";
  dev_class->type = FP_DEVICE_TYPE_USB;
  dev_class->id_table = id_table;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;
  dev_class->nr_enroll_stages = ENROLL_STAGES;

  dev_class->open = dev_open;
  dev_class->close = dev_close;
  dev_class->enroll = dev_enroll;
  dev_class->verify = dev_verify;
  dev_class->identify = dev_identify;

  fpi_device_class_auto_initialize_features (dev_class);
}

/* TOD entry point — libfprint loads this symbol from the .so */
GType
fpi_tod_shared_driver_get_type (void)
{
  return fpi_device_canvasbio_get_type ();
}
