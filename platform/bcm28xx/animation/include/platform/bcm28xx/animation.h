#pragma once

#include <stddef.h>
#include <stdint.h>

/* The renderer deliberately knows nothing about HVS, PixelValve or Linux. */
enum bcm_animation_format { BCM_ANIMATION_ARGB8888 = 1 };

typedef struct {
  uint32_t width, height, stride_bytes, pixel_format;
  uintptr_t cpu_address, dma_bus_address;
  size_t allocation_bytes;
  uint32_t safe_x, safe_y, safe_width, safe_height;
  uint32_t flip_vertical;
} bcm_animation_surface;

enum bcm_animation_state {
  BCM_ANIMATION_UNSELECTED = 0, BCM_ANIMATION_PROBED,
  BCM_ANIMATION_INITIALIZED, BCM_ANIMATION_ANIMATING,
  BCM_ANIMATION_ARM_RELEASED, BCM_ANIMATION_TAKEOVER_PENDING,
  BCM_ANIMATION_LINUX_OWNED, BCM_ANIMATION_FAILED,
};

typedef struct {
  enum bcm_animation_state state;
  uint32_t frame, frame_period;
  bcm_animation_surface surface;
  int stopped;
} bcm_animation;

int bcm_animation_init(bcm_animation *, const bcm_animation_surface *, uint32_t);
int bcm_animation_start(bcm_animation *);
int bcm_animation_arm_released(bcm_animation *);
int bcm_animation_render(bcm_animation *);
void bcm_animation_fit(uint32_t display_width, uint32_t display_height,
                       uint32_t *width, uint32_t *height);
uint32_t bcm_animation_frame_duration_ms(const bcm_animation *);
int bcm_animation_request_takeover(bcm_animation *);
int bcm_animation_stop(bcm_animation *);
const char *bcm_animation_state_name(enum bcm_animation_state);
