/* Probe the official 7-inch DSI panel controller without powering the panel. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <dev/uart.h>
#include <lk/reg.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/i2c.h>
#include <platform/bcm28xx/pll_read.h>
#include <platform/bcm28xx/udelay.h>

struct panel_bsc {
  volatile uint32_t ctrl, stat, dlen, addr, fifo, div, del, clkt;
};

static int wait_done(volatile struct panel_bsc *bsc) {
  for (unsigned i = 0; i < 10000; ++i) {
    uint32_t status = bsc->stat;
    if (status & (I2C_S_ERR | I2C_S_CLKT)) return -1;
    if (status & I2C_S_DONE) return 0;
    udelay(1);
  }
  return -2;
}

/* Returns 1 only for a recognized controller; 0 selects the HDMI fallback. */
int stage1_dsi_panel_present(void) {
  volatile struct panel_bsc *bsc = (void *)I2C0_BASE;
  uint32_t ctrl = bsc->ctrl;
  uint32_t initial_status = bsc->stat;
  if ((ctrl & I2C_C_I2CEN) || (initial_status & I2C_S_TA)) {
    printf("STAGE1 DSI probe: controller busy ctrl=0x%08x status=0x%08x\n", ctrl, initial_status);
    uart_flush_tx(0);
    return 0;
  }
  uint32_t pins = *REG32(GPIO_GPFSEL4);
  uint32_t div = bsc->div, del = bsc->del, clkt = bsc->clkt;
  uint32_t rate = get_vpu_per_freq();
  if (rate < 100000 || rate / 100000 > 65534) {
    printf("STAGE1 DSI probe: invalid input clock=%u\n", rate);
    uart_flush_tx(0);
    return 0;
  }
  uint32_t clockdiv = ((rate / 100000) + 1u) & ~1u;
  const uint32_t mask = (7u << 12) | (7u << 15);
  *REG32(GPIO_GPFSEL4) = (pins & ~mask) | (5u << 12) | (5u << 15);
  bsc->ctrl = I2C_C_I2CEN | I2C_C_CLEAR0 | I2C_C_CLEAR1;
  bsc->stat = I2C_S_CLKT | I2C_S_ERR | I2C_S_DONE;
  bsc->div = clockdiv;
  /* Keep the software deadline while allowing a stretching panel to respond. */
  bsc->clkt = 0;
  bsc->del = ((clockdiv >> 4) << 16) | (clockdiv >> 2);
  bsc->addr = 0x45;
  bsc->dlen = 1;
  bsc->fifo = 0x80;
  bsc->ctrl = I2C_C_I2CEN | I2C_C_ST;
  int found = 0, write_result = wait_done(bsc), read_result = -3;
  uint32_t write_status = bsc->stat, read_status = 0, id = 0xffffffffu;
  if (!write_result) {
    bsc->stat = I2C_S_CLKT | I2C_S_ERR | I2C_S_DONE;
    bsc->dlen = 1;
    bsc->ctrl = I2C_C_I2CEN | I2C_C_ST | I2C_C_READ;
    read_result = wait_done(bsc);
    read_status = bsc->stat;
    if (!read_result && (read_status & I2C_S_RXD)) {
      id = bsc->fifo & 0xffu;
      found = id == 0xde || id == 0xc3;
    }
  }
  bsc->ctrl = I2C_C_CLEAR0 | I2C_C_CLEAR1;
  bsc->stat = I2C_S_CLKT | I2C_S_ERR | I2C_S_DONE;
  bsc->div = div;
  bsc->del = del;
  bsc->clkt = clkt;
  bsc->ctrl = ctrl;
  *REG32(GPIO_GPFSEL4) = (*REG32(GPIO_GPFSEL4) & ~mask) | (pins & mask);
  printf("STAGE1 DSI probe: clk=%u div=%u old_clkt=0x%08x pins=0x%08x write=%d/0x%08x read=%d/0x%08x id=0x%08x\n", rate, clockdiv, clkt, pins, write_result, write_status, read_result, read_status, id);
  uart_flush_tx(0);
  return found;
}
