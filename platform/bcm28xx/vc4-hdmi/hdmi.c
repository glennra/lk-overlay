#include <app.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/pv.h>
#include <platform/bcm28xx/pll.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <platform/bcm28xx/hdmi_framebuffer.h>
#include <stdlib.h>

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
#define HDMI_CSC_CTL_ORDER_BGR      (1u << 5)

#define HDMI_SW_RESET_CNTRL         0x7e902004
#define HDMI_FIFO_CTL               0x7e90205c
#define VC4_HDMI_FIFO_CTL_MASTER_SLAVE_N                  BIT(0)
#define VC4_HDMI_FIFO_CTL_RECENTER                        BIT(6)
#define VC4_HDMI_FIFO_CTL_RECENTER_DONE                   BIT(14)
#define VC4_HDMI_FIFO_VALID_WRITE_MASK                    0xefff

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


#define VC4_HDMI_HORZA_HPOS BIT(13)
#define VC4_HDMI_HORZA_VPOS BIT(14)

#define VC4_HDMI_MISC_CONTROL_PIXEL_REP_MASK  (3 << 4)

#define VC4_HD_VID_CTL_CLRRGB BIT(23)
#define VC4_HDMI_SCHEDULER_CONTROL_MODE_HDMI    BIT(0)
#define VC4_HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE BIT(1)
#define VC4_HDMI_RAM_PACKET_ENABLE              BIT(16)
#define VC4_HD_VID_CTL_BLANKPIX                 BIT(18)

#define A2W_HDMI_CTL0 0x7e102080
#define A2W_HDMI_CTL1 0x7e102084
#define A2W_HDMI_CTL2 0x7e102088
#define A2W_HDMI_CTL3 0x7e10208c

#define HDMI_MODE_HACTIVE 640
#define HDMI_MODE_VACTIVE 480
#define HDMI_MODE_PIXEL_CLOCK_HZ 25200000
#define HDMI_MODE_HSM_CLOCK_HZ 126000000
#define HDMI_MODE_HFP 16
#define HDMI_MODE_HSYNC 96
#define HDMI_MODE_HBP 48
#define HDMI_MODE_VFP 10
#define HDMI_MODE_VSYNC 2
#define HDMI_MODE_VBP 33

static uint32_t *hdmi_fb;
static uint32_t hdmi_active;
static bool hdmi_ready;
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
         "HSMCTL[0x%08x]=0x%08x HSMDIV[0x%08x]=0x%08x\n",
         phase, PM_HDMI, *REG32(PM_HDMI), CM_HSMCTL, *REG32(CM_HSMCTL),
         CM_HSMDIV, *REG32(CM_HSMDIV));
  printf("HDMI MMIO %s: CORE_REV[0x%08x]=0x%08x "
         "HOTPLUG[0x%08x]=0x%08x FIFO_CTL[0x%08x]=0x%08x "
         "HD_M_CTL[0x%08x]=0x%08x\n",
         phase, HDMI_CORE_REV, *REG32(HDMI_CORE_REV), HDMI_HOTPLUG,
         *REG32(HDMI_HOTPLUG), HDMI_FIFO_CTL, *REG32(HDMI_FIFO_CTL),
         HD_HDM_CTL, *REG32(HD_HDM_CTL));
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
  /* PV2 register readback can stall the VPU while HDMI clocks are starting;
   * the programmed values are already known by the PV driver. */
  printf("HDMI: HVS1 list=%u PV2 scanout enabled\n", start);
}

static void hdmi_init(uint level) {
  /* PV2/HVS live in the image domain.  Do not repeat the USB/peripheral
   * sequence here: its stage-2 MMIO readback can stall the VPU. */
  power_up_image();
  hdmi_enable_power_domain();
  /* HSM is the HDMI state-machine clock, not the pixel clock.  Keep it at
   * 126MHz for this low-rate mode, matching the BCM2835 VC4 lower bound. */
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
  vc4_hdmi_reset();
  printf("HDMI core revision after reset: 0x%x\n", *REG32(HDMI_CORE_REV));
  hdmi_mmio_snapshot("after-reset");

  *REG32(HDMI_TX_PHY_TX_PHY_RESET_CTL) = 0xf << 16;
  *REG32(HDMI_TX_PHY_TX_PHY_RESET_CTL) = 0;
  *REG32(HDMI_SCHEDULER_CONTROL) |= HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT_SET | HDMI_SCHEDULER_CONTROL_IGN_VSYNC_PREDS_SET;

  vc4_hdmi_phy_init();

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

  *REG32(HDMI_SCHEDULER_CONTROL) = *REG32(HDMI_SCHEDULER_CONTROL) | VC4_HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT | VC4_HDMI_SCHEDULER_CONTROL_IGNORE_VSYNC_PREDICTS;

  vc4_hdmi_set_timings();

  printf("HDMI_FIFO_CTL: 0x%x\n", *REG32(HDMI_FIFO_CTL));
  *REG32(HDMI_FIFO_CTL) = VC4_HDMI_FIFO_CTL_MASTER_SLAVE_N;
  printf("HDMI_FIFO_CTL: 0x%x\n", *REG32(HDMI_FIFO_CTL));

  /* Write the complete video-control value.  Avoid an early read/modify/write
   * of HDMI_VID_CTL: the block can stall that read while its HSM clock is
   * changing on BCM2837. */
  /* Use one fixed write: HDMI register reads can stall the VPU while the
   * scheduler/FIFO clock is starting.  BLANKPIX is intentionally clear. */
  /* Match the BCM2837 firmware RGB path.  CLRRGB and RST_FRAMEC are not
   * required to start video and can blank the encoder while it is starting. */
  *REG32(HDMI_CSC_12_11) = 0x00000000;
  *REG32(HDMI_CSC_14_13) = (0x100u << 16) | 0x6e0u;
  *REG32(HDMI_CSC_22_21) = (0x6e0u << 16);
  *REG32(HDMI_CSC_24_23) = (0x100u << 16);
  *REG32(HDMI_CSC_32_31) = 0x0000006e0u;
  *REG32(HDMI_CSC_34_33) = (0x100u << 16);
  *REG32(HDMI_CSC_CTL) = HDMI_CSC_CTL_ORDER_BGR;
  *REG32(HDMI_VID_CTL) = HD_VID_CTL_ENABLE | HD_VID_CTL_UFEN;
  puts("HDMI: video control enabled");

  /* First test the basic TMDS path in DVI mode.  Packet RAM is intentionally
   * disabled so invalid status reads or uninitialised infoframes cannot mask
   * a clock/PHY failure. */
  *REG32(HDMI_RAM_PACKET_CONFIG) = 0;
  *REG32(HDMI_SCHEDULER_CONTROL) =
      HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT_SET |
      HDMI_SCHEDULER_CONTROL_IGN_VSYNC_PREDS_SET;
  puts("HDMI: DVI scheduler mode selected");

  /* Start the legacy PV2 video path only after the HDMI scheduler and FIFO
   * are configured.  An early PV2 VC write stalls the BCM2837 VPU bus. */
  enable_pixelvalve_video(2);

  uint32_t drift = *REG32(HDMI_FIFO_CTL);
  drift &= VC4_HDMI_FIFO_VALID_WRITE_MASK;
  *REG32(HDMI_FIFO_CTL) = drift & ~VC4_HDMI_FIFO_CTL_RECENTER;
  *REG32(HDMI_FIFO_CTL) = drift | VC4_HDMI_FIFO_CTL_RECENTER;

  udelay(1000);
  *REG32(HDMI_FIFO_CTL) = drift & ~VC4_HDMI_FIFO_CTL_RECENTER;
  *REG32(HDMI_FIFO_CTL) = drift | VC4_HDMI_FIFO_CTL_RECENTER;

  printf("HDMI_FIFO_CTL: 0x%x\n", *REG32(HDMI_FIFO_CTL));
  puts("waiting for recenter");
  //while (*REG32(HDMI_FIFO_CTL) & VC4_HDMI_FIFO_CTL_RECENTER_DONE) {}
  puts("done");
  hdmi_ready = true;
  puts("HDMI: 640x480 RGB test pattern active (DVI TMDS mode)");
}

static void vc4_hdmi_reset(void) {
  logf("\n");
  *REG32(HD_HDM_CTL) = HD_HDM_CTL_SW_RST;
  /* Match the BCM2835/BCM2837 VC4 reset sequence used by upstream Linux. */
  udelay(1);
  *REG32(HD_HDM_CTL) = 0;
  *REG32(HD_HDM_CTL) = HD_HDM_CTL_ENABLE;

  *REG32(HDMI_SW_RESET_CNTRL) = VC4_HDMI_SW_RESET_HDMI | VC4_HDMI_SW_RESET_FORMAT_DETECT;
  *REG32(HDMI_SW_RESET_CNTRL) = 0;
}

static void vc4_hdmi_set_timings(void) {
  logf("\n");
  int pixel_rep = 1;
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
  uint32_t horza = VC4_HDMI_HORZA_VPOS | VC4_HDMI_HORZA_HPOS | hactive;
  printf("HORZA: 0x%x\n", horza);

  int hbp = HDMI_MODE_HBP;
  int hsync = HDMI_MODE_HSYNC;
  int hfp = HDMI_MODE_HFP;
  uint32_t horzb = (hbp << 20) | (hsync << 10) | (hfp);
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

  uint32_t t = *REG32(HDMI_MISC_CONTROL);
  t &= ~VC4_HDMI_MISC_CONTROL_PIXEL_REP_MASK;
  t |= (pixel_rep-1) << 4;
  *REG32(HDMI_MISC_CONTROL) = t;
}

static void vc4_hdmi_phy_init(void) {
  logf("\n");

  /* BCM2837 TX-PHY values used by the working firmware path.  Use fixed
   * writes for the commit pulse: PHY readbacks can stall the VPU while the
   * HDMI clock is coming up. */
  *REG32(0x7e9022c4) = 0x8e000000;
  *REG32(0x7e9022c8) = 0x0404a808;
  *REG32(0x7e9022cc) = 0x00a63004;
  *REG32(0x7e9022d0) = 0x2ff80112;
  *REG32(0x7e9022d4) = 0x0000001f;
  *REG32(0x7e9022d8) = 0x0000000f;
  *REG32(0x7e9022dc) = 0x00003c00;
  *REG32(0x7e9022e0) = 0xffff0000;

  *REG32(HDMI_TX_PHY_RESET_CTL) = 0xf << 16;
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
