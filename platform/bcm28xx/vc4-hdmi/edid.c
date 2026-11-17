#include "edid.h"

#include <string.h>

static uint16_t le16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }

static uint32_t refresh_millihz(const struct hdmi_edid_mode *m) {
  uint32_t htotal = m->width + m->hfp + m->hsync + m->hbp;
  uint32_t vtotal = m->height + m->vfp + m->vsync + m->vbp;
  if (!htotal || !vtotal) return 0;
  return (uint32_t)(((uint64_t)m->pixel_clock_hz * 1000) /
                    ((uint64_t)htotal * vtotal));
}

static void add_mode(struct hdmi_edid_info *info,
                     const struct hdmi_edid_mode *mode) {
  if (info->mode_count >= HDMI_EDID_MAX_MODES || !mode->width ||
      !mode->height || !mode->pixel_clock_hz)
    return;
  for (size_t i = 0; i < info->mode_count; ++i) {
    const struct hdmi_edid_mode *old = &info->modes[i];
    if (old->width == mode->width && old->height == mode->height &&
        old->pixel_clock_hz == mode->pixel_clock_hz &&
        old->interlaced == mode->interlaced &&
        old->hfp == mode->hfp && old->hsync == mode->hsync &&
        old->hbp == mode->hbp && old->vfp == mode->vfp &&
        old->vsync == mode->vsync && old->vbp == mode->vbp &&
        old->sync_separate == mode->sync_separate &&
        old->hsync_positive == mode->hsync_positive &&
        old->vsync_positive == mode->vsync_positive)
      return;
  }
  info->modes[info->mode_count++] = *mode;
}

static void add_dtd(struct hdmi_edid_info *info, const uint8_t *dtd,
                    bool hdmi_signalling) {
  struct hdmi_edid_mode mode;
  memset(&mode, 0, sizeof(mode));
  uint16_t clock_10khz = le16(dtd);
  if (!clock_10khz) return;

  mode.pixel_clock_hz = (uint32_t)clock_10khz * 10000;
  mode.width = dtd[2] | ((dtd[4] & 0xf0) << 4);
  uint16_t hblank = dtd[3] | ((dtd[4] & 0x0f) << 8);
  mode.height = dtd[5] | ((dtd[7] & 0xf0) << 4);
  uint16_t vblank = dtd[6] | ((dtd[7] & 0x0f) << 8);
  uint16_t hstart = dtd[8] | ((dtd[11] & 0xc0) << 2);
  uint16_t hwidth = dtd[9] | ((dtd[11] & 0x30) << 4);
  uint16_t vstart = (dtd[10] >> 4) | ((dtd[11] & 0x0c) << 2);
  uint16_t vwidth = (dtd[10] & 0x0f) | ((dtd[11] & 0x03) << 4);
  if (hstart > hblank || hwidth > hblank - hstart ||
      vstart > vblank || vwidth > vblank - vstart)
    return;
  mode.hfp = hstart;
  mode.hsync = hwidth;
  mode.hbp = hblank - hstart - hwidth;
  mode.vfp = vstart;
  mode.vsync = vwidth;
  mode.vbp = vblank - vstart - vwidth;
  mode.interlaced = (dtd[17] & 0x80) != 0;
  mode.sync_separate = (dtd[17] & 0x18) == 0x18;
  mode.hsync_positive = (dtd[17] & 2) != 0;
  mode.vsync_positive = (dtd[17] & 4) != 0;
  mode.hdmi_signalling = hdmi_signalling;
  mode.refresh_millihz = refresh_millihz(&mode);
  add_mode(info, &mode);
}

static void add_vic(struct hdmi_edid_info *info, uint8_t vic) {
  struct hdmi_edid_mode mode;
  memset(&mode, 0, sizeof(mode));
  mode.vic = vic & 0x7f;
  mode.hdmi_signalling = info->hdmi_signalling;
  mode.sync_separate = true;
  mode.hsync_positive = true;
  mode.vsync_positive = true;
  switch (mode.vic) {
    case 16: /* 1080p60 */
      mode.width = 1920; mode.height = 1080; mode.pixel_clock_hz = 148500000;
      mode.hfp = 88; mode.hsync = 44; mode.hbp = 148;
      mode.vfp = 4; mode.vsync = 5; mode.vbp = 36; mode.aspect = 16;
      break;
    case 31: /* 1080p50 */
      mode.width = 1920; mode.height = 1080; mode.pixel_clock_hz = 148500000;
      mode.hfp = 528; mode.hsync = 44; mode.hbp = 148;
      mode.vfp = 4; mode.vsync = 5; mode.vbp = 36; mode.aspect = 16;
      break;
    case 4: /* 720p60 */
      mode.width = 1280; mode.height = 720; mode.pixel_clock_hz = 74250000;
      mode.hfp = 110; mode.hsync = 40; mode.hbp = 220;
      mode.vfp = 5; mode.vsync = 5; mode.vbp = 20; mode.aspect = 16;
      break;
    case 19: /* 720p50 */
      mode.width = 1280; mode.height = 720; mode.pixel_clock_hz = 74250000;
      mode.hfp = 440; mode.hsync = 40; mode.hbp = 220;
      mode.vfp = 5; mode.vsync = 5; mode.vbp = 20; mode.aspect = 16;
      break;
    case 2: /* 480p60 4:3 */
      mode.hsync_positive = mode.vsync_positive = false;
      mode.width = 720; mode.height = 480; mode.pixel_clock_hz = 27000000;
      mode.hfp = 16; mode.hsync = 62; mode.hbp = 60;
      mode.vfp = 9; mode.vsync = 6; mode.vbp = 30; mode.aspect = 4;
      break;
    case 3: /* 480p60 16:9 */
      mode.hsync_positive = mode.vsync_positive = false;
      mode.width = 720; mode.height = 480; mode.pixel_clock_hz = 27000000;
      mode.hfp = 16; mode.hsync = 62; mode.hbp = 60;
      mode.vfp = 9; mode.vsync = 6; mode.vbp = 30; mode.aspect = 16;
      break;
    default:
      return;
  }
  mode.refresh_millihz = refresh_millihz(&mode);
  add_mode(info, &mode);
}

int hdmi_edid_parse(const uint8_t *edid, size_t length,
                    struct hdmi_edid_info *info) {
  if (!edid || !info || length < 128 ||
      memcmp(edid, "\x00\xff\xff\xff\xff\xff\xff\x00", 8) != 0)
    return -1;
  memset(info, 0, sizeof(*info));
  info->extension_count = edid[126];
  size_t blocks = 1u + info->extension_count;
  if (blocks > 4 || length < blocks * 128u) return -2;
  for (size_t b = 0; b < blocks; ++b) {
    const uint8_t *block = edid + b * 128;
    uint8_t sum = 0;
    for (size_t i = 0; i < 128; ++i) sum = (uint8_t)(sum + block[i]);
    if (sum) return -3;
  }
  for (size_t i = 0; i < 4; ++i) add_dtd(info, edid + 54 + i * 18, false);

  for (size_t b = 1; b < blocks; ++b) {
    const uint8_t *block = edid + b * 128;
    if (block[0] != 0x02 || block[1] < 3) continue;
    size_t end = block[2] ? block[2] : 127;
    if (end > 127 || end < 4) return -4;
    size_t p = 4;
    while (p < end) {
      uint8_t tag = block[p] >> 5;
      uint8_t count = block[p] & 0x1f;
      if (count > end - p - 1) return -5;
      if (tag == 2) {
        for (uint8_t i = 0; i < count; ++i) add_vic(info, block[p + 1 + i]);
      } else if (tag == 3 && count >= 3 && block[p + 1] == 0x03 &&
                 block[p + 2] == 0x0c && block[p + 3] == 0x00) {
        info->hdmi_signalling = true;
      }
      p += 1 + count;
    }
    for (size_t dtd = end; dtd + 18 <= 127; dtd += 18)
      add_dtd(info, block + dtd, info->hdmi_signalling);
  }
  for (size_t i = 0; i < info->mode_count; ++i)
    info->modes[i].hdmi_signalling |= info->hdmi_signalling;
  return info->mode_count ? 0 : -6;
}

static int tier(const struct hdmi_edid_mode *m) {
  if (m->interlaced) return 99;
  if (m->width == 1920 && m->height == 1080) return 0;
  if (m->width == 1280 && m->height == 720) return 1;
  if (m->width == 720 && m->height == 480) return 2;
  return 99;
}

static int refresh_rank(uint32_t mhz) {
  if (mhz >= 59900 && mhz <= 60100) return 0;
  if (mhz >= 49900 && mhz <= 50100) return 2;
  return 1;
}

const struct hdmi_edid_mode *hdmi_edid_select(
    const struct hdmi_edid_info *info, const struct hdmi_mode_caps *caps) {
  const struct hdmi_edid_mode *best = NULL;
  int best_tier = 99, best_refresh = 99;
  if (!info || !caps) return NULL;
  for (size_t i = 0; i < info->mode_count; ++i) {
    const struct hdmi_edid_mode *m = &info->modes[i];
    int t = tier(m);
    if (t == 99 || m->pixel_clock_hz > caps->max_pixel_clock_hz) continue;
    if ((t == 0 && !caps->supports_1080p) ||
        (t == 1 && !caps->supports_720p) ||
        (t == 2 && !caps->supports_480p)) continue;
    int r = refresh_rank(m->refresh_millihz);
    if (!best || t < best_tier || (t == best_tier && r < best_refresh)) {
      best = m; best_tier = t; best_refresh = r;
    }
  }
  return best;
}

int hdmi_edid_auto_profile(const struct hdmi_edid_info *info) {
  if (!info) return 0;
  int best = 0;
  for (size_t i = 0; i < info->mode_count; ++i) {
    const struct hdmi_edid_mode *m = &info->modes[i];
    if (m->interlaced || !m->sync_separate || !m->hsync_positive) continue;
    if (m->width == 1920 && m->height == 1080 && m->pixel_clock_hz == 148500000 &&
        m->hfp == 88 && m->hsync == 44 && m->hbp == 148 &&
        m->vfp == 4 && m->vsync == 5 && m->vbp == 36 && m->vsync_positive)
      return 1;
    if (m->width == 1280 && m->height == 800 && m->pixel_clock_hz == 75000000 &&
        m->hfp == 64 && m->hsync == 48 && m->hbp == 124 &&
        m->vfp == 5 && m->vsync == 7 && m->vbp == 12 && !m->vsync_positive)
      best = 2;
    else if (best == 0 && m->width == 1280 && m->height == 720 &&
        m->pixel_clock_hz == 74250000 && m->hfp == 110 && m->hsync == 40 &&
        m->hbp == 220 && m->vfp == 5 && m->vsync == 5 && m->vbp == 20 &&
        m->vsync_positive)
      best = 3;
  }
  return best;
}
