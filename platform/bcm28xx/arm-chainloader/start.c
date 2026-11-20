#include <arch/ops.h>
#include <dev/gpio.h>
#include <dev/uart.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/arm.h>
#include <platform/bcm28xx/arm_chainloader.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/a2w.h>
#include <platform/bcm28xx/display_handoff.h>
#include <platform/bcm28xx/gpio.h>
#include <platform/bcm28xx/otp.h>
#include <platform/bcm28xx/pll.h>
#include <platform/bcm28xx/pll_read.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/udelay.h>
#include <platform/bcm28xx/boot_timing.h>
#include <stdio.h>
#if USB_LANRUN_EXPANDER
#include "lanrun.h"
#endif

extern const uint8_t arm_chainloader_start[], arm_chainloader_end[];

static void prepare_usb_host(uint32_t revision) {
  unsigned int type = (revision >> 4) & 0xff;
  int lan_run = 0, ethclk_pin = 0;
  switch (type) {
  case 4: lan_run = 31; ethclk_pin = 44; break;
  case 8: lan_run = 29; ethclk_pin = 42; break;
  case 0xd: lan_run = 30; ethclk_pin = 42; break;
  }
  if (ethclk_pin > 0) {
    *REG32(CM_GP1CTL) = CM_PASSWORD | CM_GPnCTL_KILL_SET;
    while (*REG32(CM_GP1CTL) & CM_GPnCTL_BUSY_SET) { }
    /* HDMI's Pi 3B+ USB hub uses PLLC_PER.  Preserve the original DSI
     * handoff clock source; changing it after scanout starts corrupts DSI. */
    uint32_t divisor = freq_pllc_per / (25000000 / 0x1000);
#if BCM283X_HDMI
    uint32_t clock_ctl = (2 << CM_GPnCTL_MASH_LSB) | CM_SRC_PLLC_PER;
#else
    uint32_t clock_ctl = (2 << CM_GPnCTL_MASH_LSB) | CM_SRC_PLLC_CORE0;
#endif
#if USB_STOCK_GP1_CLOCK
    if (type == 0x0d) {
      uint32_t lock = *REG32(CM_LOCK);
#if USB_PLLD_START
      uint32_t plld_ctrl = *REG32(A2W_PLLD_CTRL);
      uint32_t plld_per = *REG32(A2W_PLLD_PER);
      printf("USB PLLD: guard lock=0x%08x ctrl=0x%08x per=0x%08x\n", lock, plld_ctrl, plld_per);
      uart_flush_tx(0);
      if (!(lock & (1u << CM_LOCK_FLOCKD_BIT)) &&
          (plld_ctrl == 0 || plld_ctrl == A2W_PLL_CTRL_PWRDN) &&
          (plld_per == 0 || plld_per == (1u << A2W_PLLD_PER_CHENB_LSB))) {
        puts("USB PLLD: inactive; starting 2 GHz PLL and /4 peripheral");
        uart_flush_tx(0);
        int status = setup_plld(2000000000u, 4);
        if (!status) {
          uint32_t cm = *REG32(CM_PLLD);
          *REG32(CM_PLLD) = CM_PASSWORD | (cm | (1u << 6));
          *REG32(CM_PLLD) = CM_PASSWORD | (cm & ~((1u << 6) | (1u << 7)));
        }
        lock = *REG32(CM_LOCK);
        printf("USB PLLD: setup result=%d lock=0x%08x\n", status, lock);
        uart_flush_tx(0);
      } else {
        puts("USB PLLD: startup skipped; state not confirmed inactive");
        uart_flush_tx(0);
      }
#endif
      uint32_t rate = (lock & (1u << CM_LOCK_FLOCKD_BIT)) ?
          get_pll_chan_freq(PLL_CHAN_DPER) : 0;
      if (rate >= 499000000u && rate <= 501000000u) {
        divisor = 0x14d55;
        clock_ctl = (1 << CM_GPnCTL_MASH_LSB) | PERI_PLLD_PER;
        printf("USB GP1: stock clock selected PLLD_PER=%u divisor=0x%x\n", rate, divisor);
      } else {
        printf("USB GP1: PLLD unavailable or unexpected rate=%u lock=0x%08x; retaining LK clock\n", rate, lock);
      }
      uart_flush_tx(0);
    }
#endif
    *REG32(CM_GP1CTL) = CM_PASSWORD | clock_ctl;
    *REG32(CM_GP1DIV) = CM_PASSWORD | divisor;
    *REG32(CM_GP1CTL) = CM_PASSWORD | clock_ctl | CM_GPnCTL_ENAB_SET;
    gpio_config(ethclk_pin, kBCM2708Pinmux_ALT0);
  }
  if (lan_run > 0) {
    gpio_config(lan_run, kBCM2708PinmuxOut);
    gpio_set(lan_run, 0);
    udelay(type == 0xd ? 10000 : 1000);
    gpio_set(lan_run, 1);
  }
#if USB_LANRUN_EXPANDER
  if (type == 0x0d) {
    puts("USB LAN: FXL6408 line 3 setup begin");
    uart_flush_tx(0);
    int result = lanrun_expander_start();
    printf("USB LAN: FXL6408 line 3 setup result=%d\n", result);
    uart_flush_tx(0);
  }
#endif
}

#if HDMI_USB_EARLY_HUB && BCM283X_HDMI
static void hdmi_early_hub_init(uint level) {
  (void)level;
  uint32_t revision = otp_read(30);
  if (((revision >> 4) & 0xffu) != 0x0du)
    return;
  puts("USB hub: before early 3B+ reference clock and reset");
  uart_flush_tx(0);
  prepare_usb_host(revision);
  puts("USB hub: early reference clock and reset complete");
  uart_flush_tx(0);
}
LK_INIT_HOOK(hdmi_early_hub, &hdmi_early_hub_init, LK_INIT_LEVEL_PLATFORM + 4);
#endif

static void __attribute__((optimize("-O1"))) arm_chainloader_init(uint level) {
  (void)level;
#if HDMI_HOLD_ARM_BOOT
  puts("ARM: Linux handoff disabled; staying in LK app shell");
  return;
#endif
  bcm_boot_timing_mark(BCM_BOOT_T3_LK_ENTRY);
  struct bcm_display_handoff display = { 0 };
  uint32_t revision = otp_read(30), serial = otp_read(28);
  uint32_t uart_hz;
  size_t payload_size = (uintptr_t)arm_chainloader_end - (uintptr_t)arm_chainloader_start;
  /* The firmware-data producer currently describes a 1 GiB Pi 3B/3B+. */
  int display_status = bcm_display_get_handoff(&display);
  printf("DISPLAY_BACKEND=%s status=%d fb=0x%08x bus=0x%08x %ux%u pitch=%u reserved=0x%08x+0x%08x\n",
         bcm_display_backend_name(), display_status, display.fb_arm_phys,
         display.fb_bus_addr, display.width, display.height, display.pitch,
         display.reserved_base, display.reserved_size);
  if (!arm_chainloader_board_supported(revision) || display_status != BCM_DISPLAY_OK ||
      display.backend == 0) {
    puts("ARM: unsupported board or display handoff invalid; not releasing ARM");
    return;
  }
  arch_disable_ints();
  uart_hz = get_uart_base_freq();
  if (arm_chainloader_prepare_image((void *)0xc0000000u, 0x100000u,
                                    arm_chainloader_start, payload_size,
                                    serial, revision, uart_hz, display.fb_arm_phys) ||
      arm_chainloader_configure_mappings(REG32(ARM_TRANSLATE), ARM_CHAINLOADER_MAP_ENTRIES)) {
    puts("ARM: chainloader preparation failed; not releasing ARM");
    return;
  }
  printf("ARM: payload=%u bytes framebuffer=0x%08x uart=%u Hz\n", payload_size,
         display.fb_arm_phys, uart_hz);
#if DSI_USB_POWER_PREFLIGHT && !BCM283X_HDMI
  puts("DSI: before IMAGE/USB power setup");
  power_up_usb();
  puts("DSI: IMAGE/USB power setup returned");
#endif
#if HDMI_USB_EARLY_HUB && BCM283X_HDMI
  if (((revision >> 4) & 0xffu) != 0x0du)
#endif
    prepare_usb_host(revision);
  *REG32(ARM_CONTROL0) |= ARM_C0_BRESP2 | ARM_C0_SIZ1G | ARM_C0_APROTPASS |
                          ARM_C0_APROTMSK | ARM_C0_FULLPERI |
                          (0x8u << 20) | (0x5u << 24) | (0xau << 28);
  *REG32(ARM_CONTROL1) |= ARM_C1_PERSON;
  arch_disable_ints();
  if (arm_chainloader_setup_clock(1000)) {
    puts("ARM: clock initialization failed; not releasing ARM");
    return;
  }
  power_arm_start();
  puts("ARM: releasing chainloader");
  bcm_boot_timing_mark(BCM_BOOT_T11_ARM_HANDOFF);
  arm_chainloader_release_bridge();
  bcm_display_run_boot_animation();
}
LK_INIT_HOOK(arm_chainloader, &arm_chainloader_init, LK_INIT_LEVEL_PLATFORM + 10);
