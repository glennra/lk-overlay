#pragma once

#include <stdint.h>

int bcm_dsi7_scanout(void);
int bcm_dsi7_framebuffer_handoff(uint32_t *physical);

/* Animate the firmware-owned framebuffer until Linux fbcon writes to it. */
void bcm_dsi7_run_boot_animation(void);
