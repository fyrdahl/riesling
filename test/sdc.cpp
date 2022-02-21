#include "../src/sdc.h"
#include "../src/log.h"
#include "../src/traj_spirals.h"
#include "../src/trajectory.h"
#include <catch2/catch.hpp>

TEST_CASE("SDC")
{
  Index const M = 32;
  float const os = 2.f;
  Info const info{
    .type = Info::Type::ThreeD,
    .matrix = Eigen::Array3l::Constant(M),
    .channels = 1,
    .read_points = Index(os * M / 2),
    .spokes = Index(M * M / 4),
    .volumes = 1,
    .echoes = 1,
    .tr = 1.f,
    .voxel_size = Eigen::Array3f::Constant(1.f),
    .origin = Eigen::Array3f::Constant(0.f),
    .direction = Eigen::Matrix3f::Identity()};
  auto const points = ArchimedeanSpiral(info.read_points, info.spokes);
  Trajectory const traj(info, points);

  Log::SetLevel(Log::Level::Info);
  SECTION("Pipe")
  {
    R2 sdc = SDC::Pipe(traj, false, 2.1f);
    CHECK(sdc.dimension(0) == info.read_points);
    CHECK(sdc.dimension(1) == info.spokes);
    // Central points should be very small
    CHECK(sdc(0, 0) == Approx(0.00129f).margin(1.e-4f));
    CHECK(sdc(1, 0) == Approx(0.00519f).margin(1.e-4f));
    // Undersampled points should be close to one
    CHECK(sdc(25, 0) == Approx(0.99172f).margin(1.e-1f));
    CHECK(sdc(26, 0) == Approx(1.08636f).margin(1.e-1f));
    // Point excluded by margin at edge of grid
    CHECK(sdc(31, 0) == Approx(0.0f).margin(1.e-4f));
  }

  SECTION("Pipe-NN")
  {
    R2 sdc = SDC::Pipe(traj, true, 2.1f);
    CHECK(sdc.dimension(0) == info.read_points);
    CHECK(sdc.dimension(1) == info.spokes);
    // Central points should be very small
    CHECK(sdc(0, 0) == Approx(0.00129f).margin(1.e-4f));
    CHECK(sdc(1, 0) == Approx(0.00519f).margin(1.e-4f));
    // Undersampled points should be close to one
    CHECK(sdc(25, 0) == Approx(0.99172f).margin(1.e-1f));
    CHECK(sdc(26, 0) == Approx(1.08636f).margin(1.e-1f));
    // Point excluded by margin at edge of grid
    CHECK(sdc(31, 0) == Approx(0.0f).margin(1.e-4f));
  }
  Log::SetLevel(Log::Level::None);
}
