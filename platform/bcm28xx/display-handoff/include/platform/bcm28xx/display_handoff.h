#pragma once

#include <stdint.h>

enum bcm_display_backend_id {
  BCM_DISPLAY_HDMI = 1,
  BCM_DISPLAY_DSI = 2,
};

enum bcm_display_error {
  BCM_DISPLAY_OK = 0,
  BCM_DISPLAY_NOT_READY,
  BCM_DISPLAY_EDID_UNAVAILABLE,
  BCM_DISPLAY_UNSUPPORTED_MODE,
  BCM_DISPLAY_PANEL_INIT_FAILED,
  BCM_DISPLAY_INVALID_FRAMEBUFFER,
};

enum bcm_display_probe_result {
  BCM_DISPLAY_PROBE_PRESENT = 1,
  BCM_DISPLAY_PROBE_ABSENT = 2,
  BCM_DISPLAY_PROBE_UNKNOWN = 3,
  BCM_DISPLAY_PROBE_ERROR = 4,
};

struct bcm_display_handoff {
  enum bcm_display_backend_id backend;
  uint32_t fb_arm_phys;
  uint32_t fb_bus_addr;
  uint32_t fb_primary_phys;
  uint32_t fb_back_phys;
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  uint32_t pixel_format;
  uint32_t buffer_size;
  uint32_t reserved_base;
  uint32_t reserved_size;
};

int bcm_display_get_handoff(struct bcm_display_handoff *out);
int bcm_display_manager_init(void);
int bcm_display_selected(void);
const char *bcm_display_backend_name(void);
void bcm_display_run_boot_animation(void);
