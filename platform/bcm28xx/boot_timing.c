#include <platform/bcm28xx/boot_timing.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/gpio.h>
#include <platform/bcm28xx/udelay.h>
#include <dev/gpio.h>
#include <lk/reg.h>
#include <stdio.h>

#ifndef DSI_BOOT_TIMING
#define DSI_BOOT_TIMING 0
#endif

#ifndef DSI_GPIO_TIMING
#define DSI_GPIO_TIMING 0
#endif

#define BCM_BOOT_GPIO_STAGE2 22u
#define BCM_BOOT_GPIO_DSI 27u
#define BCM_BOOT_GPIO_FRAME 17u

#define BCM_BOOT_TIMING_MAGIC 0x574f3432u

#if DSI_BOOT_TIMING
static struct bcm_boot_timing_trace trace = { .magic = BCM_BOOT_TIMING_MAGIC };
#endif

#if DSI_GPIO_TIMING
static bool gpio_markers_ready;

static void gpio_marker_init(void) {
  if (gpio_markers_ready)
    return;
  gpio_config(BCM_BOOT_GPIO_STAGE2, kBCM2708PinmuxOut);
  gpio_config(BCM_BOOT_GPIO_DSI, kBCM2708PinmuxOut);
  gpio_config(BCM_BOOT_GPIO_FRAME, kBCM2708PinmuxOut);
  gpio_set(BCM_BOOT_GPIO_STAGE2, 0);
  gpio_set(BCM_BOOT_GPIO_DSI, 0);
  gpio_set(BCM_BOOT_GPIO_FRAME, 0);
  gpio_markers_ready = true;
  /* Make the stage-1-to-stage-2 falling edges visible on the scope. */
  udelay(1000);
}

static void gpio_marker_event(uint32_t event_id) {
  gpio_marker_init();
  if (event_id == BCM_BOOT_T0_PLATFORM_INIT)
    gpio_set(BCM_BOOT_GPIO_STAGE2, 1);
  else if (event_id == BCM_BOOT_T4_DSI_INIT_BEGIN)
    gpio_set(BCM_BOOT_GPIO_DSI, 1);
  else if (event_id == BCM_BOOT_T10_FIRST_FRAME_READY)
    gpio_set(BCM_BOOT_GPIO_FRAME, 1);
}
#endif

void bcm_boot_timing_mark(uint32_t event_id) {
#if DSI_GPIO_TIMING
  gpio_marker_event(event_id);
#endif
#if DSI_BOOT_TIMING
  uint32_t count = trace.count;
  if (count < (sizeof(trace.events) / sizeof(trace.events[0]))) {
    trace.events[count].event_id = event_id;
    trace.events[count].timestamp = *REG32(ST_CLO);
    trace.count = count + 1;
  }
#else
  (void)event_id;
#endif
}

void bcm_boot_timing_dump(void) {
#if DSI_BOOT_TIMING
  printf("WO042_TIMING magic=0x%08x count=%u clock=ST_CLO_US\n",
         trace.magic, trace.count);
  for (uint32_t i = 0; i < trace.count; ++i)
    printf("WO042_TIMING event=%u timestamp=%u\n",
           trace.events[i].event_id, trace.events[i].timestamp);
#endif
}
