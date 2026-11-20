#include <platform/bcm28xx/display_handoff.h>
#include <string.h>

const char *bcm_display_output_name(enum bcm_display_output output) {
  switch (output) {
    case BCM_DISPLAY_OUTPUT_DSI: return "dsi";
    case BCM_DISPLAY_OUTPUT_HDMI: return "hdmi";
    case BCM_DISPLAY_OUTPUT_COMPOSITE: return "composite";
    default: return "auto";
  }
}

static int config_value(const char *text, size_t length, const char *key,
                        const char **value, size_t *value_length) {
  size_t key_len = strlen(key);
  for (size_t i = 0; i + key_len < length; ++i) {
    if ((i == 0 || text[i - 1] == '\n') &&
        !strncmp(text + i, key, key_len) && text[i + key_len] == '=') {
      size_t start = i + key_len + 1, end = start;
      while (end < length && text[end] != '\n' && text[end] != '\r' && text[end] != '#') ++end;
      while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
      *value = text + start; *value_length = end - start; return 0;
    }
  }
  return -1;
}

int bcm_display_parse_config(const char *text, size_t length,
                             enum bcm_display_output *output,
                             enum bcm_display_composite_standard *standard) {
  const char *value; size_t n;
  if (!output || !standard || length > 4096 || (!text && length)) return -1;
  *output = BCM_DISPLAY_OUTPUT_AUTO; *standard = BCM_DISPLAY_STANDARD_PAL;
  if (text && !config_value(text, length, "display.output", &value, &n)) {
    if (n == 3 && !strncmp(value, "dsi", n)) *output = BCM_DISPLAY_OUTPUT_DSI;
    else if (n == 4 && !strncmp(value, "hdmi", n)) *output = BCM_DISPLAY_OUTPUT_HDMI;
    else if (n == 9 && !strncmp(value, "composite", n)) *output = BCM_DISPLAY_OUTPUT_COMPOSITE;
    else if (n != 4 || strncmp(value, "auto", n)) return -1;
  }
  if (text && !config_value(text, length, "display.composite_standard", &value, &n)) {
    if (n == 3 && !strncmp(value, "pal", n)) *standard = BCM_DISPLAY_STANDARD_PAL;
    else if (n == 4 && !strncmp(value, "ntsc", n)) *standard = BCM_DISPLAY_STANDARD_NTSC;
    else return -1;
  }
  return 0;
}
