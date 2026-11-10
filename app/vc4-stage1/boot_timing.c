#include "boot_timing.h"

#include <stdbool.h>
#include <stdint.h>
#include <dev/gpio.h>
#include <platform/bcm28xx/gpio.h>

#ifndef STAGE1_GPIO_TIMING
#define STAGE1_GPIO_TIMING 0
#endif

/* Reuse the three WO-042 pins. Stage 2 clears them before its own markers. */
#define STAGE1_GPIO_ENTRY 22u
#define STAGE1_GPIO_LOAD 27u
#define STAGE1_GPIO_COMPLETE 17u

#if STAGE1_GPIO_TIMING
static bool stage1_gpio_initialized;

static void stage1_gpio_init(void) {
  if (stage1_gpio_initialized)
    return;
  gpio_config(STAGE1_GPIO_ENTRY, kBCM2708PinmuxOut);
  gpio_config(STAGE1_GPIO_LOAD, kBCM2708PinmuxOut);
  gpio_config(STAGE1_GPIO_COMPLETE, kBCM2708PinmuxOut);
  gpio_set(STAGE1_GPIO_ENTRY, 0);
  gpio_set(STAGE1_GPIO_LOAD, 0);
  gpio_set(STAGE1_GPIO_COMPLETE, 0);
  stage1_gpio_initialized = true;
}
#endif

void stage1_boot_timing_mark(enum stage1_boot_timing_event event) {
#if STAGE1_GPIO_TIMING
  stage1_gpio_init();
  switch (event) {
    case STAGE1_BOOT_T0_APP_ENTRY:
      gpio_set(STAGE1_GPIO_ENTRY, 1);
      break;
    case STAGE1_BOOT_T1_SD_LOAD_BEGIN:
      gpio_set(STAGE1_GPIO_LOAD, 1);
      break;
    case STAGE1_BOOT_T2_STAGE2_LOAD_COMPLETE:
      gpio_set(STAGE1_GPIO_COMPLETE, 1);
      break;
  }
#else
  (void)event;
#endif
}
