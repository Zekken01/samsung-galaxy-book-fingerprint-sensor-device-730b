#pragma once
/* Small-sensor fingerprint matcher: SIFT keypoints + RANSAC geometric check.
 *
 * NBIS minutiae matching can't work on the CB2000's ~5x4 mm window (3-6
 * real minutiae per frame), so the driver matches local ridge texture
 * instead, as small-sensor devices generally do. Offline evaluation on 40
 * captures: impostor pairs score 0-6 inliers, genuine pairs median ~40.
 *
 * Features are serialised to an opaque byte blob (keypoint positions and
 * uint8 descriptors; no image data) for storage in FpPrint. */

#include <glib.h>

G_BEGIN_DECLS

/* Extract features from a raw 8-bit image. Returns NULL and sets *n_kp to
 * the keypoint count when there are too few to be useful. */
GBytes *cb_match_extract (const guint8 *img, int width, int height,
                          int *n_kp);

/* Number of geometrically consistent matches between two feature blobs
 * (0 when they don't share a plausible rigid transform). */
int cb_match_score (GBytes *a, GBytes *b);

G_END_DECLS
