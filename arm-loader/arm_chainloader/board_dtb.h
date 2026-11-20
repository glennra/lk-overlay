#pragma once

#include <stdint.h>

static inline int board_is_pi3bplus(uint32_t revision) {
  return (revision & (1U << 23)) && ((revision >> 12) & 0xfU) == 2U &&
         ((revision >> 4) & 0xffU) == 0x0dU;
}

static inline int board_is_pi3b(uint32_t revision) {
  return (revision & (1U << 23)) && ((revision >> 12) & 0xfU) == 2U &&
         ((revision >> 4) & 0xffU) == 0x08U;
}
