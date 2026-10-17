/* SIFT + RANSAC matcher for the CB2000; see cb-match.h.
 * Parameters mirror the offline prototype they were evaluated with. */

#include "cb-match.h"

#include <cstring>
#include <vector>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/calib3d.hpp>

namespace {

const guint32 BLOB_MAGIC = 0x314D4243;  /* "CBM1" */
const int UPSCALE = 2;                   /* SIFT needs more pixels than 112x96 */
const int MAX_KEYPOINTS = 300;
const int MIN_KEYPOINTS = 40;
const float RATIO = 0.8f;                /* Lowe ratio test */
const double RANSAC_PX = 6.0;            /* reprojection tolerance (upscaled px) */

struct Features {
  std::vector<cv::Point2f> pts;
  cv::Mat desc;  /* CV_32F, one row per keypoint */
};

/* Blob layout: magic u32, count u32, then count x (x f32, y f32),
 * then count x 128 u8 descriptors. */
GBytes *
serialise (const std::vector<cv::KeyPoint> &kp, const cv::Mat &desc)
{
  guint32 n = kp.size ();
  gsize len = 8 + n * 8 + (gsize) n * 128;
  guint8 *buf = (guint8 *) g_malloc (len);
  guint8 *p = buf;

  memcpy (p, &BLOB_MAGIC, 4); p += 4;
  memcpy (p, &n, 4); p += 4;
  for (guint32 i = 0; i < n; i++)
    {
      float xy[2] = { kp[i].pt.x, kp[i].pt.y };
      memcpy (p, xy, 8); p += 8;
    }
  for (guint32 i = 0; i < n; i++)
    for (int j = 0; j < 128; j++)
      /* OpenCV SIFT values are already scaled into 0..255. */
      *p++ = (guint8) cv::saturate_cast<uchar> (desc.at<float> (i, j));

  return g_bytes_new_take (buf, len);
}

bool
deserialise (GBytes *blob, Features &f)
{
  gsize len;
  const guint8 *p = (const guint8 *) g_bytes_get_data (blob, &len);
  guint32 magic, n;

  if (!p || len < 8)
    return false;
  memcpy (&magic, p, 4);
  memcpy (&n, p + 4, 4);
  if (magic != BLOB_MAGIC || len != 8 + (gsize) n * 8 + (gsize) n * 128)
    return false;
  p += 8;

  f.pts.resize (n);
  for (guint32 i = 0; i < n; i++, p += 8)
    {
      float xy[2];
      memcpy (xy, p, 8);
      f.pts[i] = cv::Point2f (xy[0], xy[1]);
    }
  cv::Mat u8 (n, 128, CV_8U, (void *) p);
  u8.convertTo (f.desc, CV_32F);
  return true;
}

}  // namespace

extern "C" GBytes *
cb_match_extract (const guint8 *img, int width, int height, int *n_kp)
{
  thread_local cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE (2.0, cv::Size (4, 4));
  thread_local cv::Ptr<cv::SIFT> sift =
    cv::SIFT::create (MAX_KEYPOINTS, 3, 0.01, 20.0, 1.6);

  cv::Mat src (height, width, CV_8U, (void *) img);
  cv::Mat eq, up;
  clahe->apply (src, eq);
  cv::resize (eq, up, cv::Size (width * UPSCALE, height * UPSCALE), 0, 0,
              cv::INTER_CUBIC);

  std::vector<cv::KeyPoint> kp;
  cv::Mat desc;
  sift->detectAndCompute (up, cv::noArray (), kp, desc);

  if (n_kp)
    *n_kp = kp.size ();
  if ((int) kp.size () < MIN_KEYPOINTS || desc.empty ())
    return NULL;
  return serialise (kp, desc);
}

extern "C" int
cb_match_score (GBytes *a, GBytes *b)
{
  Features fa, fb;
  if (!deserialise (a, fa) || !deserialise (b, fb) ||
      fa.pts.size () < 3 || fb.pts.size () < 3)
    return 0;

  cv::BFMatcher bf (cv::NORM_L2);
  std::vector<std::vector<cv::DMatch>> knn;
  bf.knnMatch (fa.desc, fb.desc, knn, 2);

  std::vector<cv::Point2f> pa, pb;
  for (const auto &m : knn)
    if (m.size () == 2 && m[0].distance < RATIO * m[1].distance)
      {
        pa.push_back (fa.pts[m[0].queryIdx]);
        pb.push_back (fb.pts[m[0].trainIdx]);
      }
  if (pa.size () < 3)
    return 0;

  /* Same finger on the same sensor: rotation + translation, scale ~1. */
  std::vector<uchar> inliers;
  cv::Mat M = cv::estimateAffinePartial2D (pa, pb, inliers, cv::RANSAC,
                                           RANSAC_PX);
  if (M.empty ())
    return 0;
  double scale = std::hypot (M.at<double> (0, 0), M.at<double> (1, 0));
  if (scale < 0.85 || scale > 1.15)
    return 0;
  return cv::countNonZero (inliers);
}
