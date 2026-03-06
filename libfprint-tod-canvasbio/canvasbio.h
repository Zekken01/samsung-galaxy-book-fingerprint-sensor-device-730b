#pragma once

#include <glib-object.h>

/* USB endpoints */
#define CB_EP_OUT  0x01
#define CB_EP_IN   0x82

/* Cypress MCU vendor requests */
#define CY_SPI_TRANSFER  0xCA
#define CY_READY_CHECK   0xCC
#define CY_POWER         0xDB
#define CY_GPIO_STATUS   0xDA

/* SPI direction flags (wValue for 0xCA) */
#define SPI_DIR_WRITE  0x0002
#define SPI_DIR_READ   0x0003

/* SPI command prefixes */
#define SPI_WRITE_CMD  0xA9
#define SPI_READ_CMD   0xA8

/* Image dimensions */
#define IMG_WIDTH   112
#define IMG_HEIGHT  96
#define IMG_SIZE    (IMG_WIDTH * IMG_HEIGHT)  /* 10752 */

/* Finger-down status value from register 0x20 */
#define STATUS_FINGER_DOWN  0x0107

/* USB timeouts (milliseconds) */
#define TIMEOUT_CMD   1000
#define TIMEOUT_IMG   2000

/* Register operation types */
typedef enum {
  REG_WRITE1,     /* spi_write_reg: [A9, reg, val_u8] */
  REG_WRITE2,     /* spi_write_reg2: [A9, reg, hi, lo] */
  REG_READ,       /* spi_read_reg: [A8, reg, 00] -> 3 bytes, use byte[2] */
  REG_READ_FULL,  /* spi_read_reg_full: [A8, reg, 00] -> 3 bytes, keep all */
} RegOp;

typedef struct {
  RegOp  op;
  guint8 reg;
  guint16 val;  /* for WRITE1: low byte used; for WRITE2: full 16-bit */
} RegEntry;

/* Sensor init registers (17 entries from INIT_REGISTERS) */
static const RegEntry init_registers[] = {
  { REG_WRITE2, 0x60, 0x1B00 },
  { REG_WRITE2, 0x50, 0x2100 },
  { REG_WRITE2, 0x61, 0x0000 },
  { REG_WRITE2, 0x62, 0x001A },
  { REG_WRITE2, 0x63, 0x001A },
  { REG_WRITE2, 0x64, 0x040A },
  { REG_WRITE2, 0x66, 0x0F80 },
  { REG_WRITE2, 0x67, 0x1B00 },
  { REG_WRITE2, 0x68, 0x000F },
  { REG_WRITE2, 0x69, 0x0014 },
  { REG_WRITE2, 0x6A, 0x0019 },
  { REG_WRITE2, 0x6C, 0x0019 },
  { REG_WRITE2, 0x40, 0x4300 },
  { REG_WRITE2, 0x41, 0x6F00 },
  { REG_WRITE2, 0x55, 0x2000 },
  { REG_WRITE2, 0x5F, 0x0000 },
  { REG_WRITE2, 0x52, 0x2700 },
};

/* Note: detect_registers removed — finger detection now uses
 * scan_registers + GPIO status poll (0xDA), not SPI reg 0x20 polling. */

/* Scan trigger registers (15 operations from trigger_scan()) */
static const RegEntry scan_registers[] = {
  { REG_WRITE2, 0x5F, 0x0000 },
  { REG_WRITE2, 0x52, 0x2700 },
  { REG_WRITE2, 0x09, 0x0000 },
  { REG_WRITE2, 0x5D, 0x4D00 },
  { REG_WRITE2, 0x51, 0xA825 },
  { REG_WRITE1, 0x03, 0x0000 },
  { REG_WRITE2, 0x38, 0x0100 },
  { REG_WRITE2, 0x3D, 0xFF0F },
  { REG_WRITE2, 0x10, 0x6000 },
  { REG_WRITE2, 0x3B, 0x1400 },
  { REG_WRITE2, 0x2F, 0xF6FF },
  { REG_WRITE2, 0x09, 0x0000 },
  { REG_WRITE1, 0x0C, 0x0000 },
  { REG_READ_FULL, 0x20, 0 },
  { REG_WRITE1, 0x04, 0x0000 },
};

/* Readout mode registers (14 operations from set_mode_readout()) */
static const RegEntry readout_registers[] = {
  { REG_READ,      0x08, 0 },
  { REG_WRITE2,    0x09, 0x0000 },
  { REG_READ_FULL, 0x3E, 0 },
  { REG_WRITE2,    0x03, 0x0000 },
  { REG_READ_FULL, 0x20, 0 },
  { REG_WRITE2,    0x10, 0x0001 },
  { REG_WRITE2,    0x2F, 0xEF00 },
  { REG_WRITE2,    0x09, 0x0000 },
  { REG_WRITE2,    0x5D, 0x4D00 },
  { REG_WRITE2,    0x51, 0x3A25 },
  { REG_WRITE1,    0x0C, 0x0000 },
  { REG_READ_FULL, 0x20, 0 },
  { REG_WRITE2,    0x04, 0x0000 },
  { REG_WRITE2,    0x09, 0x0000 },
};

/* Note: poll_cycle_registers removed — detect uses GPIO status poll (0xDA)
 * instead of SPI register 0x20 polling. */

/* Instance data struct — declared here, defined via G_DEFINE_TYPE */
typedef struct _FpiDeviceCanvasbio FpiDeviceCanvasbio;

struct _FpiDeviceCanvasbio {
  FpImageDevice parent;

  gboolean initialized;

  /* Register sequence state */
  gint          reg_idx;
  const RegEntry *reg_table;
  gint          reg_table_len;

  /* SPI result buffer (for reads) */
  guint8  spi_result[64];
  gsize   spi_result_len;

  /* Image buffer */
  guint8  img_buf[IMG_SIZE];
  gsize   img_offset;
  gint    bulk_block;

  /* DA poll counter */
  gint    poll_timeout_counter;

  /* Detect poll timer */
  GSource *poll_source;

  /* Timestamp when scan was triggered (for stale DA detection) */
  gint64  scan_trigger_time;

  /* Pre-captured image flag (image captured in detect phase) */
  gboolean image_ready;

  /* Raw USB fd for bulk transfers (bypasses GUsb nested main loop issue) */
  int raw_usb_fd;
};
