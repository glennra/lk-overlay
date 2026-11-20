#include <app.h>
#include <dev/uart.h>
#include <lk/console_cmd.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/cm.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/pll.h>
#include <platform/bcm28xx/pll_read.h>
#include <platform/bcm28xx/power.h>
#include <platform/bcm28xx/pv.h>
#include <platform/bcm28xx/vec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WITH_TGA
#include <lib/tga.h>
const
#include "librerpi-logo.h"
//#include "ResD1_720X480.h"
//#include <dance.h>
#endif

//extern uint8_t* pilogo;

enum vec_mode {
  ntsc,
  ntscj,
  pal,
  pal60,
  palm,
};


#ifdef WITH_TGA
static gfx_surface *logo;
#endif


static void draw_background_grid(void) {
  //hvs_add_plane(gfx_grid, 0, 0, false);
}

int bcm_vec_init(int standard) {
  int channel = 1; // on VC4, the VEC is hard-wired to hvs channel 1
  int width;
  int height;

  puts("VEC: initialization begin");
  uart_flush_tx(0);

#if !defined(RPI4)
  power_up_usb();
#endif
  puts("VEC: power setup complete; initializing HVS");
  uart_flush_tx(0);
  hvs_initialize();

  /* Match the earlier PAL bring-up's HVS and 324 MHz PLLH_AUX setup. */
  *REG32(SCALER_DISPCTRL) &= ~SCALER_DISPCTRL_ENABLE;
  *REG32(SCALER_DISPCTRL) = 0x800c007fu;
  *REG32(SCALER_DISPEOLN) = 0x40000000u;
  puts("VEC: HVS initialized; configuring PLLH");
  uart_flush_tx(0);
  setup_pllh(648000000u, 2, 6);

  // TODO, this assumes freq_plla_per is a multiple of 108mhz
  // it should check if it isnt, and report an error
  if (!clock_set_vec(108000000, PERI_PLLH_AUX)) {
    puts("VEC: failed to configure 108 MHz encoder clock");
    return -1;
  }
  puts("VEC: encoder clock configured; measuring clock");
  uart_flush_tx(0);

  int rate = measure_clock(29);
  printf("vec rate: %f, plla: %lld, pllc: %lld\n", ((double)rate)/1000/1000, freq_plla_per, freq_pllc_per);

  *REG32(VEC_WSE_RESET) = 1;
  *REG32(VEC_SOFT_RESET) = 1;
  *REG32(VEC_WSE_CONTROL) = 0;

  *REG32(VEC_SCHPH) = 0x28;
  *REG32(VEC_CLMP0_START) = 0xac;
  *REG32(VEC_CLMP0_END) = 0xac;
  *REG32(VEC_CONFIG2) = VEC_CONFIG2_UV_DIG_DIS | VEC_CONFIG2_RGB_DIG_DIS;
  *REG32(VEC_CONFIG3) = VEC_CONFIG3_HORIZ_LEN_STD;
#ifdef RPI4
  *REG32(VEC_DAC_CONFIG) = VEC_DAC_CONFIG_DAC_CTRL(0x0) | VEC_DAC_CONFIG_DRIVER_CTRL(0x80) | VEC_DAC_CONFIG_LDO_BIAS_CTRL(0x61);
#else
  *REG32(VEC_DAC_CONFIG) = VEC_DAC_CONFIG_DAC_CTRL(0xc) | VEC_DAC_CONFIG_DRIVER_CTRL(0xc) | VEC_DAC_CONFIG_LDO_BIAS_CTRL(0x46);
#endif
  *REG32(VEC_MASK0) = 0;
  enum vec_mode mode = standard ? ntsc : pal;
  switch (mode) {
    case ntsc:
      *REG32(VEC_CONFIG0) = VEC_CONFIG0_NTSC_STD | VEC_CONFIG0_PDEN;
      *REG32(VEC_CONFIG1) = VEC_CONFIG1_C_CVBS_CVBS;
      break;
    case ntscj:
      *REG32(VEC_CONFIG0) = VEC_CONFIG0_NTSC_STD;
      *REG32(VEC_CONFIG1) = VEC_CONFIG1_C_CVBS_CVBS;
      break;
    case pal:
      *REG32(VEC_CONFIG0) = VEC_CONFIG0_PAL_BDGHI_STD;
      *REG32(VEC_CONFIG1) = VEC_CONFIG1_C_CVBS_CVBS;
      break;
    case pal60:
      *REG32(VEC_CONFIG0) = VEC_CONFIG0_PAL_M_STD;
      *REG32(VEC_CONFIG1) = VEC_CONFIG1_C_CVBS_CVBS | VEC_CONFIG1_CUSTOM_FREQ;
      *REG32(VEC_FREQ3_2) = 0x2a09;
      *REG32(VEC_FREQ1_0) = 0x8acb;
      break;
    case palm:
      *REG32(VEC_CONFIG0) = VEC_CONFIG0_PAL_BDGHI_STD;
      *REG32(VEC_CONFIG1) = VEC_CONFIG1_C_CVBS_CVBS | VEC_CONFIG1_CUSTOM_FREQ;
      *REG32(VEC_FREQ3_2) = 0x223b;
      *REG32(VEC_FREQ1_0) = 0x61d1;
      break;
  }
  *REG32(VEC_DAC_MISC) = VEC_DAC_MISC_VID_ACT | VEC_DAC_MISC_DAC_RST_N;
  *REG32(VEC_CFG) = VEC_CFG_VEC_EN;
  puts("VEC: encoder enabled; configuring PV2");
  uart_flush_tx(0);
  struct pv_timings t;
  t.clock_mux = clk_vec;
  bool ntsc_mode = standard != 0;
  if (ntsc_mode) {
    t.vfp = 3;
    t.vsync = 4; // try 3
    t.vbp = 16;
    t.vactive = 240;

    t.vfp_even = 4; // try 3
    t.vsync_even = 3; // try 4
    t.vbp_even = 16;
    t.vactive_even = 240;

    t.interlaced = true;

    t.hfp = 14;
    t.hsync = 64;
    t.hbp = 60;
    t.hactive = 720;
  } else {
    t.vfp = 5; t.vsync = 5; t.vbp = 15; t.vactive = 288;
    t.vfp_even = 5; t.vsync_even = 5; t.vbp_even = 14; t.vactive_even = 288;
    t.interlaced = true; t.hfp = 12; t.hsync = 64; t.hbp = 68; t.hactive = 720;
  }
#ifdef RPI4
  setup_pixelvalve(&t, 3);
#else
  setup_pixelvalve(&t, 2);
#endif

  uint32_t t0 = *REG32(ST_CLO);
  printf("VEC: %s initialized at %u\n", ntsc_mode ? "NTSC" : "PAL", t0);

  width = t.hactive;
  height = t.vactive * 2;

  mutex_acquire(&channels[channel].lock);
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);

  hvs_configure_channel(1, width, height, true);
  hvs_set_background_color(channel, 0x0);
  /* The composite backend publishes its initial framebuffer list before
   * starting PV2, so the first field cannot scan an empty or stale list. */
  puts("VEC: HVS channel configured; PV2 awaiting base list");
  uart_flush_tx(0);

#ifdef WITH_TGA
  //logo = tga_decode(pilogo, sizeof(pilogo), GFX_FORMAT_ARGB_8888);

  if (false) {
    hvs_layer *new_layer = malloc(sizeof(hvs_layer));
    new_layer->layer = 100;
    new_layer->fb = logo;
    new_layer->x = 50;
    new_layer->y = 50;
    new_layer->w = logo->width / 4;
    new_layer->h = logo->height / 4;
    new_layer->name = strdup("logo 1");
    hvs_dlist_add(channel, new_layer);
  }

  //dance_start(logo, 1);
#endif
  return 0;
}
