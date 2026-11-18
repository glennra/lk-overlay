/* Pi 3B+ FXL6408 line 3 only; preserve all unrelated outputs. */
#include "lanrun.h"
#define FXL_ID 0x01
#define FXL_DIR 0x03
#define FXL_OUT 0x05
#define FXL_HIZ 0x07
#define LAN_RUN 0x08

int lanrun_fxl_enable(const struct lanrun_bus *bus) {
  uint8_t id, dir, out, hiz, v;
  if (!bus || !bus->read || !bus->write) return -1;
  /* Reading ID clears the FXL6408 reset-interrupt flag. */
  if (bus->read(bus->context, FXL_ID, &id) || (id & 0xfcu) != 0xa0u)
    return -2;
  if (bus->read(bus->context, FXL_DIR, &dir) ||
      bus->read(bus->context, FXL_OUT, &out) ||
      bus->read(bus->context, FXL_HIZ, &hiz)) return -3;
  if (!(out & LAN_RUN) && bus->write(bus->context, FXL_OUT, out | LAN_RUN)) return -4;
  if (!(dir & LAN_RUN) && bus->write(bus->context, FXL_DIR, dir | LAN_RUN)) return -4;
  if ((hiz & LAN_RUN) && bus->write(bus->context, FXL_HIZ, hiz & ~LAN_RUN)) return -4;
  if (bus->read(bus->context, FXL_OUT, &v) || v != (uint8_t)(out | LAN_RUN)) return -5;
  if (bus->read(bus->context, FXL_DIR, &v) || v != (uint8_t)(dir | LAN_RUN)) return -5;
  if (bus->read(bus->context, FXL_HIZ, &v) || v != (uint8_t)(hiz & ~LAN_RUN)) return -5;
  return 0;
}
