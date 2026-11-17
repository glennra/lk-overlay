#include <app.h>
#include <dev/uart.h>
#include <lk/console_cmd.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/pv.h>
#include <platform/bcm28xx/pll.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/udelay.h>
#include <platform/bcm28xx/a2w.h>
#include <stdio.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <platform/bcm28xx/hdmi_framebuffer.h>
#include <stdlib.h>

#if HDMI_DIAG_DVI && HDMI_MODE_1080P60 && !HDMI_DIAG_REDOX_BACKGROUND
#error "HDMI_DIAG_DVI is limited to the 640x480 diagnostic"
#endif

#define logf(fmt, ...) { print_timestamp(); printf("[hdmi:%s:%d]: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); }

// based heavily on https://github.com/raspberrypi/linux/blob/rpi-5.15.y/drivers/gpu/drm/vc4/vc4_hdmi.c

#define BIT(n) (1<<n)

#define HD_HDM_CTL                  0x7e80800c
#define HD_HDM_CTL_ENABLE           BIT(0)
#define HD_HDM_CTL_ENDIAN           BIT(1)
#define HD_HDM_CTL_SW_RST           BIT(2)
#define HD_MAI_CTL                  0x7e808014
#define HD_MAI_THR                  0x7e808018
#define HD_MAI_FMT                  0x7e80801c
#define HD_MAI_DAT                  0x7e808020
#define HDMI_VID_CTL                0x7e808038
#define HDMI_CSC_CTL                0x7e808040
#define HDMI_CSC_12_11              0x7e808044
#define HDMI_CSC_14_13              0x7e808048
#define HDMI_CSC_22_21              0x7e80804c
#define HDMI_CSC_24_23              0x7e808050
#define HDMI_CSC_32_31              0x7e808054
#define HDMI_CSC_34_33              0x7e808058
// frame counter reset
#define HD_VID_CTL_RST_FRAMEC       BIT(29)
// underflow enable
#define HD_VID_CTL_UFEN             BIT(30)
#define HD_VID_CTL_ENABLE           BIT(31)
#define HD_VID_CTL_VSYNC_LOW        BIT(28)
#define HD_VID_CTL_HSYNC_LOW        BIT(27)
#define HD_VID_CTL_BLANKPIX         BIT(18)
#define HD_VID_CTL_BLANK_INSERT_EN  BIT(16)
#define HD_VID_CTL_CLRRGB           BIT(23)
#define HDMI_CSC_CTL_ORDER_BGR      (1u << 5)

#define HDMI_SW_RESET_CNTRL         0x7e902004
#define HDMI_FIFO_CTL               0x7e90205c
#define VC4_HDMI_FIFO_CTL_MASTER_SLAVE_N                  BIT(0)
#define VC4_HDMI_FIFO_CTL_RECENTER                        BIT(6)
#define VC4_HDMI_FIFO_CTL_RECENTER_DONE                   BIT(14)

#define HDMI_RAM_PACKET_CONFIG      0x7e9020a0
#define HDMI_SCHEDULER_CONTROL      0x7e9020c0
#define HDMI_CORE_REV               0x7e902000
#define HDMI_HOTPLUG                0x7e90200c
#define VC4_HDMI_SCHEDULER_CONTROL_IGNORE_VSYNC_PREDICTS  BIT(5)
#define VC4_HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT BIT(15)
#define HDMI_HORZA           0x7e9020c4
#define HDMI_HORZB           0x7e9020c8
#define HDMI_VERTA0           0x7e9020cc

#define HDMI_VERTB0           0x7e9020d0
#define HDMI_VERTA1           0x7e9020d4
#define HDMI_VERTB1           0x7e9020d8

#define HDMI_MISC_CONTROL     0x7e9020e4

// TODO, merge
#define HDMI_TX_PHY_RESET_CTL        0x7e9022c0
#define HDMI_TX_PHY_TX_PHY_RESET_CTL 0x7e9022c0

#define VC4_HDMI_SW_RESET_HDMI  BIT(0)
#define VC4_HDMI_SW_RESET_FORMAT_DETECT   BIT(1)

#define HDMI_SCHEDULER_CONTROL_IGN_VSYNC_PREDS_SET         0x00000020
#define HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT_SET           0x00008000
#define HDMI_SCHEDULER_CONTROL_MODE_HDMI_SET               0x00000001


#define VC4_HDMI_HORZA_HPOS BIT(13)
#define VC4_HDMI_HORZA_VPOS BIT(14)


#define VC4_HD_VID_CTL_CLRRGB BIT(23)
#define VC4_HDMI_RAM_PACKET_ENABLE              BIT(16)

#define A2W_HDMI_CTL0 0x7e102080
#define A2W_HDMI_CTL1 0x7e102084
#define A2W_HDMI_CTL2 0x7e102088
#define A2W_HDMI_CTL3 0x7e10208c

#if HDMI_MODE_1080P60
#define HDMI_MODE_HACTIVE 1920
#define HDMI_MODE_VACTIVE 1080
#define HDMI_MODE_PIXEL_CLOCK_HZ 148500000
#define HDMI_MODE_HSM_CLOCK_HZ 150000000
#define HDMI_EXPECTED_HSM_DIV  0x9e60u
#define HDMI_EXPECTED_PIX_DIV  1u
#define HDMI_MODE_HFP 88
#define HDMI_MODE_HSYNC 44
#define HDMI_MODE_HBP 148
#define HDMI_MODE_VFP 4
#define HDMI_MODE_VSYNC 5
#define HDMI_MODE_VBP 36
#define HDMI_MODE_SYNC_POSITIVE 1
#else
#define HDMI_MODE_HACTIVE 640
#define HDMI_MODE_VACTIVE 480
#define HDMI_MODE_PIXEL_CLOCK_HZ 25200000
#define HDMI_MODE_HSM_CLOCK_HZ 126000000
#define HDMI_EXPECTED_HSM_DIV  0x2000u
#define HDMI_EXPECTED_PIX_DIV  3u
#define HDMI_MODE_HFP 16
#define HDMI_MODE_HSYNC 96
#define HDMI_MODE_HBP 48
#define HDMI_MODE_VFP 10
#define HDMI_MODE_VSYNC 2
#define HDMI_MODE_VBP 33
#define HDMI_MODE_SYNC_POSITIVE 0
#endif

static uint32_t *hdmi_fb;
static uint32_t hdmi_active;
static bool hdmi_ready;

/* Explicit, single-load diagnostic: an inaccessible HDMI bus can stall the
 * load itself, so do not perform it during normal boot. */
static int cmd_hdmi_core_probe(int argc, const console_cmd_args *argv) {
  if (!hdmi_ready) {
    puts("HDMI core probe: HDMI initialization not completed");
    return -1;
  }
  puts("HDMI core probe: before read 0x7e902000");
  uart_flush_tx(0);
  uint32_t revision = *REG32(HDMI_CORE_REV);
  printf("HDMI core probe: after read CORE_REV=0x%08x\n", revision);
  uart_flush_tx(0);
  return 0;
}

static int cmd_hdmi_pllh_probe(int argc, const console_cmd_args *argv) {
  puts("HDMI PLLH probe: before CM_PLLH read 0x7e101110");
  uart_flush_tx(0);
  uint32_t cm_pllh = *REG32(CM_PLLH);
  printf("HDMI PLLH probe: CM_PLLH=0x%08x\n", cm_pllh);
  puts("HDMI PLLH probe: before PLLH_AUX read 0x7e102360");
  uart_flush_tx(0);
  uint32_t pllh_aux = *REG32(A2W_PLLH_AUX);
  printf("HDMI PLLH probe: PLLH_AUX=0x%08x\n", pllh_aux);
  puts("HDMI PLLH probe: before PERIICTL read 0x7e101020");
  uart_flush_tx(0);
  uint32_t perii = *REG32(CM_PERIICTL);
  printf("HDMI PLLH probe: PERIICTL=0x%08x\n", perii);
  uart_flush_tx(0);
  return 0;
}

static int cmd_hdmi_status_probe(int argc, const console_cmd_args *argv) {
  if (!hdmi_ready) {
    puts("HDMI status probe: HDMI initialization not completed");
    return -1;
  }
  puts("HDMI status probe: before FIFO_CTL read 0x7e90205c");
  uart_flush_tx(0);
  uint32_t fifo = *REG32(HDMI_FIFO_CTL);
  printf("HDMI status probe: FIFO_CTL=0x%08x\n", fifo);
  puts("HDMI status probe: before SCHEDULER_CONTROL read 0x7e9020c0");
  uart_flush_tx(0);
  uint32_t scheduler = *REG32(HDMI_SCHEDULER_CONTROL);
  printf("HDMI status probe: SCHEDULER_CONTROL=0x%08x\n", scheduler);
  uart_flush_tx(0);
  return 0;
}

static int cmd_hdmi_pv_probe(int argc, const console_cmd_args *argv) {
  if (!hdmi_ready) {
    puts("HDMI PV probe: HDMI initialization not completed");
    return -1;
  }
  puts("HDMI PV probe: before CONTROL read 0x7e807000");
  uart_flush_tx(0);
  uint32_t control = *REG32(0x7e807000);
  printf("HDMI PV probe: CONTROL=0x%08x\n", control);
  puts("HDMI PV probe: before V_CONTROL read 0x7e807004");
  uart_flush_tx(0);
  uint32_t video = *REG32(0x7e807004);
  printf("HDMI PV probe: V_CONTROL=0x%08x\n", video);
  puts("HDMI PV probe: before INT_STATUS read 0x7e807028");
  uart_flush_tx(0);
  uint32_t status = *REG32(0x7e807028);
  printf("HDMI PV probe: INT_STATUS=0x%08x\n", status);
  uart_flush_tx(0);
  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("hdmi_core_probe", "read HDMI core revision once (may stall)", &cmd_hdmi_core_probe)
STATIC_COMMAND("hdmi_pllh_probe", "read PLLH clock control and AUX status", &cmd_hdmi_pllh_probe)
STATIC_COMMAND("hdmi_status_probe", "read HDMI FIFO and scheduler status (may stall)", &cmd_hdmi_status_probe)
STATIC_COMMAND("hdmi_pv_probe", "read PV2 control and status (may stall)", &cmd_hdmi_pv_probe)
STATIC_COMMAND_END(hdmi_core_probe);

static const struct pv_timings hdmi_timings = {
  .vfp = HDMI_MODE_VFP, .vsync = HDMI_MODE_VSYNC, .vbp = HDMI_MODE_VBP,
  .vactive = HDMI_MODE_VACTIVE,
  .hfp = HDMI_MODE_HFP, .hsync = HDMI_MODE_HSYNC, .hbp = HDMI_MODE_HBP,
  .hactive = HDMI_MODE_HACTIVE, .clock_mux = clk_dpi_smi_hdmi,
  .interlaced = false,
};

static void vc4_hdmi_set_timings(void);
static void vc4_hdmi_phy_init(void);
static void vc4_hdmi_reset(void);

/* Read-only E0 diagnostic.  Keep the address and value together so UART
 * evidence distinguishes a bad register definition from a uniform bus
 * response.  All addresses are documented BCM2835-family read-safe status
 * registers; this function performs no writes or polling. */
static void hdmi_mmio_snapshot(const char *phase) {
  printf("HDMI MMIO %s: PM_HDMI[0x%08x]=0x%08x "
         "HSMCTL[0x%08x]=0x%08x HSMDIV[0x%08x]=0x%08x "
         "PLLH_PIX[0x%08x]=0x%08x\n",
         phase, PM_HDMI, *REG32(PM_HDMI), CM_HSMCTL, *REG32(CM_HSMCTL),
         CM_HSMDIV, *REG32(CM_HSMDIV), A2W_PLLH_PIX, *REG32(A2W_PLLH_PIX));
  /* HDMI core-bank reads can stall the VPU before the block reset has
   * completed. Keep this diagnostic limited to clock/power registers. */
}

/* Validate only the clock/power registers that are known to be readable on
 * this BCM2837 path.  This proves the programmed clock-manager state, not
 * electrical TMDS activity at the connector. */
static bool hdmi_verify_clock_state(void) {
  uint32_t lock = *REG32(CM_LOCK);
  uint32_t hsmctl = *REG32(CM_HSMCTL);
  uint32_t hsmdiv = *REG32(CM_HSMDIV);
  uint32_t pix = *REG32(A2W_PLLH_PIX);
  bool pllh_locked = (lock & (1u << CM_LOCK_FLOCKH_BIT)) != 0;
  bool hsm_running = (hsmctl & (CM_HSMCTL_ENAB_SET |
                                CM_HSMCTL_GATE_SET |
                                CM_HSMCTL_BUSY_SET)) ==
                     (CM_HSMCTL_ENAB_SET | CM_HSMCTL_GATE_SET |
                      CM_HSMCTL_BUSY_SET);
  bool hsm_configured = (hsmctl & 0xfu) == PERI_PLLH_AUX &&
                        (!(HDMI_EXPECTED_HSM_DIV & 0xfffu) ||
                         (hsmctl & CM_HSMCTL_FRAC_SET) != 0) &&
                        hsmdiv == HDMI_EXPECTED_HSM_DIV;
  bool pix_enabled = (pix & (1u << A2W_PLLH_PIX_CHENB_LSB)) == 0;
  bool pix_configured = (pix & A2W_PLLH_PIX_DIV_SET) == HDMI_EXPECTED_PIX_DIV;

  printf("HDMI clock verify: CM_LOCK=0x%08x HSMCTL=0x%08x "
         "HSMDIV=0x%08x PLLH_PIX=0x%08x\n",
         lock, hsmctl, hsmdiv, pix);
  printf("HDMI clock verify: PLLH_LOCK=%s HSM_RUNNING=%s "
         "HSM_CONFIG=%s PLLH_PIX_ENABLED=%s PLLH_PIX_CONFIG=%s\n",
         pllh_locked ? "yes" : "no", hsm_running ? "yes" : "no",
         hsm_configured ? "yes" : "no", pix_enabled ? "yes" : "no",
         pix_configured ? "yes" : "no");
  return pllh_locked && hsm_running && hsm_configured && pix_enabled &&
         pix_configured;
}

static void hdmi_fill_test_pattern(void) {
  static const uint32_t colors[] = {
    0xffffffffu, 0xffffff00u, 0xff00ffffu, 0xff00ff00u,
    0xffff00ffu, 0xffff0000u, 0xff0000ffu, 0xff000000u,
  };
  for (unsigned y = 0; y < HDMI_MODE_VACTIVE; ++y) {
    for (unsigned x = 0; x < HDMI_MODE_HACTIVE; ++x) {
      uint32_t color = colors[(x * 8u) / HDMI_MODE_HACTIVE];
      if (x < 4 || x >= HDMI_MODE_HACTIVE - 4 || y < 4 ||
          y >= HDMI_MODE_VACTIVE - 4)
        color = 0xffffffffu;
      hdmi_fb[y * HDMI_MODE_HACTIVE + x] = color;
    }
  }
}

static void hdmi_prepare_scanout(void) {
  hvs_initialize();
  hdmi_fb = (uint32_t *)(uintptr_t)(BCM_SDRAM_BUS_ADDR_BASE_NO_L2 |
                                    HDMI_FB_PRIMARY_PHYS);
  hdmi_active = HDMI_FB_PRIMARY_PHYS;
  hdmi_fill_test_pattern();
  hvs_configure_channel(1, HDMI_MODE_HACTIVE, HDMI_MODE_VACTIVE, false);

  /* Seven-word, unscaled ARGB8888 list; HVS1 feeds PV2 on BCM2837.
   * CONTROL_WORDS(7) includes the control word itself, so the terminator is
   * not part of this entry. */
  unsigned start = display_slot;
  dlist_memory[display_slot++] = CONTROL_VALID | CONTROL_WORDS(7) |
      CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_ABGR) | CONTROL_UNITY |
      CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGBA8888);
  dlist_memory[display_slot++] = POS0_X(0) | POS0_Y(0) | POS0_ALPHA(0xff);
  dlist_memory[display_slot++] = POS2_H(HDMI_MODE_VACTIVE) | POS2_W(HDMI_MODE_HACTIVE);
  dlist_memory[display_slot++] = 0xc0c0c0c0u;
  dlist_memory[display_slot++] = 0xc0000000u | HDMI_FB_PRIMARY_PHYS;
  dlist_memory[display_slot++] = 0xc0c0c0c0u;
  dlist_memory[display_slot++] = HDMI_FB_STRIDE_BYTES;
  channels[1].dlist_target = start;
  *REG32(SCALER_DISPLIST1) = start;
  setup_pixelvalve(&hdmi_timings, 2);
  enable_pixelvalve(2);
  hvs_channels[1].dispctrl = SCALER_DISPCTRLX_ENABLE |
      SCALER_DISPCTRL_W(HDMI_MODE_HACTIVE) | SCALER_DISPCTRL_H(HDMI_MODE_VACTIVE);
  puts("HDMI: HVS channel enabled");
  /* PV2 register readback can stall the VPU while HDMI clocks are starting;
   * the programmed values are already known by the PV driver. */
  printf("HDMI: HVS1 list=%u PV2 scanout enabled\n", start);
}

#if HDMI_DIAG_REDOX_BACKGROUND
#if !HDMI_MODE_1080P60 || !HDMI_DIAG_DVI || !HDMI_HOLD_ARM_BOOT
#error "S1 requires 1080p, DVI signalling and ARM boot hold"
#endif
#ifdef BCM283X_DSI_HVS
#error "S1 must not reset or occupy an active DSI HVS"
#endif
/* Diagnostic implementation derived from the ordered hardware sequence in
 * redox-rpi/rpi-open-firmware a4f24c913bfb7405a564109c8c1f68e97b49143b,
 * bootcode/hdmi.c (GPL-2.0-or-later). This file's adaptations are likewise
 * offered under GPL-2.0-or-later for this experimental profile.
 * LK starts PLLH earlier, and its stage-2 startup is not Redox's selector.
 * This opt-in profile programs PLLC_PER to 500 MHz during early LK startup;
 * it changes a shared clock and must not be used as production firmware. */
static void s1_marker(const char *s) {
  puts(s);
  uart_flush_tx(0);
}

static bool s1_pllh(void) {
  s1_marker("S1: PLLH start (1485 MHz; PIX /1)");
  *REG32(A2W_XOSC_CTRL) |= A2W_PASSWORD | A2W_XOSC_CTRL_HDMIEN_SET;
  *REG32(CM_PLLH) = CM_PASSWORD | CM_PLL_ANARST_SET | CM_PLLH_DIGRST_SET;
  *REG32(A2W_PLLH_CTRL) = A2W_PASSWORD | A2W_PLL_CTRL_PWRDN;
  *REG32(A2W_PLLH_ANA3) = A2W_PASSWORD;
  *REG32(A2W_PLLH_ANA2) = A2W_PASSWORD;
  *REG32(A2W_PLLH_ANA1) = A2W_PASSWORD | 0x0c;
  *REG32(A2W_PLLH_ANA0) = A2W_PASSWORD | 0x900000;
  *REG32(A2W_PLLH_FRAC) = A2W_PASSWORD | 0x58000;
  *REG32(A2W_PLLH_CTRL) = A2W_PASSWORD | A2W_PLL_CTRL_PWRDN | 0x104d;
  *REG32(A2W_PLLH_CTRL) = A2W_PASSWORD | 0x104d;
  *REG32(CM_PLLH) = CM_PASSWORD | CM_PLLH_DIGRST_SET;
  *REG32(CM_PLLH) = CM_PASSWORD;
  for (unsigned i = 0; i < 10000; ++i) {
    if (*REG32(CM_LOCK) & (1u << CM_LOCK_FLOCKH_BIT)) goto locked;
    udelay(1);
  }
  puts("S1: PLLH lock timeout");
  return false;
locked:
  *REG32(A2W_PLLH_CTRL) = A2W_PASSWORD | 0x104d | A2W_PLL_CTRL_PRSTN;
  *REG32(A2W_PLLH_PIX) = A2W_PASSWORD | 1;
  /* HSM uses PLLC_PER; preserve the inherited AUX state. */
  *REG32(CM_PLLH) = CM_PASSWORD | CM_PLLH_LOADPIX;
  *REG32(CM_PLLH) = CM_PASSWORD;
  return true;
}

static void s1_background_init(void) {
  s1_marker("S1: 1080p60 DVI colour bars, ARM held");
  s1_marker("S1: LK 0a00cdfffce83387a22a7aa0c044ce4b90e5f4cc; experimental uncommitted profile");
  s1_marker("S1: HVS colour-bar plane");
  /* Match Redox's channel-1 background path; this target excludes DSI. */
  if (!(*REG32(SCALER_DISPCTRL) & SCALER_DISPCTRL_ENABLE))
    *REG32(SCALER_DISPCTRL) = 0x9a0c0000u;
  /* Reuse the reference diagnostic's static RGBA8888 colour bars. */
  hdmi_fb = (uint32_t *)(uintptr_t)(BCM_SDRAM_BUS_ADDR_BASE_NO_L2 |
                                    HDMI_FB_PRIMARY_PHYS);
  hdmi_active = HDMI_FB_PRIMARY_PHYS;
  hdmi_fill_test_pattern();
  dlist_memory[16] = CONTROL_VALID | CONTROL_WORDS(7) |
      CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_ABGR) | CONTROL_UNITY |
      CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGBA8888);
  dlist_memory[17] = POS0_X(0) | POS0_Y(0) | POS0_ALPHA(0xff);
  dlist_memory[18] = POS2_H(HDMI_MODE_VACTIVE) | POS2_W(HDMI_MODE_HACTIVE);
  dlist_memory[19] = 0xc0c0c0c0u;
  dlist_memory[20] = 0xc0000000u | HDMI_FB_PRIMARY_PHYS;
  dlist_memory[21] = 0xc0c0c0c0u;
  dlist_memory[22] = HDMI_FB_STRIDE_BYTES;
  dlist_memory[23] = 0x80000000u;
  hvs_channels[1].dispbase = 0x3c8f0800u;
  *REG32(SCALER_DISPLIST1) = 16;
  hvs_channels[1].dispctrl = SCALER_DISPCTRLX_RESET;
  hvs_channels[1].dispctrl = 0;
  hvs_channels[1].dispbkgnd = SCALER_DISPBKGND_FILL | 0x00ff8000u;
  hvs_channels[1].dispctrl = SCALER_DISPCTRLX_ENABLE | (1920u << 12) | 1080u;

  s1_marker("S1: IMAGE and HDMI power prerequisites");
  /* Keep the proven LK USB/IMAGE prerequisite as an explicit adaptation. */
  power_up_usb();
  hdmi_enable_power_domain();
  s1_marker("S1: A2W HDMI CTL3..0");
  *REG32(A2W_HDMI_CTL3) = PM_PASSWORD | 0x000040;
  *REG32(A2W_HDMI_CTL2) = PM_PASSWORD | 0x18008e;
  *REG32(A2W_HDMI_CTL1) = PM_PASSWORD | 0x011c00;
  *REG32(A2W_HDMI_CTL0) = PM_PASSWORD | 0x470238;
  if (!s1_pllh()) return;
  s1_marker("S1: HSM PLLC_PER target 163682864 Hz (divider 0x30e0)");
  if (freq_pllc_per != 500000000u) {
    puts("S1: PLLC_PER is not 500 MHz; refusing HSM setup");
    return;
  }
  if (!clock_set_hsm(163682864, PERI_PLLC_PER)) {
    puts("S1: HSM setup failed");
    return;
  }
  for (unsigned i = 0; i < 10000; ++i) {
    if (*REG32(CM_HSMCTL) & CM_HSMCTL_BUSY_SET) goto hsm_ready;
    udelay(1);
  }
  puts("S1: HSM busy timeout");
  return;
hsm_ready:
  printf("S1: HSMCTL=0x%08x HSMDIV=0x%08x\n",
         *REG32(CM_HSMCTL), *REG32(CM_HSMDIV));
  if (*REG32(CM_HSMDIV) != 0x30e0u) {
    puts("S1: unexpected HSM divider");
    return;
  }
  s1_marker("S1: before HD M_CTL read 0x7e80800c");
  uint32_t hd_ctl = *REG32(HD_HDM_CTL);
  printf("S1: HD M_CTL=0x%08x\n", hd_ctl);
  s1_marker("S1: before HDMI CORE_REV read 0x7e902000");
  uint32_t revision = *REG32(HDMI_CORE_REV);
  printf("S1: HDMI CORE_REV=0x%08x\n", revision);
  uart_flush_tx(0);
  s1_marker("S1: encoder/core/PHY reset");
  *REG32(HD_HDM_CTL) = 4;
  udelay(1);
  *REG32(HD_HDM_CTL) = 0;
  *REG32(HD_HDM_CTL) = 0x201;
  *REG32(HDMI_SW_RESET_CNTRL) = 3;
  *REG32(HDMI_SW_RESET_CNTRL) = 0;
  s1_marker("S1: before PHY reset assert 0x7e9022c0");
  *REG32(HDMI_TX_PHY_RESET_CTL) = 0x000f0000;
  *REG32(HDMI_TX_PHY_RESET_CTL) = 0;
  s1_marker("S1: HDMI timings and video disabled");
  s1_marker("S1: before scheduler timing read 0x7e9020c0");
  uint32_t scheduler = *REG32(HDMI_SCHEDULER_CONTROL);
  *REG32(HDMI_SCHEDULER_CONTROL) = scheduler | 0x00008020u;
  *REG32(HDMI_HORZA) = 0x00006780;
  *REG32(HDMI_HORZB) = 0x0940b058;
  *REG32(HDMI_VERTA0) = 0x00508438;
  *REG32(HDMI_VERTB0) = 0x00000024;
  *REG32(HDMI_VERTA1) = 0x00508438;
  *REG32(HDMI_VERTB1) = 0x00000024;
  *REG32(HDMI_MISC_CONTROL) = 0;
  *REG32(HDMI_VID_CTL) = 0;
  *REG32(HDMI_CSC_CTL) = HDMI_CSC_CTL_ORDER_BGR;
  s1_marker("S1: FIFO master and PV2 setup");
  *REG32(HDMI_FIFO_CTL) = 1;
  /* Known writes, without unqualified reads of the potentially stalled PV. */
  s1_marker("S1: before PV2 control zero 0x7e807000");
  *REG32(0x7e807000) = 0;
  s1_marker("S1: before PV2 FIFO clear 0x7e807000");
  *REG32(0x7e807000) = 3;
  s1_marker("S1: before PV2 control reset 0x7e807000");
  *REG32(0x7e807000) = 0;
  *REG32(0x7e80700c) = 0x0094002c;
  *REG32(0x7e807010) = 0x00580780;
  *REG32(0x7e807014) = 0x00240005;
  *REG32(0x7e807018) = 0x00040438;
  s1_marker("S1: before PV2 video control 0x7e807004");
  *REG32(0x7e807004) = 2;
  s1_marker("S1: before PV2 control setup 0x7e807000");
  *REG32(0x7e807000) = 0x00177004;
  s1_marker("S1: before PV2 control enable 0x7e807000");
  *REG32(0x7e807000) = 0x00177005;
  s1_marker("S1: video, DVI and FIFO recenter");
  *REG32(HDMI_VID_CTL) = HD_VID_CTL_ENABLE | HD_VID_CTL_UFEN |
      HD_VID_CTL_RST_FRAMEC | HD_VID_CTL_CLRRGB | HD_VID_CTL_BLANK_INSERT_EN;
  s1_marker("S1: before video control read 0x7e808038");
  *REG32(HDMI_VID_CTL) = *REG32(HDMI_VID_CTL) & ~HD_VID_CTL_BLANKPIX;
  s1_marker("S1: before PV2 video read 0x7e807004");
  *REG32(0x7e807004) = *REG32(0x7e807004) | 1u;
  *REG32(HDMI_RAM_PACKET_CONFIG) = 0;
  s1_marker("S1: before scheduler DVI read 0x7e9020c0");
  *REG32(HDMI_SCHEDULER_CONTROL) = *REG32(HDMI_SCHEDULER_CONTROL) & ~1u;
  s1_marker("S1: before scheduler DVI polling 0x7e9020c0");
  bool dvi_ready = false;
  for (unsigned i = 0; i < 100000; ++i) {
    if (!(*REG32(HDMI_SCHEDULER_CONTROL) & 2u)) {
      dvi_ready = true;
      break;
    }
    udelay(1);
  }
  if (!dvi_ready) {
    puts("S1: DVI scheduler timeout");
    return;
  }
  s1_marker("S1: before FIFO read 0x7e90205c");
  uint32_t fifo = *REG32(HDMI_FIFO_CTL) & 0xefffu;
  *REG32(HDMI_FIFO_CTL) = fifo & ~VC4_HDMI_FIFO_CTL_RECENTER;
  *REG32(HDMI_FIFO_CTL) = fifo | VC4_HDMI_FIFO_CTL_RECENTER;
  udelay(1000);
  *REG32(HDMI_FIFO_CTL) = fifo & ~VC4_HDMI_FIFO_CTL_RECENTER;
  *REG32(HDMI_FIFO_CTL) = fifo | VC4_HDMI_FIFO_CTL_RECENTER;
  s1_marker("S1: before FIFO recenter polling 0x7e90205c");
  bool recentered = false;
  for (unsigned i = 0; i < 100000; ++i) {
    if (*REG32(HDMI_FIFO_CTL) & VC4_HDMI_FIFO_CTL_RECENTER_DONE) {
      recentered = true;
      break;
    }
    udelay(1);
  }
  if (!recentered) {
    puts("S1: FIFO recenter timeout");
    return;
  }
  s1_marker("S1: before PHY powerdown read 0x7e9022c4");
  *REG32(0x7e9022c4) = *REG32(0x7e9022c4) & ~(1u << 25);
  hdmi_ready = true;
  puts("S1: transmitter programmed; physical display not yet verified");
  uart_flush_tx(0);
}
#endif

#line 321 "platform/bcm28xx/vc4-hdmi/hdmi.c"
static void hdmi_init(uint level) {
#if HDMI_DIAG_REDOX_BACKGROUND
  s1_background_init();
  return;
#endif
#line 322 "platform/bcm28xx/vc4-hdmi/hdmi.c"
  /* Match the historical HDMI prerequisite sequence.  The USB/peripheral
   * domain is required before the HDMI clock/bus path is touched. */
  power_up_usb();
  hdmi_enable_power_domain();
  /* HSM is the HDMI state-machine clock, not the pixel clock. */
  if (!clock_set_hsm(HDMI_MODE_HSM_CLOCK_HZ, PERI_PLLH_AUX)) {
    puts("HDMI: failed to start pixel-rate HSM clock");
    return;
  }
  printf("HDMI clocks: HSMCTL=0x%x HSMDIV=0x%x\n",
         *REG32(CM_HSMCTL), *REG32(CM_HSMDIV));
  udelay(100);
  hdmi_mmio_snapshot("hsm+100us");
  udelay(900);
  hdmi_mmio_snapshot("hsm+1ms");
  if (!hdmi_verify_clock_state()) {
    puts("HDMI: clock verification failed");
    return;
  }
#if HDMI_EARLY_CORE_PROBE
  puts("HDMI early core probe: before read 0x7e902000");
  uart_flush_tx(0);
  uint32_t early_revision = *REG32(HDMI_CORE_REV);
  printf("HDMI early core probe: after read CORE_REV=0x%08x\n", early_revision);
  uart_flush_tx(0);
#endif
#if HDMI_EARLY_PV_PROBE
  puts("HDMI early PV probe: before CONTROL read 0x7e807000");
  uart_flush_tx(0);
  uint32_t early_pv_control = *REG32(0x7e807000);
  printf("HDMI early PV probe: after read CONTROL=0x%08x\n", early_pv_control);
  uart_flush_tx(0);
#endif
  vc4_hdmi_reset();
  puts("HDMI: reset stage complete");
  hdmi_mmio_snapshot("after-reset");

  /* HDMI register reads stall on this BCM2837 path in both the HDMI core and
   * HD banks.  Do not probe either bank here; the reset writes above are kept
   * write-only and the PM/CPRMAN snapshots remain the safe boundary. */

  /* vc4_hdmi_phy_init() performs the complete TX-PHY reset/commit sequence.
   * Do not touch the PHY reset register here: on BCM2837 that early access
   * can stall before the PHY clock has been configured. */
  puts("HDMI: starting PHY initialization");
  vc4_hdmi_phy_init();
  puts("HDMI: PHY initialization complete");

  /* HDMI analog boost/impedance values from the known BCM2837 bring-up
   * sequence. */
  *REG32(A2W_HDMI_CTL3) = PM_PASSWORD | 0x40;
  *REG32(A2W_HDMI_CTL2) = PM_PASSWORD | 0x180086;
  *REG32(A2W_HDMI_CTL1) = PM_PASSWORD | 0x10c00;
  *REG32(A2W_HDMI_CTL0) = PM_PASSWORD | 0x430218;

  /* The PV2 control block is accessed only after the HDMI core and PHY have
   * completed reset/initialization.  On BCM2837, touching PV2 earlier can
   * stall the VPU bus at the first PV control write. */
  hdmi_prepare_scanout();

  /* Do not read HDMI_SCHEDULER_CONTROL here.  Invalid HDMI readbacks are
   * known on this path, and this value is fully specified for the DVI test
   * mode below. */
  *REG32(HDMI_SCHEDULER_CONTROL) =
      HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT_SET |
      HDMI_SCHEDULER_CONTROL_IGN_VSYNC_PREDS_SET;

  vc4_hdmi_set_timings();

  /* Use the documented reset value as a software shadow.  Do not reuse a
   * FIFO readback until HDMI MMIO access has been independently validated. */
  *REG32(HDMI_FIFO_CTL) = VC4_HDMI_FIFO_CTL_MASTER_SLAVE_N;

  /* Match the Linux video-start sequence without a read/modify/write.  The
   * HDMI register read path stalls on this platform, so assert the known
   * frame-counter/RGB/underflow controls in one fixed write. */
  *REG32(HDMI_CSC_12_11) = 0x00000000;
  *REG32(HDMI_CSC_14_13) = (0x100u << 16) | 0x6e0u;
  *REG32(HDMI_CSC_22_21) = (0x6e0u << 16);
  *REG32(HDMI_CSC_24_23) = (0x100u << 16);
  *REG32(HDMI_CSC_32_31) = 0x0000006e0u;
  *REG32(HDMI_CSC_34_33) = (0x100u << 16);
  *REG32(HDMI_CSC_CTL) = HDMI_CSC_CTL_ORDER_BGR;
  *REG32(HDMI_VID_CTL) = HD_VID_CTL_ENABLE | HD_VID_CTL_UFEN |
      HD_VID_CTL_RST_FRAMEC | HD_VID_CTL_CLRRGB |
      HD_VID_CTL_BLANK_INSERT_EN |
      (HDMI_MODE_SYNC_POSITIVE ? 0 : HD_VID_CTL_VSYNC_LOW | HD_VID_CTL_HSYNC_LOW);
  puts("HDMI: video control enabled");

  /* Select the diagnostic signalling mode after programming video control. */
  *REG32(HDMI_RAM_PACKET_CONFIG) = 0;
  *REG32(HDMI_SCHEDULER_CONTROL) =
      HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT_SET |
      HDMI_SCHEDULER_CONTROL_IGN_VSYNC_PREDS_SET
#if !HDMI_DIAG_DVI
      | HDMI_SCHEDULER_CONTROL_MODE_HDMI_SET
#endif
      ;
  udelay(100);
#if HDMI_DIAG_DVI
  puts("HDMI: DVI scheduler mode selected");
#else
  *REG32(HDMI_RAM_PACKET_CONFIG) = VC4_HDMI_RAM_PACKET_ENABLE;
  puts("HDMI: HDMI scheduler mode selected");
#endif

  /* Start the legacy PV2 video path only after the HDMI scheduler and FIFO
   * are configured.  An early PV2 VC write stalls the BCM2837 VPU bus. */
  enable_pixelvalve_video(2);

  uint32_t fifo_ctl = VC4_HDMI_FIFO_CTL_MASTER_SLAVE_N;
  *REG32(HDMI_FIFO_CTL) = fifo_ctl;
  *REG32(HDMI_FIFO_CTL) = fifo_ctl | VC4_HDMI_FIFO_CTL_RECENTER;

  udelay(1000);
  *REG32(HDMI_FIFO_CTL) = fifo_ctl;
  *REG32(HDMI_FIFO_CTL) = fifo_ctl | VC4_HDMI_FIFO_CTL_RECENTER;

  puts("waiting for recenter");
  //while (*REG32(HDMI_FIFO_CTL) & VC4_HDMI_FIFO_CTL_RECENTER_DONE) {}
  puts("done");
  hdmi_ready = true;
#if HDMI_MODE_1080P60
  puts("HDMI: 1920x1080 RGB test pattern active (HDMI TMDS mode)");
#elif HDMI_DIAG_DVI
  puts("HDMI: 640x480 RGB test pattern active (DVI TMDS mode)");
#else
  puts("HDMI: 640x480 RGB test pattern active (HDMI TMDS mode)");
#endif
}

static void vc4_hdmi_reset(void) {
  puts("HDMI: encoder reset assert");
  *REG32(HD_HDM_CTL) = HD_HDM_CTL_SW_RST;
  udelay(10000);
  puts("HDMI: encoder reset clear");
  *REG32(HD_HDM_CTL) = 0;
  udelay(100);
  puts("HDMI: encoder enable");
  *REG32(HD_HDM_CTL) = HD_HDM_CTL_ENABLE;
  udelay(100);

  puts("HDMI: core reset assert");
  *REG32(HDMI_SW_RESET_CNTRL) =
      VC4_HDMI_SW_RESET_HDMI | VC4_HDMI_SW_RESET_FORMAT_DETECT;
  udelay(100);
  puts("HDMI: core reset clear");
  *REG32(HDMI_SW_RESET_CNTRL) = 0;
  udelay(100);
}

static void vc4_hdmi_set_timings(void) {
  logf("\n");
  int vsync = HDMI_MODE_VSYNC;
  int vfp = HDMI_MODE_VFP;
  int vactive = HDMI_MODE_VACTIVE;
  uint32_t verta = (vsync << 20) | (vfp << 13) | (vactive);
  printf("VERTA: 0x%x\n", verta);

  int vbp = HDMI_MODE_VBP;
  int vspo = 0;
  uint32_t vertb = (vspo << 9) | (vbp);
  printf("VERTB: 0x%x\n", vertb);

  int hactive = HDMI_MODE_HACTIVE;
  uint32_t horza = (HDMI_MODE_SYNC_POSITIVE ?
                    VC4_HDMI_HORZA_VPOS | VC4_HDMI_HORZA_HPOS : 0) | hactive;
  printf("HORZA: 0x%x\n", horza);

  int hbp = HDMI_MODE_HBP;
  int hsync = HDMI_MODE_HSYNC;
  int hfp = HDMI_MODE_HFP;
  /* Match the unadjusted mode porches used by Linux VC4 and the stock
   * Pi 3B+ 1080p60 register capture. */
  uint32_t horzb = (hbp << 20) | (hsync << 10) | hfp;
  printf("HORZB: 0x%x\n", horzb);

  *REG32(HDMI_HORZA) = horza;
  *REG32(HDMI_HORZB) = horzb;

  int vsync_offset = 0;
  uint32_t vertb_even = (vsync_offset << 9) | (vbp);
  printf("VERTB0: 0x%x\n", vertb_even);

  *REG32(HDMI_VERTA0) = verta;
  *REG32(HDMI_VERTA1) = verta;

  *REG32(HDMI_VERTB0) = vertb_even;
  *REG32(HDMI_VERTB1) = vertb;

  /* Pixel repetition is zero for this 640x480 mode.  Avoid an HDMI
   * read-modify-write while register readbacks remain untrusted. */
  *REG32(HDMI_MISC_CONTROL) = 0;
}

static void vc4_hdmi_phy_init(void) {
  logf("\n");

  /* BCM2837 TX-PHY values used by the working firmware path.  PHY reads
   * stall on this board just like the HDMI core/HD-bank reads, so the commit
   * pulse must remain write-only. */
  *REG32(0x7e9022c4) = 0x8e000000;
  *REG32(0x7e9022c8) = 0x0404a808;
  *REG32(0x7e9022cc) = 0x00a63004;
  *REG32(0x7e9022d0) = 0x2ff80112;
  *REG32(0x7e9022d4) = 0x0000001f;
  *REG32(0x7e9022d8) = 0x0000000f;
  *REG32(0x7e9022dc) = 0x00003c00;
  *REG32(0x7e9022e0) = 0xffff0000;

  *REG32(HDMI_TX_PHY_RESET_CTL) = 0xf << 16;
  /* Equivalent commit pulse using the known BCM2837 write-only sequence. */
  *REG32(0x7e9022c4) = 0x8c000000;
  *REG32(0x7e9022c4) = 0x8d000000;
  *REG32(0x7e9022c4) = 0x8c000000;
  *REG32(0x7e9022c4) = 0x8e000000;
  *REG32(HDMI_TX_PHY_RESET_CTL) = 0;
}

/* Must complete before the same-level ARM chainloader hook. */
LK_INIT_HOOK(hdmi, &hdmi_init, LK_INIT_LEVEL_PLATFORM+9);

int bcm_hdmi_framebuffer_handoff(uint32_t *physical) {
  if (!physical || !hdmi_ready)
    return -1;
  *physical = hdmi_active;
  return 0;
}

void bcm_hdmi_run_boot_animation(void) {
  /* Static test pattern is intentional for Milestone A. */
}
