#include <lk/init.h>
#include <dev/uart.h>
#include <stdio.h>
#include <platform/bcm28xx/display_handoff.h>

int bcm_display_probe_hdmi(void);
int bcm_display_probe_dsi(void);
int bcm_display_init_hdmi(void);
int bcm_display_init_dsi(void);
int bcm_display_get_handoff_hdmi(struct bcm_display_handoff *out);
int bcm_display_get_handoff_dsi(struct bcm_display_handoff *out);
const char *bcm_display_backend_name_hdmi(void);
const char *bcm_display_backend_name_dsi(void);
void bcm_display_run_boot_animation_hdmi(void);
void bcm_display_run_boot_animation_dsi(void);
int bcm_display_probe_composite(void);
int bcm_display_init_composite(enum bcm_display_composite_standard standard);
int bcm_display_get_handoff_composite(struct bcm_display_handoff *out);
void bcm_display_run_boot_animation_composite(void);
const char *bcm_display_backend_name(void);

static enum bcm_display_backend_id selected;
static int selected_ready;
static enum bcm_display_output requested_output = BCM_DISPLAY_OUTPUT_AUTO;
static enum bcm_display_composite_standard composite_standard = BCM_DISPLAY_STANDARD_PAL;

extern uint32_t bcm_boot_display_selection;

static const char *probe_name(int result) {
  switch (result) {
    case BCM_DISPLAY_PROBE_PRESENT: return "PRESENT";
    case BCM_DISPLAY_PROBE_ABSENT: return "ABSENT";
    case BCM_DISPLAY_PROBE_UNKNOWN: return "UNKNOWN";
    default: return "ERROR";
  }
}

int bcm_display_manager_init(void) {
  uint32_t boot_selection = bcm_boot_display_selection;
  if ((boot_selection & 0xffff0000u) == BCM_DISPLAY_BOOT_MAGIC &&
      (boot_selection & 0xffu) <= BCM_DISPLAY_OUTPUT_COMPOSITE &&
      ((boot_selection >> 8) & 0xffu) <= BCM_DISPLAY_STANDARD_NTSC) {
    requested_output = boot_selection & 0xffu;
    composite_standard = (boot_selection >> 8) & 0xffu;
  }
  printf("DISPLAY: output=%s composite-standard=%s\n",
         bcm_display_output_name(requested_output), composite_standard == BCM_DISPLAY_STANDARD_PAL ? "pal" : "ntsc");
  if (requested_output == BCM_DISPLAY_OUTPUT_COMPOSITE) {
    selected = BCM_DISPLAY_COMPOSITE;
    puts("DISPLAY: starting COMPOSITE initialization");
    uart_flush_tx(0);
    if (bcm_display_init_composite(composite_standard) || bcm_display_get_handoff_composite(&(struct bcm_display_handoff){0})) return -1;
    selected_ready = 1; puts("DISPLAY: selected=COMPOSITE reason=explicit-config"); return 0;
  }
  int dsi_probe = bcm_display_probe_dsi();
  printf("DISPLAY: DSI probe=%s\n", probe_name(dsi_probe));
  /* HDMI HPD/core reads are not safe on this BCM2837 path.  HDMI remains a
   * staged attempt only after the passive DSI probe is negative. */
  int hdmi_probe = dsi_probe == BCM_DISPLAY_PROBE_PRESENT ?
      BCM_DISPLAY_PROBE_ABSENT : BCM_DISPLAY_PROBE_UNKNOWN;
  printf("DISPLAY: HDMI probe=%s\n", probe_name(hdmi_probe));

  if (requested_output == BCM_DISPLAY_OUTPUT_DSI ||
      (requested_output == BCM_DISPLAY_OUTPUT_AUTO && dsi_probe == BCM_DISPLAY_PROBE_PRESENT)) {
    selected = BCM_DISPLAY_DSI;
    puts("DISPLAY: selected=DSI reason=panel-present");
    if (bcm_display_init_dsi()) {
      puts("DISPLAY: DSI initialization failed; ARM held");
      return -1;
    }
  } else {
    selected = BCM_DISPLAY_HDMI;
    puts("DISPLAY: selected=HDMI reason=staged-attempt-after-DSI-absent-or-unknown");
    /* The pre-merge HDMI path initialized at PLATFORM + 9.  Leave the
     * selected backend pending until the late hook so DSI keeps its original
     * PLATFORM + 1 timing without moving HDMI earlier. */
    return 0;
  }
  selected_ready = 1;
  printf("DISPLAY: READY backend=%s\n", bcm_display_backend_name());
  return 0;
}

static int bcm_display_manager_init_hdmi(void) {
  if (selected != BCM_DISPLAY_HDMI || selected_ready)
    return 0;
  if (bcm_display_init_hdmi()) {
    puts("DISPLAY: HDMI initialization failed; ARM held");
    return -1;
  }
  struct bcm_display_handoff hdmi;
  if (bcm_display_get_handoff_hdmi(&hdmi)) {
    puts("DISPLAY: HDMI readiness/framebuffer validation failed; ARM held");
    return -1;
  }
  selected_ready = 1;
  printf("DISPLAY: READY backend=%s\n", bcm_display_backend_name());
  return 0;
}

int bcm_display_selected(void) { return selected_ready ? selected : 0; }

int bcm_display_get_handoff(struct bcm_display_handoff *out) {
  if (!selected_ready)
    return BCM_DISPLAY_NOT_READY;
  if (selected == BCM_DISPLAY_DSI) return bcm_display_get_handoff_dsi(out);
  if (selected == BCM_DISPLAY_COMPOSITE) return bcm_display_get_handoff_composite(out);
  return bcm_display_get_handoff_hdmi(out);
}

const char *bcm_display_backend_name(void) {
  if (selected == BCM_DISPLAY_DSI) return bcm_display_backend_name_dsi();
  if (selected == BCM_DISPLAY_COMPOSITE) return "COMPOSITE";
  return bcm_display_backend_name_hdmi();
}

void bcm_display_run_boot_animation(void) {
  if (selected == BCM_DISPLAY_DSI)
    bcm_display_run_boot_animation_dsi();
  else if (selected == BCM_DISPLAY_HDMI)
    bcm_display_run_boot_animation_hdmi();
  else
    bcm_display_run_boot_animation_composite();
}

static void bcm_display_manager_hook(uint level) {
  (void)level;
  bcm_display_manager_init();
}

LK_INIT_HOOK(bcm_display_manager, &bcm_display_manager_hook,
             LK_INIT_LEVEL_PLATFORM + 1);

static void bcm_display_manager_hdmi_hook(uint level) {
  (void)level;
  bcm_display_manager_init_hdmi();
}

LK_INIT_HOOK(bcm_display_manager_hdmi, &bcm_display_manager_hdmi_hook,
             LK_INIT_LEVEL_PLATFORM + 9);
