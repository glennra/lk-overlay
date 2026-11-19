#include <lk/console_cmd.h>
#include <lk/reg.h>
#include <dev/uart.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/pll.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>

#define PM_PROC_ARMRSTN_CLR    0xffffffbf
#define PM_IMAGE_PERIRSTN_SET  0x00000040
#define PM_IMAGE_H264RSTN_SET  0x00000080
#define PM_IMAGE_ISPRSTN_SET   0x00000100

static int cmd_pm_dump(int argc, const console_cmd_args *argv);
static int cmd_pm_usb_on(int argc, const console_cmd_args *argv);

STATIC_COMMAND_START
#ifdef ARCH_VPU
STATIC_COMMAND("pm_dump_all", "dump power domain states", &cmd_pm_dump)
STATIC_COMMAND("pm_usb_on", "enable usb power domain", &cmd_pm_usb_on)
#endif
STATIC_COMMAND_END(pm);

#ifdef ARCH_VPU
#if IMAGE_TWO_STAGE_STARTUP && !VPU_STAGE2
/* Run only at stage-1 cold startup, before LK enables HDMI or SDRAM. */
static int wait_image_power_cycle(uint32_t mask, bool set) {
  for (unsigned int i = 0; i < 10000; ++i) {
    if (!!(*REG32(PM_IMAGE) & mask) == set) return 0;
    udelay(1);
  }
  return -1;
}

int image_two_stage_power_cycle(void) {
  uint32_t state = *REG32(PM_IMAGE);
  printf("IMAGE startup: entry stage=1 power_state=0x%08x\n", state);
  uart_flush_tx(0);
  /* Never cycle a domain that the ROM or an earlier owner left powered. */
  if (state & (PM_POWUP | PM_POWOK)) {
    puts("IMAGE startup: already powered; refusing restart");
    uart_flush_tx(0);
    return -1;
  }
  uint32_t pm = (state & 0x007f01ffu) | PM_PASSWORD | 0x00010000u;
  *REG32(PM_IMAGE) = pm;
  *REG32(PM_IMAGE) = pm | PM_POWUP;
  if (wait_image_power_cycle(PM_POWOK, true)) return -1;
  puts("IMAGE startup: CFG1 power-up complete"); uart_flush_tx(0);

  *REG32(PM_IMAGE) = pm;
  if (wait_image_power_cycle(PM_POWOK, false)) return -1;
  puts("IMAGE startup: power-down complete"); uart_flush_tx(0);

  pm = (pm & ~0x00030000u) | 0x00030000u | PM_POWUP;
  *REG32(PM_IMAGE) = pm;
  if (wait_image_power_cycle(PM_POWOK, true)) return -1;
  puts("IMAGE startup: CFG3 power-up complete"); uart_flush_tx(0);

  pm |= PM_ISPOW; *REG32(PM_IMAGE) = pm;
  pm |= PM_MEMREP; *REG32(PM_IMAGE) = pm;
  if (wait_image_power_cycle(PM_MRDONE, true)) return -1;
  pm |= PM_ISFUNC; *REG32(PM_IMAGE) = pm;
  puts("IMAGE startup: isolation/repair complete"); uart_flush_tx(0);

  /* Linux's 32-clock reset propagation delay; no v16mov reset window. */
  *REG32(CM_PERIICTL) = CM_PASSWORD | (*REG32(CM_PERIICTL) | 0x40u);
  udelay(1);
  *REG32(CM_PERIICTL) = CM_PASSWORD | (*REG32(CM_PERIICTL) & ~0x40u);
  udelay(1);
  pm |= PM_IMAGE_ISPRSTN_SET | PM_IMAGE_H264RSTN_SET |
        PM_IMAGE_PERIRSTN_SET;
  *REG32(PM_IMAGE) = pm;
  *REG32(CM_PERIICTL) = CM_PASSWORD | (*REG32(CM_PERIICTL) | 0x40u);
  puts("IMAGE startup: PERII gate/reset release complete"); uart_flush_tx(0);
  return 0;
}
#endif
void hdmi_enable_power_domain(void) {
  static bool domain_enabled = false;
  if (domain_enabled) return;
  puts("bringing HDMI domain online");

  *REG32(PM_HDMI) |= PM_PASSWORD | PM_HDMI_RSTDR;
  *REG32(PM_HDMI) |= PM_PASSWORD | PM_HDMI_CTRLEN;
  uint32_t t = *REG32(PM_HDMI);
  *REG32(PM_HDMI) = PM_PASSWORD | (t & ~PM_HDMI_LDOPD);
  udelay(100);
  *REG32(PM_HDMI) = PM_PASSWORD | (*REG32(PM_HDMI) & ~PM_HDMI_RSTDR);

  domain_enabled = true;
}

void power_up_image(void) {
  puts("image domain starting...");
#if IMAGE_TWO_STAGE_STARTUP
  /* Stage 1 is the only owner allowed to perform the cold-start sequence.
   * Do not rewrite CFG or reset state after HDMI/USB consumers are active. */
  if (*REG32(PM_IMAGE) & PM_POWUP) {
    puts("IMAGE startup: repeated request -> already initialized; no restart");
    return;
  }
#endif
  //dumpreg(PM_IMAGE);
  *REG32(PM_IMAGE) |= PM_PASSWORD | 0x10000 | BV(6); // CFG = 1
#if 0
  printf("PM_IMAGE: 0x%x\n", *REG32(PM_IMAGE));
  *REG32(PM_IMAGE) |= PM_PASSWORD | 1; // POWUP = 1
  for (int i=0; i<10; i++) {
    if (*REG32(PM_IMAGE) & 2) { // if power ok
      break;
    }
    udelay(1);
    if (i == 9) puts("PM_IMAGE timeout");
  }
  printf("PM_IMAGE: 0x%x\n", *REG32(PM_IMAGE));
  *REG32(PM_IMAGE) = PM_PASSWORD | (*REG32(PM_IMAGE) & ~1); // POWUP = 0

  *REG32(PM_IMAGE) |= PM_PASSWORD | 0x30000; // CFG = 3
  *REG32(PM_IMAGE) |= PM_PASSWORD | 1; // POWUP = 1
  for (int i=0; i<10; i++) {
    if (*REG32(PM_IMAGE) & 2) { // if power ok
      break;
    }
    udelay(1);
    if (i == 9) puts("PM_IMAGE timeout");
  }
  printf("PM_IMAGE: 0x%x\n", *REG32(PM_IMAGE));
  *REG32(PM_IMAGE) |= PM_PASSWORD | 0x40;
#endif
  //dumpreg(PM_IMAGE);
  power_domain_on(REG32(PM_IMAGE), (PM_IMAGE_ISPRSTN_SET | PM_IMAGE_H264RSTN_SET | PM_IMAGE_PERIRSTN_SET), "IMAGE");
  //dumpreg(PM_IMAGE);
}

void power_up_usb(void) {
  // TODO, move power domain code into its own driver
  power_up_image();

  puts("usb power on...");
  *REG32(PM_USB) = PM_PASSWORD | 1;
  udelay(600);

  //dumpreg(CM_PERIICTL);
  *REG32(CM_PERIIDIV) = CM_PASSWORD | 0x1000;
  *REG32(CM_PERIICTL) |= CM_PASSWORD | 0x40;
  //dumpreg(CM_PERIICTL);
  *REG32(CM_PERIICTL) = (*REG32(CM_PERIICTL) & 0xffffffbf) | CM_PASSWORD;
  //dumpreg(CM_PERIICTL);
  //dumpreg(CM_PERIIDIV);

  *REG32(PM_IMAGE) |= PM_PASSWORD | PM_V3DRSTN; // TODO, PM_V3DRSTN is actually PM_IMAGE_PERIRSTN
  *REG32(CM_PERIICTL) |= CM_PASSWORD | 0x40;
}

void power_domain_on(volatile uint32_t *reg, uint32_t rstn, const char *name) {
  printf("bringing up %s domain, reg: 0x%08x\n", name, (uint32_t)reg);
  /* If it was already powered on by the fw, leave it that way. */
  if (*REG32(reg) & PM_POWUP) {
    puts("already on");
    return;
  }
  /* Enable power */
  *REG32(reg) |= PM_PASSWORD | PM_POWUP;

  puts("waiting");
  while (!(*REG32(reg) & PM_POWOK)) {
    udelay(1); // TODO, add timeout
  }

  /* Disable electrical isolation */
  *REG32(reg) |= PM_PASSWORD | PM_ISPOW;

  puts("mem rep");
  /* Repair memory */
  *REG32(reg) |= PM_PASSWORD | PM_MEMREP;
  while (!(*REG32(reg) & PM_MRDONE)) {
    udelay(1); // TODO, add timeout
  }
  /* Disable functional isolation */
  *REG32(reg) |= PM_PASSWORD | PM_ISFUNC;

  printf("de-aserting reset lines\n");
  *REG32(reg) |= PM_PASSWORD | rstn;
}

void power_arm_start(void) {
  power_domain_on(REG32(PM_PROC), PM_PROC_ARMRSTN_CLR, "PROC");
}

static void dump_power_domain(const char *name, uint32_t pmreg) {
  volatile uint32_t *reg = REG32(pmreg);
  uint32_t v = *reg;
  printf("%8s: 0x%x == 0x%08x  ", name, pmreg, v);
  printf("%2s  %2s %2s", v & PM_POWUP ? "UP":"", v & PM_POWOK ? "OK":"", v & PM_ENABLE ? "EN":"");
  if ((pmreg == PM_IMAGE) && (v & BV(7))) printf("  H264RSTN");
  puts("");
}

static int cmd_pm_dump(int argc, const console_cmd_args *argv) {
  dump_power_domain("PM_USB",   PM_USB);
  dump_power_domain("PM_SMPS",  PM_SMPS);
  dump_power_domain("PM_IMAGE", PM_IMAGE);
  dump_power_domain("PM_GRAFX", PM_GRAFX);
  dump_power_domain("PM_PROC",  PM_PROC);
  dump_power_domain("PM_HDMI",  PM_HDMI);
  return 0;
}

static int cmd_pm_usb_on(int argc, const console_cmd_args *argv) {
  power_up_usb();
  return 0;
}
#endif
