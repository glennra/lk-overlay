#include <assert.h>
#include <lk/console_cmd.h>
#include <lk/reg.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/pv.h>

struct pixel_valve *getPvAddr(int pvnr) {
  uint32_t addr;
  switch (pvnr) {
#ifdef RPI4
  assert(pvnr <= 4);
  case 0:
    // DSI0/DPI
    addr = BCM_PERIPH_BASE_VIRT + 0x206000;
    break;
  case 1:
    // DSI1/SMI
    addr = BCM_PERIPH_BASE_VIRT + 0x207000;
    break;
  case 2:
    // HDMI0
    addr = BCM_PERIPH_BASE_VIRT + 0x20a000;
    break;
  case 3:
    // VEC
    addr = BCM_PERIPH_BASE_VIRT + 0xc12000;
    break;
  case 4:
    // HDMI1
    addr = BCM_PERIPH_BASE_VIRT + 0x216000;
    break;
#else
  assert(pvnr <= 2);
  case 0:
    // DSI0/DPI
    addr = BCM_PERIPH_BASE_VIRT + 0x206000;
    break;
  case 1:
    // DSI1/SMI
    addr = BCM_PERIPH_BASE_VIRT + 0x207000;
    break;
  case 2:
    // HDMI/VEC
    addr = BCM_PERIPH_BASE_VIRT + 0x807000;
    break;
#endif
  default:
    return NULL;
  }
  struct pixel_valve *rawpv = (struct pixel_valve*) addr;
  return rawpv;
}

/* Some BCM2837 PV2 status reads can stall while the HDMI clock is starting.
 * Retain the programmed values so enabling the PV does not require a readback
 * of the control registers. */
static uint32_t pv_control_value[3];
static uint32_t pv_vc_value[3];

unsigned int getPvIrq(int pvnr) {
  assert(pvnr <= 2);
  switch (pvnr) {
  case 0:
    return 45;
  case 1:
    return 46;
  case 2:
    return 42;
  default:
    return -1;
  }
}

void setup_pixelvalve(const struct pv_timings *t, int pvnr) {
  struct pixel_valve *rawpv = getPvAddr(pvnr);
  printf("setup_pixelvalve, pvnr=%d, %dx%d\n", pvnr, t->hactive, t->vactive);

  printf("PV%d: writing horizontal timing\n", pvnr);
  rawpv->horza = (t->hbp << 16) | t->hsync;
  rawpv->horzb = (t->hfp << 16) | t->hactive;

  printf("PV%d: writing vertical timing\n", pvnr);
  printf("PV%d: writing verta\n", pvnr);
  rawpv->verta = (t->vbp << 16) | t->vsync;
  printf("PV%d: writing vertb\n", pvnr);
  rawpv->vertb = (t->vfp << 16) | t->vactive;
  printf("PV%d: writing vc\n", pvnr);

  if (t->interlaced) {
    puts("interlaced pv");
    rawpv->verta_even = (t->vbp_even << 16) | t->vsync_even;
    rawpv->vertb_even = (t->vfp_even << 16) | t->vactive_even;
  }

#define CLK_SELECT(n) ((n & 3) << 2)
# define PV_CONTROL_CLK_SELECT_DSI              0
# define PV_CONTROL_CLK_SELECT_DPI_SMI_HDMI     1
# define PV_CONTROL_CLK_SELECT_VEC              2
#define PIXEL_REP(n) (((n) & 0x3) << 4)
#define FIFO_LEVEL(n) ((n & 0x3f) << 15)
#define PV_CONTROL_FORMAT_DSIV_24 (4u << 21)

  bool dsi = t->clock_mux == clk_dsi;
  pv_vc_value[pvnr] = (dsi ? BV(3) : BV(0)) | // DSI output mode or legacy video enable
            BV(1) | // continuous
            (t->interlaced ? BV(4) : 0);
  if (t->interlaced && t->clock_mux == clk_vec && t->vactive == 288) {
    /* PAL VEC uses even field first. Delay the odd field by half a line
     * plus one VEC clock, as in the VC4 CRTC interlace sequence. */
    unsigned int htotal = t->hactive + t->hfp + t->hsync + t->hbp;
    unsigned int odd_delay = htotal / 2u + 1u;
    pv_vc_value[pvnr] |= odd_delay << 6;
    rawpv->vsyncd_even = 0;
    printf("PV%d: PAL odd-field delay %u clocks\n", pvnr, odd_delay);
  }
  /* HDMI PV2 VC writes can stall while the HDMI scheduler is still being
   * brought up. DSI retains the original early VC programming sequence. */
  if (dsi)
    rawpv->vc = pv_vc_value[pvnr];
  printf("PV%d: vc write complete\n", pvnr);

  if (dsi)
    rawpv->dsi_h_active = t->hactive;
  else {
    printf("PV%d: writing h_active\n", pvnr);
    rawpv->h_active = t->hactive;
  }

  uint32_t fifo_len_bytes = 64;
  fifo_len_bytes = fifo_len_bytes - 3 * 6;

  int clk = 0;
  if (t->clock_mux == clk_dpi_smi_hdmi) clk = PV_CONTROL_CLK_SELECT_DPI_SMI_HDMI;
#ifdef RPI4
  else if (t->clock_mux == clk_vec) clk = 0;
#else
  else if (t->clock_mux == clk_vec) clk = PV_CONTROL_CLK_SELECT_VEC;
#endif

  printf("PV%d: timing registers programmed, enabling output\n", pvnr);
  /*
   * Keep the PixelValve disabled while programming its final control word.
   * The enable is a separate operation below, matching the Linux VC4
   * sequence.  Enabling PV2 or clearing its FIFO in this write can stall the
   * VPU before the HDMI encoder and HVS are ready to consume the first FIFO
   * word.  The image-domain reset already provides an empty FIFO here.
   */
  uint32_t control = (dsi ? PV_CONTROL_FORMAT_DSIV_24 : 0) |
            CLK_SELECT(clk) |
            PIXEL_REP(1 - 1) |
            BV(12) | // wait for h-start
            BV(13) | // trigger underflow
            BV(14) | // clear at start
            FIFO_LEVEL(fifo_len_bytes);
  printf("PV%d: writing control 0x%08x\n", pvnr, control);
  pv_control_value[pvnr] = control;
  rawpv->c = control;
  printf("PV%d: control write complete\n", pvnr);
}

void enable_pixelvalve(int pvnr) {
  struct pixel_valve *rawpv = getPvAddr(pvnr);
  if (!rawpv)
    return;
  printf("PV%d: enabling control\n", pvnr);
  pv_control_value[pvnr] |= PV_CONTROL_EN;
  rawpv->c = pv_control_value[pvnr];
  printf("PV%d: control enabled\n", pvnr);
  if (pv_vc_value[pvnr] & BV(3)) {
    pv_vc_value[pvnr] |= BV(0);
    rawpv->vc = pv_vc_value[pvnr];
  }
  printf("PV%d: video clock enabled\n", pvnr);
}

void enable_pixelvalve_video(int pvnr) {
  struct pixel_valve *rawpv = getPvAddr(pvnr);
  if (!rawpv || (pv_vc_value[pvnr] & BV(3)))
    return;
  pv_vc_value[pvnr] |= BV(0);
  rawpv->vc = pv_vc_value[pvnr];
  printf("PV%d: HDMI video path enabled\n", pvnr);
}

void setup_pv_interrupt(int pvnr, int_handler handler, void *arg) {
  //struct pixel_valve *rawpv = getPvAddr(pvnr);
  unsigned int irq = getPvIrq(pvnr);

  register_int_handler(irq, handler, arg);
  unmask_interrupt(irq);
}
