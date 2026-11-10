#include <stdint.h>
#include <lib/fs.h>
#include <lib/elf.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <stdio.h>
#include <arch.h>
#include <stdlib.h>

#include "stage1.h"
#include "boot_timing.h"

#ifndef STAGE1_UART_OUTPUT
#define STAGE1_UART_OUTPUT 1
#endif

#ifndef STAGE1_PRELOAD_ELF
#define STAGE1_PRELOAD_ELF 0
#endif

#if !STAGE1_UART_OUTPUT
#define puts(...) do { } while (0)
#define printf(...) do { } while (0)
#define print_timestamp(...) do { } while (0)
#endif

#define logf(fmt, ...) { print_timestamp(); printf("[stage1:%s:%d]: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); }

static ssize_t fs_read_wrapper(struct elf_handle *handle, void *buf, uint64_t offset, size_t len) {
  return fs_read_file(handle->read_hook_arg, buf, offset, len);
}

void try_sd_boot(const char *device) {
  int ret;
  logf("trying to boot from %s\n", device);
  stage1_boot_timing_mark(STAGE1_BOOT_T1_SD_LOAD_BEGIN);
#if defined(STAGE1_SD_FAT)
  ret = fs_mount("/root", "fat", device);
#else
  ret = fs_mount("/root", "ext2", device);
#endif
  if (ret) {
    printf("mount failure: %d\n", ret);
    return;
  }
  filehandle *stage2;
  ret = fs_open_file("/root/boot/lk.elf", &stage2);
  if (ret) {
    printf("failed to open /root/boot/lk.elf: %d\n", ret);
    goto unmount;
  }

  elf_handle_t *stage2_elf = malloc(sizeof(elf_handle_t));
#if STAGE1_PRELOAD_ELF
  struct file_stat stage2_stat;
  void *stage2_image = NULL;
  if (!stage2_elf || fs_stat_file(stage2, &stage2_stat) || !stage2_stat.size) {
    printf("failed to stat /root/boot/lk.elf\n");
    goto closefile;
  }
  stage2_image = malloc(stage2_stat.size);
  if (!stage2_image ||
      fs_read_file(stage2, stage2_image, 0, stage2_stat.size) !=
          (ssize_t)stage2_stat.size ||
      elf_open_handle_memory(stage2_elf, stage2_image, stage2_stat.size)) {
    printf("failed to preload /root/boot/lk.elf\n");
    free(stage2_image);
    goto closefile;
  }
#else
  ret = elf_open_handle(stage2_elf, fs_read_wrapper, stage2, false);
  if (ret) {
    printf("failed to elf open: %d\n", ret);
    goto closefile;
  }
#endif
  void *entry = load_and_run_elf(stage2_elf);
#if STAGE1_PRELOAD_ELF
  free(stage2_image);
#endif
  stage1_boot_timing_mark(STAGE1_BOOT_T2_STAGE2_LOAD_COMPLETE);
  fs_close_file(stage2);
  arch_chain_load(entry, 0, 0, 0, 0);
  return;
  closefile:
    fs_close_file(stage2);
  unmount:
    fs_unmount("/root");
}
