#include "sdc.h"

#include "io/hd5.hpp"
#include "kernel.hpp"
#include "mapping.h"
#include "op/gridBase.hpp"
#include "op/sdc.hpp"
#include "tensorOps.h"
#include "threads.h"
#include "trajectory.h"

namespace rl {

namespace SDC {

Opts::Opts(args::Subparser &parser)
  : type(parser, "SDC", "SDC type: 'pipe', 'pipenn', 'none', or filename", {"sdc"}, "pipe")
  , pow(parser, "P", "SDC Power (default 1.0)", {"sdc-pow"}, 1.0f)
{
}

Re2 Pipe(Trajectory const &inTraj, bool const nn, float const os, Index const its)
{
  Log::Print(FMT_STRING("Using Pipe/Zwart/Menon SDC..."));
  auto info = inTraj.info();
  // Reset to one channel
  info.channels = 1;
  Trajectory traj{info, inTraj.points(), inTraj.frames()};
  Re3 W(1, info.read_points, info.spokes);
  Re3 Wp(W.dimensions());

  std::unique_ptr<Kernel> k;
  if (nn) {
    k = make_kernel("NN", info.type, os);
  } else {
    k = make_kernel("FI7", info.type, os);
  }
  auto const m = Mapping(traj, k.get(), os, 32);
  auto gridder = make_grid<float>(k.get(), m, 1);
  gridder->doNotWeightFrames();
  
  W.setConstant(1.f);
  for (Index ii = 0; ii < its; ii++) {
    Wp = gridder->A(gridder->Adj(W)); // Use the gridder's workspace
    Wp.device(Threads::GlobalDevice()) =
      (Wp > 0.f).select(W / Wp, Wp.constant(0.f)).eval(); // Avoid divide by zero problems
    float const delta = Norm(Wp - W) / Norm(W);
    W.device(Threads::GlobalDevice()) = Wp;
    if (delta < 1e-7) {
      Log::Print(FMT_STRING("SDC converged, delta was {}"), delta);
      break;
    } else {
      Log::Print(FMT_STRING("SDC Delta {}"), delta);
    }
  }
  Log::Print(FMT_STRING("SDC finished."));
  return W.chip<0>(0);
}

Re2 Radial2D(Trajectory const &traj)
{
  Log::Print(FMT_STRING("Calculating 2D radial analytic SDC"));
  Info const &info = traj.info();
  auto spoke_sdc = [&](Index const spoke, Index const N) -> Re1 {
    float const k_delta = (traj.point(1, spoke, 1.f) - traj.point(0, spoke, 1.f)).norm();
    float const V = 2.f * k_delta * M_PI / N; // Area element
    // When k-space becomes undersampled need to flatten DC (Menon & Pipe 1999)
    float const R = (M_PI * info.matrix.maxCoeff()) / N;
    float const flat_start = info.read_points / sqrt(R);
    float const flat_val = V * flat_start;
    Re1 sdc(info.read_points);
    for (Index ir = 0; ir < info.read_points; ir++) {
      float const rad = traj.point(ir, spoke, info.read_points).norm();
      if (rad == 0.f) {
        sdc(ir) = V / 8.f;
      } else if (rad < flat_start) {
        sdc(ir) = V * rad;
      } else {
        sdc(ir) = flat_val;
      }
    }
    return sdc;
  };

  Re1 const ss = spoke_sdc(0, info.spokes);
  Re2 sdc = ss.reshape(Sz2{info.read_points, 1}).broadcast(Sz2{1, info.spokes});
  return sdc;
}

Re2 Radial3D(Trajectory const &traj, Index const lores, Index const gap)
{
  Log::Print(FMT_STRING("Calculating 2D radial analytic SDC"));
  auto const &info = traj.info();

  Eigen::ArrayXf ind = Eigen::ArrayXf::LinSpaced(info.read_points, 0, info.read_points - 1);
  Eigen::ArrayXf mergeHi = ind - (gap - 1);
  mergeHi = (mergeHi > 0).select(mergeHi, 0);
  mergeHi = (mergeHi < 1).select(mergeHi, 1);

  Eigen::ArrayXf mergeLo;
  if (lores) {
    float const scale =
      traj.point(info.read_points - 1, lores, 1.f).norm() / traj.point(info.read_points - 1, 0, 1.f).norm();
    mergeLo = ind / scale - (gap - 1);
    mergeLo = (mergeLo > 0).select(mergeLo, 0);
    mergeLo = (mergeLo < 1).select(mergeLo, 1);
    mergeLo = (1 - mergeLo) / scale; // Match intensities of k-space
    mergeLo.head(gap) = 0.;          // Don't touch these points
  }

  auto spoke_sdc = [&](Index const &spoke, Index const N) -> Re1 {
    // When k-space becomes undersampled need to flatten DC (Menon & Pipe 1999)
    float const R = (M_PI * info.matrix.maxCoeff() * info.matrix.maxCoeff()) / N;
    float const flat_start = info.read_points / sqrt(R);
    float const V = 1.f / (3. * (flat_start * flat_start) + 1. / 4.);
    Re1 sdc(info.read_points);
    for (Index ir = 0; ir < info.read_points; ir++) {
      float const rad = traj.point(ir, spoke, info.read_points).norm();
      float const merge = (spoke < lores) ? mergeLo(ir) : mergeHi(ir);
      if (rad == 0.f) {
        sdc(ir) = merge * V * 1.f / 8.f;
      } else if (rad < flat_start) {
        sdc(ir) = merge * V * (3.f * (rad * rad) + 1.f / 4.f);
      } else {
        sdc(ir) = merge;
      }
    }
    return sdc;
  };

  Re2 sdc(info.read_points, info.spokes);
  if (lores) {
    Re1 const ss = spoke_sdc(0, lores);
    sdc.slice(Sz2{0, 0}, Sz2{info.read_points, lores}) = ss.reshape(Sz2{info.read_points, 1}).broadcast(Sz2{1, lores});
  }
  Re1 const ss = spoke_sdc(lores, info.spokes - lores);
  sdc.slice(Sz2{0, lores}, Sz2{info.read_points, info.spokes - lores}) =
    ss.reshape(Sz2{info.read_points, 1}).broadcast(Sz2{1, info.spokes - lores});

  return sdc;
}

Re2 Radial(Trajectory const &traj, Index const lores, Index const gap)
{

  if (traj.info().type == Info::Type::ThreeD) {
    return Radial3D(traj, lores, gap);
  } else {
    return Radial2D(traj);
  }
}

std::unique_ptr<SDCOp> Choose(Opts &opts, Trajectory const &traj, float const os)
{
  Re2 sdc(traj.info().read_points, traj.info().spokes);
  auto const iname = opts.type.Get();
  if (iname == "" || iname == "none") {
    Log::Print(FMT_STRING("Using no density compensation"));
    auto const info = traj.info();
    return std::make_unique<SDCOp>(Sz2{info.read_points, info.spokes}, info.channels);
  } else if (iname == "pipe") {
    sdc = Pipe(traj, false, 2.1f, 40);
  } else if (iname == "pipenn") {
    sdc = Pipe(traj, true, os, 40);
  } else {
    HD5::Reader reader(iname);
    sdc = reader.readTensor<Re2>(HD5::Keys::SDC);
    auto const trajInfo = traj.info();
    if (sdc.dimension(0) != trajInfo.read_points || sdc.dimension(1) != trajInfo.spokes) {
      Log::Fail(
        FMT_STRING("SDC dimensions on disk {}x{} did not match info {}x{}"),
        sdc.dimension(0),
        sdc.dimension(1),
        trajInfo.read_points,
        trajInfo.spokes);
    }
  }
  return std::make_unique<SDCOp>(sdc.pow(opts.pow.Get()), traj.info().channels);
}

} // namespace SDC
} // namespace rl
