#pragma once

#include "func/multiply.hpp"
#include "log.hpp"
#include "parse_args.hpp"
#include "trajectory.hpp"
#include "types.hpp"

namespace rl {

namespace SDC {

template <int ND>
auto Pipe(Trajectory const &traj, std::string const &ktype = "ES3", float const os = 2.f, Index const max_its = 10, float const pow = 1.)
  -> Re2;
auto Radial(Trajectory const &traj, Index const lores, Index const gap) -> Re2;

struct Opts
{
  Opts(args::Subparser &parser);
  args::ValueFlag<std::string> type;
  args::ValueFlag<float> pow;
  args::ValueFlag<Index> maxIterations;
};

auto Choose(Opts &opts, Trajectory const &t, Index const nC, std::string const &ktype, float const os)
  -> std::shared_ptr<Functor<Cx3>>;

} // namespace SDC
} // namespace rl
