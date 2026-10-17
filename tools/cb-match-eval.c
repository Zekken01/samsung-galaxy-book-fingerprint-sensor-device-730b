/* Score the driver's real matcher (cb-match.cpp) on a labelled dataset of
 * bit-corrected raw PGMs (<finger>_<n>.pgm), simulating enrollment the way
 * the driver does it: first <stages> touches of a finger form the template,
 * a probe's score is its best score against any of them.
 *
 * Usage: cb-match-eval <dataset-dir> [stages] [threshold]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "../libfprint-tod-canvasbio/cb-match.h"

#define W 112
#define H 96

typedef struct { gchar *finger; GBytes *feat; int n_kp; } Sample;

static gboolean
load_pgm (const gchar *path, guint8 *out)
{
  g_autofree gchar *buf = NULL;
  gsize len;
  int w, h, maxv, hdr = 0;

  if (!g_file_get_contents (path, &buf, &len, NULL) ||
      sscanf (buf, "P5 %d %d %d%n", &w, &h, &maxv, &hdr) != 3 ||
      w != W || h != H || len < (gsize) hdr + 1 + W * H)
    return FALSE;
  memcpy (out, buf + hdr + 1, W * H);
  return TRUE;
}

static gint
cmp_int (gconstpointer x, gconstpointer y)
{
  return *(const int *) x - *(const int *) y;
}

int
main (int argc, char **argv)
{
  if (argc < 2)
    {
      g_printerr ("usage: %s <dataset-dir> [stages] [threshold]\n", argv[0]);
      return 2;
    }
  int stages = argc > 2 ? atoi (argv[2]) : 5;
  int thr = argc > 3 ? atoi (argv[3]) : 12;

  g_autoptr(GDir) dir = g_dir_open (argv[1], 0, NULL);
  GPtrArray *names = g_ptr_array_new_with_free_func (g_free);
  const gchar *fn;
  while (dir && (fn = g_dir_read_name (dir)))
    if (g_str_has_suffix (fn, ".pgm") && strrchr (fn, '_'))
      g_ptr_array_add (names, g_strdup (fn));
  g_ptr_array_sort_values (names, (GCompareFunc) g_strcmp0);

  GArray *s = g_array_new (FALSE, TRUE, sizeof (Sample));
  for (guint i = 0; i < names->len; i++)
    {
      const gchar *n = g_ptr_array_index (names, i);
      g_autofree gchar *path = g_build_filename (argv[1], n, NULL);
      guint8 img[W * H];
      if (!load_pgm (path, img))
        continue;
      Sample x = { g_strndup (n, strrchr (n, '_') - n), NULL, 0 };
      x.feat = cb_match_extract (img, W, H, &x.n_kp);
      g_array_append_val (s, x);
    }

  int nfail = 0, kmin = G_MAXINT;
  for (guint i = 0; i < s->len; i++)
    {
      Sample *x = &g_array_index (s, Sample, i);
      nfail += x->feat == NULL;
      kmin = MIN (kmin, x->n_kp);
    }
  printf ("%u images, %d rejected for too few keypoints (min %d)\n", s->len, nfail, kmin);

  int gen = 0, gen_rej = 0, imp = 0, imp_acc = 0, imp_max = 0;
  GArray *gs = g_array_new (FALSE, FALSE, sizeof (int));
  for (guint a = 0; a < s->len; a++)
    {
      Sample *fa = &g_array_index (s, Sample, a);
      gboolean first = TRUE;
      for (guint b = 0; b < a; b++)
        if (g_str_equal (g_array_index (s, Sample, b).finger, fa->finger))
          first = FALSE;
      if (!first)
        continue;

      GPtrArray *tmpl = g_ptr_array_new ();
      GHashTable *in = g_hash_table_new (NULL, NULL);
      for (guint b = 0; b < s->len && (int) tmpl->len < stages; b++)
        {
          Sample *x = &g_array_index (s, Sample, b);
          if (g_str_equal (x->finger, fa->finger) && x->feat)
            {
              g_ptr_array_add (tmpl, x->feat);
              g_hash_table_add (in, GUINT_TO_POINTER (b + 1));
            }
        }
      for (guint b = 0; b < s->len; b++)
        {
          if (g_hash_table_contains (in, GUINT_TO_POINTER (b + 1)))
            continue;
          Sample *p = &g_array_index (s, Sample, b);
          int best = 0;
          for (guint t = 0; p->feat && t < tmpl->len; t++)
            best = MAX (best, cb_match_score (g_ptr_array_index (tmpl, t), p->feat));
          if (g_str_equal (p->finger, fa->finger))
            {
              gen++;
              gen_rej += best < thr;
              g_array_append_val (gs, best);
            }
          else
            {
              imp++;
              imp_acc += best >= thr;
              imp_max = MAX (imp_max, best);
            }
        }
      g_hash_table_destroy (in);
      g_ptr_array_free (tmpl, TRUE);
    }

  g_array_sort (gs, cmp_int);
  printf ("enroll %d, threshold %d:\n", stages, thr);
  printf ("  genuine  %3d probes, median score %d, rejected %d (FRR %.1f%%)\n",
          gen, gs->len ? g_array_index (gs, int, gs->len / 2) : 0, gen_rej,
          gen ? 100.0 * gen_rej / gen : 0);
  printf ("  impostor %3d probes, max score %d, accepted %d (FAR %.1f%%)\n",
          imp, imp_max, imp_acc, imp ? 100.0 * imp_acc / imp : 0);
  return 0;
}
