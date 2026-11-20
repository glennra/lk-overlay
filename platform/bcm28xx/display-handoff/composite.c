#include <platform/bcm28xx/display_handoff.h>
#include <platform/bcm28xx/arm_chainloader.h>
#include <stdio.h>

#define COMPOSITE_FB_BASE 0x07800000u
#define COMPOSITE_FB_SIZE 0x00400000u
#define COMPOSITE_WIDTH 720u
#define COMPOSITE_HEIGHT 576u
#define COMPOSITE_STRIDE (COMPOSITE_WIDTH * 4u)

int bcm_vec_init(int standard);

int bcm_display_probe_composite(void) { return BCM_DISPLAY_PROBE_PRESENT; }

int bcm_display_init_composite(void) { return bcm_vec_init(0); }

int bcm_display_get_handoff_composite(struct bcm_display_handoff *out) {
  if (!out) return BCM_DISPLAY_INVALID_FRAMEBUFFER;
  *out = (struct bcm_display_handoff) {
    .backend = BCM_DISPLAY_COMPOSITE,
    .fb_arm_phys = COMPOSITE_FB_BASE, .fb_bus_addr = COMPOSITE_FB_BASE,
    .fb_primary_phys = COMPOSITE_FB_BASE, .fb_back_phys = COMPOSITE_FB_BASE + 0x00100000u,
    .width = COMPOSITE_WIDTH, .height = COMPOSITE_HEIGHT,
    .pitch = COMPOSITE_STRIDE, .pixel_format = ARM_CHAINLOADER_FORMAT_A8R8G8B8,
    .buffer_size = COMPOSITE_STRIDE * COMPOSITE_HEIGHT,
    .reserved_base = COMPOSITE_FB_BASE, .reserved_size = COMPOSITE_FB_SIZE,
  };
  return BCM_DISPLAY_OK;
}

void bcm_display_run_boot_animation_composite(void) {
  /* VEC is interlaced: the adapter must advance only at complete-frame cadence.
   * The shared takeover protocol is not asserted until a field-boundary event
   * and a Linux write have both been observed on hardware. */
  puts("VEC: animation adapter pending hardware field-boundary validation");
  for (;;) __asm__ volatile("nop");
}
