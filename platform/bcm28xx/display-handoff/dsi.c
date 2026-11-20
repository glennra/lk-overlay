#include <platform/bcm28xx/display_handoff.h>
#include <platform/bcm28xx/arm_chainloader.h>
#include <platform/bcm28xx/dsi_framebuffer.h>
#include <stdio.h>

int bcm_dsi7_framebuffer_handoff(uint32_t *physical);
int bcm_dsi7_scanout(void);
void bcm_dsi7_run_boot_animation(void);
int bcm_panel_attiny_probe(void);

int bcm_display_probe_dsi(void) {
  return bcm_panel_attiny_probe() ? BCM_DISPLAY_PROBE_ABSENT :
      BCM_DISPLAY_PROBE_PRESENT;
}

int bcm_display_init_dsi(void) { return bcm_dsi7_scanout(); }

int bcm_display_get_handoff_dsi(struct bcm_display_handoff *out) {
  uint32_t physical;
  if (!out)
    return BCM_DISPLAY_INVALID_FRAMEBUFFER;
  if (bcm_dsi7_framebuffer_handoff(&physical))
    return BCM_DISPLAY_PANEL_INIT_FAILED;

  *out = (struct bcm_display_handoff) {
    .backend = BCM_DISPLAY_DSI,
    .fb_arm_phys = physical,
    .fb_bus_addr = physical,
    .fb_primary_phys = DSI_FB_PRIMARY_PHYS,
    .fb_back_phys = DSI_FB_BACK_PHYS,
    .width = DSI_FB_WIDTH,
    .height = DSI_FB_HEIGHT,
    .pitch = DSI_FB_STRIDE_BYTES,
    .pixel_format = ARM_CHAINLOADER_FORMAT_A8R8G8B8,
    .buffer_size = DSI_FB_WIDTH * DSI_FB_HEIGHT * 4u,
    .reserved_base = DSI_FB_POOL_BASE,
    .reserved_size = DSI_FB_POOL_SIZE,
  };
  return BCM_DISPLAY_OK;
}

const char *bcm_display_backend_name_dsi(void) { return "DSI"; }

void bcm_display_run_boot_animation_dsi(void) {
#if DSI_STATIC_HANDOFF
  puts("DSI: static scanout retained after ARM release");
  for (;;)
    __asm__ volatile("nop");
#else
  bcm_dsi7_run_boot_animation();
#endif
}
