/* Open-drain, bounded I2C for the Pi 3B+ FXL6408 on the SMPS bus. */
#include <dev/gpio.h>
#include <lk/reg.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/gpio.h>
#include <platform/bcm28xx/udelay.h>
#include "lanrun.h"

#define SDA 46u
#define SCL 47u
#define FXL_ADDRESS 0x43u
#define FSEL4 (GPIO_GPFSEL0 + 16u)
#define PIN_FIELDS ((7u << 18) | (7u << 21))

static void low(unsigned pin) { gpio_set(pin, 0); gpio_config(pin, kBCM2708PinmuxOut); }
static void release(unsigned pin) { gpio_config(pin, kBCM2708PinmuxIn); }
static void tick(void) { udelay(5); }
static int clock_high(void) {
  release(SCL);
  for (unsigned int i = 0; i < 100; i++) {
    if (gpio_get(SCL)) { tick(); return 0; }
    udelay(1);
  }
  return -1;
}
static int start(void) {
  release(SDA); tick();
  if (clock_high() || !gpio_get(SDA)) return -1;
  low(SDA); tick(); low(SCL); tick();
  return 0;
}
static void stop(void) {
  low(SDA); tick();
  if (clock_high()) { release(SDA); return; }
  release(SDA); tick();
}
static int send_byte(uint8_t value) {
  for (unsigned int i = 0; i < 8; i++) {
    if (value & 0x80) release(SDA); else low(SDA);
    tick(); if (clock_high()) return -1;
    low(SCL); tick(); value <<= 1;
  }
  release(SDA); tick();
  if (clock_high()) return -1;
  int nack = gpio_get(SDA);
  low(SCL); tick();
  return nack ? -1 : 0;
}
static int receive_byte(uint8_t *value) {
  uint8_t v = 0;
  release(SDA);
  for (unsigned int i = 0; i < 8; i++) {
    tick(); if (clock_high()) return -1;
    v = (uint8_t)((v << 1) | !!gpio_get(SDA));
    low(SCL); tick();
  }
  /* Single-byte read: NACK the last byte. */
  release(SDA); tick();
  if (clock_high()) return -1;
  low(SCL); tick(); *value = v;
  return 0;
}
static int read_reg(void *context, uint8_t reg, uint8_t *value) {
  (void)context;
  int ret = start();
  if (ret) return ret;
  ret = send_byte(FXL_ADDRESS << 1);
  if (!ret) ret = send_byte(reg);
  if (!ret) ret = start();
  if (!ret) ret = send_byte((FXL_ADDRESS << 1) | 1);
  if (!ret) ret = receive_byte(value);
  stop();
  return ret;
}
static int write_reg(void *context, uint8_t reg, uint8_t value) {
  (void)context;
  int ret = start();
  if (ret) return ret;
  ret = send_byte(FXL_ADDRESS << 1);
  if (!ret) ret = send_byte(reg);
  if (!ret) ret = send_byte(value);
  stop();
  return ret;
}
int wo052_lanrun_start(void) {
  const struct lanrun_bus bus = { .context = 0, .read = read_reg, .write = write_reg };
  /* Do not seize pins that another owner configured, or a busy/stuck bus. */
  if ((*REG32(FSEL4) & PIN_FIELDS) || !gpio_get(SDA) || !gpio_get(SCL)) return -6;
  int ret = lanrun_fxl_enable(&bus);
  release(SDA); release(SCL);
  return ret;
}
