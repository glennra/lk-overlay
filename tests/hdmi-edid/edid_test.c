#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../platform/bcm28xx/vc4-hdmi/edid.h"

static void checksum(uint8_t *b) {
  uint8_t sum = 0;
  for (unsigned i = 0; i < 127; ++i) sum = (uint8_t)(sum + b[i]);
  b[127] = (uint8_t)(0 - sum);
}

static void cta_vics(uint8_t *b, const uint8_t *vics, size_t count) {
  memset(b, 0, 128);
  b[0] = 0x02; b[1] = 0x03; b[2] = (uint8_t)(4 + 1 + count);
  b[4] = (uint8_t)((2u << 5) | count);
  memcpy(b + 5, vics, count);
  checksum(b);
}

static void base(uint8_t *b, uint8_t extensions) {
  memset(b, 0, 128);
  memcpy(b, "\x00\xff\xff\xff\xff\xff\xff\x00", 8);
  b[18] = 1; b[19] = 4; b[126] = extensions;
  checksum(b);
}

int main(void) {
  uint8_t edid[256];
  struct hdmi_edid_info info;
  struct hdmi_mode_caps caps = { 200000000, true, true, true };
  const struct hdmi_edid_mode *m;
  const uint8_t all[] = { 16, 4, 2 };

  base(edid, 1); cta_vics(edid + 128, all, sizeof(all));
  assert(hdmi_edid_parse(edid, sizeof(edid), &info) == 0);
  m = hdmi_edid_select(&info, &caps);
  assert(m && m->width == 1920 && m->height == 1080 && m->vic == 16);

  base(edid, 1); cta_vics(edid + 128, all + 1, 2);
  assert(hdmi_edid_parse(edid, sizeof(edid), &info) == 0);
  m = hdmi_edid_select(&info, &caps);
  assert(m && m->width == 1280 && m->height == 720 && m->vic == 4);

  base(edid, 1); cta_vics(edid + 1 * 128, all + 2, 1);
  assert(hdmi_edid_parse(edid, sizeof(edid), &info) == 0);
  m = hdmi_edid_select(&info, &caps);
  assert(m && m->width == 720 && m->height == 480 && m->vic == 2);

  {
    const uint8_t vic31[] = { 31 };
    base(edid, 1); cta_vics(edid + 128, vic31, sizeof(vic31));
    assert(hdmi_edid_parse(edid, sizeof(edid), &info) == 0);
    assert(info.mode_count == 1);
    assert(info.modes[0].vic == 31);
    assert(info.modes[0].pixel_clock_hz == 148500000);
  }

  edid[127] ^= 1;
  assert(hdmi_edid_parse(edid, sizeof(edid), &info) == -3);
  base(edid, 0);
  assert(hdmi_edid_parse(edid, sizeof(edid), &info) == -6);
  caps.supports_1080p = caps.supports_720p = caps.supports_480p = false;
  assert(hdmi_edid_select(&info, &caps) == NULL);
  puts("HDMI EDID parser and mode-selection tests passed");
  return 0;
}
