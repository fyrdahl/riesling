#pragma once

#include <args.hxx>
#include <vector>
#include <optional>

#include "types.hpp"
#include "trajectory.hpp"

extern args::Group global_group;
extern args::HelpFlag help;
extern args::Flag verbose;

void ParseCommand(args::Subparser &parser, args::Positional<std::string> &iname);
void ParseCommand(args::Subparser &parser);

struct Vector3fReader
{
  void operator()(std::string const &name, std::string const &value, Eigen::Vector3f &x);
};

template <typename T>
struct VectorReader
{
  void operator()(std::string const &name, std::string const &value, std::vector<T> &x);
};

struct Sz2Reader
{
  void operator()(std::string const &name, std::string const &value, rl::Sz2 &x);
};

struct Sz3Reader
{
  void operator()(std::string const &name, std::string const &value, rl::Sz3 &x);
};

auto ReadBasis(std::string const &basisFile) -> std::optional<rl::Re2>;

// Helper function to generate a good output name
std::string OutName(
  std::string const &iName, std::string const &oName, std::string const &suffix, std::string const &extension = "h5");

void WriteOutput(
  rl::Cx5 const &img,
  std::string const &iname,
  std::string const &oname,
  std::string const &suffix,
  bool const keepTrajectory,
  rl::Trajectory const &traj);

struct CoreOpts
{
  CoreOpts(args::Subparser &parser);
  args::Positional<std::string> iname;
  args::ValueFlag<std::string> oname, ktype;
  args::ValueFlag<float> osamp, fov;
  args::ValueFlag<Index> bucketSize;
  args::ValueFlag<std::string> basisFile;
  args::Flag keepTrajectory;
};
