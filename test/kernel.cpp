#include "kernel/expsemi.hpp"
#include "kernel/kaiser.hpp"
#include "kernel/nn.hpp"
#include "kernel/radial.hpp"
#include "kernel/rectilinear.hpp"
#include "kernel/triangle.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include "log.hpp"

using namespace Catch;

TEMPLATE_TEST_CASE(
  "1D Kernels",
  "[kernels]",
  (rl::Radial<1, rl::KaiserBessel<3>>),
  (rl::Radial<1, rl::KaiserBessel<5>>),
  (rl::Rectilinear<1, rl::ExpSemi<3>>),
  (rl::Rectilinear<1, rl::ExpSemi<5>>))
{
  TestType kernel(2.f);
  typename TestType::Point p;
  p.setConstant(0.f);
  auto const k0 = kernel(p);
  INFO(rl::Transpose(k0));
  CHECK(rl::Norm(k0) == Approx(1.f).margin(1.e-9));
  CHECK(k0(0) == Approx(0.f).margin(1.e-9));
  p.setConstant(0.5f);
  auto const k1 = kernel(p);
  INFO(rl::Transpose(k1));
  CHECK(k1(0) == Approx(0.f).margin(1.e-9));
  CHECK(k1(1) == Approx(k1(TestType::PadWidth - 1)).margin(1.e-5));
}
