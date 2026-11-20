#include <string.h>
#include <lk/reg.h>
#include <lk/console_cmd.h>
#include <lk/debug.h>
#include <platform/bcm28xx/i2c.h>
#include <platform/bcm28xx/pll_read.h>
#include <platform/bcm28xx/udelay.h>

#define I2C_WAIT_TIMEOUT_US 20000u

struct i2c_regs {
  uint32_t ctrl;
  uint32_t stat;
  uint32_t dlen;
  uint32_t addr;
  uint32_t fifo;
  uint32_t div;
  uint32_t del;
  uint32_t clkt;
};

static int cmd_i2c_set_rate(int argc, const console_cmd_args *argv);
static int cmd_i2c_xfer(int argc, const console_cmd_args *argv);

STATIC_COMMAND_START
STATIC_COMMAND("i2c_xfer", "I2C transfer", &cmd_i2c_xfer)
STATIC_COMMAND("i2c_set_rate", "Set I2C rate", &cmd_i2c_set_rate)
STATIC_COMMAND_END(i2c);

static int unhex(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  else if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  else if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static uint32_t i2c_base[] = {
  I2C0_BASE, I2C1_BASE, I2C2_BASE, I2C3_BASE,
  I2C4_BASE, I2C5_BASE, I2C6_BASE, I2C7_BASE,
};

void i2c_set_rate(unsigned busnum, unsigned long rate)
{
  volatile struct i2c_regs *regs = (struct i2c_regs*) i2c_base[busnum];
  uint32_t base_freq = get_vpu_per_freq();
  uint32_t div = base_freq / rate;
  if (div >= (1UL << 16))
    div = 0xffff;
  uint32_t fedl = (div >> 4) ?: 1;
  uint32_t redl = (div >> 2) ?: 1;
  regs->div = div & 0xffffUL;
  regs->del = (fedl << 16) | redl;
}

static int cmd_i2c_set_rate(int argc, const console_cmd_args *argv) {
  if (argc != 3) {
    printf("usage: i2c_set_rate <bus> <rate>\n");
    return -1;
  }
  unsigned busnum = argv[1].u;
  unsigned long rate = argv[2].u;
  i2c_set_rate(busnum, rate);
  return 0;
}

static void i2c_clear_fifo(unsigned busnum) {
  volatile struct i2c_regs *regs = (struct i2c_regs*) i2c_base[busnum];
  regs->ctrl |= I2C_C_CLEAR0 | I2C_C_CLEAR1;
}

static int i2c_wait_status(volatile struct i2c_regs *regs, uint32_t mask) {
  for (unsigned elapsed = 0; elapsed < I2C_WAIT_TIMEOUT_US; ++elapsed) {
    if (regs->stat & mask)
      return 0;
    udelay(1);
  }
  return -1;
}

int i2c_xfer(unsigned busnum, unsigned addr,
	     char *sendbuf, size_t sendsz,
	     char *recvbuf, size_t recvsz) {
  int ret = 0;

  if (busnum > 7)
    return -1;

  volatile struct i2c_regs *regs = (struct i2c_regs*) i2c_base[busnum];

  regs->ctrl = I2C_C_I2CEN;
  regs->stat = I2C_S_CLKT | I2C_S_ERR | I2C_S_DONE;
  i2c_clear_fifo(busnum);
  regs->addr = addr;

  // Send bytes
  if (sendsz) {
    regs->dlen = sendsz;
    regs->ctrl |= I2C_C_ST;
    if (i2c_wait_status(regs, I2C_S_ERR | I2C_S_TA)) {
      ret = -1;
      goto fail;
    }

    while(sendsz--) {
      if (i2c_wait_status(regs, I2C_S_CLKT | I2C_S_ERR | I2C_S_TXD)) {
        ret = -1;
        goto fail;
      }
      if (regs->stat & (I2C_S_CLKT | I2C_S_ERR)) {
	ret = -1;
	goto fail;
      }
      regs->fifo = *sendbuf++;
    }

    if (i2c_wait_status(regs, I2C_S_CLKT | I2C_S_ERR | I2C_S_DONE)) {
      ret = -1;
      goto fail;
    }
    if ((regs->stat & (I2C_S_CLKT | I2C_S_ERR))) {
      i2c_wait_status(regs, I2C_S_TA | I2C_S_DONE | I2C_S_ERR);
      dprintf(INFO, "Write failed, status 0x%08x\n", regs->stat);
      recvsz = 0;
      ret = -1;
    }
  }

  // Receive bytes
  if (recvsz) {
    regs->dlen = recvsz;
    regs->ctrl |= I2C_C_ST | I2C_C_READ;
    if (i2c_wait_status(regs, I2C_S_ERR | I2C_S_DONE | I2C_S_TA)) {
      ret = -1;
      goto fail;
    }

    while(!(regs->stat & (I2C_S_ERR | I2C_S_CLKT))) {
      if ((regs->stat & I2C_S_RXD)) {
	*recvbuf++ = regs->fifo;
	if (--recvsz == 0)
	  break;
      }
      if (i2c_wait_status(regs, I2C_S_RXD | I2C_S_ERR | I2C_S_CLKT)) {
        ret = -1;
        goto fail;
      }
    }
    if (!recvsz && !regs->dlen) {
      if (i2c_wait_status(regs, I2C_S_DONE)) {
        ret = -1;
        goto fail;
      }
    } else {
      dprintf(INFO, "Read failed, status 0x%08x\n", regs->stat);
      ret = -1;
    }
  }

  if ((regs->stat & (I2C_S_CLKT | I2C_S_ERR))) {
    i2c_wait_status(regs, I2C_S_TA | I2C_S_RXD | I2C_S_DONE | I2C_S_ERR);
  }

fail:
  regs->stat = I2C_S_CLKT | I2C_S_ERR | I2C_S_DONE;
  i2c_clear_fifo(busnum);
  regs->ctrl = 0;
  return ret;
}

static int cmd_i2c_xfer(int argc, const console_cmd_args *argv) {
  if (argc != 5) {
    printf("usage: i2c_xfer <bus> <addr> <hex_str> <recv_len>\n");
    return -1;
  }
  unsigned busnum = argv[1].u;
  unsigned addr = argv[2].u;
  char send_buf[strlen(argv[3].str) / 2];
  size_t recv_len = argv[4].u;
  char recv_buf[recv_len];
  const char *p;
  char *q;
  for (p = argv[3].str, q = send_buf; *p; p += 2) {
    if (!p[1]) {
      printf("Hex string must have an even number of digits!\n");
      return -1;
    }
    *q++ = (unhex(p[0]) << 4) | unhex(p[1]);
  }
  int status = i2c_xfer(busnum, addr, send_buf, q - send_buf,
			recv_buf, recv_len);
  if (status < 0) {
    printf("Transfer failed: %d\n", status);
    return -1;
  } else {
    for (p = recv_buf; p < recv_buf + recv_len; ++p) {
      printf("%02x ", *p);
    }
    printf("\n");
  }
  return 0;
}
