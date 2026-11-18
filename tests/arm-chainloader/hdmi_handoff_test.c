#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <platform/bcm28xx/arm_chainloader.h>
#include <platform/bcm28xx/hdmi_framebuffer.h>

static uint32_t test_width, test_height;
static int fail_dimensions;

int bcm_hdmi_framebuffer_dimensions(uint32_t *width, uint32_t *height) {
  if (fail_dimensions) return -1;
  *width = test_width;
  *height = test_height;
  return 0;
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
