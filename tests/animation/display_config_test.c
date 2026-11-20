#include <assert.h>
#include <string.h>
#include <platform/bcm28xx/display_handoff.h>

int main(void) {
  enum bcm_display_output output;
  enum bcm_display_composite_standard standard;
  const char pal[] = "# select composite\ndisplay.output=composite\n";
  const char ntsc[] = "display.output=composite\r\ndisplay.composite_standard=ntsc\r\n";
  const char invalid[] = "display.composite_standard=bad\n";
  assert(!bcm_display_parse_config(NULL, 0, &output, &standard));
  assert(output == BCM_DISPLAY_OUTPUT_AUTO && standard == BCM_DISPLAY_STANDARD_PAL);
  assert(!bcm_display_parse_config(pal, strlen(pal), &output, &standard));
  assert(output == BCM_DISPLAY_OUTPUT_COMPOSITE && standard == BCM_DISPLAY_STANDARD_PAL);
  assert(!bcm_display_parse_config(ntsc, strlen(ntsc), &output, &standard));
  assert(output == BCM_DISPLAY_OUTPUT_COMPOSITE && standard == BCM_DISPLAY_STANDARD_NTSC);
  assert(bcm_display_parse_config(invalid, strlen(invalid), &output, &standard));
  assert(bcm_display_parse_config(pal, 4097, &output, &standard));
  return 0;
}
