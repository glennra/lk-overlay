/*=============================================================================
Copyright (C) 2016-2017 Authors of rpi-open-firmware
All rights reserved.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

FILE DESCRIPTION
Second stage bootloader.

=============================================================================*/

#include "chainloader.h"
#include "arm-defs.h"
#include "memory_map.h"
#include "board_dtb.h"
#include "cma_experiment.h"
#include "mmu.h"
#include "drivers/fatfs/ff.h"
#include "drivers/mailbox.hpp"
#include "drivers/block_device.hpp"

#include <libfdt.h>
#include <hardware.h>
#include <crc32.h>

#include <string.h>
#include <stdio.h>

#include "drivers/fatfs/diskio.h"
#include "start.h"

#define logf(fmt, ...) print_timestamp(); printf("[LDR:%s]: " fmt, __FUNCTION__, ##__VA_ARGS__);

FATFS g_BootVolumeFs;

#define ROOT_VOLUME_PREFIX "0:"
#define DTB_LOAD_ADDRESS    0xF000000 // 240mb from start
#define KERNEL_LOAD_ADDRESS 0x2000000 // 32mb from start
#define INITRD_LOAD_ADDRESS 0x4000000 // 64mb from start
#define KERNEL_MAX_SIZE     (INITRD_LOAD_ADDRESS - KERNEL_LOAD_ADDRESS)
#define INITRD_MAX_SIZE     (DTB_LOAD_ADDRESS - INITRD_LOAD_ADDRESS)
#define DTB_MAX_SIZE        (2U * 1024U * 1024U)
#define CMDLINE_MAX_SIZE    (64U * 1024U)
/* Runtime-auto LK stage 2 plus its 20 MiB live VPU heap. */
#define LK_VPU_RESIDENT_BASE 0x10000000U
#define LK_VPU_RESIDENT_SIZE 0x01500000U

struct framebuffer_layout {
  uint32_t pool_base;
  uint32_t pool_size;
  uint32_t primary;
  uint32_t back;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t pixel_length;
  const char *pool_name;
};

static const framebuffer_layout kDsiFramebuffer = {
  0x07000000U, 0x00300000U, 0x07000000U, 0x07177000U,
  800U, 480U, 800U, 0x00177000U, "framebuffer-pool@7000000"
};

static const framebuffer_layout kHdmiFramebuffer = {
  0x07400000U, 0x00300000U, 0x07400000U, 0x0752c000U,
  640U, 480U, 640U, 0x0012c000U, "framebuffer-pool@7400000"
};
static const framebuffer_layout kHdmiAuto1080 = {
  0x07400000U, 0x01000000U, 0x07400000U, 0x07c00000U,
  1920U, 1080U, 1920U, 0x007e9000U, "framebuffer-pool@7400000"
};
static const framebuffer_layout kHdmiAuto800 = {
  0x07400000U, 0x01000000U, 0x07400000U, 0x07c00000U,
  1280U, 800U, 1280U, 0x003e8000U, "framebuffer-pool@7400000"
};
static const framebuffer_layout kHdmiAuto720 = {
  0x07400000U, 0x01000000U, 0x07400000U, 0x07c00000U,
  1280U, 720U, 1280U, 0x00384000U, "framebuffer-pool@7400000"
};
static const framebuffer_layout kHdmiFixed800 = {
  0x07400000U, 0x00800000U, 0x07400000U, 0x07800000U,
  1280U, 800U, 1280U, 0x003e8000U, "framebuffer-pool@7400000"
};
static const framebuffer_layout kHdmiFixed720 = {
  0x07400000U, 0x00800000U, 0x07400000U, 0x07800000U,
  1280U, 720U, 1280U, 0x00384000U, "framebuffer-pool@7400000"
};
static const framebuffer_layout kCompositePal = {
  0x07800000U, 0x00400000U, 0x07800000U, 0x07a00000U,
  720U, 576U, 720U, 0x00195000U, "framebuffer-pool@7800000"
};
static const framebuffer_layout kCompositeNtsc = {
  0x07800000U, 0x00400000U, 0x07800000U, 0x07a00000U,
  720U, 480U, 720U, 0x00151800U, "framebuffer-pool@7800000"
};

typedef void __attribute__((noreturn)) (*linux_t)(uint32_t, uint32_t, void*);

struct mem_entry {
  uint32_t address;
  uint32_t size;
};

struct ranges {
  uint32_t child;
  uint32_t parent;
  uint32_t size;
};

struct boot_interval {
  uint32_t start;
  uint32_t end;
  const char *owner;
};

static uint32_t checked_end(uint32_t start, size_t size, const char *owner) {
  if (size > UINT32_MAX - start)
    panic("%s interval overflows physical address space", owner);
  return start + (uint32_t)size;
}

static void validate_layout(size_t kernel_size, size_t initrd_size) {
  boot_interval intervals[5];
  size_t count = 0;
  intervals[count++] = { 0, MEM_USABLE_START, "chainloader" };

  if (g_FirmwareData.reserved[FW_RESERVED_FB_MAGIC] == FRAMEBUFFER_HANDOFF_MAGIC) {
    const uint32_t start = g_FirmwareData.reserved[FW_RESERVED_FB_POOL_BASE];
    const uint32_t size = g_FirmwareData.reserved[FW_RESERVED_FB_POOL_SIZE];
    intervals[count++] = { start, checked_end(start, size, "framebuffer"),
                           "retained framebuffer" };
  }
  intervals[count++] = { KERNEL_LOAD_ADDRESS,
                         checked_end(KERNEL_LOAD_ADDRESS, kernel_size, "kernel"),
                         "kernel" };
  if (initrd_size)
    intervals[count++] = { INITRD_LOAD_ADDRESS,
                           checked_end(INITRD_LOAD_ADDRESS, initrd_size, "initrd"),
                           "initrd" };
  intervals[count++] = { DTB_LOAD_ADDRESS,
                         checked_end(DTB_LOAD_ADDRESS, DTB_MAX_SIZE, "DTB"),
                         "DTB workspace" };

  for (size_t i = 0; i < count; ++i) {
    if (intervals[i].start >= intervals[i].end)
      panic("invalid %s interval", intervals[i].owner);
    for (size_t j = i + 1; j < count; ++j) {
      if (intervals[i].start < intervals[j].end &&
          intervals[j].start < intervals[i].end)
        panic("physical interval overlap: %s [0x%08lx,0x%08lx) and %s [0x%08lx,0x%08lx)",
              intervals[i].owner, (unsigned long)intervals[i].start,
              (unsigned long)intervals[i].end, intervals[j].owner,
              (unsigned long)intervals[j].start, (unsigned long)intervals[j].end);
    }
  }
}

static void fdt_require(int result, const char *what) {
  if (result < 0)
    panic("DTB fixup failed (%s): %d", what, result);
}

static void reserve_framebuffer_pool(void *fdt) {
  const uint32_t *reserved = g_FirmwareData.reserved;
  if (reserved[FW_RESERVED_FB_MAGIC] != FRAMEBUFFER_HANDOFF_MAGIC)
    return;

  const uint32_t base = reserved[FW_RESERVED_FB_POOL_BASE];
  const uint32_t size = reserved[FW_RESERVED_FB_POOL_SIZE];
  const uint32_t primary = reserved[FW_RESERVED_FB_PRIMARY];
  const uint32_t back = reserved[FW_RESERVED_FB_BACK];
  const uint32_t active = reserved[FW_RESERVED_FB_ACTIVE];
  const framebuffer_layout *layout = NULL;
  const framebuffer_layout *layouts[] = { &kDsiFramebuffer, &kHdmiFramebuffer,
      &kHdmiAuto1080, &kHdmiAuto800, &kHdmiAuto720,
      &kHdmiFixed800, &kHdmiFixed720, &kCompositePal, &kCompositeNtsc };
  for (unsigned i = 0; i < sizeof(layouts) / sizeof(layouts[0]); ++i) {
    const framebuffer_layout *candidate = layouts[i];
    if (base == candidate->pool_base && size == candidate->pool_size &&
        primary == candidate->primary && back == candidate->back &&
        reserved[FW_RESERVED_FB_WIDTH] == candidate->width &&
        reserved[FW_RESERVED_FB_HEIGHT] == candidate->height &&
        reserved[FW_RESERVED_FB_STRIDE] == candidate->stride) {
      layout = candidate;
      break;
    }
  }
  if (!layout || reserved[FW_RESERVED_FB_FORMAT] != FRAMEBUFFER_FORMAT_A8R8G8B8 ||
      (base & 0xfffU) || (size & 0xfffU) || (active & 0xfffU) ||
      (active != primary && active != back) ||
      primary < base || back < base || active < base ||
      primary + (layout ? layout->pixel_length : 0) > base + size ||
      back + (layout ? layout->pixel_length : 0) > base + size ||
      active + (layout ? layout->pixel_length : 0) > base + size) {
    panic("invalid framebuffer handoff: base=0x%08x size=0x%08x", base, size);
  }

  const int root = fdt_path_offset(fdt, "/");
  const int address_cells = fdt_address_cells(fdt, root);
  const int size_cells = fdt_size_cells(fdt, root);
  if ((address_cells != 1 && address_cells != 2) ||
      (size_cells != 1 && size_cells != 2)) {
    panic("unsupported DTB root cell sizes: address=%d size=%d",
          address_cells, size_cells);
  }

  int reserved_memory = fdt_path_offset(fdt, "/reserved-memory");
  if (reserved_memory < 0) {
    reserved_memory = fdt_add_subnode(fdt, root, "reserved-memory");
    fdt_require(reserved_memory, "add /reserved-memory");
    fdt_require(fdt_setprop_u32(fdt, reserved_memory, "#address-cells",
                                address_cells), "reserved address cells");
    fdt_require(fdt_setprop_u32(fdt, reserved_memory, "#size-cells",
                                size_cells), "reserved size cells");
    fdt_require(fdt_setprop(fdt, reserved_memory, "ranges", NULL, 0),
                "reserved ranges");
  } else if (fdt_address_cells(fdt, reserved_memory) != address_cells ||
             fdt_size_cells(fdt, reserved_memory) != size_cells) {
    panic("reserved-memory cell sizes do not match root");
  }

  const bool composite = layout == &kCompositePal || layout == &kCompositeNtsc;
  const char *pool_path = composite ? "/reserved-memory/framebuffer-pool@7800000" :
      layout != &kDsiFramebuffer ?
      "/reserved-memory/framebuffer-pool@7400000" :
      "/reserved-memory/framebuffer-pool@7000000";
  int pool = fdt_path_offset(fdt, pool_path);
  if (pool < 0) {
    pool = fdt_add_subnode(fdt, reserved_memory, layout->pool_name);
    fdt_require(pool, "add framebuffer pool");
  }

  fdt32_t reg[4] = { 0 };
  const int cells = address_cells + size_cells;
  if (cells > (int)(sizeof(reg) / sizeof(reg[0])))
    panic("framebuffer reg property too large");
  if (address_cells == 1) {
    reg[0] = cpu_to_fdt32(active);
  } else {
    reg[0] = cpu_to_fdt32(0);
    reg[1] = cpu_to_fdt32(active);
  }
  if (size_cells == 1) {
    reg[address_cells] = cpu_to_fdt32(layout->pixel_length);
  } else {
    reg[address_cells] = cpu_to_fdt32(0);
    reg[address_cells + 1] = cpu_to_fdt32(layout->pixel_length);
  }
  fdt_require(fdt_setprop(fdt, pool, "reg", reg, cells * sizeof(fdt32_t)),
              "framebuffer pool reg");
  /* Keep the active scanout out of Linux's linear map. simplefb uses
   * ioremap_wc(), which is rejected for ordinary mapped ARM RAM. */
  fdt_require(fdt_setprop(fdt, pool, "no-map", NULL, 0),
              "framebuffer pool no-map");
  if (composite)
    fdt_require(fdt_add_mem_rsv(fdt, base, size), "reserve composite overlay pool");

  const char *simplefb_name =
      composite ? "framebuffer@7800000" :
      layout != &kDsiFramebuffer ? "framebuffer@7400000" :
      (active == primary ? "framebuffer@7000000" : "framebuffer@7177000");
  int simplefb = fdt_subnode_offset(fdt, root, simplefb_name);
  if (simplefb < 0) {
    simplefb = fdt_add_subnode(fdt, root, simplefb_name);
    fdt_require(simplefb, "add simple framebuffer");
  }

  fdt32_t simplefb_reg[4] = { 0 };
  if (address_cells == 1) {
    simplefb_reg[0] = cpu_to_fdt32(active);
  } else {
    simplefb_reg[0] = cpu_to_fdt32(0);
    simplefb_reg[1] = cpu_to_fdt32(active);
  }
  if (size_cells == 1) {
    simplefb_reg[address_cells] = cpu_to_fdt32(layout->pixel_length);
  } else {
    simplefb_reg[address_cells] = cpu_to_fdt32(0);
    simplefb_reg[address_cells + 1] = cpu_to_fdt32(layout->pixel_length);
  }
  const char simplefb_compatible[] = "simple-framebuffer";
  const char simplefb_format[] = "a8r8g8b8";
  const char okay[] = "okay";
  fdt_require(fdt_setprop(fdt, simplefb, "compatible", simplefb_compatible,
                          sizeof(simplefb_compatible)),
              "simple framebuffer compatible");
  fdt_require(fdt_setprop(fdt, simplefb, "reg", simplefb_reg,
                          cells * sizeof(fdt32_t)),
              "simple framebuffer reg");
  fdt_require(fdt_setprop_u32(fdt, simplefb, "width", layout->width),
              "simple framebuffer width");
  fdt_require(fdt_setprop_u32(fdt, simplefb, "height", layout->height),
              "simple framebuffer height");
  fdt_require(fdt_setprop_u32(fdt, simplefb, "stride", layout->stride * 4U),
              "simple framebuffer stride");
  fdt_require(fdt_setprop(fdt, simplefb, "format", simplefb_format,
                          sizeof(simplefb_format)),
              "simple framebuffer format");
  fdt_require(fdt_setprop(fdt, simplefb, "status", okay, sizeof(okay)),
              "simple framebuffer status");

  logf("reserved LK active framebuffer [0x%08x,0x%08x); simplefb at 0x%08x\n",
       active, active + layout->pixel_length, active);
}

static void disable_linux_dsi_for_firmware_handoff(void *fdt) {
  const int dsi = fdt_path_offset(fdt, "/soc/dsi@7e700000");
  const char disabled[] = "disabled";
  if (dsi >= 0)
    fdt_require(fdt_setprop(fdt, dsi, "status", disabled, sizeof(disabled)),
                "disable Linux DSI controller for firmware scanout");

  /* The ATTiny regulator driver resets power and PWM during probe.  Linux
   * cannot subsequently re-enable the panel when LK retains DSI scanout. */
  const char *controllers[] = {
    "raspberrypi,7inch-touchscreen-panel-regulator",
    "raspberrypi,7inch-touchscreen-panel",
  };
  for (unsigned i = 0; i < sizeof(controllers) / sizeof(controllers[0]); ++i) {
    int node = fdt_node_offset_by_compatible(fdt, -1, controllers[i]);
    if (node == -FDT_ERR_NOTFOUND)
      continue;
    fdt_require(node, "find DSI panel controller");
    fdt_require(fdt_setprop(fdt, node, "status", disabled, sizeof(disabled)),
                "disable conflicting DSI panel controller");
  }
}

static_assert((MEM_USABLE_START+0x800000) < KERNEL_LOAD_ADDRESS,
              "memory layout would not allow for kernel to be loaded at KERNEL_LOAD_ADDRESS, please check memory_map.h");

static uint32_t htonl(uint32_t hostlong) {
  return ((hostlong & 0x000000ff) << 24) |
         ((hostlong & 0x0000ff00) << 8) |
         ((hostlong & 0x00ff0000) >> 8) |
         ((hostlong & 0xff000000) >> 24);
}

void hex32(uint32_t input, char *output) {
  output[8] = 0;
  for (int i=7; i>=0; i--) {
    int nibble = input & 0xf;
    if (nibble <= 9) {
      output[i] = 0x30 + nibble;
    } else {
      output[i] = 0x57 + nibble;
    }
    input = input >> 4;
  }
}

struct LoaderImpl {
  LoaderImpl() {
    find_and_mount();
    size_t kernel_size = 0;
    auto kernel = load_kernel(&kernel_size);
    size_t initrd_size = 0;
    uint8_t *initrd = NULL;
    if (file_exists("initrd")) {
      initrd = load_initrd(&initrd_size);
    } else {
      logf("initrd: not present; continuing without an initrd\n");
    }

    validate_layout(kernel_size, initrd_size);

    /* read the command-line null-terminated */
    uint8_t *cmdline;
    size_t cmdlen = read_file("cmdline.txt", cmdline, true, CMDLINE_MAX_SIZE);

    logf("kernel cmdline: %s\n", cmdline);

    const char *dtb_name = detect_model_dtb();

      /* load flat device tree */
    uint8_t* fdt = load_fdt(dtb_name, (char*)cmdline, initrd, initrd_size);

    /* once the fdt contains the cmdline, it is not needed */
    delete[] cmdline;

    /* the eMMC card in particular needs to be reset */
    teardown_hardware();
    mmu_off();
    disable_icache();

    run_linux(kernel, fdt);
  }
  inline void find_and_mount() {
    logf("Mounting boot partitiion ...\n");
    FRESULT r = f_mount(&g_BootVolumeFs, ROOT_VOLUME_PREFIX, 1);
    if (r != FR_OK) {
      panic("failed to mount boot partition, error: %d", (int)r);
    }
    logf("Boot partition mounted!\n");
  }

  linux_t load_kernel(size_t *size_out) {
    /* read the kernel as a function pointer at fixed address */
    uint8_t* zImage = reinterpret_cast<uint8_t*>(KERNEL_LOAD_ADDRESS);
    linux_t kernel = reinterpret_cast<linux_t>(zImage);

    size_t ksize = read_file("zImage", zImage, false, KERNEL_MAX_SIZE);
    *size_out = ksize;
    logf("zImage loaded at 0x%X\n", (unsigned int)kernel);
    return kernel;
  }

  uint8_t *load_initrd(size_t *size) {
    uint8_t *initrd = reinterpret_cast<uint8_t*>(INITRD_LOAD_ADDRESS);
    *size = read_file("initrd", initrd, false, INITRD_MAX_SIZE);
    return initrd;
  }

  inline void __attribute__((noreturn)) run_linux(linux_t kernel, uint8_t *fdt) {
    /* fire away -- this should never return */

#if 0
    logf("stalling until jtag attaches\n");
    debug_stall();
#endif
    arm_mask_interrupts();
    uint32_t cpsr = arm_get_cpsr();
    uint32_t sctlr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    if (sctlr & ((1U << 0) | (1U << 2)))
      panic("unsupported final ARM32 SCTLR=0x%08x: MMU or data cache enabled", sctlr);
    if ((cpsr & ARM32_MODE_MASK) != ARM32_SVC ||
        (cpsr & ((1U << 7) | (1U << 6))) != ((1U << 7) | (1U << 6)))
      panic("unsupported final ARM32 state: CPSR=0x%08x", cpsr);
    logf("Jumping to the Linux kernel from SVC with IRQ/FIQ masked...\n");
    kernel(0, ~0, fdt);
  }

  bool file_exists(const char* path) {
    return f_stat(path, NULL) == FR_OK;
  }

  void fixup_uart_clock(void* fdt, bool rewrite_clock) {
    const uint32_t uart_clock_phandle = 0x7fff0001;
    const uint32_t uart_clock_hz = g_FirmwareData.reserved[FW_RESERVED_UART_CLOCK] ?
        g_FirmwareData.reserved[FW_RESERVED_UART_CLOCK] : 48000000;
    int uart = fdt_path_offset(fdt, "/soc/serial@7e201000");
    if (uart < 0) {
      logf("PL011 node not found; retaining its original clock provider\n");
      return;
    }

    const char okay[] = "okay";
    if (rewrite_clock) {
      int clock = fdt_add_subnode(fdt, 0, "lk-uart-clock");
      if (clock < 0)
        panic("cannot add fixed PL011 clock node: %d", clock);
      const char compatible[] = "fixed-clock";
      const char clock_output_names[] = "lk-uart";
      const char clock_names[] = "uartclk\0apb_pclk\0";
      const fdt32_t cells = cpu_to_fdt32(0);
      const fdt32_t rate = cpu_to_fdt32(uart_clock_hz);
      const fdt32_t phandle = cpu_to_fdt32(uart_clock_phandle);
      const fdt32_t clocks[] = { phandle, phandle };

      fdt_require(fdt_setprop(fdt, clock, "compatible", compatible,
                              sizeof(compatible)), "PL011 clock compatible");
      fdt_require(fdt_setprop(fdt, clock, "clock-output-names",
                              clock_output_names, sizeof(clock_output_names)),
                  "PL011 clock output name");
      fdt_require(fdt_setprop(fdt, clock, "#clock-cells", &cells, sizeof(cells)),
                  "PL011 clock cells");
      fdt_require(fdt_setprop(fdt, clock, "clock-frequency", &rate, sizeof(rate)),
                  "PL011 clock rate");
      fdt_require(fdt_setprop(fdt, clock, "phandle", &phandle, sizeof(phandle)),
                  "PL011 clock phandle");
      fdt_require(fdt_setprop(fdt, clock, "linux,phandle", &phandle,
                              sizeof(phandle)), "PL011 Linux phandle");
      fdt_require(fdt_setprop(fdt, clock, "status", okay, sizeof(okay)),
                  "enable PL011 fixed clock");

      /* Property updates can move later structure entries; refresh the node. */
      uart = fdt_path_offset(fdt, "/soc/serial@7e201000");
      if (uart < 0)
        panic("PL011 node moved or disappeared after clock setup: %d", uart);
      fdt_require(fdt_setprop(fdt, uart, "clocks", clocks, sizeof(clocks)),
                  "connect PL011 clock");
      fdt_require(fdt_setprop(fdt, uart, "clock-names", clock_names,
                              sizeof(clock_names)), "name PL011 clocks");
      fdt_require(fdt_setprop(fdt, uart, "clock-frequency", &rate, sizeof(rate)),
                  "set PL011 clock-frequency");
    }

    /* Match the stock disable-bt route.  This external UART has no CTS/RTS
     * connection; leaving the workaround enabled can suppress RX progress. */
    int res = fdt_delprop(fdt, uart, "cts-event-workaround");
    if (res < 0 && res != -FDT_ERR_NOTFOUND)
      panic("cannot remove PL011 CTS workaround: %d", res);
    fdt_require(fdt_setprop(fdt, uart, "status", okay, sizeof(okay)),
                "enable PL011");

    /* LK routes PL011 to the header; retain that routing when Linux probes. */
    int uart_pins = fdt_path_offset(fdt,
                                    "/soc/gpio@7e200000/uart0-gpio14");
    if (uart_pins < 0)
      panic("PL011 GPIO14/15 pinctrl node not found: %d", uart_pins);
    uint32_t uart_pins_phandle = fdt_get_phandle(fdt, uart_pins);
    if (!uart_pins_phandle)
      panic("PL011 GPIO14/15 pinctrl node has no phandle");
    const fdt32_t uart_pinctrl = cpu_to_fdt32(uart_pins_phandle);
    const char pinctrl_names[] = "default";
    uart = fdt_path_offset(fdt, "/soc/serial@7e201000");
    if (uart < 0)
      panic("PL011 node moved or disappeared before pinctrl setup: %d", uart);
    fdt_require(fdt_setprop(fdt, uart, "pinctrl-names", pinctrl_names,
                            sizeof(pinctrl_names)), "name PL011 pinctrl");
    fdt_require(fdt_setprop(fdt, uart, "pinctrl-0", &uart_pinctrl,
                            sizeof(uart_pinctrl)), "route PL011 to GPIO14/15");

    int aliases = fdt_path_offset(fdt, "/aliases");
    if (aliases >= 0) {
      const char pl011_path[] = "/soc/serial@7e201000";
      const char aux_path[] = "/soc/serial@7e215040";
      fdt_require(fdt_setprop(fdt, aliases, "serial0", pl011_path,
                              sizeof(pl011_path)), "set serial0 alias");
      fdt_require(fdt_setprop(fdt, aliases, "serial1", aux_path,
                              sizeof(aux_path)), "set serial1 alias");
    }
    int aux_uart = fdt_path_offset(fdt, "/soc/serial@7e215040");
    if (aux_uart >= 0) {
      const char disabled[] = "disabled";
      fdt_require(fdt_setprop(fdt, aux_uart, "status", disabled,
                              sizeof(disabled)), "disable mini-UART");
    }
    int bluetooth = fdt_path_offset(fdt,
                                    "/soc/serial@7e201000/bluetooth");
    if (bluetooth >= 0) {
      const char disabled[] = "disabled";
      fdt_require(fdt_setprop(fdt, bluetooth, "status", disabled,
                              sizeof(disabled)), "disable PL011 Bluetooth");
    }
    if (rewrite_clock)
      logf("PL011 handoff clock fixed at %u Hz\n", uart_clock_hz);
  }

  size_t read_file(const char* path, uint8_t*& dest, bool should_alloc = true,
                   size_t max_size = 0) {
    uint32_t start = ST_CLO;
    /* ensure file exists first */
    if(!file_exists(path)) panic("attempted to read %s, but it does not exist", path);

    /* read entire file into buffer */
    FIL fp;
    f_open(&fp, path, FA_READ);

    unsigned int len = f_size(&fp);
    if (max_size && len > max_size)
      panic("%s is too large: %u bytes (maximum %u)", path, len,
            (unsigned int)max_size);

    if(should_alloc) {
      /*
       * since this can be used for strings, there's no harm in reserving an
       * extra byte for the null terminator and appending it.
       */
      uint8_t* buffer = new uint8_t[len + 1];
      dest = buffer;
      buffer[len] = 0;
    }

    logf("%s: reading %d bytes to 0x%X ~%dmb...\n", path, len, (unsigned int)dest, ((unsigned int)dest)/1024/1024);

    unsigned int bytes_read = 0;
    FRESULT read_result = f_read(&fp, dest, len, &bytes_read);
    f_close(&fp);
    if (read_result != FR_OK || bytes_read != len)
      panic("short read for %s: %u of %u bytes", path, bytes_read, len);

    uint32_t stop = ST_CLO;
    uint32_t elapsed = stop - start;

    uint32_t bytes_per_second = (double)len / ((double)(elapsed) / 1000 / 1000);
    printf("%d kbyte copied at a rate of %ld kbytes/second, CRC32: 0x%x\n", len/1024, bytes_per_second/1024, 0); //rc_crc32(0, (const char*)dest, len));

    return len;
  }

  uint8_t* load_fdt(const char* filename, const char* cmdline, uint8_t *initrd_start, size_t initrd_size) {
    /* read device tree blob */
    uint8_t* fdt = reinterpret_cast<uint8_t*>(DTB_LOAD_ADDRESS);
    size_t sz = read_file(filename, fdt, false, DTB_MAX_SIZE - 8192);
    logf("FDT loaded at %p, size is %d\n", fdt, sz);

    void* v_fdt = reinterpret_cast<void*>(fdt);

    int res;

    if ((res = fdt_check_header(v_fdt)) != 0) {
      panic("fdt blob invalid, fdt_check_header returned %d", res);
    }

    res = fdt_open_into(v_fdt, v_fdt, DTB_MAX_SIZE);
    if (res < 0)
      panic("cannot make DTB writable: %d", res);
    if (fdt_check_header(v_fdt) != 0 || fdt_totalsize(v_fdt) > DTB_MAX_SIZE)
      panic("DTB invalid or exceeds %u-byte workspace", DTB_MAX_SIZE);

    char serial_number[9];
    hex32(g_FirmwareData.serial, serial_number);
    // /serial-number is an ascii string containing the serial#
    res = fdt_setprop(v_fdt, 0, "serial-number", serial_number, 9);

    int ethernet0 = fdt_path_offset(v_fdt, "ethernet0");
    if (ethernet0 < 0) {
      panic("cant find ethernet0");
    } else {
      uint32_t serial = g_FirmwareData.serial;
      uint8_t mac[] = { 0xb8
                      , 0x27
                      , 0xeb
                      , (uint8_t)((serial >> 16) & 0xff)
                      , (uint8_t)((serial >> 8) & 0xff)
                      , (uint8_t)(serial & 0xff) };
      res = fdt_setprop(v_fdt, ethernet0, "local-mac-address", mac, 6);
    }

    int system = fdt_path_offset(v_fdt, "/system");
    if (system >= 0) {
      // /system/linux,serial is an 8byte raw serial#
      // /system/linux,revision is a raw 32bit revision, directly from OTP
      uint32_t revision = htonl(g_FirmwareData.revision);
      res = fdt_setprop(v_fdt, system, "linux,revision", &revision, 4);
    }

    /* These device-tree changes describe the LK-owned handoff. */
    const bool lk_handoff =
        g_FirmwareData.reserved[FW_RESERVED_FB_MAGIC] == FRAMEBUFFER_HANDOFF_MAGIC;
    int firmware = fdt_path_offset(v_fdt, "/soc/firmware");
    if (firmware >= 0) {
      /* LK does not provide the stock Raspberry Pi property service. */
      const char disabled[] = "disabled";
      fdt_require(fdt_setprop(v_fdt, firmware, "status", disabled,
                              sizeof(disabled)), "disable firmware service");
    }

    int usb = fdt_path_offset(v_fdt, "/soc/usb@7e980000");
    if (lk_handoff && usb >= 0) {
      res = fdt_delprop(v_fdt, usb, "power-domains");
      if (res < 0 && res != -FDT_ERR_NOTFOUND)
        panic("cannot remove USB power-domain dependency: %d", res);
    }

    int cprman = fdt_path_offset(v_fdt, "/soc/cprman@7e101000");
    if (cprman >= 0) {
      res = fdt_delprop(v_fdt, cprman, "firmware");
      if (res < 0 && res != -FDT_ERR_NOTFOUND)
        panic("cannot remove CPRMAN firmware dependency: %d", res);
      /*
       * Keep the complete CPRMAN clocks property.  The Linux BCM2835 clock
       * driver uses the DSI provider phandles to resolve the optional parent
       * names even when the DSI provider nodes remain disabled, which keeps
       * Linux from claiming the LK-owned display.  Reducing this property to
       * the oscillator loses the six valid DSI references and causes dsi0p to
       * register with a NULL parent.
       */
    }

    int mmcnr = fdt_path_offset(v_fdt, "/soc/mmcnr@7e300000");
    /* The SD card is handed to Linux through the SDHOST controller.
     * Keep the unrelated SDHCI/mmcnr controller disabled even when there
     * is no DSI handoff; otherwise Linux probes mmc1 and can stall there.
     */
    if (mmcnr >= 0) {
      const char disabled[] = "disabled";
      fdt_require(fdt_setprop(v_fdt, mmcnr, "status", disabled,
                              sizeof(disabled)), "disable MMCNR");
    }
    int sdhost = fdt_path_offset(v_fdt, "/soc/mmc@7e202000");
    if (lk_handoff && sdhost >= 0) {
      const char okay[] = "okay";
      fdt_require(fdt_setprop(v_fdt, sdhost, "status", okay, sizeof(okay)),
                  "enable SDHOST");
      fdt_require(fdt_setprop_u32(v_fdt, sdhost, "bus-width", 4),
                  "set SDHOST bus width");
      fdt_require(fdt_setprop(v_fdt, sdhost, "brcm,force-pio", NULL, 0),
                  "force SDHOST PIO");
      fdt_require(fdt_setprop_u32(v_fdt, sdhost, "brcm,pio-limit",
                                  0x7fffffff), "set SDHOST PIO limit");

      int pins = fdt_path_offset(v_fdt, "/soc/gpio@7e200000/sdhost-gpio48");
      if (pins >= 0) {
        uint32_t phandle = fdt_get_phandle(v_fdt, pins);
        if (phandle) {
          const fdt32_t pinctrl = cpu_to_fdt32(phandle);
          sdhost = fdt_path_offset(v_fdt, "/soc/mmc@7e202000");
          if (sdhost < 0)
            panic("SDHOST node disappeared before pinctrl setup: %d", sdhost);
          fdt_require(fdt_setprop(v_fdt, sdhost, "pinctrl-names", "default",
                                  sizeof("default")), "name SDHOST pinctrl");
          fdt_require(fdt_setprop(v_fdt, sdhost, "pinctrl-0", &pinctrl,
                                  sizeof(pinctrl)), "route SDHOST pins");
        }
      }
    }

    /* Only rewrite the PL011 clock when LK owns the framebuffer handoff.
     * Otherwise preserve the stock firmware's UART clock description. */
    fixup_uart_clock(v_fdt, lk_handoff);
    int timer = fdt_path_offset(v_fdt, "/timer");
    /* LK programs CNTFRQ during ARM setup but does not configure the
     * per-CPU timer control registers.  Tell Linux to initialize them on
     * every LK handoff, including boots without a framebuffer handoff. */
    if (timer >= 0)
      fdt_require(fdt_setprop(v_fdt, timer,
                              "arm,cpu-registers-not-fw-configured", NULL, 0),
                  "mark ARM timer unconfigured");

    int chosen = fdt_path_offset(v_fdt, "/chosen");
    if (chosen < 0) panic("no chosen node in fdt");
    else {
      /* pass in command line args */
      res = fdt_setprop(v_fdt, chosen, "bootargs", cmdline, strlen((char*) cmdline) + 1);
      if (initrd_size) {
        // chosen.txt describes linux,initrd-start and linux,initrd-end within the chosen node
        uint32_t value = htonl((uint32_t)initrd_start);
        res = fdt_setprop(v_fdt, chosen, "linux,initrd-start", &value, 4);
        uint32_t initrd_end = htonl((uint32_t)(initrd_start + initrd_size));
        res = fdt_setprop(v_fdt, chosen, "linux,initrd-end", &initrd_end, 4);
      }
    }

    /* Match stock start.elf's Pi 3B+ ARM memory map.  The raw DTB on the
     * FAT partition is not the runtime DTB produced by start.elf: stock
     * firmware exposes 886 MiB to Linux, ending at 0x37600000. */
    int memory = fdt_path_offset(v_fdt, "/memory");
    if (memory < 0) panic("no memory node in fdt");
    else {
      struct mem_entry memmap[] = {
        { .address = htonl(0), .size = htonl(0x37600000) }
      };
      res = fdt_setprop(v_fdt, memory, "reg", (void*) memmap, sizeof(memmap));
      if (res < 0) panic("cannot set stock Pi 3B+ memory map: %d", res);
      logf("stock Pi 3B+ memory map: 0x00000000 + 0x37600000\n");
    };

    int soc = fdt_path_offset(v_fdt, "/soc");
    if (soc < 0) panic("no /soc node in fdt");
    else {
      struct ranges ranges[] = {
        { .child = htonl(VC4_PERIPH_BASE), .parent = htonl(ARM_PERIPH_BASE), .size = htonl(16 * 1024 * 1024) },
        { .child = htonl(0x40000000), .parent = htonl(0x40000000), .size = htonl(0x1000) }
      };
      fdt_setprop(v_fdt, soc, "ranges", (void*)ranges, sizeof(ranges));
    };

    reserve_framebuffer_pool(v_fdt);
    if (lk_handoff)
      fdt_require(fdt_add_mem_rsv(v_fdt, LK_VPU_RESIDENT_BASE,
                                  LK_VPU_RESIDENT_SIZE),
                  "reserve resident LK VPU image and heap");
    if (lk_handoff &&
        g_FirmwareData.reserved[FW_RESERVED_FB_POOL_BASE] == 0x07000000U &&
        g_FirmwareData.reserved[FW_RESERVED_FB_WIDTH] == 800U &&
        g_FirmwareData.reserved[FW_RESERVED_FB_HEIGHT] == 480U) {
      disable_linux_dsi_for_firmware_handoff(v_fdt);
      int cma_test = dsi_cma_reference_experiment(v_fdt, 1);
      fdt_require(cma_test, "DSI 64 MiB CMA experiment");
      if (cma_test)
        logf("DSI experiment: runtime CMA size set to 64 MiB\n");
    }

    logf("(valid) fdt loaded at 0x%p\n", fdt);

    return fdt;
  }

  void teardown_hardware() {
    BlockDevice* bd = get_sdhost_device();
    if (bd)
      bd->stop();
  }

  const char *detect_model_dtb() {
    // Both Pi 3B and 3B+ use the same ARM core; use the OTP revision handed
    // over by LK to distinguish their board-specific device trees.
    uint32_t arm_cpuid;
    // read MIDR reg
    __asm__("mrc p15, 0, %0, c0, c0, 0" : "=r"(arm_cpuid));
    // from https://github.com/dwelch67/raspberrypi/blob/master/boards/cpuid/cpuid.c
    switch (arm_cpuid) {
    case 0x410FB767:
      return "rpi1.dtb";
      break;
    case 0x410FC075:
      return "rpi2.dtb";
      break;
    case 0x410FD034:
      if (board_is_pi3bplus(g_FirmwareData.revision)) {
        logf("Pi 3B+ revision 0x%08x: selecting rpi3bp.dtb\n", g_FirmwareData.revision);
        return "rpi3bp.dtb";
      }
      if (board_is_pi3b(g_FirmwareData.revision)) {
        logf("Pi 3B revision 0x%08x: selecting rpi3b.dtb\n", g_FirmwareData.revision);
        return "rpi3b.dtb";
      }
      if (file_exists("rpi3.dtb"))
        return "rpi3.dtb";
      if (file_exists("bcm2710-rpi-3-b.dtb"))
        return "bcm2710-rpi-3-b.dtb";
      return "rpi3.dtb";
      break;
    // 410FD083 is cortex A72, rpi4
    default:
      logf("unknown rpi model, cpuid is 0x%lx\n", arm_cpuid);
      return "unknown.dtb";
    }
  }

};

static LoaderImpl STATIC_APP g_Loader {};
