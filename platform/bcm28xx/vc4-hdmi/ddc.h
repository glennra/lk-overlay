#pragma once

#include <stddef.h>
#include <stdint.h>

int hdmi_ddc_read_edid(uint8_t *buffer, size_t length);

/* Phase 2 diagnostic: read BSC2 status before any BSC2 write. */
int hdmi_ddc_test_idle_status(void);

/* Phase 2 diagnostic: configure/enable BSC2, then read status before ST. */
int hdmi_ddc_test_configured_status(void);
