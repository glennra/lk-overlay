#include "ddc.h"

#include <platform/bcm28xx/i2c.h>
#include <platform/bcm28xx/arm.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/udelay.h>
#include <dev/gpio.h>
#include <dev/uart.h>
#include <lk/reg.h>
#include <stdio.h>

#define HDMI_DDC_ADDR 0x50u
#define HDMI_DDC_RATE 100000u
#define HDMI_DDC_TIMEOUT_US 20000u
#define HDMI_DDC_PERIPHERAL_CLOCK_HZ 250000000u
#define HDMI_HPD_FIRMWARE_GPIO 4u
#define HDMI_PROPERTY_CHANNEL 8u
#define HDMI_MAILBOX_TIMEOUT_US 500000u
#define HDMI_MAILBOX_EMPTY 0x40000000u
#define HDMI_MAILBOX_FULL 0x80000000u
#define HDMI_MAILBOX_STATUS ((uintptr_t)(ARM0_MAILBOX_BASE + 0x18))
#define HDMI_MAILBOX_READ ((uintptr_t)(ARM0_MAILBOX_BASE + 0x00))
#define HDMI_MAILBOX_WRITE ((uintptr_t)(ARM0_MAILBOX_BASE + 0x20))

struct bsc_regs {
  volatile uint32_t control;
  volatile uint32_t status;
  volatile uint32_t data_length;
  volatile uint32_t slave_address;
  volatile uint32_t fifo;
  volatile uint32_t clock_divider;
  volatile uint32_t data_delay;
  volatile uint32_t clock_stretch_timeout;
};

#define BSC2 ((struct bsc_regs *)(uintptr_t)I2C2_BASE)
#define BSC_C_READ 0x1u
#define BSC_C_CLEAR 0x30u
#define BSC_C_START 0x80u
#define BSC_C_ENABLE 0x8000u
#define BSC_S_TA 0x1u
#define BSC_S_DONE 0x2u
#define BSC_S_TXD 0x10u
#define BSC_S_RXD 0x20u
#define BSC_S_ERR 0x100u
#define BSC_S_CLKT 0x200u

static void hdmi_ddc_enable_peri_image_clock(void) {
  static bool enabled;
  if (enabled) return;
  puts("HDMI DDC: enabling PERI_IMAGE clock");
  *REG32(CM_PERIIDIV) = CM_PASSWORD | 0x1000;
  *REG32(CM_PERIICTL) |= CM_PASSWORD | 0x40;
  *REG32(CM_PERIICTL) = (*REG32(CM_PERIICTL) & 0xffffffbf) | CM_PASSWORD;
  *REG32(CM_PERIICTL) |= CM_PASSWORD | 0x40;
  enabled = true;
}

int hdmi_ddc_test_idle_status(void) {
  struct bsc_regs *bsc0 =
      (struct bsc_regs *)(uintptr_t)I2C0_BASE;
  struct bsc_regs *regs = BSC2;
  puts("HDMI DDC TEST A: before idle BSC0 status read");
  uart_flush_tx(0);
  uint32_t bsc0_status = bsc0->status;
  uart_flush_tx(0);
  printf("HDMI DDC TEST A: idle BSC0 status=0x%08x\n", bsc0_status);
  puts("HDMI DDC TEST A: before idle BSC2 status read");
  uart_flush_tx(0);
  uint32_t status = regs->status;
  uart_flush_tx(0);
  printf("HDMI DDC TEST A: idle BSC2 status=0x%08x\n", status);
  return (int)status;
}

int hdmi_ddc_test_configured_status(void) {
  struct bsc_regs *bsc1 =
      (struct bsc_regs *)(uintptr_t)I2C1_BASE;
  struct bsc_regs *regs = BSC2;
  uint32_t divider = HDMI_DDC_PERIPHERAL_CLOCK_HZ / HDMI_DDC_RATE;
  if (!divider || divider > 0xffffu) divider = 0xffffu;

  puts("HDMI DDC TEST B: reading PERI_IMAGE clock state");
  uart_flush_tx(0);
  uint32_t perii_ctl = *REG32(CM_PERIICTL);
  uint32_t perii_div = *REG32(CM_PERIIDIV);
  uart_flush_tx(0);
  printf("HDMI DDC TEST B: CM_PERIICTL=0x%08x CM_PERIIDIV=0x%08x\n",
         perii_ctl, perii_div);
  puts("HDMI DDC TEST B: enabling existing PERI_IMAGE clock sequence");
  *REG32(CM_PERIIDIV) = CM_PASSWORD | 0x1000;
  *REG32(CM_PERIICTL) |= CM_PASSWORD | 0x40;
  *REG32(CM_PERIICTL) = (*REG32(CM_PERIICTL) & 0xffffffbf) | CM_PASSWORD;
  *REG32(CM_PERIICTL) |= CM_PASSWORD | 0x40;
  printf("HDMI DDC TEST B: PERI_IMAGE clock state=0x%08x div=0x%08x\n",
         *REG32(CM_PERIICTL), *REG32(CM_PERIIDIV));
  puts("HDMI DDC TEST B: before idle BSC1 status read");
  uart_flush_tx(0);
  uint32_t bsc1_status = bsc1->status;
  uart_flush_tx(0);
  printf("HDMI DDC TEST B: idle BSC1 status=0x%08x\n", bsc1_status);
  puts("HDMI DDC TEST B: configuring BSC2 without starting transfer");
  regs->control = 0;
  regs->status = BSC_S_CLKT | BSC_S_ERR | BSC_S_DONE;
  regs->control = BSC_C_CLEAR;
  regs->clock_divider = divider;
  regs->data_delay = ((divider / 16u) << 16) | (divider / 4u);
  regs->clock_stretch_timeout = 0;
  regs->slave_address = HDMI_DDC_ADDR;
  regs->data_length = 1;
  regs->control = BSC_C_ENABLE;
  puts("HDMI DDC TEST B: before configured idle BSC2 status read");
  uart_flush_tx(0);
  uint32_t status = regs->status;
  uart_flush_tx(0);
  printf("HDMI DDC TEST B: configured idle BSC2 status=0x%08x\n", status);
  return (int)status;
}

static int wait_status(struct bsc_regs *regs, uint32_t mask) {
  for (unsigned elapsed = 0; elapsed < HDMI_DDC_TIMEOUT_US; ++elapsed) {
    if (regs->status & mask) return 0;
    udelay(1);
  }
  return -1;
}

static void clear_bsc(struct bsc_regs *regs) {
  puts("HDMI DDC: clearing BSC status");
  regs->status = BSC_S_CLKT | BSC_S_ERR | BSC_S_DONE;
  puts("HDMI DDC: BSC status cleared");
  /* CLEAR is issued with I2CEN disabled, matching the BCM2835 driver. */
  regs->control = BSC_C_CLEAR;
  puts("HDMI DDC: BSC clear command written");
}

static int hdmi_property_call(uint32_t *message) {
  for (unsigned elapsed = 0; elapsed < HDMI_MAILBOX_TIMEOUT_US; ++elapsed) {
    if ((*REG32(HDMI_MAILBOX_STATUS) & HDMI_MAILBOX_FULL) == 0) break;
    udelay(1);
    if (elapsed + 1 == HDMI_MAILBOX_TIMEOUT_US) return -1;
  }
  *REG32(HDMI_MAILBOX_WRITE) =
      ((uint32_t)((uintptr_t)message & 0x3fffffff) + BCM_SDRAM_BUS_ADDR_BASE) |
      HDMI_PROPERTY_CHANNEL;

  for (unsigned elapsed = 0; elapsed < HDMI_MAILBOX_TIMEOUT_US; ++elapsed) {
    if ((*REG32(HDMI_MAILBOX_STATUS) & HDMI_MAILBOX_EMPTY) == 0) {
      uint32_t reply = *REG32(HDMI_MAILBOX_READ);
      if ((reply & 0xf) == HDMI_PROPERTY_CHANNEL) return 0;
    }
    udelay(1);
  }
  return -1;
}

/* Pi 3 exposes HDMI HPD through the firmware GPIO controller (expgpio line
 * 4), not through a BCM2837 GPIO pin.  Query the stock firmware property
 * interface rather than sampling an unrelated physical GPIO. */
static int hdmi_hpd_firmware_gpio(void) {
  static uint32_t message[7] __ALIGNED(16);
  message[0] = sizeof(message);
  message[1] = 0;
  message[2] = 0x00030041; /* GET_GPIO_STATE */
  message[3] = 4;
  message[4] = 0;
  message[5] = HDMI_HPD_FIRMWARE_GPIO;
  message[6] = 0;

  if (hdmi_property_call(message) != 0) return -1;
  if (message[1] != 0x80000000 ||
      (message[4] & 0x80000000) == 0) return -1;
  return (message[5] & 1) ? 1 : 0;
}

static int ddc_write_offset(struct bsc_regs *regs) {
  uint32_t divider = HDMI_DDC_PERIPHERAL_CLOCK_HZ / HDMI_DDC_RATE;
  if (!divider || divider > 0xffffu) divider = 0xffffu;
  puts("HDMI DDC: programming divider and address");
  regs->clock_divider = divider;
  regs->data_delay = ((divider / 16u) << 16) | (divider / 4u);
  regs->clock_stretch_timeout = 0;
  regs->slave_address = HDMI_DDC_ADDR;
  regs->data_length = 1;
  puts("HDMI DDC: starting offset write");
  regs->control = BSC_C_ENABLE | BSC_C_START;
  puts("HDMI DDC: waiting for TXD");
  if (wait_status(regs, BSC_S_TXD | BSC_S_ERR | BSC_S_CLKT)) {
    puts("HDMI DDC: TXD wait failed");
    return -1;
  }
  puts("HDMI DDC: TXD ready");
  if (regs->status & (BSC_S_ERR | BSC_S_CLKT)) return -1;
  regs->fifo = 0;
  puts("HDMI DDC: offset byte written; waiting for DONE");
  if (wait_status(regs, BSC_S_DONE | BSC_S_ERR | BSC_S_CLKT)) return -1;
  puts("HDMI DDC: offset write DONE");
  if (regs->status & (BSC_S_ERR | BSC_S_CLKT)) return -1;
  return 0;
}

static int ddc_read_block(struct bsc_regs *regs, uint8_t *out) {
  regs->data_length = 128;
  regs->control = BSC_C_ENABLE | BSC_C_START | BSC_C_READ;
  unsigned remaining = 128;
  for (unsigned elapsed = 0; elapsed < HDMI_DDC_TIMEOUT_US && remaining;
       ++elapsed) {
    uint32_t status = regs->status;
    if (status & (BSC_S_ERR | BSC_S_CLKT)) return -1;
    if (status & BSC_S_RXD) {
      *out++ = (uint8_t)regs->fifo;
      --remaining;
    } else {
      udelay(1);
    }
  }
  if (remaining || wait_status(regs, BSC_S_DONE | BSC_S_ERR | BSC_S_CLKT))
    return -1;
  return (regs->status & (BSC_S_ERR | BSC_S_CLKT)) ? -1 : 0;
}

int hdmi_ddc_read_edid(uint8_t *buffer, size_t length) {
  if (!buffer || length < 128) return -1;

  /* Do not start a BSC2 transaction when no sink has asserted HPD: the
   * peripheral can leave the VPU stalled on the first status read after
   * START in that state. */
  int hpd_n = hdmi_hpd_firmware_gpio();
  printf("HDMI DDC: firmware HPD_N expgpio4=%d\n", hpd_n);
  if (hpd_n < 0) {
    puts("HDMI DDC: firmware HPD query failed; skipping BSC2 transaction");
    return -3;
  }
  if (hpd_n != 0) {
    puts("HDMI DDC: HPD not asserted; skipping BSC2 transaction");
    return -2;
  }

  hdmi_ddc_enable_peri_image_clock();
  /* BSC2 is the dedicated HDMI DDC controller; no GPIO pinmux is needed. */
  puts("HDMI DDC: using dedicated BSC2 pins");
  struct bsc_regs *regs = BSC2;
  puts("HDMI DDC: BSC2 pointer ready");
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    printf("HDMI DDC: attempt %u enabling BSC2\n", attempt + 1);
    regs->control = 0;
    clear_bsc(regs);
    if (ddc_write_offset(regs) == 0 && ddc_read_block(regs, buffer) == 0) {
      clear_bsc(regs);
      regs->control = 0;
      return 0;
    }
    clear_bsc(regs);
    udelay(100);
  }
  regs->control = 0;
  return -1;
}
