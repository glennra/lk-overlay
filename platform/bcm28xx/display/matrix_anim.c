#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <lib/gfx.h>
#include <kernel/mutex.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/dsi_framebuffer.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/pv.h>
#include <lk/reg.h>

#include "../../../../../doc/reference/matrix_glyph_atlas.h"

#define MATRIX_FPS 30u
#define MATRIX_VBLANKS_PER_FRAME 2u
#define MATRIX_SPEED_PX_S 160u
#define MATRIX_MAX_STREAMS 90u
#define MATRIX_MAX_GLYPHS 48u
#define MATRIX_TIMEOUT_FRAMES (30u * MATRIX_FPS)
#define MATRIX_INTRUSION_FRAMES (MATRIX_FPS / 4u)
#define MATRIX_SEED 2402395335u
#define MATRIX_BAR_HOLD_VBLANKS 0u
#define MATRIX_SECONDARY_PHYS \
  (DSI_FB_POOL_BASE + 2u * DSI_FB_STRIDE_BYTES * DSI_FB_HEIGHT)

struct matrix_glyph {
  int16_t y;
  uint8_t glyph;
  uint8_t age;
  uint8_t brightness;
  uint8_t flare;
};

struct matrix_stream {
  uint16_t x;
  int32_t head_fp;
  int32_t next_y_fp;
  uint8_t length;
  uint8_t count;
  uint32_t seed;
  struct matrix_glyph glyphs[MATRIX_MAX_GLYPHS];
};

static uint32_t matrix_rng = MATRIX_SEED;

static uint32_t matrix_xorshift(void) {
  matrix_rng ^= matrix_rng << 13;
  matrix_rng ^= matrix_rng >> 17;
  matrix_rng ^= matrix_rng << 5;
  return matrix_rng;
}

static uint32_t matrix_mix(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  return x ^ (x >> 16);
}

static int16_t matrix_floor_grid(int y) {
  if (y >= 0)
    return (int16_t)((y / (int)MATRIX_GLYPH_HEIGHT) * MATRIX_GLYPH_HEIGHT);
  return (int16_t)(-(((-y + (int)MATRIX_GLYPH_HEIGHT - 1) /
                      (int)MATRIX_GLYPH_HEIGHT) * MATRIX_GLYPH_HEIGHT));
}

static void matrix_reset(struct matrix_stream *s, unsigned column) {
  int head = -(int)(matrix_mix(s->seed) % (DSI_FB_HEIGHT + 1u));
  s->head_fp = head * 65536;
  s->next_y_fp = matrix_floor_grid(head) * 65536;
  s->length = 10u + (uint8_t)(matrix_mix(s->seed + 2u) % 21u);
  s->count = 0;
  s->seed = matrix_mix(s->seed + column + 0x9e3779b9u);
}

static void matrix_init(struct matrix_stream *streams) {
  for (unsigned x = 0; x < MATRIX_MAX_STREAMS; ++x) {
    streams[x].x = (uint16_t)(x * DSI_FB_WIDTH / MATRIX_MAX_STREAMS);
    streams[x].seed = matrix_mix(MATRIX_SEED + x * 0x9e3779b9u);
    matrix_reset(&streams[x], x);
  }
}

static void matrix_advance(struct matrix_stream *s) {
  unsigned kept = 0;
  for (unsigned i = 0; i < s->count; ++i) {
    struct matrix_glyph *g = &s->glyphs[i];
    if (g->age != 255u)
      ++g->age;
    if ((matrix_xorshift() % MATRIX_FPS) < 3u)
      g->glyph = (uint8_t)(matrix_xorshift() % MATRIX_GLYPH_COUNT);
    /* Remove expired glyphs so a stream can continue through the full panel. */
    if (g->age < 200u)
      s->glyphs[kept++] = *g;
  }
  s->count = (uint8_t)kept;
  s->head_fp += (int32_t)((MATRIX_SPEED_PX_S * 65536u +
                           MATRIX_FPS / 2u) / MATRIX_FPS);
  while (s->next_y_fp <= s->head_fp && s->count < MATRIX_MAX_GLYPHS) {
    struct matrix_glyph *g = &s->glyphs[s->count++];
    g->y = (int16_t)(s->next_y_fp / 65536);
    g->glyph = (uint8_t)(matrix_mix(s->seed + s->count) % MATRIX_GLYPH_COUNT);
    g->age = 0;
    g->brightness = 80u + (uint8_t)(matrix_mix(s->seed + s->count + 1u) % 36u);
    g->flare = (uint8_t)((matrix_mix(s->seed + s->count + 2u) % 100u) < 8u);
    s->next_y_fp += MATRIX_GLYPH_HEIGHT * 65536;
  }
  if (s->head_fp > (int32_t)DSI_FB_HEIGHT * 65536 && s->count == 0u) {
    matrix_reset(s, 0);
  }
}

static void matrix_put_pixel(uint32_t *pixels, unsigned x, unsigned y,
                             uint32_t color) {
  if (x < DSI_FB_WIDTH && y < DSI_FB_HEIGHT)
    pixels[y * DSI_FB_WIDTH + x] = color;
}

static void matrix_draw_glyph(uint32_t *pixels, unsigned x, int y,
                              unsigned glyph, unsigned r, unsigned g,
                              unsigned b) {
  if (glyph >= MATRIX_GLYPH_COUNT)
    return;
  const unsigned char *src = &matrix_glyph_atlas[
      glyph * MATRIX_GLYPH_WIDTH * MATRIX_GLYPH_HEIGHT];
  for (unsigned gy = 0; gy < MATRIX_GLYPH_HEIGHT; ++gy) {
    for (unsigned gx = 0; gx < MATRIX_GLYPH_WIDTH; ++gx) {
      unsigned a = src[gy * MATRIX_GLYPH_WIDTH + gx];
      int display_y = y + (int)gy;
      int fb_y = (int)DSI_FB_HEIGHT - 1 - display_y;
      if (a && display_y >= 0 && display_y < (int)DSI_FB_HEIGHT) {
        unsigned ar = (a * r) / 255u;
        unsigned ag = (a * g) / 255u;
        unsigned ab = (a * b) / 255u;
        matrix_put_pixel(pixels, x + gx, (unsigned)fb_y,
                         0xff000000u | (ar << 16) | (ag << 8) | ab);
      }
    }
  }
}

static void matrix_render_rain(uint32_t *pixels,
                               struct matrix_stream *streams) {
  for (unsigned y = 0; y < DSI_FB_HEIGHT; ++y)
    for (unsigned x = 0; x < DSI_FB_WIDTH; ++x)
      pixels[y * DSI_FB_WIDTH + x] = 0xff000000u;

  for (unsigned x = 0; x < MATRIX_MAX_STREAMS; ++x) {
    struct matrix_stream *s = &streams[x];
    for (unsigned i = 0; i < s->count; ++i) {
      struct matrix_glyph *g = &s->glyphs[i];
      unsigned fade = g->age >= 200u ? 0u : 255u - (g->age * 255u) / 200u;
      unsigned intensity = (fade * g->brightness) / 100u;
      if (intensity > 255u)
        intensity = 255u;
      if (intensity < 4u)
        continue;
      bool head = i + 1u == s->count;
      bool flare = head || g->flare || g->age < 2u;
      unsigned r = flare ? (intensity + 35u > 255u ? 255u : intensity + 35u)
                         : 3u + intensity / 25u;
      unsigned green = flare ? 255u : intensity;
      unsigned blue = flare ? intensity : 8u + intensity / 10u;
      matrix_draw_glyph(pixels, s->x, g->y, g->glyph, r, green, blue);
    }
  }
}

static void matrix_blend_pixel(uint32_t *pixels, unsigned x, unsigned y,
                               unsigned r, unsigned g, unsigned b,
                               unsigned alpha) {
  if (x >= DSI_FB_WIDTH || y >= DSI_FB_HEIGHT)
    return;
  uint32_t old = pixels[y * DSI_FB_WIDTH + x];
  unsigned dr = (old >> 16) & 0xffu;
  unsigned dg = (old >> 8) & 0xffu;
  unsigned db = old & 0xffu;
  r = (r * alpha + dr * (255u - alpha)) / 255u;
  g = (g * alpha + dg * (255u - alpha)) / 255u;
  b = (b * alpha + db * (255u - alpha)) / 255u;
  pixels[y * DSI_FB_WIDTH + x] = 0xff000000u | (r << 16) | (g << 8) | b;
}

static void matrix_render_intrusion(uint32_t *pixels, unsigned frame) {
  unsigned phase = frame % 10u;
  unsigned pulse = phase <= 5u ? phase * 51u : (10u - phase) * 51u;
  unsigned bands = 4u + (pulse * 4u) / 255u;

  for (unsigned y = 0; y < DSI_FB_HEIGHT; y += 2u)
    for (unsigned x = 0; x < DSI_FB_WIDTH; ++x)
      matrix_blend_pixel(pixels, x, y, 120u, 255u, 160u, 22u);

  for (unsigned band = 0; band < bands; ++band) {
    unsigned y0 = matrix_xorshift() % DSI_FB_HEIGHT;
    unsigned band_height = 2u + matrix_xorshift() % 11u;
    unsigned colour = matrix_xorshift() % 3u;
    unsigned r = colour == 0u ? 180u : colour == 1u ? 255u : 20u;
    unsigned g = colour == 0u ? 255u : colour == 1u ? 255u : 120u;
    unsigned b = colour == 0u ? 190u : colour == 1u ? 255u : 70u;
    unsigned alpha = 25u + matrix_xorshift() % 51u;
    for (unsigned y = y0; y < y0 + band_height && y < DSI_FB_HEIGHT; ++y)
      for (unsigned x = 0; x < DSI_FB_WIDTH; ++x)
        matrix_blend_pixel(pixels, x, y, r, g, b, alpha);
  }

  unsigned noise_pixels = 16u + (pulse * 16u) / 255u;
  for (unsigned i = 0; i < noise_pixels; ++i) {
    unsigned x = matrix_xorshift() % DSI_FB_WIDTH;
    unsigned y = matrix_xorshift() % DSI_FB_HEIGHT;
    unsigned r = (matrix_xorshift() % 3u) == 0u ? 80u :
                 ((matrix_xorshift() % 2u) ? 160u : 255u);
    unsigned g = 255u;
    unsigned b = (matrix_xorshift() % 3u) == 0u ? 100u :
                 ((matrix_xorshift() % 2u) ? 180u : 220u);
    unsigned alpha = 35u + matrix_xorshift() % 116u;
    unsigned width = 1u + matrix_xorshift() % 3u;
    for (unsigned dx = 0; dx < width && x + dx < DSI_FB_WIDTH; ++dx)
      matrix_blend_pixel(pixels, x + dx, y, r, g, b, alpha);
  }
}

static void matrix_commit(void) {
  struct pixel_valve *pv = getPvAddr(1);
  pv->int_status = PV_INTEN_VFP_START;
  while (!(pv->int_status & PV_INTEN_VFP_START))
    asm volatile("nop");
  pv->int_status = PV_INTEN_VFP_START;
  *REG32(SCALER_DISPLIST1) = channels[HVS_DSI1_CHANNEL].dlist_target;
}

static bool matrix_linux_took_over(uint32_t *pixels, uint32_t *sentinel) {
  unsigned n = 0;
  for (unsigned y = 0; y < 64u; y += 4u)
    for (unsigned x = 0; x < 128u; x += 4u)
      if (pixels[y * DSI_FB_WIDTH + x] != sentinel[n++])
        return true;
  return false;
}

void bcm_matrix_boot_animation(gfx_surface *linux_fb) {
  struct matrix_stream streams[MATRIX_MAX_STREAMS];
  uint32_t *pixels;
  uint32_t sentinel[(128u / 4u) * (64u / 4u)];
  gfx_surface *frames[2];
  hvs_layer overlay;
  unsigned current = 0;

  if (!linux_fb)
    return;
  for (unsigned y = 0, n = 0; y < 64u; y += 4u)
    for (unsigned x = 0; x < 128u; x += 4u)
      sentinel[n++] = ((uint32_t *)linux_fb->ptr)[y * DSI_FB_WIDTH + x];

  /* Optional bar hold; zero means Matrix starts immediately. */
#if MATRIX_BAR_HOLD_VBLANKS > 0
  for (unsigned i = 0; i < MATRIX_BAR_HOLD_VBLANKS; ++i)
    matrix_commit();
#endif

  frames[0] = gfx_create_surface((void *)(uintptr_t)(
      BCM_SDRAM_BUS_ADDR_BASE_NO_L2 | DSI_FB_PRIMARY_PHYS),
      DSI_FB_WIDTH, DSI_FB_HEIGHT, DSI_FB_WIDTH, GFX_FORMAT_ARGB_8888);
  frames[1] = gfx_create_surface((void *)(uintptr_t)(
      BCM_SDRAM_BUS_ADDR_BASE_NO_L2 | MATRIX_SECONDARY_PHYS),
      DSI_FB_WIDTH, DSI_FB_HEIGHT, DSI_FB_WIDTH, GFX_FORMAT_ARGB_8888);
  if (!frames[0] || !frames[1])
    return;
  frames[0]->free_on_destroy = false;
  frames[1]->free_on_destroy = false;
  pixels = (uint32_t *)frames[0]->ptr;
  matrix_init(streams);
  matrix_render_rain(pixels, streams);
  gfx_flush(frames[0]);

  mk_unity_layer(&overlay, frames[0], 20, 0, 0);
  overlay.alpha_mode = alpha_mode_pipeline;
  hvs_allocate_premade(&overlay, DSI_FB_DLIST_WORDS);
  if (!overlay.premade_dlist)
    return;
  hvs_regen_noscale_noviewport(&overlay);
  mutex_acquire(&channels[HVS_DSI1_CHANNEL].lock);
  hvs_dlist_add(HVS_DSI1_CHANNEL, &overlay);
  hvs_update_dlist(HVS_DSI1_CHANNEL);
  mutex_release(&channels[HVS_DSI1_CHANNEL].lock);
  matrix_commit();

  bool intrusion = false;
  unsigned intrusion_frame = 0;
  for (unsigned vblank = 0, frame = 0;
       vblank < (MATRIX_TIMEOUT_FRAMES + MATRIX_INTRUSION_FRAMES + 1u) *
                    MATRIX_VBLANKS_PER_FRAME; ++vblank) {
    if (!intrusion && matrix_linux_took_over((uint32_t *)linux_fb->ptr,
                                             sentinel))
      intrusion = true;
    matrix_commit();
    if ((vblank + 1u) % MATRIX_VBLANKS_PER_FRAME)
      continue;
    unsigned next = current ^ 1u;
    pixels = (uint32_t *)frames[next]->ptr;
    if (!intrusion) {
      for (unsigned x = 0; x < MATRIX_MAX_STREAMS; ++x)
        matrix_advance(&streams[x]);
      matrix_render_rain(pixels, streams);
      if (++frame >= MATRIX_TIMEOUT_FRAMES)
        intrusion = true;
    } else {
      matrix_render_intrusion(pixels, frame);
      if (++intrusion_frame >= MATRIX_INTRUSION_FRAMES)
        break;
    }
    gfx_flush(frames[next]);
    mutex_acquire(&channels[HVS_DSI1_CHANNEL].lock);
    overlay.fb = frames[next];
    hvs_regen_noscale_noviewport(&overlay);
    hvs_update_dlist(HVS_DSI1_CHANNEL);
    mutex_release(&channels[HVS_DSI1_CHANNEL].lock);
    current = next;
  }

  overlay.visible = false;
  mutex_acquire(&channels[HVS_DSI1_CHANNEL].lock);
  hvs_update_dlist(HVS_DSI1_CHANNEL);
  mutex_release(&channels[HVS_DSI1_CHANNEL].lock);
  matrix_commit();
  for (;;) asm volatile("nop");
}
