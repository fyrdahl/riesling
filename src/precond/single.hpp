#pragma once

#include "../trajectory.hpp"
#include "precond.hpp"

#include <optional>

namespace rl {

struct SingleChannel final : Precond<Cx3>
{
  SingleChannel(Trajectory const &traj);
  Cx3 apply(Cx3 const &in) const;
  Cx3 inv(Cx3 const &in) const;

  Re3 pre_;
};

}
