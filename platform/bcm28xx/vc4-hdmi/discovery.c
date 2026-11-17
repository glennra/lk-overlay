#include "ddc.h"
#include "edid.h"
#include <lk/console_cmd.h>
#include <dev/uart.h>
#include <stdio.h>

/* Explicitly invoked: BSC2 MMIO has previously stalled the VPU. */
static int cmd_hdmi_discover(int argc, const console_cmd_args *argv) {
  static uint8_t edid[256];
  static struct hdmi_edid_info info;
  (void)argc;
  (void)argv;
  puts("HDMI discovery: reading EDID, BSC2 may stall; connect sink first");
  uart_flush_tx(0);
  int rc = hdmi_ddc_read_edid(edid, sizeof(edid));
  if (rc) {
    printf("HDMI discovery: DDC failed (%d); current video unchanged\n", rc);
    return rc;
  }
  size_t length = edid[126] ? 256 : 128;
  puts("HDMI discovery: raw EDID follows");
  for (size_t i = 0; i < length; ++i) {
    printf("%02x%s", edid[i], (i % 16 == 15) ? "\n" : " ");
  }
  rc = hdmi_edid_parse(edid, length, &info);
  if (rc) {
    printf("HDMI discovery: EDID invalid or unsupported (%d)\n", rc);
    return rc;
  }
  printf("HDMI discovery: %u modes, %u extensions, %s sink\n",
         (unsigned)info.mode_count, info.extension_count,
         info.hdmi_signalling ? "HDMI" : "DVI or HDMI capability unknown");
  for (size_t i = 0; i < info.mode_count; ++i) {
    const struct hdmi_edid_mode *m = &info.modes[i];
    printf("  %ux%u%s %u mHz pixel %u Hz VIC %u H %u/%u/%u V %u/%u/%u\n",
           m->width, m->height, m->interlaced ? "i" : "p",
           m->refresh_millihz, m->pixel_clock_hz, m->vic,
           m->hfp, m->hsync, m->hbp, m->vfp, m->vsync, m->vbp);
  }
  /* Standard timings advertise resolution and nominal refresh, not exact
   * porches. Report them separately rather than inventing scanout timings. */
  for (size_t i = 38; i < 54; i += 2) {
    unsigned a = edid[i], b = edid[i + 1];
    if ((a == 1 && b == 1) || (a == 0 && b == 0)) continue;
    unsigned width = (a + 31) * 8;
    unsigned ratio = b >> 6;
    unsigned height;
    if (ratio == 0) height = edid[18] == 1 && edid[19] < 3 ? width : width * 10 / 16;
    else if (ratio == 1) height = width * 3 / 4;
    else if (ratio == 2) height = width * 4 / 5;
    else height = width * 9 / 16;
    printf("  standard %ux%u %u Hz (exact timing not decoded)\n",
           width, height, (b & 0x3f) + 60);
  }
  puts("HDMI discovery: reporting only; output mode unchanged");
  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("hdmi_discover", "read and report EDID (BSC2 may stall)", &cmd_hdmi_discover)
STATIC_COMMAND_END(hdmi_discovery);
