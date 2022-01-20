#include "filter.h"

#include "fft_plan.h"
#include "tensorOps.h"
#include "threads.h"

inline float Tukey(float const &r, float const &sw, float const &ew, float const &eh)
{
  if (r > ew) {
    return 0.f;
  } else if (r > sw) {
    return (0.5f * ((1 + eh) + (1 - eh) * cos((M_PI * (r - sw)) / (ew - sw))));
  } else {
    return 1.f;
  }
}

template <typename Scalar, int D>
void KSFilter(std::function<float(float const &)> const &f, Eigen::Tensor<Scalar, D> &ks)
{
  auto const sz = ks.dimension(D - 1);
  auto const sy = ks.dimension(D - 2);
  auto const sx = ks.dimension(D - 3);

  auto const hz = sz / 2;
  auto const hy = sy / 2;
  auto const hx = sx / 2;

  auto task = [&](Index const lo, Index const hi) {
    for (Index iz = lo; iz < hi; iz++) {
      for (Index iy = 0; iy < sy; iy++) {
        for (Index ix = 0; ix < sx; ix++) {
          float const rz = static_cast<float>(iz - hz) / hz;
          float const ry = static_cast<float>(iy - hy) / hy;
          float const rx = static_cast<float>(ix - hx) / hx;
          float const r = sqrt(rx * rx + ry * ry + rz * rz);
          float const val = f(r);
          ks.template chip<D - 1>(iz).template chip<D - 2>(iy).template chip<D - 3>(ix) *=
            ks.template chip<D - 1>(iz).template chip<D - 2>(iy).template chip<D - 3>(ix).constant(
              val);
        }
      }
    }
  };
  auto const start = Log::Now();
  Threads::RangeFor(task, sz);
  Log::Debug("Filtering took: {}", Log::ToNow(start));
}

void ImageTukey(float const &s, float const &e, float const &h, Cx3 &x)
{
  Log::Print(FMT_STRING("Applying Tukey filter width {}-{} height {}"), s, e, h);
  auto const &f = [&](float const &r) { return Tukey(r, s, e, h); };
  FFT::ThreeD fft(x.dimensions());
  Log::Image(x, "tukey-img-before.nii");
  fft.forward(x);
  Log::Image(x, "tukey-ks-before.nii");
  KSFilter(f, x);
  Log::Image(x, "tukey-ks-after.nii");
  fft.reverse(x);
  Log::Image(x, "tukey-img-after.nii");
}

void KSTukey(float const &s, float const &e, float const &h, Cx4 &x)
{
  Log::Print(FMT_STRING("Applying Tukey filter width {}-{} height {}"), s, e, h);
  auto const &f = [&](float const &r) { return Tukey(r, s, e, h); };
  KSFilter(f, x);
}
