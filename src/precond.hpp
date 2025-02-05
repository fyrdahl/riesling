#pragma once

#include "func/functor.hpp"
#include "trajectory.hpp"
#include <optional>

namespace rl {

auto KSpaceSingle(Trajectory const &traj, std::optional<Re2> const &basis = std::nullopt, float const bias = 1.f) -> Re2;

std::shared_ptr<Functor1<Cx4>>
make_pre(std::string const &type, Trajectory const &traj, std::optional<Re2> const &basis, float const bias);

} // namespace rl
