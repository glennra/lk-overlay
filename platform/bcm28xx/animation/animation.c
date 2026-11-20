#include <platform/bcm28xx/animation.h>
#include <string.h>
#include "wo046_animation_data.h"

static int surface_valid(const bcm_animation_surface *s) {
  return s && s->cpu_address && s->width && s->height &&
      s->pixel_format == BCM_ANIMATION_ARGB8888 &&
      s->stride_bytes >= s->width * 4u &&
      s->allocation_bytes >= (size_t)s->stride_bytes * s->height;
}

const char *bcm_animation_state_name(enum bcm_animation_state state) {
  static const char *const names[] = { "UNSELECTED", "PROBED", "INITIALIZED",
    "ANIMATING", "ARM_RELEASED", "TAKEOVER_PENDING", "LINUX_OWNED", "FAILED" };
  return state <= BCM_ANIMATION_FAILED ? names[state] : "INVALID";
}

int bcm_animation_init(bcm_animation *a, const bcm_animation_surface *s,
                       uint32_t period) {
  if (!a || !surface_valid(s) || !period) return -1;
  memset(a, 0, sizeof(*a)); a->surface = *s; a->frame_period = period;
  a->state = BCM_ANIMATION_INITIALIZED; return 0;
}

int bcm_animation_start(bcm_animation *a) {
  if (!a || (a->state != BCM_ANIMATION_INITIALIZED &&
             a->state != BCM_ANIMATION_ARM_RELEASED)) return -1;
  a->state = BCM_ANIMATION_ANIMATING; return 0;
}

int bcm_animation_arm_released(bcm_animation *a) {
  if (!a || a->state != BCM_ANIMATION_INITIALIZED) return -1;
  a->state = BCM_ANIMATION_ARM_RELEASED; return 0;
}

int bcm_animation_render(bcm_animation *a) {
  bcm_animation_surface *s;
  if (!a || a->state != BCM_ANIMATION_ANIMATING || !surface_valid(&a->surface)) return -1;
  s = &a->surface;
  unsigned frame = a->frame % WO046_FRAMES;
  uint32_t sy = 0, y_error = 0;
  uint32_t y_step = WO046_HEIGHT / s->height, y_rem = WO046_HEIGHT % s->height;
  uint32_t x_step = WO046_WIDTH / s->width, x_rem = WO046_WIDTH % s->width;
  for (uint32_t y = 0; y < s->height; ++y) {
    uint32_t output_y = s->flip_vertical ? s->height - 1u - y : y;
    uint32_t *line = (uint32_t *)((uint8_t *)s->cpu_address + (size_t)output_y * s->stride_bytes);
    uint32_t sx = 0, x_error = 0;
    const uint32_t *run = &wo046_runs[wo046_row_offsets[frame * WO046_HEIGHT + sy]];
    for (uint32_t x = 0; x < s->width; ++x)
    {
      while (sx >= (*run >> 8)) ++run;
      uint32_t color = wo046_palette[*run & 255u];
      if (!a->frame || line[x] != color) line[x] = color;
      sx += x_step;
      x_error += x_rem;
      if (x_error >= s->width) { ++sx; x_error -= s->width; }
    }
    sy += y_step;
    y_error += y_rem;
    if (y_error >= s->height) { ++sy; y_error -= s->height; }
  }
  ++a->frame; return 0;
}

void bcm_animation_fit(uint32_t display_width, uint32_t display_height,
                       uint32_t *width, uint32_t *height) {
  uint32_t w = display_width < WO046_WIDTH ? display_width : WO046_WIDTH;
  uint32_t h = w * WO046_HEIGHT / WO046_WIDTH;
  if (h > display_height) {
    h = display_height;
    w = h * WO046_WIDTH / WO046_HEIGHT;
  }
  if (width) *width = w;
  if (height) *height = h;
}

uint32_t bcm_animation_frame_duration_ms(const bcm_animation *a) {
  if (!a || !a->frame) return 0;
  return wo046_durations_ms[(a->frame - 1u) % WO046_FRAMES];
}

int bcm_animation_request_takeover(bcm_animation *a) {
  if (!a || a->state != BCM_ANIMATION_ANIMATING) return -1;
  a->state = BCM_ANIMATION_TAKEOVER_PENDING; return 0;
}

int bcm_animation_stop(bcm_animation *a) {
  if (!a || a->state == BCM_ANIMATION_UNSELECTED || a->state == BCM_ANIMATION_FAILED) return -1;
  a->stopped = 1; a->state = BCM_ANIMATION_LINUX_OWNED; return 0;
}
