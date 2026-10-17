/* Offline evaluation for the CanvasBio driver's image pipeline.
 *
 * Loads raw 112x96 PGMs named <finger>_<n>.pgm (from capture-dataset.sh),
 * runs them through the same preprocessing the driver uses at several scale
 * factors, and scores genuine vs impostor comparisons with libfprint's own
 * NBIS/bozorth3 code. Also estimates ridge period to sanity-check sensor dpi.
 *
 * Usage: cb-eval <dataset-dir> [enroll-stages]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>
#include <drivers_api.h>
#include "../libfprint-tod-canvasbio/canvasbio.h"
#include "../libfprint-tod-canvasbio/cb-image.h"

#define NBIS_PPMM        19.685  /* 500 dpi */
#define RIDGE_PERIOD_MM  0.46    /* typical adult ridge-to-ridge distance */

typedef struct {
  gchar  *finger;
  gchar  *name;
  guint8  raw[IMG_SIZE];
} Sample;

static gboolean
load_pgm (const gchar *path, guint8 *out)
{
  g_autofree gchar *buf = NULL;
  gsize len;
  int w, h, maxv, hdr = 0;

  if (!g_file_get_contents (path, &buf, &len, NULL))
    return FALSE;
  if (sscanf (buf, "P5 %d %d %d%n", &w, &h, &maxv, &hdr) != 3 ||
      w != IMG_WIDTH || h != IMG_HEIGHT || maxv != 255)
    return FALSE;
  hdr++; /* single whitespace after maxval */
  if (len < (gsize) hdr + IMG_SIZE)
    return FALSE;
  memcpy (out, buf + hdr, IMG_SIZE);
  return TRUE;
}

/* Dominant ridge period in pixels from the radially-averaged power spectrum
 * (separable DFT of the Hann-windowed, mean-removed image). */
static gdouble
ridge_period_px (const guint8 *img)
{
  const int W = IMG_WIDTH, H = IMG_HEIGHT;
  double complex *a = g_new (double complex, W * H);
  double complex *tmp = g_new (double complex, MAX (W, H));
  gdouble mean = 0;

  for (int i = 0; i < W * H; i++)
    mean += img[i];
  mean /= W * H;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      {
        gdouble wx = 0.5 - 0.5 * cos (2 * G_PI * x / (W - 1));
        gdouble wy = 0.5 - 0.5 * cos (2 * G_PI * y / (H - 1));
        a[y * W + x] = (img[y * W + x] - mean) * wx * wy;
      }
  for (int y = 0; y < H; y++)   /* rows */
    {
      for (int k = 0; k < W; k++)
        {
          double complex s = 0;
          for (int x = 0; x < W; x++)
            s += a[y * W + x] * cexp (-2 * I * G_PI * k * x / W);
          tmp[k] = s;
        }
      memcpy (&a[y * W], tmp, W * sizeof *tmp);
    }
  for (int x = 0; x < W; x++)   /* columns */
    {
      for (int k = 0; k < H; k++)
        {
          double complex s = 0;
          for (int y = 0; y < H; y++)
            s += a[y * W + x] * cexp (-2 * I * G_PI * k * y / H);
          tmp[k] = s;
        }
      for (int k = 0; k < H; k++)
        a[k * W + x] = tmp[k];
    }

  /* Bin power by spatial frequency (cycles/pixel), periods 3..20 px. */
  enum { NB = 120 };
  gdouble pw[NB] = { 0 };
  const gdouble fmin = 1.0 / 20, fmax = 1.0 / 3;
  for (int ky = 0; ky < H; ky++)
    for (int kx = 0; kx < W; kx++)
      {
        gdouble fx = (kx <= W / 2 ? kx : kx - W) / (gdouble) W;
        gdouble fy = (ky <= H / 2 ? ky : ky - H) / (gdouble) H;
        gdouble f = sqrt (fx * fx + fy * fy);
        if (f < fmin || f >= fmax)
          continue;
        int b = (int) ((f - fmin) / (fmax - fmin) * NB);
        gdouble m = cabs (a[ky * W + kx]);
        pw[b] += m * m;
      }
  int best = 0;
  for (int b = 1; b < NB; b++)
    if (pw[b] > pw[best])
      best = b;

  g_free (a);
  g_free (tmp);
  return 1.0 / (fmin + (best + 0.5) * (fmax - fmin) / NB);
}

typedef struct { gboolean done; gboolean ok; GError *err; } DetectWait;

static void
detect_cb (GObject *src, GAsyncResult *res, gpointer user_data)
{
  DetectWait *w = user_data;
  w->ok = fp_image_detect_minutiae_finish (FP_IMAGE (src), res, &w->err);
  w->done = TRUE;
}

/* Driver pipeline: optional CLAHE -> bicubic resample -> minutiae. */
static FpImage *
process (const guint8 *raw, guint scale_pct, gboolean clahe)
{
  guint8 buf[IMG_SIZE];
  int dw = IMG_WIDTH * scale_pct / 100;
  int dh = IMG_HEIGHT * scale_pct / 100;
  DetectWait w = { 0 };

  memcpy (buf, raw, IMG_SIZE);
  if (clahe)
    cb_clahe (buf, IMG_WIDTH, IMG_HEIGHT, 8, 8, 2.5);

  FpImage *img = fp_image_new (dw, dh);
  img->ppmm = NBIS_PPMM;
  img->flags = FPI_IMAGE_COLORS_INVERTED;
  cb_bicubic_resample (buf, IMG_WIDTH, IMG_HEIGHT, img->data, dw, dh);

  fp_image_detect_minutiae (img, NULL, detect_cb, &w);
  while (!w.done)
    g_main_context_iteration (NULL, TRUE);
  if (!w.ok)
    {
      /* e.g. "No minutiae found": the driver would ask for a retry. */
      g_clear_error (&w.err);
      g_object_unref (img);
      return NULL;
    }
  return img;
}

static FpPrint *
print_new (void)
{
  FpPrint *p = g_object_new (FP_TYPE_PRINT, "driver", "canvasbio",
                              "device-id", "cb-eval", NULL);
  fpi_print_set_type (p, FPI_PRINT_NBIS);
  return p;
}

/* libfprint logs "score <s>/<threshold>" for each template image it
 * compares; capture those instead of binary-searching the threshold. */
static int last_max_score;

static void
note_score (const gchar *msg)
{
  const gchar *p = msg ? strstr (msg, "score ") : NULL;
  if (p)
    last_max_score = MAX (last_max_score, atoi (p + 6));
}

static GLogWriterOutput
log_writer (GLogLevelFlags level, const GLogField *fields, gsize n,
            gpointer user_data)
{
  for (gsize i = 0; i < n; i++)
    if (g_str_equal (fields[i].key, "MESSAGE"))
      note_score (fields[i].value);
  if (level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING))
    return g_log_writer_standard_streams (level, fields, n, user_data);
  return G_LOG_WRITER_HANDLED;
}

static void
log_handler (const gchar *domain, GLogLevelFlags level, const gchar *msg,
             gpointer user_data)
{
  note_score (msg);
  if (level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING))
    g_printerr ("%s: %s\n", domain ? domain : "", msg);
}

/* Best bozorth3 score of probe against any image in template. The
 * threshold is unreachable, so every template image gets compared. */
static int
score (FpPrint *tmpl, FpPrint *probe)
{
  g_autoptr(GError) err = NULL;

  last_max_score = 0;
  if (fpi_print_bz3_match (tmpl, probe, G_MAXINT, &err) == FPI_MATCH_ERROR)
    {
      g_printerr ("match error: %s\n", err ? err->message : "?");
      exit (1);
    }
  return last_max_score;
}

static int
cmp_int (const void *a, const void *b)
{
  return *(const int *) a - *(const int *) b;
}

static int
median (GArray *v)
{
  if (v->len == 0)
    return 0;
  qsort (v->data, v->len, sizeof (int), cmp_int);
  return g_array_index (v, int, v->len / 2);
}

static int
max_of (GArray *v)
{
  int m = 0;
  for (guint i = 0; i < v->len; i++)
    m = MAX (m, g_array_index (v, int, i));
  return m;
}

/* Fraction of genuine scores below threshold (false rejects). */
static gdouble
frr_at (GArray *gen, int thr)
{
  guint rej = 0;
  for (guint i = 0; i < gen->len; i++)
    if (g_array_index (gen, int, i) < thr)
      rej++;
  return gen->len ? (gdouble) rej / gen->len : 1.0;
}

int
main (int argc, char **argv)
{
  if (argc < 2)
    {
      g_printerr ("usage: %s <dataset-dir> [enroll-stages]\n", argv[0]);
      return 2;
    }
  int stages = argc > 2 ? atoi (argv[2]) : 5;

  setvbuf (stdout, NULL, _IOLBF, 0);
  g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
  g_log_set_writer_func (log_writer, NULL, NULL);
  g_log_set_default_handler (log_handler, NULL);

  /* Load samples */
  GPtrArray *samples = g_ptr_array_new ();
  g_autoptr(GDir) dir = g_dir_open (argv[1], 0, NULL);
  if (!dir)
    {
      g_printerr ("cannot open %s\n", argv[1]);
      return 1;
    }
  const gchar *fn;
  GPtrArray *names = g_ptr_array_new_with_free_func (g_free);
  while ((fn = g_dir_read_name (dir)))
    if (g_str_has_suffix (fn, ".pgm"))
      g_ptr_array_add (names, g_strdup (fn));
  g_ptr_array_sort (names, (GCompareFunc) g_strcmp0);
  for (guint i = 0; i < names->len; i++)
    {
      const gchar *n = g_ptr_array_index (names, i);
      const gchar *us = strrchr (n, '_');
      if (!us)
        continue;
      Sample *s = g_new0 (Sample, 1);
      g_autofree gchar *path = g_build_filename (argv[1], n, NULL);
      if (!load_pgm (path, s->raw))
        {
          g_printerr ("skip %s (not a %dx%d PGM)\n", n, IMG_WIDTH, IMG_HEIGHT);
          g_free (s);
          continue;
        }
      s->name = g_strdup (n);
      s->finger = g_strndup (n, us - n);
      g_ptr_array_add (samples, s);
    }
  guint N = samples->len;
  printf ("Loaded %u images\n\n", N);
  if (N < 4)
    return 1;

  /* Ridge period -> effective dpi -> scale that yields NBIS's 500 dpi */
  GArray *periods = g_array_new (FALSE, FALSE, sizeof (int));
  for (guint i = 0; i < N; i++)
    {
      Sample *s = g_ptr_array_index (samples, i);
      int p100 = (int) (ridge_period_px (s->raw) * 100 + 0.5);
      g_array_append_val (periods, p100);
    }
  gdouble period = median (periods) / 100.0;
  gdouble dpi = period / RIDGE_PERIOD_MM * 25.4;
  printf ("Ridge period: median %.2f px (min %.2f, max %.2f)\n", period,
          g_array_index (periods, int, 0) / 100.0,
          g_array_index (periods, int, periods->len - 1) / 100.0);
  printf ("=> sensor ~%.0f dpi, ideal scale for 500 dpi: %.0f%%\n\n",
          dpi, 500.0 / dpi * 100);

  static const guint scales[] = { 100, 125, 140, 150, 160, 175, 200, 250, 300 };
  printf ("%-6s %-5s | %-9s | %-26s | %-28s\n", "scale", "clahe", "minutiae",
          "single image: gen-med imp-max", "enrolled: gen-med imp-max FRR*");
  printf ("------------------------------------------------------------------------------------------\n");

  for (int clahe = 0; clahe <= 1; clahe++)
    for (guint si = 0; si < G_N_ELEMENTS (scales); si++)
      {
        FpImage **imgs = g_new (FpImage *, N);
        FpPrint **prints = g_new (FpPrint *, N);
        GArray *mcount = g_array_new (FALSE, FALSE, sizeof (int));
        guint nfail = 0;

        for (guint i = 0; i < N; i++)
          {
            Sample *s = g_ptr_array_index (samples, i);
            imgs[i] = process (s->raw, scales[si], clahe);
            prints[i] = NULL;
            int c = imgs[i] ? (int) fp_image_get_minutiae (imgs[i])->len : 0;
            g_array_append_val (mcount, c);
            if (!imgs[i])
              {
                nfail++;
                continue;
              }
            prints[i] = print_new ();
            g_autoptr(GError) err = NULL;
            if (!fpi_print_add_from_image (prints[i], imgs[i], &err))
              {
                g_printerr ("add_from_image: %s\n", err->message);
                return 1;
              }
          }

        /* Single image vs single image */
        GArray *gen1 = g_array_new (FALSE, FALSE, sizeof (int));
        GArray *imp1 = g_array_new (FALSE, FALSE, sizeof (int));
        for (guint i = 0; i < N; i++)
          for (guint j = i + 1; j < N; j++)
            {
              Sample *a = g_ptr_array_index (samples, i);
              Sample *b = g_ptr_array_index (samples, j);
              int sc = (prints[i] && prints[j]) ? score (prints[i], prints[j]) : 0;
              g_array_append_val (g_str_equal (a->finger, b->finger) ? gen1 : imp1, sc);
            }

        /* Enrollment simulation: first `stages` images of each finger form
         * the template (like fprintd-enroll), the rest are probes. */
        GArray *genE = g_array_new (FALSE, FALSE, sizeof (int));
        GArray *impE = g_array_new (FALSE, FALSE, sizeof (int));
        GHashTable *seen = g_hash_table_new (g_str_hash, g_str_equal);
        for (guint i = 0; i < N; i++)
          {
            Sample *s = g_ptr_array_index (samples, i);
            if (g_hash_table_contains (seen, s->finger))
              continue;
            g_hash_table_add (seen, s->finger);

            FpPrint *tmpl = print_new ();
            GHashTable *in_tmpl = g_hash_table_new (NULL, NULL);
            int used = 0;
            for (guint j = 0; j < N && used < stages; j++)
              {
                Sample *t = g_ptr_array_index (samples, j);
                if (!g_str_equal (t->finger, s->finger) || !imgs[j])
                  continue;
                fpi_print_add_from_image (tmpl, imgs[j], NULL);
                g_hash_table_add (in_tmpl, GUINT_TO_POINTER (j + 1));
                used++;
              }
            for (guint j = 0; j < N; j++)
              {
                if (g_hash_table_contains (in_tmpl, GUINT_TO_POINTER (j + 1)))
                  continue;
                Sample *t = g_ptr_array_index (samples, j);
                int sc = (used && prints[j]) ? score (tmpl, prints[j]) : 0;
                g_array_append_val (g_str_equal (t->finger, s->finger) ? genE : impE, sc);
              }
            g_hash_table_destroy (in_tmpl);
            g_object_unref (tmpl);
          }
        g_hash_table_destroy (seen);

        int impmax = max_of (impE);
        printf ("%4u%%  %-5s | %4d med %2u! | %8d %8d          | %8d %8d   %5.0f%%\n",
                scales[si], clahe ? "yes" : "no", median (mcount), nfail,
                median (gen1), max_of (imp1),
                median (genE), impmax, 100 * frr_at (genE, impmax + 1));

        for (guint i = 0; i < N; i++)
          {
            g_clear_object (&prints[i]);
            g_clear_object (&imgs[i]);
          }
        g_free (prints);
        g_free (imgs);
        g_array_free (mcount, TRUE);
        g_array_free (gen1, TRUE);
        g_array_free (imp1, TRUE);
        g_array_free (genE, TRUE);
        g_array_free (impE, TRUE);
      }

  printf ("\n* FRR = share of genuine attempts rejected at the lowest threshold\n"
          "  that rejects every impostor in this set (threshold = imp-max + 1).\n"
          "  Lower is better; pick the row with lowest FRR, then add margin.\n"
          "  N! = images with no usable minutiae (would need a retry).\n");
  return 0;
}
