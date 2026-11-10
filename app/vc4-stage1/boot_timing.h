#pragma once

enum stage1_boot_timing_event {
  STAGE1_BOOT_T0_APP_ENTRY = 1,
  STAGE1_BOOT_T1_SD_LOAD_BEGIN,
  STAGE1_BOOT_T2_STAGE2_LOAD_COMPLETE,
};

void stage1_boot_timing_mark(enum stage1_boot_timing_event event);
