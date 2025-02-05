#include "trajectory.hpp"

#include "tensorOps.hpp"

namespace rl {

Trajectory::Trajectory() {}

Trajectory::Trajectory(Info const &info, Re3 const &points)
  : info_{info}
  , points_{points}

{
  init();
}

Trajectory::Trajectory(HD5::Reader const &reader)
  : info_{reader.readInfo()}
  , points_{reader.readTensor<Re3>(HD5::Keys::Trajectory)}
{
  init();
}

void Trajectory::init()
{
  if (points_.dimension(0) < 1 || points_.dimension(0) > 3) {
    Log::Fail(FMT_STRING("Trajectory has {} dimensions"), points_.dimension(0));
  }

  float const maxCoord = Maximum(points_.abs());
  if (maxCoord > 0.5f) {
    Log::Fail(FMT_STRING("Maximum trajectory co-ordinate {} exceeded 0.5"), maxCoord);
  }
  Log::Print<Log::Level::Debug>(FMT_STRING("{}D Trajectory size {},{}"), nDims(), nSamples(), nTraces());
}

void Trajectory::write(HD5::Writer &writer) const
{
  writer.writeInfo(info_);
  writer.writeTensor(points_, HD5::Keys::Trajectory);
}

auto Trajectory::nDims() const -> Index { return points_.dimension(0); }

auto Trajectory::nSamples() const -> Index { return points_.dimension(1); }

auto Trajectory::nTraces() const -> Index { return points_.dimension(2); }

Info const &Trajectory::info() const { return info_; }

auto Trajectory::matrix(float const fov) const -> Sz3 {
  if (fov > 0) {
    Eigen::Array3l bigMatrix = (((fov / info_.voxel_size) / 2.f).floor() * 2).cast<Index>();
    Sz3 matrix;
    for (Index ii = 0; ii < 3; ii++) {
      matrix[ii] = bigMatrix[ii];
    }
    Log::Print<Log::Level::High>(FMT_STRING("Requested FOV {} from matrix {}, calculated {}"), fov, info_.matrix, matrix);
    return matrix;
  } else {
    return info_.matrix;
  }
}


Re3 const &Trajectory::points() const { return points_; }

Re1 Trajectory::point(int16_t const read, int32_t const spoke) const
{
  Re1 const p = points_.chip(spoke, 2).chip(read, 1);
  return p;
}

auto Trajectory::downsample(float const res, Index const lores, bool const shrink) const
  -> std::tuple<Trajectory, Index, Index>
{
  float const dsamp = res / info_.voxel_size.minCoeff();
  if (dsamp < 1.f) {
    Log::Fail(
      FMT_STRING("Downsample resolution {} is lower than input resolution {}"), res, info_.voxel_size.minCoeff());
  }
  auto dsInfo = info_;
  float scale = 1.f;
  if (shrink) {
    // Account for rounding
    std::transform(
      info_.matrix.begin(), info_.matrix.begin() + nDims(), dsInfo.matrix.begin(), [dsamp](Index const i) { return (i / dsamp); });
    scale = static_cast<float>(info_.matrix[0]) / dsInfo.matrix[0];
    dsInfo.voxel_size = info_.voxel_size * scale;
  }
  Index minSamp = nSamples(), maxSamp = 0;
  Re3 dsPoints(points_.dimensions());
  for (Index it = 0; it < nTraces(); it++) {
    for (Index is = 0; is < nSamples(); is++) {
      Re1 p = points_.chip<2>(it).chip<1>(is);
      if (Norm(p) <= 0.5f / dsamp) {
        dsPoints.chip<2>(it).chip<1>(is) = p * scale;
        if (it >= lores) { // Ignore lo-res traces for this calculation
          minSamp = std::min(minSamp, is);
          maxSamp = std::max(maxSamp, is);
        }
      } else {
        dsPoints.chip<2>(it).chip<1>(is).setConstant(std::numeric_limits<float>::quiet_NaN());
      }
    }
  }
  Index const dsSamples = maxSamp + 1 - minSamp;
  Log::Print(
    FMT_STRING("Downsample res {} mm, factor {}, matrix {}, voxel-size {} mm, read-points {}-{}{}"),
    res,
    dsamp,
    dsInfo.matrix,
    dsInfo.voxel_size.transpose(),
    minSamp,
    maxSamp,
    lores > 0 ? fmt::format(FMT_STRING(", ignoring {} lo-res traces"), lores) : "");
  dsPoints = Re3(dsPoints.slice(Sz3{0, minSamp, 0}, Sz3{nDims(), dsSamples, nTraces()}));
  Log::Print(FMT_STRING("Downsampled trajectory dims {}"), dsPoints.dimensions());
  return std::make_tuple(Trajectory(dsInfo, dsPoints), minSamp, dsSamples);
}

auto Trajectory::downsample(Cx5 const &ks, float const res, Index const lores, bool const shrink) const
  -> std::tuple<Trajectory, Cx5>
{
  auto const [dsTraj, minSamp, nSamp] = downsample(res, lores, shrink);
  Cx5 dsKs =
    ks.slice(Sz5{0, minSamp, 0, 0, 0}, Sz5{ks.dimension(0), nSamp, ks.dimension(2), ks.dimension(3), ks.dimension(4)});
  return std::make_tuple(dsTraj, dsKs);
}

auto Trajectory::downsample(Cx4 const &ks, float const res, Index const lores, bool const shrink) const
  -> std::tuple<Trajectory, Cx4>
{
  auto const [dsTraj, minSamp, nSamp] = downsample(res, lores, shrink);
  Cx4 dsKs = ks.slice(Sz4{0, minSamp, 0, 0}, Sz4{ks.dimension(0), nSamp, ks.dimension(2), ks.dimension(3)});
  return std::make_tuple(dsTraj, dsKs);
}

} // namespace rl
