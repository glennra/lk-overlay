/*
 * VideoCore4_Drivers
 * Copyright (c) 2017 Authors of rpi-open-firmware
 *
 * USB PHY initialization driver.
 */

#include <kernel/thread.h>
#include <dev/uart.h>
#include <lk/console_cmd.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/a2w.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>

#define USB_GUSBCFG   0x7e98000c
#define USB_GUSBCFG_USB_TRD_TIM_LSB         10
#define USB_GUSBCFG_SRP_CAP_SET             0x00000100
#define USB_GUSBCFG_HNP_CAP_SET             0x00000200
#define USB_GUSBCFG_TERM_SEL_DL_PULSE_SET   0x00400000
#define USB_GUSBCFG_FORCE_DEV_MODE_SET      0x40000000
#define USB_GMDIOCSR  0x7e980080
#define FLAG_BUSY       BV(31)
#define USB_GMDIOGEN  0x7e980084
#define USB_GVBUSDRV  0x7e980088
#define USB_HCFG 0x7e980400
#define USB_HFIR 0x7e980404
#define USB_PHY_XOSC_USBEN (1u << 2)
#define USB_PHY_XOSC_USBOK (1u << 14)
#define USB_PHY_TDCLKEN_USBDFT (1u << 11)

#if HDMI_USB_PHY_PREFLIGHT
static void usb_phy_power_snapshot(const char *name, uint32_t address) {
  printf("USB PHY: before %s read 0x%08x\n", name, address);
  uart_flush_tx(0);
  uint32_t value = *REG32(address);
  printf("USB PHY: %s=0x%08x\n", name, value);
  uart_flush_tx(0);
}
#endif

//#define PHY_DEBUG

static void wait(void) {
#if !HDMI_USB_PHY_WRITE_ONLY
  while (*REG32(USB_GMDIOCSR) & FLAG_BUSY);
#endif
}

void write_bare(int reg, uint16_t value, int type) {
  reg &= 0x1F;

#if DSI_USB_PHY_PREFLIGHT || HDMI_USB_PHY_PREFLIGHT
  static int first_transaction = 1;
  printf("USB PHY: before MDIO reg=0x%02x type=0x%08x\n", reg, type);
  uart_flush_tx(0);
#endif
  /* precede MDIO access */
#if USB_PHY_MDIO_PRE_PREAMBLE && HDMI_USB_PHY_PREFLIGHT && !HDMI_USB_PHY_WRITE_ONLY
  if (first_transaction) {
    puts("USB PHY: pre-preamble CSR read begin addr=0x7e980080");
    uart_flush_tx(0);
    uint32_t before = *REG32(USB_GMDIOCSR);
    printf("USB PHY: pre-preamble CSR read returned value=0x%08x\n", before);
    uart_flush_tx(0);
  }
#endif
  *REG32(USB_GMDIOGEN) = 0xFFFFFFFF;
#if HDMI_USB_PHY_WRITE_ONLY
  udelay(100);
#endif
#if DSI_USB_PHY_PREFLIGHT || HDMI_USB_PHY_PREFLIGHT
  if (first_transaction) {
    puts("USB PHY: MDIO preamble write returned; before CSR read");
    uart_flush_tx(0);
#if !HDMI_USB_PHY_WRITE_ONLY
    uint32_t status = *REG32(USB_GMDIOCSR);
    printf("USB PHY: first CSR=0x%08x; before busy wait\n", status);
#else
    puts("USB PHY: CSR read skipped");
#endif
    uart_flush_tx(0);
  }
#endif
  wait();
#if DSI_USB_PHY_PREFLIGHT || HDMI_USB_PHY_PREFLIGHT
  if (first_transaction) {
    puts("USB PHY: preamble busy wait returned; before command write");
    uart_flush_tx(0);
  }
#endif

  /* write the actual value, with flags */
  *REG32(USB_GMDIOGEN) = type | (reg << 18) | value;
#if HDMI_USB_PHY_WRITE_ONLY
  udelay(100);
#endif
#if DSI_USB_PHY_PREFLIGHT || HDMI_USB_PHY_PREFLIGHT
  if (first_transaction) {
    puts("USB PHY: command write returned; before command busy wait");
    uart_flush_tx(0);
  }
#endif
  wait();

  /* dummy write due to errata; see BCM2835 peripheral manual */
  *REG32(USB_GMDIOGEN) = 0;
#if HDMI_USB_PHY_WRITE_ONLY
  udelay(100);
#endif
  wait();
#if DSI_USB_PHY_PREFLIGHT || HDMI_USB_PHY_PREFLIGHT
  if (first_transaction) {
    puts("USB PHY: first MDIO transaction returned");
    uart_flush_tx(0);
    first_transaction = 0;
  }
#endif
}

static uint16_t usb_read(int reg) {
#if HDMI_USB_PHY_WRITE_ONLY
  (void)reg;
  return 0;
#else
  write_bare(reg, 0, 0x60020000);
  uint16_t ret = *REG32(USB_GMDIOCSR) & 0xFFFFF;
#ifdef PHY_DEBUG
  printf("R 0x%x -> 0x%x\n", reg, ret);
#endif
  return ret;
#endif
}

static void usb_write(int reg, uint16_t value) {
#ifdef PHY_DEBUG
  printf("W 0x%x <- 0x%x\n", reg, value);
#endif
  write_bare(reg, value, 0x50020000);
}

static int cmd_phy_dump(int argc, const console_cmd_args *argv) {
  for (int i=0; i<0x20; i++) {
    uint32_t t = usb_read(i);
    if (t) printf("0x%02x == 0x%04x\n", i, t);
  }
  return 0;
}

#ifdef MEASURE_SOF
extern int measure_sof;
extern int sof_total;
#endif

static int cmd_phy_write(int argc, const console_cmd_args *argv) {
  if (argc != 3) {
    puts("usage: phy_write 0x12 0x1234");
    return 0;
  }

  usb_write(argv[1].u, argv[2].u);
#ifdef MEASURE_SOF
  if (argv[1].u == 0x17) {
    THREAD_LOCK(state);
    measure_sof = 0;
    sof_total = 0;
    THREAD_UNLOCK(state);
  }
#endif
  return 0;
}

#ifdef MEASURE_SOF
static int phy_divisor = 5682;

static int cmd_phy_up(int argc, const console_cmd_args *argv) {
  THREAD_LOCK(state);
  phy_divisor++;
  usb_write(0x17, phy_divisor);
  measure_sof = 0;
  sof_total = 0;
  printf("div %d\n", phy_divisor);
  THREAD_UNLOCK(state);
  return 0;
}

static int cmd_phy_down(int argc, const console_cmd_args *argv) {
  THREAD_LOCK(state);
  phy_divisor--;
  usb_write(0x17, phy_divisor);
  measure_sof = 0;
  sof_total = 0;
  printf("div %d\n", phy_divisor);
  THREAD_UNLOCK(state);
  return 0;
}
#endif

STATIC_COMMAND_START
STATIC_COMMAND("phy_dump", "dump all usb phy control regs", &cmd_phy_dump)
STATIC_COMMAND("phy_write", "write to the usb phy", &cmd_phy_write)
#ifdef MEASURE_SOF
STATIC_COMMAND("up", "", &cmd_phy_up)
STATIC_COMMAND("down", "", &cmd_phy_down)
#endif
STATIC_COMMAND_END(usbphy);

static void usbphy_init(uint level) {
  int devmode = 0;

#if DSI_USB_PHY_PREFLIGHT || HDMI_USB_PHY_PREFLIGHT
  puts("USB PHY: before power setup");
  uart_flush_tx(0);
#endif
  power_up_usb();

#if HDMI_USB_PHY_PREFLIGHT
  usb_phy_power_snapshot("PM_IMAGE", PM_IMAGE);
  usb_phy_power_snapshot("PM_USB", PM_USB);
  usb_phy_power_snapshot("CM_PERIICTL", CM_PERIICTL);
  usb_phy_power_snapshot("CM_PERIIDIV", CM_PERIIDIV);
  usb_phy_power_snapshot("CM_TDCLKEN", CM_TDCLKEN);
  usb_phy_power_snapshot("A2W_XOSC_CTRL", A2W_XOSC_CTRL);
#endif
#if DSI_USB_PHY_PREFLIGHT || HDMI_USB_PHY_PREFLIGHT
  puts("USB PHY: power setup returned; before PHY MMIO");
  uart_flush_tx(0);
#else
  puts("bringing up usb PHY...");
#endif
#if HDMI_USB_PHY_WRITE_ONLY
  puts("USB PHY: write-only MDIO mode; status reads and busy polling disabled");
#endif

#if USB_PHY_REFERENCE_CLOCK
  /* Reference PHY sequence: make the USB oscillator ready before MDIO. */
  *REG32(A2W_XOSC_CTRL) |= A2W_PASSWORD | USB_PHY_XOSC_USBEN;
  unsigned int clock_retries = 100000;
  while (!(*REG32(A2W_XOSC_CTRL) & USB_PHY_XOSC_USBOK) && --clock_retries)
    ;
  if (!clock_retries) {
    puts("USB PHY: oscillator not ready; PHY setup stopped");
    return;
  }
  *REG32(CM_TDCLKEN) |= CM_PASSWORD | USB_PHY_TDCLKEN_USBDFT;
  puts("USB PHY: reference clock enabled; before MDIO");
  uart_flush_tx(0);
#endif

  *REG32(USB_GMDIOCSR) = BV(18);

  udelay(1000);

  usb_write(0x15, devmode ? 4369 : 272);
  usb_write(0x19, 0x4);
  usb_write(0x18, devmode ? 0x342 : 0x10);
  usb_write(0x1D, 0x4);
  #if !HDMI_USB_PHY_SKIP_17
  usb_write(0x17, 0x1632); // based on crystal speed
  #else
  puts("USB PHY: skipping register 0x17 write");
  #endif
  // 0x32 * 160 == 8000, the microframe rate
  // both 0x1632 and 0x1432 seem to work

  int tries = 20;
  uint16_t phy_ready_status;

  while(((phy_ready_status = usb_read(0x1B)) & (1 << 7)) != 0) { if (tries-- <= 0) break; }
  printf("USB PHY: reg1b=0x%04x retries_left=%d %s\n", phy_ready_status,
         tries, tries < 0 ? "retry-limit" : "ready-bit-clear");
  uart_flush_tx(0);

#if HDMI_USB_PHY_WRITE_ONLY
  puts("USB PHY: clearing VBUS drive without status read");
  *REG32(USB_GVBUSDRV) = 0;
#else
  *REG32(USB_GVBUSDRV) &= ~BV(7);
#endif

  usb_write(0x1E, 0x8000);

  usb_write(0x1D, 0x5000);
  usb_write(0x19, 0xC004);
  usb_write(0x20, 0x1C2F);
  usb_write(0x22, 0x0100);
  usb_write(0x24, 0x0010);
  usb_write(0x19, 0x0004);


  // from here
#if HDMI_USB_PHY_WRITE_ONLY
  *REG32(USB_GVBUSDRV) = 0xD0000; // axi priority, without a status read
#else
  *REG32(USB_GVBUSDRV) = (*REG32(USB_GVBUSDRV) & 0xFFF0FFFF) | 0xD0000; // axi priority
#endif
  udelay(300);
  if (devmode) {
    *REG32(USB_GUSBCFG) =
      USB_GUSBCFG_FORCE_DEV_MODE_SET |
      USB_GUSBCFG_TERM_SEL_DL_PULSE_SET |
      (9 << USB_GUSBCFG_USB_TRD_TIM_LSB) |
      USB_GUSBCFG_HNP_CAP_SET |
      USB_GUSBCFG_SRP_CAP_SET;
  } else {
    if (0) {
      *REG32(0x7E980400 + 3084) = 0x20402700; // USB_HCFG + something
      udelay(300);
      *REG32(USB_HCFG) = 1;
      udelay(300);
      *REG32(USB_HFIR) = 0xBB80; // 48mhz / 0xBB80 == 1ms frame interval
      udelay(300);
    }
  }
  /* to here
   * something in this block is responsible for fixing the phy corrupting things bug
   * when the bug is in effect, tcp packets silently get corrupted
   */
  puts("usb PHY up");
}

LK_INIT_HOOK(usbphy, &usbphy_init, LK_INIT_LEVEL_PLATFORM + 5);
