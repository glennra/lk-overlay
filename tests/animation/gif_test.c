#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <platform/bcm28xx/animation.h>

int main(int argc, char **argv) {
  uint32_t width = 800, height = 480, w, h;
  if (argc >= 3) { width = (uint32_t)atoi(argv[1]); height = (uint32_t)atoi(argv[2]); }
  uint32_t stride = width + 3;
  uint32_t *pixels = malloc((size_t)height * stride * sizeof(*pixels));
  assert(pixels);
  memset(pixels, 0x5a, (size_t)height * stride * sizeof(*pixels));
  bcm_animation animation;
  bcm_animation_surface surface = {
    .width = width, .height = height, .stride_bytes = stride * 4u,
    .pixel_format = BCM_ANIMATION_ARGB8888,
    .cpu_address = (uintptr_t)pixels,
    .allocation_bytes = (size_t)height * stride * sizeof(*pixels),
    .flip_vertical = argc >= 4 ? (uint32_t)atoi(argv[3]) : 0,
  };
  bcm_animation_fit(720, 576, &w, &h); assert(w == 720 && h == 432);
  bcm_animation_fit(640, 480, &w, &h); assert(w == 640 && h == 384);
  bcm_animation_fit(1920, 1080, &w, &h); assert(w == 800 && h == 480);
  assert(!bcm_animation_init(&animation, &surface, 1));
  assert(!bcm_animation_arm_released(&animation));
  assert(!bcm_animation_start(&animation));
  for (unsigned frame = 0; frame < 14; ++frame) {
    assert(!bcm_animation_render(&animation));
    assert(bcm_animation_frame_duration_ms(&animation) == 100);
    for (unsigned y = 0; y < height; ++y) {
      for (unsigned x = width; x < stride; ++x) assert(pixels[y * stride + x] == 0x5a5a5a5a);
      assert(fwrite(&pixels[y * stride], sizeof(*pixels), width, stdout) == width);
    }
  }
  assert(!bcm_animation_request_takeover(&animation));
  assert(!bcm_animation_stop(&animation));
  assert(bcm_animation_render(&animation));
  free(pixels);
  return 0;
}
