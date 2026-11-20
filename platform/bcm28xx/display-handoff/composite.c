#include <kernel/mutex.h>
#include <dev/uart.h>
#include <lib/gfx.h>
#include <lk/reg.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/animation.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/arm_chainloader.h>
#include <platform/bcm28xx/display_handoff.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/pv.h>
#include <stdio.h>
#include <stdlib.h>

#define COMPOSITE_FB_BASE 0x07800000u
#define COMPOSITE_FB_BACK 0x07a00000u
#define COMPOSITE_FB_SIZE 0x00400000u
#define COMPOSITE_WIDTH 720u
#define COMPOSITE_STRIDE (COMPOSITE_WIDTH * 4u)

int bcm_vec_init(int standard);
static gfx_surface *base_fb;
static hvs_layer *base_layer;
static unsigned composite_height;
static bool composite_ready;
static uint32_t sentinel[512];

int bcm_display_probe_composite(void) { return BCM_DISPLAY_PROBE_PRESENT; }

int bcm_display_init_composite(enum bcm_display_composite_standard standard) {
  static const uint32_t colors[] = {
    0xffffffffu, 0xffffff00u, 0xff00ffffu, 0xff00ff00u,
    0xffff00ffu, 0xffff0000u, 0xff0000ffu, 0xff000000u,
  };
  composite_height = standard == BCM_DISPLAY_STANDARD_NTSC ? 480u : 576u;
  if (bcm_vec_init(standard == BCM_DISPLAY_STANDARD_NTSC)) return -1;
  puts("VEC: preparing base framebuffer");
  uart_flush_tx(0);
  base_fb = gfx_create_surface((void *)(uintptr_t)(BCM_SDRAM_BUS_ADDR_BASE_NO_L2 |
      COMPOSITE_FB_BASE), COMPOSITE_WIDTH, composite_height, COMPOSITE_WIDTH,
      GFX_FORMAT_ARGB_8888);
  base_layer = malloc(sizeof(*base_layer));
  if (!base_fb || !base_layer) return -1;
  base_fb->free_on_destroy = false;
  /* Populate the Linux-visible base before ARM release, as on DSI. */
  for (unsigned bar = 0; bar < 8u; ++bar)
    gfx_fillrect(base_fb, bar * (COMPOSITE_WIDTH / 8u), 0,
                 COMPOSITE_WIDTH / 8u, composite_height, colors[bar]);
  gfx_flush(base_fb);
  /* Sample corresponding pixels above and below the reported top band
   * before exposing the base plane;
   * this distinguishes bad framebuffer contents from later scanout effects. */
  volatile uint32_t *base_pixels = (volatile uint32_t *)base_fb->ptr;
  static const unsigned sample_rows[] = { 0u, 16u, 32u };
  for (unsigned row = 0; row < 3u; ++row) {
    unsigned y = sample_rows[row];
    printf("VEC: bars y=%u", y);
    for (unsigned bar = 0; bar < 8u; ++bar)
      printf(" %08x", base_pixels[y * COMPOSITE_WIDTH + bar * 90u + 45u]);
    puts("");
  }
  uart_flush_tx(0);
  mk_unity_layer(base_layer, base_fb, 10, 0, 0);
  base_layer->alpha_mode = alpha_mode_pipeline;
  hvs_allocate_premade(base_layer, 7);
  if (!base_layer->premade_dlist) return -1;
  hvs_regen_noscale_noviewport(base_layer);
  mutex_acquire(&channels[1].lock);
  hvs_dlist_add(1, base_layer);
  hvs_update_dlist(1);
  mutex_release(&channels[1].lock);
  *REG32(SCALER_DISPLIST1) = channels[1].dlist_target;
  puts("VEC: base list published; enabling PV2");
  uart_flush_tx(0);
  enable_pixelvalve(2);
  enable_pixelvalve_video(2);
  puts("VEC: PV2 enabled");
  uart_flush_tx(0);
  composite_ready = true;
  printf("VEC: %s %ux%u base framebuffer ready\n",
         standard == BCM_DISPLAY_STANDARD_NTSC ? "NTSC" : "PAL",
         COMPOSITE_WIDTH, composite_height);
  uart_flush_tx(0);
  return 0;
}

int bcm_display_get_handoff_composite(struct bcm_display_handoff *out) {
  if (!out || !composite_ready) return BCM_DISPLAY_NOT_READY;
  *out = (struct bcm_display_handoff) {
    .backend = BCM_DISPLAY_COMPOSITE,
    .fb_arm_phys = COMPOSITE_FB_BASE, .fb_bus_addr = COMPOSITE_FB_BASE,
    .fb_primary_phys = COMPOSITE_FB_BASE, .fb_back_phys = COMPOSITE_FB_BACK,
    .width = COMPOSITE_WIDTH, .height = composite_height,
    .pitch = COMPOSITE_STRIDE, .pixel_format = ARM_CHAINLOADER_FORMAT_A8R8G8B8,
    .buffer_size = COMPOSITE_STRIDE * composite_height,
    .reserved_base = COMPOSITE_FB_BASE, .reserved_size = COMPOSITE_FB_SIZE,
  };
  return BCM_DISPLAY_OK;
}

static void composite_wait_field(void) {
  struct pixel_valve *pv = getPvAddr(2);
  pv->int_status = PV_INTEN_VFP_START;
  while (!(pv->int_status & PV_INTEN_VFP_START)) __asm__ volatile("nop");
  pv->int_status = PV_INTEN_VFP_START;
}

void bcm_display_run_boot_animation_composite(void) {
  uint32_t animation_width, animation_height;
  bcm_animation_fit(COMPOSITE_WIDTH, composite_height, &animation_width, &animation_height);
  if (!animation_width || !animation_height) return;
  gfx_surface *fb = gfx_create_surface((void *)(uintptr_t)(BCM_SDRAM_BUS_ADDR_BASE_NO_L2 |
      COMPOSITE_FB_BACK), animation_width, animation_height, animation_width,
      GFX_FORMAT_ARGB_8888);
  hvs_layer *overlay = malloc(sizeof(*overlay));
  bcm_animation animation;
  if (!fb || !overlay) return;
  fb->free_on_destroy = false;
  volatile uint32_t *pixels = (volatile uint32_t *)base_fb->ptr;
  unsigned n = 0;
  for (unsigned y = 0; y < 64u; y += 4u)
    for (unsigned x = 0; x < 128u; x += 4u)
      sentinel[n++] = pixels[y * COMPOSITE_WIDTH + x];
  bcm_animation_surface surface = {
    .width = animation_width, .height = animation_height,
    .stride_bytes = animation_width * 4u, .pixel_format = BCM_ANIMATION_ARGB8888,
    .cpu_address = (uintptr_t)fb->ptr, .dma_bus_address = (uintptr_t)fb->ptr,
    .allocation_bytes = fb->len, .safe_width = animation_width,
    .safe_height = animation_height,
  };
  if (bcm_animation_init(&animation, &surface, 1) ||
      bcm_animation_arm_released(&animation) || bcm_animation_start(&animation) ||
      bcm_animation_render(&animation)) return;
  gfx_flush(fb);
  mk_unity_layer(overlay, fb, 20, (COMPOSITE_WIDTH - animation_width) / 2u,
                 (composite_height - animation_height) / 2u);
  overlay->alpha_mode = alpha_mode_pipeline;
  hvs_allocate_premade(overlay, 7);
  if (!overlay->premade_dlist) return;
  hvs_regen_noscale_noviewport(overlay);
  mutex_acquire(&channels[1].lock);
  hvs_dlist_add(1, overlay);
  hvs_update_dlist(1);
  mutex_release(&channels[1].lock);
  puts("VEC: animation waiting for field boundary");
  uart_flush_tx(0);
  composite_wait_field();
  *REG32(SCALER_DISPLIST1) = channels[1].dlist_target;
  uint32_t frame_started = *REG32(ST_CLO);
  for (;;) {
    bool changed = false;
    n = 0;
    for (unsigned y = 0; y < 64u && !changed; y += 4u)
      for (unsigned sx = 0; sx < 128u; sx += 4u)
        if (pixels[y * COMPOSITE_WIDTH + sx] != sentinel[n++]) { changed = true; break; }
    if (changed) {
      bcm_animation_request_takeover(&animation);
      mutex_acquire(&channels[1].lock);
      overlay->visible = false;
      hvs_update_dlist(1);
      mutex_release(&channels[1].lock);
      composite_wait_field();
      *REG32(SCALER_DISPLIST1) = channels[1].dlist_target;
      bcm_animation_stop(&animation);
      puts("VEC: framebuffer console takeover detected");
      for (;;) __asm__ volatile("nop");
    }
    composite_wait_field();
    uint32_t now = *REG32(ST_CLO);
    if ((uint32_t)(now - frame_started) >= bcm_animation_frame_duration_ms(&animation) * 1000u) {
      bcm_animation_render(&animation);
      gfx_flush(fb);
      frame_started = now;
    }
  }
}
