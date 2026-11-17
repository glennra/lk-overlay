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

  /* 1280x720 CTA timing as a detailed timing descriptor. */
  base(edid, 0);
  {
    const uint8_t dtd[18] = { 0x01, 0x1d, 0x00, 0x72, 0x51, 0xd0,
      0x1e, 0x20, 0x6e, 0x28, 0x55, 0x00, 0, 0, 0, 0, 0, 0x1e };
    memcpy(edid + 54, dtd, sizeof(dtd));
    checksum(edid);
    assert(hdmi_edid_parse(edid, 128, &info) == 0);
    assert(info.modes[0].width == 1280 && info.modes[0].height == 720);
    assert(info.modes[0].vfp == 5 && info.modes[0].vsync == 5);
    assert(info.modes[0].vbp == 20 && info.modes[0].refresh_millihz == 60000);
  }

  /* Captured LG Full HD preferred 1080p DTD. */
  base(edid, 0);
  {
    const uint8_t dtd[18] = {0x02,0x3a,0x80,0x18,0x71,0x38,0x2d,0x40,
      0x58,0x2c,0x45,0x00,0xe0,0x0e,0x11,0,0,0x1e};
    memcpy(edid + 54, dtd, sizeof(dtd)); checksum(edid);
    assert(hdmi_edid_parse(edid, 128, &info) == 0);
    assert(hdmi_edid_auto_profile(&info) == 1);
  }

  /* Captured 1280x800 panel DTD: negative vertical, positive horizontal. */
  base(edid, 0);
  {
    const uint8_t dtd[18] = {0x4c,0x1d,0x00,0xec,0x50,0x20,0x18,0x30,
      0x40,0x30,0x57,0x00,0xd9,0x88,0,0,0,0x1b};
    memcpy(edid + 54, dtd, sizeof(dtd)); checksum(edid);
    assert(hdmi_edid_parse(edid, 128, &info) == 0);
    assert(hdmi_edid_auto_profile(&info) == 2);
    assert(info.modes[0].hsync_positive && !info.modes[0].vsync_positive);
    edid[54+17] = 0x1f; checksum(edid);
    assert(hdmi_edid_parse(edid, 128, &info) == 0);
    assert(hdmi_edid_auto_profile(&info) == 0);
  }
  base(edid, 1); cta_vics(edid + 128, all, sizeof(all));
  assert(hdmi_edid_parse(edid, 256, &info) == 0);
  assert(hdmi_edid_auto_profile(&info) == 1);
  base(edid, 1); cta_vics(edid + 128, all + 1, 1);
  assert(hdmi_edid_parse(edid, 256, &info) == 0);
  assert(hdmi_edid_auto_profile(&info) == 3);
  {
    const uint8_t unsupported[] = {31};
    base(edid, 1); cta_vics(edid + 128, unsupported, 1);
    assert(hdmi_edid_parse(edid, 256, &info) == 0);
    assert(hdmi_edid_auto_profile(&info) == 0);
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
