#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HDMI_EDID_MAX_MODES 32

struct hdmi_edid_mode {
  uint16_t width;
  uint16_t height;
  uint32_t pixel_clock_hz;
  uint16_t hfp, hsync, hbp;
  uint16_t vfp, vsync, vbp;
  uint32_t refresh_millihz;
  uint8_t vic;
  uint8_t aspect;
  bool interlaced;
  bool sync_separate, hsync_positive, vsync_positive;
  bool hdmi_signalling;
};

struct hdmi_edid_info {
  struct hdmi_edid_mode modes[HDMI_EDID_MAX_MODES];
  size_t mode_count;
  uint8_t extension_count;
  bool hdmi_signalling;
};

struct hdmi_mode_caps {
  uint32_t max_pixel_clock_hz;
  bool supports_1080p;
  bool supports_720p;
  bool supports_480p;
};

int hdmi_edid_parse(const uint8_t *edid, size_t length,
                    struct hdmi_edid_info *info);
const struct hdmi_edid_mode *hdmi_edid_select(
    const struct hdmi_edid_info *info, const struct hdmi_mode_caps *caps);

/* Restricted automatic diagnostic modes, matched on exact timings. */
int hdmi_edid_auto_profile(const struct hdmi_edid_info *info);
