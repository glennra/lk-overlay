#include <stddef.h>
#include <string.h>
#include <platform/bcm28xx/arm_chainloader.h>
#include <platform/bcm28xx/display_handoff.h>

int arm_chainloader_board_supported(uint32_t revision) {
  uint32_t type = (revision >> 4) & 0xff;
  return (revision & (1u << 23)) && ((revision >> 12) & 0xf) == 2 &&
         ((revision >> 20) & 7) == 2 && (type == 8 || type == 0xd);
}

int arm_chainloader_write_firmware_data(volatile uint32_t *data, uint32_t serial,
                                       uint32_t revision, uint32_t uart_hz,
                                       uint32_t active_framebuffer) {
  struct bcm_display_handoff fb;
  if (!data || bcm_display_get_handoff(&fb) != BCM_DISPLAY_OK ||
      (active_framebuffer != fb.fb_primary_phys &&
       active_framebuffer != fb.fb_back_phys) ||
      (fb.fb_bus_addr != fb.fb_primary_phys &&
       fb.fb_bus_addr != fb.fb_back_phys) || !fb.width || !fb.height ||
      fb.pitch < fb.width * 4u ||
      fb.buffer_size < fb.pitch * fb.height ||
      fb.fb_primary_phys < fb.reserved_base ||
      fb.fb_back_phys < fb.reserved_base ||
      fb.fb_primary_phys + fb.buffer_size > fb.reserved_base + fb.reserved_size ||
      fb.fb_back_phys + fb.buffer_size > fb.reserved_base + fb.reserved_size ||
      active_framebuffer < fb.reserved_base ||
      active_framebuffer + fb.buffer_size > fb.reserved_base + fb.reserved_size)
    return -1;

  /* Matches firmware_arm_data_t and reserved indices in rpi-open-firmware. */
  data[0] = 0; /* kRamSize1GB */
  data[1] = 0; /* VPU CPUID is diagnostic only */
  data[2] = serial;
  data[3] = revision;
  data[4] = uart_hz;
  data[5] = ARM_CHAINLOADER_FRAMEBUFFER_MAGIC;
  data[6] = fb.reserved_base;
  data[7] = fb.reserved_size;
  data[8] = fb.fb_primary_phys;
  data[9] = fb.fb_back_phys;
  data[10] = active_framebuffer;
  data[11] = fb.width;
  data[12] = fb.height;
  data[13] = fb.pitch / 4u; /* stride in pixels, not bytes */
  data[14] = fb.pixel_format;
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
      !active_framebuffer)
    return -1;

  memcpy(destination, payload, payload_size);
  return arm_chainloader_write_firmware_data(
      (volatile uint32_t *)((uint8_t *)destination + ARM_CHAINLOADER_FIRMWARE_DATA_OFFSET),
      serial, revision, uart_hz, active_framebuffer);
}
