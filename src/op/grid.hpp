#pragma once

#include "fft/fft.hpp"
#include "gridBase.hpp"
#include "mapping.hpp"
#include "pad.hpp"
#include "tensorOps.hpp"
#include "threads.hpp"

#include <mutex>

namespace {

inline Index Wrap(Index const ii, Index const sz)
{
  if (ii < 0)
    return -1;
  else if (ii >= sz)
    return -1;
  else
    return ii;
}

} // namespace

namespace rl {

template <typename Scalar_, typename Kernel>
struct Grid final : GridBase<Scalar_, Kernel::NDim>
{
  static constexpr size_t NDim = Kernel::NDim;
  static constexpr Index kW = Kernel::PadWidth;

  OP_INHERIT(Scalar_, NDim + 2, 3)

  Mapping<NDim> mapping;
  Kernel kernel;
  Re2 basis;

  Grid(Mapping<NDim> const m, Index const nC, std::optional<Re2> const &b = std::nullopt)
    : GridBase<Scalar, NDim>(AddFront(m.cartDims, nC, b ? b.value().dimension(1) : 1), AddFront(m.noncartDims, nC))
    , mapping{m}
    , kernel{mapping.osamp}
    , basis{b ? *b : Re2(1, 1)}
  {
    static_assert(NDim < 4);
    if (!b) {
      basis.setConstant(1.f);
    }
    Log::Print<Log::Level::High>(FMT_STRING("Grid Dims {}"), this->inputDimensions());
  }

  auto forward(InputMap x) const -> OutputMap
  {
    auto const time = this->startForward(x);
    this->output().device(Threads::GlobalDevice()) = this->output().constant(0.f);
    Index const nC = this->inputDimensions()[0];
    Index const nB = this->inputDimensions()[1];
    float const scale = std::sqrt(basis.dimension(0));
    auto const &map = this->mapping;
    auto const &cdims = map.cartDims;

    auto grid_task = [&](Index const ibucket) {
      auto const &bucket = map.buckets[ibucket];

      for (auto ii = 0; ii < bucket.size(); ii++) {
        auto const si = bucket.indices[ii];
        auto const c = map.cart[si];
        auto const n = map.noncart[si];
        auto const k = this->kernel(map.offset[si]);
        Index const kW_2 = ((kW - 1) / 2);
        Index const btp = n.trace % basis.dimension(0);
        Eigen::Tensor<Scalar, 1> sum(nC);
        sum.setZero();

        for (Index i1 = 0; i1 < kW; i1++) {
          if (Index const ii1 = Wrap(c[NDim - 1] - kW_2 + i1, cdims[NDim - 1]); ii1 > -1) {
            if constexpr (NDim == 1) {
              float const kval = k(i1) * scale;
              for (Index ib = 0; ib < nB; ib++) {
                float const bval = kval * basis(btp, ib);
                for (Index ic = 0; ic < nC; ic++) {
                  this->output()(ic, n.sample, n.trace) += x(ic, ib, ii1) * bval;
                }
              }
            } else {
              for (Index i2 = 0; i2 < kW; i2++) {
                if (Index const ii2 = Wrap(c[NDim - 2] - kW_2 + i2, cdims[NDim - 2]); ii2 > -1) {
                  if constexpr (NDim == 2) {
                    float const kval = k(i2, i1) * scale;
                    for (Index ib = 0; ib < nB; ib++) {
                      float const bval = kval * basis(btp, ib);
                      for (Index ic = 0; ic < nC; ic++) {
                        this->output()(ic, n.sample, n.trace) += x(ic, ib, ii2, ii1) * bval;
                      }
                    }
                  } else {
                    for (Index i3 = 0; i3 < kW; i3++) {
                      if (Index const ii3 = Wrap(c[NDim - 3] - kW_2 + i3, cdims[NDim - 3]); ii3 > -1) {
                        float const kval = k(i3, i2, i1) * scale;
                        for (Index ib = 0; ib < nB; ib++) {
                          float const bval = kval * basis(btp, ib);
                          for (Index ic = 0; ic < nC; ic++) {
                            this->output()(ic, n.sample, n.trace) += x(ic, ib, ii3, ii2, ii1) * bval;
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    };

    Threads::For(grid_task, map.buckets.size(), "Grid Forward");
    this->finishForward(this->output(), time);
    return this->output();
  }

  auto adjoint(OutputMap y) const -> InputMap
  {
    auto const time = this->startAdjoint(y);
    auto const &map = this->mapping;
    Index const nC = this->inputDimensions()[0];
    Index const nB = this->inputDimensions()[1];
    float const scale = std::sqrt(basis.dimension(0));
    auto const &cdims = map.cartDims;

    std::mutex writeMutex;
    auto grid_task = [&](Index ibucket) {
      auto const &bucket = map.buckets[ibucket];
      auto const bSz = bucket.gridSize();
      Eigen::Tensor<Scalar, 2> bSample(nC, nB);
      Input bGrid(AddFront(bSz, nC, nB));
      bGrid.setZero();
      for (auto ii = 0; ii < bucket.size(); ii++) {
        auto const si = bucket.indices[ii];
        auto const c = map.cart[si];
        auto const n = map.noncart[si];
        auto const k = this->kernel(map.offset[si]);
        Index constexpr hW = kW / 2;
        Index const btp = n.trace % basis.dimension(0);
        for (Index ib = 0; ib < nB; ib++) {
          float const bval = basis(btp, ib);
          for (Index ic = 0; ic < nC; ic++) {
            bSample(ic, ib) = y(ic, n.sample, n.trace) * bval;
          }
        }

        for (Index i1 = 0; i1 < kW; i1++) {
          Index const ii1 = i1 + c[NDim - 1] - hW - bucket.minCorner[NDim - 1];
          if constexpr (NDim == 1) {
            float const kval = k(i1);
            for (Index ib = 0; ib < nB; ib++) {
              for (Index ic = 0; ic < nC; ic++) {
                bGrid(ic, ib, ii1) += bSample(ic, ib) * kval;
              }
            }
          } else {
            for (Index i2 = 0; i2 < kW; i2++) {
              Index const ii2 = i2 + c[NDim - 2] - hW - bucket.minCorner[NDim - 2];
              if constexpr (NDim == 2) {
                float const kval = k(i2, i1) * scale;
                for (Index ib = 0; ib < nB; ib++) {
                  for (Index ic = 0; ic < nC; ic++) {
                    bGrid(ic, ib, ii2, ii1) += bSample(ic, ib) * kval;
                  }
                }
              } else {
                for (Index i3 = 0; i3 < kW; i3++) {
                  Index const ii3 = i3 + c[NDim - 3] - hW - bucket.minCorner[NDim - 3];
                  float const kval = k(i3, i2, i1) * scale;
                  for (Index ib = 0; ib < nB; ib++) {
                    for (Index ic = 0; ic < nC; ic++) {
                      bGrid(ic, ib, ii3, ii2, ii1) += bSample(ic, ib) * kval;
                    }
                  }
                }
              }
            }
          }
        }
      }

      {
        std::scoped_lock lock(writeMutex);
        for (Index i1 = 0; i1 < bSz[NDim - 1]; i1++) {
          if (Index const ii1 = Wrap(bucket.minCorner[NDim - 1] + i1, cdims[NDim - 1]); ii1 > -1) {
            if constexpr (NDim == 1) {
              for (Index ib = 0; ib < nB; ib++) {
                for (Index ic = 0; ic < nC; ic++) {
                  this->input()(ic, ib, ii1) += bGrid(ic, ib, i1);
                }
              }
            } else {
              for (Index i2 = 0; i2 < bSz[NDim - 2]; i2++) {
                if (Index const ii2 = Wrap(bucket.minCorner[NDim - 2] + i2, cdims[NDim - 2]); ii2 > -1) {
                  if constexpr (NDim == 2) {
                    for (Index ib = 0; ib < nB; ib++) {
                      for (Index ic = 0; ic < nC; ic++) {
                        this->input()(ic, ib, ii2, ii1) += bGrid(ic, ib, i2, i1);
                      }
                    }
                  } else {
                    for (Index i3 = 0; i3 < bSz[NDim - 3]; i3++) {
                      if (Index const ii3 = Wrap(bucket.minCorner[NDim - 3] + i3, cdims[NDim - 3]); ii3 > -1) {
                        for (Index ib = 0; ib < nB; ib++) {
                          for (Index ic = 0; ic < nC; ic++) {
                            this->input()(ic, ib, ii3, ii2, ii1) += bGrid(ic, ib, i3, i2, i1);
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    };

    this->input().device(Threads::GlobalDevice()) = this->input().constant(0.f);
    Threads::For(grid_task, map.buckets.size(), "Grid Adjoint");
    this->finishAdjoint(this->input(), time);
    return this->input();
  }

  auto apodization(Sz<NDim> const sz) const -> Eigen::Tensor<float, NDim>
  {
    Eigen::Tensor<Cx, NDim> temp(LastN<NDim>(this->inputDimensions()));
    auto const fft = FFT::Make<NDim, NDim>(temp);
    temp.setZero();
    float const scale = std::sqrt(Product(mapping.nomDims));
    Sz<NDim> kSt, kSz;
    kSt.fill((Kernel::PadWidth - Kernel::Width) / 2);
    kSz.fill(Kernel::Width);
    Eigen::Tensor<Cx, NDim> k = kernel(Kernel::Point::Zero()).slice(kSt, kSz).template cast<Cx>();
    PadOp<Cx, NDim, NDim> padK(k.dimensions(), temp.dimensions());
    temp = padK.forward(k * k.constant(scale));
    fft->reverse(temp);
    PadOp<Cx, NDim, NDim> padA(sz, temp.dimensions());
    Eigen::Tensor<float, NDim> a = padA.adjoint(temp).abs();
    a.device(Threads::GlobalDevice()) = a.inverse();
    Sz<NDim> center;
    std::transform(sz.begin(), sz.end(), center.begin(), [](Index i) { return i / 2; });
    LOG_DEBUG("Apodization size {} Scale: {} Norm: {} Val: {}", a.dimensions(), scale, Norm(a), a(center));
    return a;
  }
};

} // namespace rl
