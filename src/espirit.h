#pragma once

#include "log.h"
#include "op/grids.h"

Cx4 ESPIRIT(
  GridBase *grid,
  Cx3 const &data,
  Index const kernelRad,
  Index const calRad,
  Index const gap,
  float const thresh);
