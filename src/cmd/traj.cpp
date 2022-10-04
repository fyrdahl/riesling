#include "types.hpp"

#include "fft/fft.hpp"
#include "io/hd5.hpp"
#include "log.hpp"
#include "op/gridBase.hpp"
#include "parse_args.hpp"
#include "sdc.hpp"
#include "tensorOps.hpp"
#include "threads.hpp"
#include <complex>

using namespace rl;

int main_traj(args::Subparser &parser)
{
  CoreOpts core(parser);
  SDC::Opts sdcOpts(parser);

  args::Flag savePSF(parser, "PSF", "Write out Point-Spread-Function", {"psf", 'p'});

  ParseCommand(parser, core.iname);

  HD5::Reader reader(core.iname.Get());
  Trajectory traj(reader);
  auto const basis = ReadBasis(core.basisFile);
  auto gridder = make_grid<Cx, 3>(traj, core.ktype.Get(), core.osamp.Get(), 1, basis);
  auto const sdc = SDC::Choose(sdcOpts, traj, 1, core.ktype.Get(), core.osamp.Get());
  Cx3 rad_ks(1, traj.nSamples(), traj.nTraces());
  rad_ks.setConstant(1.0f);
  Cx4 out = gridder->adjoint(sdc->adjoint(rad_ks)).chip<0>(0);
  auto const fname = OutName(core.iname.Get(), core.oname.Get(), "traj", "h5");
  HD5::Writer writer(fname);
  writer.writeTensor(
    Cx5(out.reshape(Sz5{out.dimension(0), out.dimension(1), out.dimension(2), out.dimension(3), 1})), "traj-image");

  if (savePSF) {
    Log::Print(FMT_STRING("Calculating PSF"));
    auto const fft = FFT::Make<4, 3>(out.dimensions());
    fft->reverse(out);
    writer.writeTensor(
      Cx5(out.reshape(Sz5{out.dimension(0), out.dimension(1), out.dimension(2), out.dimension(3), 1})), "psf-image");
  }

  return EXIT_SUCCESS;
}
