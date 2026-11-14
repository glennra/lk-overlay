#include <stddef.h>
#include <string.h>
#include <platform/bcm28xx/arm_chainloader.h>
#if BCM283X_HDMI
#include <platform/bcm28xx/hdmi_framebuffer.h>
#define FB_POOL_BASE HDMI_FB_POOL_BASE
#define FB_POOL_SIZE HDMI_FB_POOL_SIZE
#define FB_PRIMARY_PHYS HDMI_FB_PRIMARY_PHYS
#define FB_BACK_PHYS HDMI_FB_BACK_PHYS
#define FB_WIDTH HDMI_FB_WIDTH
#define FB_HEIGHT HDMI_FB_HEIGHT
#else
#include <platform/bcm28xx/dsi_framebuffer.h>
#define FB_POOL_BASE DSI_FB_POOL_BASE
#define FB_POOL_SIZE DSI_FB_POOL_SIZE
#define FB_PRIMARY_PHYS DSI_FB_PRIMARY_PHYS
#define FB_BACK_PHYS DSI_FB_BACK_PHYS
#define FB_WIDTH DSI_FB_WIDTH
#define FB_HEIGHT DSI_FB_HEIGHT
#endif

int arm_chainloader_board_supported(uint32_t revision) {
  uint32_t type = (revision >> 4) & 0xff;
  return (revision & (1u << 23)) && ((revision >> 12) & 0xf) == 2 &&
         ((revision >> 20) & 7) == 2 && (type == 8 || type == 0xd);
}

int arm_chainloader_write_firmware_data(volatile uint32_t *data, uint32_t serial,
                                       uint32_t revision, uint32_t uart_hz,
                                       uint32_t active_framebuffer) {
  if (!data || (active_framebuffer != FB_PRIMARY_PHYS &&
                active_framebuffer != FB_BACK_PHYS))
    return -1;

  /* Matches firmware_arm_data_t and reserved indices in rpi-open-firmware. */
  data[0] = 0; /* kRamSize1GB */
  data[1] = 0; /* VPU CPUID is diagnostic only */
  data[2] = serial;
  data[3] = revision;
  data[4] = uart_hz;
  data[5] = ARM_CHAINLOADER_FRAMEBUFFER_MAGIC;
  data[6] = FB_POOL_BASE;
  data[7] = FB_POOL_SIZE;
  data[8] = FB_PRIMARY_PHYS;
  data[9] = FB_BACK_PHYS;
  data[10] = active_framebuffer;
  data[11] = FB_WIDTH;
  data[12] = FB_HEIGHT;
  data[13] = FB_WIDTH; /* stride in pixels, not bytes */
  data[14] = ARM_CHAINLOADER_FORMAT_A8R8G8B8;
  return 0;
}

int arm_chainloader_prepare_image(void *destination, size_t capacity,
                                  const void *payload, size_t payload_size,
                                  uint32_t serial, uint32_t revision,
                                  uint32_t uart_hz, uint32_t active_framebuffer) {
  const size_t data_end = ARM_CHAINLOADER_FIRMWARE_DATA_OFFSET +
                          ARM_CHAINLOADER_FIRMWARE_DATA_WORDS * sizeof(uint32_t);
  if (!destination || !payload || ((uintptr_t)destination & 3u) ||
      payload_size < data_end || payload_size > capacity ||
      (active_framebuffer != FB_PRIMARY_PHYS && active_framebuffer != FB_BACK_PHYS))
    return -1;

  memcpy(destination, payload, payload_size);
  return arm_chainloader_write_firmware_data(
      (volatile uint32_t *)((uint8_t *)destination + ARM_CHAINLOADER_FIRMWARE_DATA_OFFSET),
      serial, revision, uart_hz, active_framebuffer);
}
