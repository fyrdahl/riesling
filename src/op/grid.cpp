#include "grid.h"
#include "grid-kb.h"
#include "grid-nn.h"

#include "../tensorOps.h"
#include "../threads.h"

#include <algorithm>
#include <cfenv>
#include <cmath>

GridOp::GridOp(Mapping map, bool const unsafe, Log &log)
  : GridBase(map, unsafe, log)
{
}

Sz3 GridOp::outputDimensions() const
{
  return mapping_.noncartDims;
}

Cx4 GridOp::newMultichannel(long const nc) const
{
  Cx4 g(nc, mapping_.cartDims[0], mapping_.cartDims[1], mapping_.cartDims[2]);
  g.setZero();
  return g;
}

std::unique_ptr<GridOp> make_grid(
  Trajectory const &traj,
  float const os,
  Kernels const k,
  bool const fastgrid,
  Log &log,
  float const res,
  bool const shrink)
{
  switch (k) {
  case Kernels::NN:
    return std::make_unique<GridNN>(traj, os, fastgrid, log, res, shrink);
  case Kernels::KB3:
    if (traj.info().type == Info::Type::ThreeD) {
      return std::make_unique<GridKB<3, 3>>(traj, os, fastgrid, log, res, shrink);
    } else {
      return std::make_unique<GridKB<3, 1>>(traj, os, fastgrid, log, res, shrink);
    }
  case Kernels::KB5:
    if (traj.info().type == Info::Type::ThreeD) {
      return std::make_unique<GridKB<5, 5>>(traj, os, fastgrid, log, res, shrink);
    } else {
      return std::make_unique<GridKB<5, 1>>(traj, os, fastgrid, log, res, shrink);
    }
  }
  __builtin_unreachable();
}

std::unique_ptr<GridOp>
make_grid(Mapping const &mapping, Kernels const k, bool const fastgrid, Log &log)
{
  switch (k) {
  case Kernels::NN:
    return std::make_unique<GridNN>(mapping, fastgrid, log);
  case Kernels::KB3:
    if (mapping.type == Info::Type::ThreeD) {
      return std::make_unique<GridKB<3, 3>>(mapping, fastgrid, log);
    } else {
      return std::make_unique<GridKB<3, 1>>(mapping, fastgrid, log);
    }
  case Kernels::KB5:
    if (mapping.type == Info::Type::ThreeD) {
      return std::make_unique<GridKB<5, 5>>(mapping, fastgrid, log);
    } else {
      return std::make_unique<GridKB<5, 1>>(mapping, fastgrid, log);
    }
  }
  __builtin_unreachable();
}
