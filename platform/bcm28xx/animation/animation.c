#include <platform/bcm28xx/animation.h>
#include <string.h>

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
  /* Match the original DSI sprite: opaque red with an 8-pixel white inset. */
  for (uint32_t y = 0; y < s->height; ++y) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->cpu_address + (size_t)y * s->stride_bytes);
    for (uint32_t x = 0; x < s->width; ++x)
      line[x] = x >= 8u && y >= 8u && x + 8u < s->width && y + 8u < s->height
          ? 0xffffffffu : 0xffff0000u;
  }
  ++a->frame; return 0;
}

int bcm_animation_request_takeover(bcm_animation *a) {
  if (!a || a->state != BCM_ANIMATION_ANIMATING) return -1;
  a->state = BCM_ANIMATION_TAKEOVER_PENDING; return 0;
}

int bcm_animation_stop(bcm_animation *a) {
  if (!a || a->state == BCM_ANIMATION_UNSELECTED || a->state == BCM_ANIMATION_FAILED) return -1;
  a->stopped = 1; a->state = BCM_ANIMATION_LINUX_OWNED; return 0;
}
