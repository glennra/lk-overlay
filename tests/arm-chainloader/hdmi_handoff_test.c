#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <platform/bcm28xx/arm_chainloader.h>
#include <platform/bcm28xx/hdmi_framebuffer.h>
#include <platform/bcm28xx/display_handoff.h>

static uint32_t test_width, test_height;
static int fail_dimensions;

int bcm_hdmi_framebuffer_dimensions(uint32_t *width, uint32_t *height) {
  if (fail_dimensions) return -1;
  *width = test_width;
  *height = test_height;
  return 0;
}

int bcm_display_get_handoff(struct bcm_display_handoff *out) {
  if (!out || fail_dimensions ||
      !((test_width == 1920 && test_height == 1080) ||
        (test_width == 1280 && (test_height == 800 || test_height == 720)) ||
        (test_width == 640 && test_height == 480)))
    return BCM_DISPLAY_INVALID_FRAMEBUFFER;
  *out = (struct bcm_display_handoff) {
    .backend = BCM_DISPLAY_HDMI,
    .fb_arm_phys = HDMI_FB_PRIMARY_PHYS,
    .fb_bus_addr = HDMI_FB_PRIMARY_PHYS,
    .fb_primary_phys = HDMI_FB_PRIMARY_PHYS,
    .fb_back_phys = HDMI_FB_BACK_PHYS,
    .width = test_width, .height = test_height,
    .pitch = test_width * 4u,
    .pixel_format = ARM_CHAINLOADER_FORMAT_A8R8G8B8,
    .buffer_size = test_width * test_height * 4u,
    .reserved_base = HDMI_FB_POOL_BASE, .reserved_size = HDMI_FB_POOL_SIZE,
  };
  return BCM_DISPLAY_OK;
}

int main(void) {
  uint32_t data[ARM_CHAINLOADER_FIRMWARE_DATA_WORDS];
  static const uint32_t modes[][2] = {{1920, 1080}, {1280, 800}, {1280, 720}, {640, 480}};
  for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
    test_width = modes[i][0]; test_height = modes[i][1];
    assert(!arm_chainloader_write_firmware_data(data, 1, 2, 3, HDMI_FB_PRIMARY_PHYS));
    assert(data[6] == 0x07400000 && data[7] == 0x01000000);
    assert(data[8] == 0x07400000 && data[9] == 0x07c00000);
    assert(data[11] == test_width && data[12] == test_height && data[13] == test_width);
  }
  test_width = 1280; test_height = 768;
  assert(arm_chainloader_write_firmware_data(data, 1, 2, 3, HDMI_FB_PRIMARY_PHYS) == -1);
  fail_dimensions = 1;
  assert(arm_chainloader_write_firmware_data(data, 1, 2, 3, HDMI_FB_PRIMARY_PHYS) == -1);
  puts("HDMI selected-mode handoff tests passed");
  return 0;
}
