#include <platform/bcm28xx/display_handoff.h>
#include <platform/bcm28xx/arm_chainloader.h>
#include <platform/bcm28xx/hdmi_framebuffer.h>

int bcm_hdmi_framebuffer_handoff(uint32_t *physical);
int bcm_hdmi_framebuffer_dimensions(uint32_t *width, uint32_t *height);
void bcm_hdmi_init(unsigned level);
void bcm_hdmi_run_boot_animation(void);

int bcm_display_probe_hdmi(void) { return BCM_DISPLAY_PROBE_UNKNOWN; }

int bcm_display_init_hdmi(void) { bcm_hdmi_init(0); return 0; }

int bcm_display_get_handoff_hdmi(struct bcm_display_handoff *out) {
  uint32_t width, height, physical;
  if (!out)
    return BCM_DISPLAY_INVALID_FRAMEBUFFER;
  if (bcm_hdmi_framebuffer_handoff(&physical))
    return BCM_DISPLAY_EDID_UNAVAILABLE;
  if (bcm_hdmi_framebuffer_dimensions(&width, &height))
    return BCM_DISPLAY_NOT_READY;

  *out = (struct bcm_display_handoff) {
    .backend = BCM_DISPLAY_HDMI,
    .fb_arm_phys = physical,
    .fb_bus_addr = physical,
    .fb_primary_phys = HDMI_FB_PRIMARY_PHYS,
    .fb_back_phys = HDMI_FB_BACK_PHYS,
    .width = width,
    .height = height,
    .pitch = width * 4u,
    .pixel_format = ARM_CHAINLOADER_FORMAT_A8R8G8B8,
    .buffer_size = width * height * 4u,
    .reserved_base = HDMI_FB_POOL_BASE,
    .reserved_size = HDMI_FB_POOL_SIZE,
  };
  return BCM_DISPLAY_OK;
}

const char *bcm_display_backend_name_hdmi(void) { return "HDMI"; }

void bcm_display_run_boot_animation_hdmi(void) { bcm_hdmi_run_boot_animation(); }
