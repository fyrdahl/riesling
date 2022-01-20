#pragma once

#include "fft_util.h"
#include "types.h"

namespace FFT {

/* Templated wrapped around the fftw_plan_many_dft interface
 * Takes two template arguments - TensorRank and FFTRank. The last FFTRank dimensions will
 * be transformed, and the first (TensorRank - FFTRank) will not be transformed.
 */
template <int TensorRank, int FFTRank>
struct Planned
{
  static_assert(FFTRank <= TensorRank);
  using Tensor = Eigen::Tensor<Cx, TensorRank>;
  using TensorDims = typename Tensor::Dimensions;

  /*! Uses the specified Tensor as a workspace during planning
   */
  Planned(Tensor &workspace, Index const nThreads = Threads::GlobalThreadCount());

  /*! Will allocate a workspace during planning
   */
  Planned(TensorDims const &dims, Index const nThreads = Threads::GlobalThreadCount());

  ~Planned();

  void forward(Tensor &x) const; //!< Image space to k-space
  void reverse(Tensor &x) const; //!< K-space to image space

  float scale() const; //!< Return the scaling for the unitary transform

private:
  void plan(Tensor &x, Index const nThreads);
  void applyPhase(Tensor &x, float const scale, bool const forward) const;

  TensorDims dims_;
  std::array<Cx1, FFTRank> phase_;
  fftwf_plan forward_plan_, reverse_plan_;
  float scale_;

  bool threaded_;
};

using ThreeD = Planned<3, 3>;
using TwoDMulti = Planned<3, 2>;

} // namespace FFT
