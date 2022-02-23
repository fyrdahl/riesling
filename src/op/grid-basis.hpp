#pragma once

#include "grid-base.hpp"

template <int IP, int TP>
struct GridBasis final : SizedGrid<IP, TP>
{
  using typename SizedGrid<IP, TP>::Input;
  using typename SizedGrid<IP, TP>::Output;

  GridBasis(
    SizedKernel<IP, TP> const *k, Mapping const &mapping, R2 const &basis, bool const unsafe)
    : SizedGrid<IP, TP>(k, mapping, basis.dimension(1), unsafe)
    , basis_{basis}
  {
    Log::Debug(FMT_STRING("GridBasis<{},{}>, dims"), IP, TP, this->inputDimensions());
  }

  R2 const &basis() const
  {
    return basis_;
  }

  Output A(Input const &cart) const
  {
    Sz5 const cdims = cart.dimensions();
    Index const nC = cdims[0];
    Index const nB = cdims[1];
    if (LastN<4>(cdims) != LastN<4>(this->inputDimensions())) {
      Log::Fail(
        FMT_STRING("Cartesian k-space dims {} did not match {}"), cdims, this->inputDimensions());
    }
    Sz3 ncdims = this->outputDimensions();
    ncdims[0] = nC;
    Output noncart(ncdims);
    noncart.setZero();

    Eigen::IndexList<int, int, FixIn, FixIn, FixThrough> szC;
    szC.set(0, nC);
    szC.set(1, nB);

    auto grid_task = [&](Index const lo, Index const hi) {
      Eigen::IndexList<FixZero, FixZero, int, int, int> stC;
      for (auto ii = lo; ii < hi; ii++) {
        Log::Progress(ii, lo, hi);
        auto const si = this->mapping_.sortedIndices[ii];
        auto const c = this->mapping_.cart[si];
        auto const n = this->mapping_.noncart[si];
        auto const b = (basis_.chip<0>(n.spoke % basis_.dimension(0))).template cast<Cx>();
        auto const scale = this->mapping_.scale;
        auto const k = this->kernel_->k(this->mapping_.offset[si]);
        stC.set(2, c.x - ((IP - 1) / 2));
        stC.set(3, c.y - ((IP - 1) / 2));
        stC.set(4, c.z - ((TP - 1) / 2));
        noncart.template chip<2>(n.spoke).template chip<1>(n.read) =
          cart.slice(stC, szC)
            .contract(b, Eigen::IndexPairList<Eigen::type2indexpair<1, 0>>())
            .contract(
              (k * k.constant(scale)).template cast<Cx>(),
              Eigen::IndexPairList<
                Eigen::type2indexpair<1, 0>,
                Eigen::type2indexpair<2, 1>,
                Eigen::type2indexpair<3, 2>>());
      }
    };
    auto const &start = Log::Now();
    noncart.setZero();
    Threads::RangeFor(grid_task, this->mapping_.cart.size());
    Log::Debug("Cart -> Non-cart: {}", Log::ToNow(start));
    return noncart;
  }

  Input Adj(Output const &noncart) const
  {
    Sz5 const ncdims = this->inputDimensions();
    Index const nC = ncdims[0];
    if (LastN<2>(ncdims) != LastN<2>(this->outputDimensions())) {
      Log::Fail(
        FMT_STRING("Noncartesian k-space dims {} did not match {}"),
        ncdims,
        this->outputDimensions());
    }
    auto cdims = this->inputDimensions();
    cdims[0] = nC;
    Index const nB = cdims[1];
    Input cart(cdims);

    Eigen::IndexList<int, FixOne> rshNC;
    Eigen::IndexList<FixOne, int> brdNC;
    rshNC.set(0, nC);
    brdNC.set(1, nB);

    Eigen::IndexList<FixOne, int> rshB;
    Eigen::IndexList<int, FixOne> brdB;
    rshB.set(1, nB);
    brdB.set(0, nC);

    constexpr Eigen::IndexList<FixOne, FixOne, FixIn, FixIn, FixThrough> rshK;
    Eigen::IndexList<int, int, FixOne, FixOne, FixOne> brdK;
    brdK.set(0, nC);
    brdK.set(1, nB);

    Eigen::IndexList<int, int, FixOne, FixOne, FixOne> rshNCB;
    constexpr Eigen::IndexList<FixOne, FixOne, FixIn, FixIn, FixThrough> brdNCB;
    rshNCB.set(0, nC);
    rshNCB.set(1, nB);

    Eigen::IndexList<int, int, FixIn, FixIn, FixThrough> szC;
    szC.set(0, nC);
    szC.set(1, nB);

    auto dev = Threads::GlobalDevice();
    Index const nThreads = dev.numThreads();
    std::vector<Cx5> threadSpaces(nThreads);
    std::vector<Index> minZ(nThreads, 0L), szZ(nThreads, 0L);
    auto grid_task = [&](Index const lo, Index const hi, Index const ti) {
      auto const scale = this->mapping_.scale;
      // Allocate working space for this thread
      Eigen::IndexList<FixZero, FixZero, int, int, int> stC;
      Cx2 ncb(nC, nB);
      minZ[ti] = this->mapping_.cart[this->mapping_.sortedIndices[lo]].z - ((TP - 1) / 2);

      if (this->safe_) {
        Index const maxZ = this->mapping_.cart[this->mapping_.sortedIndices[hi - 1]].z + (TP / 2);
        szZ[ti] = maxZ - minZ[ti] + 1;
        threadSpaces[ti].resize(nC, nB, ncdims[2], ncdims[3], szZ[ti]);
        threadSpaces[ti].setZero();
      }

      for (auto ii = lo; ii < hi; ii++) {
        Log::Progress(ii, lo, hi);
        auto const si = this->mapping_.sortedIndices[ii];
        auto const c = this->mapping_.cart[si];
        auto const n = this->mapping_.noncart[si];
        auto const nc = noncart.template chip<2>(n.spoke).template chip<1>(n.read);
        auto const b = basis_.chip<0>(n.spoke % basis_.dimension(0));
        auto const k = this->kernel_->k(this->mapping_.offset[si]);
        ncb = (nc * nc.constant(scale)).reshape(rshNC).broadcast(brdNC) *
              b.template cast<Cx>().reshape(rshB).broadcast(brdB);
        auto const nbk = ncb.reshape(rshNCB).broadcast(brdNCB) *
                         k.template cast<Cx>().reshape(rshK).broadcast(brdK);

        stC.set(2, c.x - ((IP - 1) / 2));
        stC.set(3, c.y - ((IP - 1) / 2));
        if (this->safe_) {
          stC.set(4, c.z - ((TP - 1) / 2) - minZ[ti]);
          threadSpaces[ti].slice(stC, szC) += nbk;
        } else {
          stC.set(4, c.z - ((TP - 1) / 2));
          cart.slice(stC, szC) += nbk;
        }
      }
    };

    auto const start = Log::Now();
    cart.setZero();
    Threads::RangeFor(grid_task, this->mapping_.cart.size());
    Log::Debug("Basis Non-cart -> Cart: {}", Log::ToNow(start));
    if (this->safe_) {
      Log::Print(FMT_STRING("Combining thread workspaces..."));
      auto const start2 = Log::Now();
      Sz5 st{0, 0, 0, 0, 0};
      Sz5 sz{nC, nB, ncdims[2], ncdims[3], 0};
      for (Index ti = 0; ti < nThreads; ti++) {
        if (szZ[ti]) {
          st[4] = minZ[ti];
          sz[4] = szZ[ti];
          cart.slice(st, sz).device(dev) += threadSpaces[ti];
        }
      }
      Log::Debug(FMT_STRING("Combining took: {}"), Log::ToNow(start2));
    }
    return cart;
  }

private:
  using FixIn = Eigen::type2index<IP>;
  using FixThrough = Eigen::type2index<TP>;

  R2 basis_;
};
