#pragma once

#include "common.hpp"
#include "signals.hpp"
#include "threads.hpp"

namespace rl {
/*
 * Wrapper for solving normal equations
 */
template <typename Op>
struct NormalEqOp
{
  using Input = typename Op::Input;
  using InputMap = typename Op::InputMap;

  std::shared_ptr<Op> op;
  NormalEqOp(std::shared_ptr<Op> o)
    : op{o}
  {
  }

  auto inputDimensions() const { return op->inputDimensions(); }

  auto outputDimensions() const { return op->inputDimensions(); }

  auto forward(Input const &x) const -> InputMap
  {
    op->input() = x;
    return op->adjfwd(op->input());
  }
};

template <typename Op>
auto make_normal(std::shared_ptr<Op> op)
{
  return std::make_shared<NormalEqOp<Op>>(op);
}

template <typename Op>
struct ConjugateGradients
{
  using Input = typename Op::Input;
  using InputMap = typename Op::InputMap;
  std::shared_ptr<Op> op;
  Index iterLimit = 16;
  float resTol = 1.e-6f;
  bool debug = false;

  Input run(InputMap const b, Input const &x0 = Input()) const
  {
    auto dev = Threads::GlobalDevice();
    // Allocate all memory
    CheckDimsEqual(op->outputDimensions(), b.dimensions());
    auto const dims = op->inputDimensions();
    Input q(dims), p(dims), r(dims), x(dims);
    // If we have an initial guess, use it
    if (x0.size()) {
      CheckDimsEqual(dims, x0.dimensions());
      Log::Print("Warm-start CG");
      r.device(dev) = b - op->forward(x0);
      x.device(dev) = x0;
    } else {
      r.device(dev) = b;
      x.setZero();
    }
    p.device(dev) = r;
    float r_old = Norm2(r);
    float const thresh = resTol * sqrt(r_old);
    Log::Print(FMT_STRING("CG |r| {:5.3E} threshold {:5.3E}"), sqrt(r_old), thresh);
    Log::Print(FMT_STRING("IT |r|       α         β         |x|"));
    PushInterrupt();
    for (Index icg = 0; icg < iterLimit; icg++) {
      q = op->forward(p);
      float const alpha = r_old / CheckedDot(p, q);
      x.device(dev) = x + p * p.constant(alpha);
      if (debug) {
        Log::Tensor(x, fmt::format(FMT_STRING("cg-x-{:02}"), icg));
        Log::Tensor(r, fmt::format(FMT_STRING("cg-r-{:02}"), icg));
      }
      r.device(dev) = r - q * q.constant(alpha);
      float const r_new = Norm2(r);
      float const beta = r_new / r_old;
      p.device(dev) = r + p * p.constant(beta);
      float const nr = sqrt(r_new);
      Log::Print(FMT_STRING("{:02d} {:5.3E} {:5.3E} {:5.3E} {:5.3E}"), icg, nr, alpha, beta, Norm(x));
      if (nr < thresh) {
        Log::Print(FMT_STRING("Reached convergence threshold"));
        break;
      }
      r_old = r_new;
      if (InterruptReceived()) {
        break;
      }
    }
    PopInterrupt();
    return x;
  }
};

} // namespace rl
