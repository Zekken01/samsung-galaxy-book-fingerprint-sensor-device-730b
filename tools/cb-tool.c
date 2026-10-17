/* Standalone CB2000 capture tool for driver development.
 *
 * Talks to the sensor directly over usbdevfs (no fprintd, no sudo when the
 * udev uaccess rule is installed) and reuses the driver's own protocol code
 * by compiling canvasbio.c into this binary.
 *
 *   cb-tool sweep   [--out DIR]
 *       Hold one finger on the sensor; captures frames at several analog
 *       settings (gain, ADC reference, offset) and reports image quality.
 *
 *   cb-tool capture --label NAME [--count N] [--out DIR] [--set REG=VAL]...
 *       Guided dataset capture: waits for finger down, captures, waits for
 *       lift, repeats. Images are saved raw as DIR/NAME_NN.pgm.
 */

#include "../libfprint-tod-canvasbio/canvasbio.c"

#include <stdlib.h>
#include <errno.h>

typedef struct { guint8 reg; guint16 val; } Setting;

#define MAX_SETTINGS 8

typedef struct {
  const char *tag;
  Setting     set[MAX_SETTINGS];
  int         n;
} Config;

static char dev_path[64];
static int  usb_fd = -1;

static gboolean
find_device (void)
{
  g_autoptr(GDir) d = g_dir_open ("/sys/bus/usb/devices", 0, NULL);
  const gchar *name;

  while (d && (name = g_dir_read_name (d)))
    {
      g_autofree gchar *base = g_build_filename ("/sys/bus/usb/devices", name, NULL);
      g_autofree gchar *vp = g_build_filename (base, "idVendor", NULL);
      g_autofree gchar *pp = g_build_filename (base, "idProduct", NULL);
      g_autofree gchar *bp = g_build_filename (base, "busnum", NULL);
      g_autofree gchar *np = g_build_filename (base, "devnum", NULL);
      g_autofree gchar *v = NULL, *p = NULL, *b = NULL, *n = NULL;

      if (!g_file_get_contents (vp, &v, NULL, NULL) ||
          !g_file_get_contents (pp, &p, NULL, NULL))
        continue;
      if (strncmp (v, "04e8", 4) != 0 || strncmp (p, "730b", 4) != 0)
        continue;
      if (!g_file_get_contents (bp, &b, NULL, NULL) ||
          !g_file_get_contents (np, &n, NULL, NULL))
        continue;
      snprintf (dev_path, sizeof (dev_path), "/dev/bus/usb/%03d/%03d", atoi (b), atoi (n));
      return TRUE;
    }
  return FALSE;
}

static gboolean
apply (const Config *c, GError **error)
{
  for (int i = 0; i < c->n; i++)
    if (!sync_spi_write_reg2 (usb_fd, c->set[i].reg, c->set[i].val, error))
      return FALSE;
  return TRUE;
}

static gboolean
open_and_init (const Config *c, GError **error)
{
  int iface = 0;

  usb_fd = open (dev_path, O_RDWR);
  if (usb_fd < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "open %s: %s%s", dev_path,
                   g_strerror (errno),
                   errno == EACCES ? " (is the udev rule installed?)" : "");
      return FALSE;
    }
  if (ioctl (usb_fd, USBDEVFS_CLAIMINTERFACE, &iface) < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "claim interface: %s%s",
                   g_strerror (errno),
                   errno == EBUSY ? " (fprintd is using the sensor; stop any enroll/verify)" : "");
      close (usb_fd);
      usb_fd = -1;
      return FALSE;
    }
  return sync_init_early (usb_fd, error) && apply (c, error);
}

static void
close_dev (void)
{
  if (usb_fd < 0)
    return;
  int iface = 0;
  ioctl (usb_fd, USBDEVFS_RELEASEINTERFACE, &iface);
  close (usb_fd);
  usb_fd = -1;
}

/* Same two tiers as the driver's sync_reinit_sensor(). */
static gboolean
reinit (const Config *c, GError **error)
{
  unsigned int ep_out = CB_EP_OUT, ep_in = CB_EP_IN;
  GError *e = NULL;

  ioctl (usb_fd, USBDEVFS_CLEAR_HALT, &ep_out);
  ioctl (usb_fd, USBDEVFS_CLEAR_HALT, &ep_in);
  guint8 id = sync_spi_read_reg (usb_fd, 0xB9, &e);
  if (!e && (id == 0x34 || id == 0x68))
    return TRUE;
  g_clear_error (&e);

  ioctl (usb_fd, USBDEVFS_RESET, NULL);
  close_dev ();
  g_usleep (500000);
  return open_and_init (c, error);
}

typedef struct { gdouble mean, std, clip, wrap; } Quality;

static Quality
quality (const guint8 *img)
{
  Quality q = { 0 };
  gdouble s = 0, s2 = 0;
  int clip = 0, wrap = 0;

  for (int i = 0; i < IMG_SIZE; i++)
    {
      s += img[i];
      s2 += (gdouble) img[i] * img[i];
      clip += img[i] == 0 || img[i] == 255;
    }
  for (int y = 0; y < IMG_HEIGHT; y++)
    for (int x = 0; x + 1 < IMG_WIDTH; x++)
      wrap += abs (img[y * IMG_WIDTH + x] - img[y * IMG_WIDTH + x + 1]) > 160;
  q.mean = s / IMG_SIZE;
  q.std = sqrt (MAX (0, s2 / IMG_SIZE - q.mean * q.mean));
  q.clip = 100.0 * clip / IMG_SIZE;
  q.wrap = 100.0 * wrap / (IMG_HEIGHT * (IMG_WIDTH - 1));
  return q;
}

static gboolean
save_pgm (const char *path, const guint8 *img)
{
  FILE *f = fopen (path, "wb");
  if (!f)
    return FALSE;
  fprintf (f, "P5\n%d %d\n255\n", IMG_WIDTH, IMG_HEIGHT);
  fwrite (img, 1, IMG_SIZE, f);
  return fclose (f) == 0;
}

static gboolean
capture (const Config *c, guint8 *img, GError **error)
{
  gboolean ok = sync_full_capture (usb_fd, img, error);
  GError *re = NULL;
  if (!reinit (c, &re))
    {
      if (ok)
        g_propagate_error (error, re);
      else
        g_clear_error (&re);
      return FALSE;
    }
  return ok;
}

/* Finger present = quick scan-complete (same probe the driver uses) and a
 * frame with real contrast. */
static gboolean
finger_present (const Config *c, guint8 *img, GError **error)
{
  gboolean ready = FALSE;
  if (!sync_probe_scan_ready (usb_fd, 180, &ready, error))
    return FALSE;
  if (!ready)
    return FALSE;
  if (!capture (c, img, error))
    return FALSE;
  return quality (img).std > 25;
}

static void
wait_finger (const Config *c, guint8 *img, gboolean want_down)
{
  int stable = 0;
  for (;;)
    {
      GError *e = NULL;
      gboolean down = finger_present (c, img, &e);
      if (e)
        {
          g_clear_error (&e);
          GError *re = NULL;
          if (!reinit (c, &re))
            {
              g_printerr ("reinit failed: %s\n", re->message);
              exit (1);
            }
          continue;
        }
      stable = (down == want_down) ? stable + 1 : 0;
      if (stable >= (want_down ? 1 : 2))
        return;
      g_usleep (50000);
    }
}

static int
cmd_sweep (const char *out)
{
  static const Config cfgs[] = {
    { "default-a" },
    { "gain67-0400", { { 0x67, 0x0400 } }, 1 },
    { "gain67-0800", { { 0x67, 0x0800 } }, 1 },
    { "gain67-0c00", { { 0x67, 0x0C00 } }, 1 },
    { "gain67-1000", { { 0x67, 0x1000 } }, 1 },
    { "gain67-1400", { { 0x67, 0x1400 } }, 1 },
    { "gain67-2400", { { 0x67, 0x2400 } }, 1 },
    { "adcref-0010", { { 0x6A, 0x0010 }, { 0x6C, 0x0010 } }, 2 },
    { "adcref-0022", { { 0x6A, 0x0022 }, { 0x6C, 0x0022 } }, 2 },
    { "adcref-002c", { { 0x6A, 0x002C }, { 0x6C, 0x002C } }, 2 },
    { "adcref-0038", { { 0x6A, 0x0038 }, { 0x6C, 0x0038 } }, 2 },
    { "offs61-0010", { { 0x61, 0x0010 } }, 1 },
    { "offs61-0040", { { 0x61, 0x0040 } }, 1 },
    { "offs61-0100", { { 0x61, 0x0100 } }, 1 },
    { "default-b" },
  };
  const Config def = { "default" };
  guint8 img[IMG_SIZE];
  g_autoptr(GError) error = NULL;

  g_mkdir_with_parents (out, 0700);
  if (!open_and_init (&def, &error))
    {
      g_printerr ("init failed: %s\n", error->message);
      return 1;
    }

  printf ("Place a finger flat on the sensor and HOLD IT STILL until told to lift...\n");
  wait_finger (&def, img, TRUE);
  printf ("Finger detected - sweeping %d settings, keep holding.\n\n", (int) G_N_ELEMENTS (cfgs));
  printf ("%-12s  %6s %6s %7s %7s\n", "setting", "mean", "std", "clip%", "wrap%");

  for (guint i = 0; i < G_N_ELEMENTS (cfgs); i++)
    {
      /* Full re-init so each setting starts from the driver's defaults. */
      close_dev ();
      if (!open_and_init (&cfgs[i], &error))
        {
          g_printerr ("%s: init failed: %s\n", cfgs[i].tag, error->message);
          return 1;
        }
      for (int n = 0; n < 2; n++)
        {
          if (!capture (&cfgs[i], img, &error))
            {
              printf ("%-12s  capture failed: %s\n", cfgs[i].tag, error->message);
              g_clear_error (&error);
              continue;
            }
          Quality q = quality (img);
          printf ("%-12s  %6.1f %6.1f %7.2f %7.2f%s\n", cfgs[i].tag, q.mean, q.std,
                  q.clip, q.wrap, q.std < 25 ? "   <- no finger?" : "");
          g_autofree gchar *p = g_strdup_printf ("%s/%s_%02d.pgm", out, cfgs[i].tag, n + 1);
          save_pgm (p, img);
        }
    }

  close_dev ();
  open_and_init (&def, NULL);   /* leave the sensor on driver defaults */
  close_dev ();
  printf ("\nDone - you can lift your finger. Images in %s/\n", out);
  return 0;
}

/* One immediate frame, finger or not: checks init + transfer end to end. */
static int
cmd_snap (const char *out, const Config *c)
{
  guint8 img[IMG_SIZE];
  g_autoptr(GError) error = NULL;

  if (!open_and_init (c, &error))
    {
      g_printerr ("init failed: %s\n", error->message);
      return 1;
    }
  for (int n = 1; n <= 3; n++)
    {
      if (!capture (c, img, &error))
        {
          g_printerr ("capture %d failed: %s\n", n, error->message);
          return 1;
        }
      Quality q = quality (img);
      printf ("frame %d: mean %.1f std %.1f clip %.2f%% wrap %.2f%%\n",
              n, q.mean, q.std, q.clip, q.wrap);
    }
  g_mkdir_with_parents (out, 0700);
  g_autofree gchar *p = g_strdup_printf ("%s/snap.pgm", out);
  save_pgm (p, img);
  close_dev ();
  printf ("saved %s\n", p);
  return 0;
}

static gboolean
parse_set (const char *arg, Setting *s)
{
  unsigned int reg, val;
  if (sscanf (arg, "%x=%x", &reg, &val) != 2 || reg > 0xFF || val > 0xFFFF)
    return FALSE;
  s->reg = reg;
  s->val = val;
  return TRUE;
}

static int
cmd_capture (const char *out, const char *label, int count, const Config *c)
{
  guint8 img[IMG_SIZE];
  g_autoptr(GError) error = NULL;

  g_mkdir_with_parents (out, 0700);
  if (!open_and_init (c, &error))
    {
      g_printerr ("init failed: %s\n", error->message);
      return 1;
    }
  printf ("Capturing %d touches of '%s'. Touch, lift fully, repeat; vary position a little.\n",
          count, label);
  for (int n = 1; n <= count; n++)
    {
      printf ("  [%d/%d] touch... ", n, count);
      fflush (stdout);
      wait_finger (c, img, TRUE);
      Quality q = quality (img);
      g_autofree gchar *p = g_strdup_printf ("%s/%s_%02d.pgm", out, label, n);
      save_pgm (p, img);
      printf ("saved (std %.0f, clip %.1f%%, wrap %.2f%%) - lift\n", q.std, q.clip, q.wrap);
      wait_finger (c, img, FALSE);
    }
  close_dev ();
  return 0;
}

int
main (int argc, char **argv)
{
  const char *out = "cb-out", *label = NULL;
  int count = 10;
  Config cfg = { "custom" };

  if (argc < 2)
    goto usage;
  for (int i = 2; i < argc; i++)
    {
      if (!strcmp (argv[i], "--out") && i + 1 < argc)
        out = argv[++i];
      else if (!strcmp (argv[i], "--label") && i + 1 < argc)
        label = argv[++i];
      else if (!strcmp (argv[i], "--count") && i + 1 < argc)
        count = atoi (argv[++i]);
      else if (!strcmp (argv[i], "--set") && i + 1 < argc && cfg.n < MAX_SETTINGS)
        {
          if (!parse_set (argv[++i], &cfg.set[cfg.n++]))
            goto usage;
        }
      else
        goto usage;
    }

  if (!find_device ())
    {
      g_printerr ("CB2000 sensor (04e8:730b) not found\n");
      return 1;
    }

  if (!strcmp (argv[1], "snap"))
    return cmd_snap (out, &cfg);
  if (!strcmp (argv[1], "sweep"))
    return cmd_sweep (out);
  if (!strcmp (argv[1], "capture") && label && count > 0)
    return cmd_capture (out, label, count, &cfg);

usage:
  g_printerr ("usage: %s snap [--out DIR] [--set REG=VAL]...\n"
              "       %s sweep [--out DIR]\n"
              "       %s capture --label NAME [--count N] [--out DIR] [--set REG=VAL]...\n"
              "       (REG and VAL in hex, e.g. --set 67=1000)\n", argv[0], argv[0], argv[0]);
  return 2;
}
