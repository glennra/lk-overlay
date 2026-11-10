#pragma once

#include <stdint.h>

/* Optional WO-042 startup trace. Set DSI_BOOT_TIMING=1 at build time. */
enum bcm_boot_timing_event_id {
  BCM_BOOT_T0_PLATFORM_INIT = 1,
  BCM_BOOT_T3_LK_ENTRY,
  BCM_BOOT_T4_DSI_INIT_BEGIN,
  BCM_BOOT_T5_PANEL_POWER_BEGIN,
  BCM_BOOT_T6_BRIDGE_PANEL_READY,
  BCM_BOOT_T7_HVS_DISPLAY_LIST_READY,
  BCM_BOOT_T8_SCANOUT_ENABLED,
  BCM_BOOT_T9_BACKLIGHT_ENABLED,
  BCM_BOOT_T10_FIRST_FRAME_READY,
  BCM_BOOT_T11_ARM_HANDOFF,
};

struct bcm_boot_timing_event {
  uint32_t event_id;
  uint32_t timestamp;
};

struct bcm_boot_timing_trace {
  uint32_t magic;
  uint32_t count;
  struct bcm_boot_timing_event events[64];
};

void bcm_boot_timing_mark(uint32_t event_id);
void bcm_boot_timing_dump(void);
