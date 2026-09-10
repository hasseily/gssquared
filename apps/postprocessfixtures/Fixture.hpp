#pragma once
// Shared deterministic source pixels and acceptance metrics for reference and
// portable-backend tests. Changing these fixtures requires regenerating
// goldens.
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>
namespace gs2::postprocess::testfixtures {
inline constexpr int fixture_version = 1;
inline constexpr int W = 1280, H = 800;
using Pixels = std::vector<unsigned char>;
struct Metric {
  double mean, rms, p99, changed;
  int max;
  double ssim, signed_mean, rounding_adjusted_mean;
};
inline Pixels fixture(int phase) {
  Pixels p(W * H * 4);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      int i = (y * W + x) * 4;
      float f = phase == 1 ? .12f : phase == 2 ? .58f : 1.f;
      p[i] = f * (24 + 210 * x / (W - 1));
      p[i + 1] = f * (24 + 210 * y / (H - 1));
      p[i + 2] = f * ((x / 32 + y / 24) % 2 ? 216 : 39);
      p[i + 3] = 255;
      if ((x - 320 - phase * 28) * (x - 320 - phase * 28) +
              (y - 380) * (y - 380) <
          150 * 150) {
        p[i] = f * 240;
        p[i + 1] = f * 75;
        p[i + 2] = f * 35;
      }
    }
  return p;
}
inline Metric compare(const Pixels &a, const Pixels &b) {
  double sum = 0, sq = 0, changed = 0, signed_sum = 0,
         rounding_adjusted_sum = 0;
  int max = 0;
  std::array<size_t, 256> histogram{};
  size_t count = 0;
  for (size_t j = 0; j < a.size(); ++j) {
    if (j % 4 == 3)
      continue;
    int delta = std::abs(int(a[j]) - int(b[j]));
    signed_sum += int(a[j]) - int(b[j]);
    ++histogram[delta];
    ++count;
    sum += delta;
    rounding_adjusted_sum += std::max(delta - 1, 0);
    sq += delta * delta;
    max = std::max(max, delta);
    changed += delta > 3;
  }
  size_t cumulative = 0;
  int percentile99 = 0;
  for (; percentile99 < 255; ++percentile99) {
    cumulative += histogram[percentile99];
    if (cumulative > count * 99 / 100)
      break;
  }
  double ssim = 0;
  int windows = 0;
  for (int by = 0; by < H; by += 8)
    for (int bx = 0; bx < W; bx += 8) {
      double sa = 0, sb = 0, aa = 0, bb = 0, ab = 0;
      int n = 0;
      for (int y = by; y < std::min(by + 8, H); ++y)
        for (int x = bx; x < std::min(bx + 8, W); ++x) {
          int j = (y * W + x) * 4;
          double av = .2126 * a[j] + .7152 * a[j + 1] + .0722 * a[j + 2],
                 bv = .2126 * b[j] + .7152 * b[j + 1] + .0722 * b[j + 2];
          sa += av;
          sb += bv;
          aa += av * av;
          bb += bv * bv;
          ab += av * bv;
          ++n;
        }
      double ma = sa / n, mb = sb / n, va = std::max(0., aa / n - ma * ma),
             vb = std::max(0., bb / n - mb * mb), cov = ab / n - ma * mb;
      ssim += ((2 * ma * mb + 6.5025) * (2 * cov + 58.5225)) /
              ((ma * ma + mb * mb + 6.5025) * (va + vb + 58.5225));
      ++windows;
    }
  return {sum / count,
          std::sqrt(sq / count),
          double(percentile99),
          changed / count * 100,
          max,
          ssim / windows,
          signed_sum / count,
          rounding_adjusted_sum / count};
}
inline bool within_tolerance(const std::string &name, const Metric &m) {
  if (m.max <= 1)
    return true; // One-byte render-target quantization is backend-dependent.
  // Sharp discard/nearest/mask thresholds can move a few pixels by one ULP.
  // A grain hash can change phase across different sine/FMA implementations.
  if (name == "grain")
    return m.mean <= 3 && m.rms <= 5 && m.ssim >= .97 &&
           std::abs(m.signed_mean) <= .2;
  return m.rounding_adjusted_mean <= .15 && m.rms <= 3 && m.changed <= .5 &&
         m.ssim >= .995;
}
} // namespace gs2::postprocess::testfixtures
