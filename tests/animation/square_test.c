#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <platform/bcm28xx/animation.h>

int main(void) {
  enum { width = 96, height = 96, stride = 100 };
  uint32_t pixels[height][stride];
  bcm_animation animation;
  bcm_animation_surface surface = {
    .width = width, .height = height, .stride_bytes = stride * 4u,
    .pixel_format = BCM_ANIMATION_ARGB8888,
    .cpu_address = (uintptr_t)pixels, .allocation_bytes = sizeof(pixels),
    .safe_width = width, .safe_height = height,
  };
  memset(pixels, 0x5a, sizeof(pixels));
  assert(!bcm_animation_init(&animation, &surface, 1));
  assert(!bcm_animation_arm_released(&animation));
  assert(!bcm_animation_start(&animation));
  assert(!bcm_animation_render(&animation));
  for (unsigned y = 0; y < height; ++y) {
    for (unsigned x = 0; x < width; ++x) {
      uint32_t expected = x >= 8 && y >= 8 && x < 88 && y < 88
          ? 0xffffffffu : 0xffff0000u;
      assert(pixels[y][x] == expected);
    }
    for (unsigned x = width; x < stride; ++x)
      assert(pixels[y][x] == 0x5a5a5a5au);
  }
  assert(!bcm_animation_request_takeover(&animation));
  assert(!bcm_animation_stop(&animation));
  assert(bcm_animation_render(&animation));
  return 0;
}
