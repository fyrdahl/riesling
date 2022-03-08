#include "../../src/op/nufft.hpp"
#include "../../src/precond/sdc.hpp"
#include "../../src/sdc.h"
#include "../../src/tensorOps.h"
#include "../../src/traj_spirals.h"
#include "../../src/trajectory.h"

#include <catch2/catch.hpp>

TEST_CASE("ops-nufft")
{
  Index const M = 16;
  float const os = 2.f;
  Info const info{
    .type = Info::Type::ThreeD,
    .matrix = Eigen::Array3l::Constant(M),
    .channels = 1,
    .read_points = Index(os * M / 2),
    .spokes = Index(M * M),
    .volumes = 1,
    .frames = 1,
    .tr = 1.f,
    .voxel_size = Eigen::Array3f::Constant(1.f),
    .origin = Eigen::Array3f::Constant(0.f),
    .direction = Eigen::Matrix3f::Identity()};
  auto const points = ArchimedeanSpiral(info.read_points, info.spokes);
  Trajectory const traj(info, points);
  auto const nn = make_kernel("NN", info.type, os);
  auto const m1 = traj.mapping(1, os);
  auto grid = make_grid(nn.get(), m1, false);
  SDCPrecond sdc{SDC::Pipe(traj, true, os)};
  auto nufft = NUFFTOp(Sz3{M, M, M}, grid.get(), &sdc);
  nufft.calcToeplitz();
  auto const dims = nufft.inputDimensions();
  Cx5 x(dims), y(dims);
  Cx3 r(info.channels, info.read_points, info.spokes);

  /* See note in grid.cpp - classic Dot test isn't appropriate.
   * Give this test a decent amount of tolerance because the cropping/padding means we don't
   * get back exactly what we should
   */
  SECTION("SDC-Full")
  {
    x.setRandom();
    y = nufft.AdjA(x);
    auto const xy = Dot(x, y);
    auto const yy = Dot(y, y);
    CHECK(std::abs((yy - xy) / (yy + xy + 1.e-15f)) == Approx(0).margin(1.e-1));
  }
}