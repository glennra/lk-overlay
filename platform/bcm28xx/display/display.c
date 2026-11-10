#include <kernel/mutex.h>
#include <lib/gfx.h>
#include <lk/init.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/bridge.h>
#include <platform/bcm28xx/display.h>
#include <platform/bcm28xx/dsi.h>
#include <platform/bcm28xx/dsi_framebuffer.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/panel.h>
#include <platform/bcm28xx/pv.h>
#include <platform/bcm28xx/udelay.h>
#include <platform/bcm28xx/boot_timing.h>
#include <lk/reg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifndef DSI_FIRST_FRAME_SETTLE_US
#define DSI_FIRST_FRAME_SETTLE_US 49000u
#endif

#ifndef WITH_MATRIX_BOOT_ANIMATION
#define WITH_MATRIX_BOOT_ANIMATION 0
#endif

#if WITH_MATRIX_BOOT_ANIMATION
void bcm_matrix_boot_animation(gfx_surface *linux_fb);
#endif

static gfx_surface *primary_fb;
static gfx_surface *back_fb;
static hvs_layer *display_layer;
static bool scanout_ready;

#define ANIMATION_SENTINEL_X 128u
#define ANIMATION_SENTINEL_Y 64u
#define ANIMATION_SENTINEL_STEP 4u
#define ANIMATION_SENTINEL_WORDS \
  ((ANIMATION_SENTINEL_X / ANIMATION_SENTINEL_STEP) * \
   (ANIMATION_SENTINEL_Y / ANIMATION_SENTINEL_STEP))

static uint32_t animation_sentinel[ANIMATION_SENTINEL_WORDS];

static const struct pv_timings dsi7_timings = {
  .hfp = 1, .hsync = 2, .hbp = 46, .hactive = DSI_FB_WIDTH,
  .vfp = 7, .vsync = 2, .vbp = 21, .vactive = DSI_FB_HEIGHT,
  .interlaced = false, .clock_mux = clk_dsi,
};

int bcm_dsi7_framebuffer_handoff(uint32_t *physical) {
  if (!physical || !scanout_ready || !display_layer || !display_layer->fb)
    return -1;
  *physical = (uint32_t)(uintptr_t)display_layer->fb->ptr & 0x3fffffffu;
  return (*physical == DSI_FB_PRIMARY_PHYS || *physical == DSI_FB_BACK_PHYS) ? 0 : -1;
}

static void animation_capture_sentinel(gfx_surface *surface) {
  volatile uint32_t *pixels = (volatile uint32_t *)surface->ptr;
  unsigned int n = 0;

  for (unsigned int y = 0; y < ANIMATION_SENTINEL_Y;
       y += ANIMATION_SENTINEL_STEP) {
    for (unsigned int x = 0; x < ANIMATION_SENTINEL_X;
         x += ANIMATION_SENTINEL_STEP)
      animation_sentinel[n++] = pixels[y * DSI_FB_WIDTH + x];
  }
}

static bool animation_sentinel_changed(gfx_surface *surface) {
  volatile uint32_t *pixels = (volatile uint32_t *)surface->ptr;
  unsigned int n = 0;

  for (unsigned int y = 0; y < ANIMATION_SENTINEL_Y;
       y += ANIMATION_SENTINEL_STEP) {
    for (unsigned int x = 0; x < ANIMATION_SENTINEL_X;
         x += ANIMATION_SENTINEL_STEP) {
      if (pixels[y * DSI_FB_WIDTH + x] != animation_sentinel[n++])
        return true;
    }
  }
  return false;
}

static void animation_commit_dlist(void) {
  /*
   * WO-041 leaves VPU interrupts masked after ARM release, so pv_irq() cannot
   * perform the normal VFP-start page flip.  Poll the same PixelValve event
   * that pv_irq() normally handles, then commit the already-built list while
   * the DSI output is in vertical blank.
   */
  struct pixel_valve *pv = getPvAddr(1);
  pv->int_status = PV_INTEN_VFP_START;
  while (!(pv->int_status & PV_INTEN_VFP_START))
    asm volatile("nop");
  pv->int_status = PV_INTEN_VFP_START;
  *REG32(SCALER_DISPLIST1) = channels[HVS_DSI1_CHANNEL].dlist_target;
}

void bcm_dsi7_run_boot_animation(void) {
  if (!scanout_ready || !back_fb)
    return;

#if WITH_MATRIX_BOOT_ANIMATION
  bcm_matrix_boot_animation(display_layer->fb);
  return;
#endif

  /* Linux receives the buffer that is active at ARM release. */
  gfx_surface *linux_fb = display_layer->fb;
  const unsigned int square = 96u;
  const unsigned int square_y = 176u;
  gfx_surface *overlay_fb = gfx_create_surface(NULL, square, square, square,
                                                GFX_FORMAT_ARGB_8888);
  hvs_layer *overlay_layer = NULL;

  if (!overlay_fb) {
    puts("DSI: unable to allocate animation overlay");
    return;
  }
  /* Keep the diagnostic sprite opaque while validating the HVS path. */
  gfx_clear(overlay_fb, 0xffff0000);
  gfx_fillrect(overlay_fb, 8u, 8u, square - 16u, square - 16u,
               0xffffffff);
  gfx_flush(overlay_fb);
  overlay_layer = malloc(sizeof(*overlay_layer));
  if (!overlay_layer) {
    puts("DSI: unable to allocate animation layer");
    return;
  }
  mk_unity_layer(overlay_layer, overlay_fb, 20, 16u, square_y);
  /*
   * Do not send this layer through hvs_add_plane_scaled().  That path is
   * known to be unreliable on VC4, even when source and destination sizes
   * happen to match.  Use the same premade, unscaled ARGB8888 list as the
   * working console layer and regenerate only its position each frame.
   */
  overlay_layer->alpha_mode = alpha_mode_pipeline;
  hvs_allocate_premade(overlay_layer, 7);
  if (!overlay_layer->premade_dlist) {
    puts("DSI: unable to allocate animation display list");
    return;
  }
  hvs_regen_noscale_noviewport(overlay_layer);
  mutex_acquire(&channels[HVS_DSI1_CHANNEL].lock);
  hvs_dlist_add(HVS_DSI1_CHANNEL, overlay_layer);
  hvs_update_dlist(HVS_DSI1_CHANNEL);
  mutex_release(&channels[HVS_DSI1_CHANNEL].lock);
  animation_commit_dlist();

  animation_capture_sentinel(linux_fb);
  puts("DSI: VPU boot animation active");

  /*
   * This loop deliberately does not use LK timers or interrupts.  The ARM
   * handoff masks VPU interrupts, while the VPU remains able to execute this
   * code after releasing the ARM.  Linux maps this same framebuffer through
   * simplefb; its first fbcon output changes the sentinel and ends the loop.
   * The animation itself is an HVS overlay, so the framebuffer remains static
   * while the VPU changes only the overlay position between frames.
   */
  for (unsigned int x = 16u;; x += 4u) {
    if (animation_sentinel_changed(linux_fb)) {
      mutex_acquire(&channels[HVS_DSI1_CHANNEL].lock);
      overlay_layer->visible = false;
      hvs_update_dlist(HVS_DSI1_CHANNEL);
      mutex_release(&channels[HVS_DSI1_CHANNEL].lock);
      animation_commit_dlist();
      puts("DSI: framebuffer console takeover detected");
      /*
       * The ARM side now owns the console and shared peripherals.  Do not
       * return into LK app startup: the automatic VPU shell would touch the
       * handoff path after Linux has taken ownership and can fault.  This is
       * the intentional terminal state for the retained-display checkpoint.
       */
      for (;;)
        asm volatile("nop");
    }
    if (x > DSI_FB_WIDTH - 112u)
      x = 16u;
    mutex_acquire(&channels[HVS_DSI1_CHANNEL].lock);
    overlay_layer->x = x;
    hvs_regen_noscale_noviewport(overlay_layer);
    hvs_update_dlist(HVS_DSI1_CHANNEL);
    mutex_release(&channels[HVS_DSI1_CHANNEL].lock);
    /* animation_commit_dlist() waits for the next VFP: one update per refresh. */
    animation_commit_dlist();
  }
}

static int prepare_framebuffer(void) {
  if (display_layer)
    return 0;
  primary_fb = gfx_create_surface((void *)(uintptr_t)(BCM_SDRAM_BUS_ADDR_BASE_NO_L2 | DSI_FB_PRIMARY_PHYS),
                                  DSI_FB_WIDTH, DSI_FB_HEIGHT, DSI_FB_WIDTH, GFX_FORMAT_ARGB_8888);
  back_fb = gfx_create_surface((void *)(uintptr_t)(BCM_SDRAM_BUS_ADDR_BASE_NO_L2 | DSI_FB_BACK_PHYS),
                               DSI_FB_WIDTH, DSI_FB_HEIGHT, DSI_FB_WIDTH, GFX_FORMAT_ARGB_8888);
  if (!primary_fb || !back_fb)
    return -1;
  primary_fb->free_on_destroy = false;
  back_fb->free_on_destroy = false;
  display_layer = malloc(sizeof(*display_layer));
  if (!display_layer)
    return -1;
  mk_unity_layer(display_layer, primary_fb, 10, 0, 0);
  display_layer->name = NULL;
  mutex_acquire(&channels[HVS_DSI1_CHANNEL].lock);
  hvs_dlist_add(HVS_DSI1_CHANNEL, display_layer);
  mutex_release(&channels[HVS_DSI1_CHANNEL].lock);
  return 0;
}

static int prepare_scanout(void) {
#if !WITH_MATRIX_BOOT_ANIMATION
  static const uint32_t colors[] = {
    0xffffffff, 0xffffff00, 0xff00ffff, 0xff00ff00,
    0xffff00ff, 0xffff0000, 0xff0000ff, 0xff000000,
  };
#endif
  if (prepare_framebuffer())
    return -1;
#if WITH_MATRIX_BOOT_ANIMATION
  /* Matrix owns the first visible frame; never expose the diagnostic bars. */
  gfx_clear(back_fb, 0xff000000);
#else
  for (unsigned int bar = 0; bar < 8; ++bar)
    gfx_fillrect(back_fb, bar * 100u, 0, 100u, DSI_FB_HEIGHT, colors[bar]);
#endif
  gfx_flush(back_fb);
  display_layer->fb = back_fb;
  hvs_configure_channel_dsi1(DSI_FB_WIDTH, DSI_FB_HEIGHT);
  mutex_acquire(&channels[HVS_DSI1_CHANNEL].lock);
  hvs_update_dlist(HVS_DSI1_CHANNEL);
  mutex_release(&channels[HVS_DSI1_CHANNEL].lock);
  hvs_start_channel_dsi1(DSI_FB_WIDTH, DSI_FB_HEIGHT);
  return 0;
}

int bcm_dsi7_scanout(void) {
  struct bcm_dsi_clock_config cfg = { .pixel_hz_requested = 25979400u };
  struct pixel_valve *pv = getPvAddr(1);

  scanout_ready = false;
  bcm_boot_timing_mark(BCM_BOOT_T4_DSI_INIT_BEGIN);
  bcm_dsi1_write(BCM_DSI1_DISP0_CTRL, 0);
  if (pv) pv->c = 0;
  hvs_channels[HVS_DSI1_CHANNEL].dispctrl = 0;
  udelay(1000);
  if (bcm_dsi1_clock_init(&cfg)) {
    puts("DSI: clock initialization failed");
    return -1;
  }
  if (bcm_dsi1_phy_init(&cfg)) {
    puts("DSI: PHY initialization failed");
    return -1;
  }
  if (bcm_dsi1_video_prepare())
    return -1;
  hvs_initialize();
  setup_pixelvalve(&dsi7_timings, 1);
  if (prepare_scanout())
    return -1;
  bcm_boot_timing_mark(BCM_BOOT_T7_HVS_DISPLAY_LIST_READY);
  if (bcm_tc358762_init() || bcm_dsi1_video_enable())
    return -1;
  if (bcm_panel_attiny_set_backlight(255))
    return -1;
  bcm_boot_timing_mark(BCM_BOOT_T9_BACKLIGHT_ENABLED);
  enable_pixelvalve(1);
  bcm_boot_timing_mark(BCM_BOOT_T8_SCANOUT_ENABLED);
  udelay(DSI_FIRST_FRAME_SETTLE_US);
  scanout_ready = true;
  bcm_boot_timing_mark(BCM_BOOT_T10_FIRST_FRAME_READY);
  bcm_boot_timing_dump();
#if !WITH_MATRIX_BOOT_ANIMATION
  puts("DSI: 800x480 color bars active");
#endif
  return 0;
}

static void dsi7_init_hook(uint level) {
  (void)level;
  if (bcm_dsi7_scanout())
    puts("DSI: display initialization failed");
}
LK_INIT_HOOK(dsi7_display, &dsi7_init_hook, LK_INIT_LEVEL_PLATFORM + 1);
