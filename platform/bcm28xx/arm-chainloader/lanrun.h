#pragma once
#include <stdint.h>

typedef int (*lanrun_read_fn)(void *context, uint8_t reg, uint8_t *value);
typedef int (*lanrun_write_fn)(void *context, uint8_t reg, uint8_t value);
struct lanrun_bus {
  void *context;
  lanrun_read_fn read;
  lanrun_write_fn write;
};
int lanrun_fxl_enable(const struct lanrun_bus *bus);
int wo052_lanrun_start(void);
