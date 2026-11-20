#pragma once

#include <libfdt.h>

/* Temporary DSI experiment: preserve all nonmatching DT configurations. */
static inline int dsi_cma_reference_experiment(void *fdt, int is_dsi) {
  if (!is_dsi)
    return 0;
  int node = fdt_path_offset(fdt, "/reserved-memory/linux,cma");
  if (node == -FDT_ERR_NOTFOUND)
    return 0;
  if (node < 0)
    return node;
  int len = 0;
  const fdt32_t *size = (const fdt32_t *)fdt_getprop(fdt, node, "size", &len);
  if (!size || len != (int)sizeof(*size) || fdt32_to_cpu(*size) != 0x10000000U)
    return 0;
  if (fdt_getprop(fdt, node, "reg", NULL) ||
      fdt_getprop(fdt, node, "alloc-ranges", NULL))
    return 0;
  int ret = fdt_setprop_u32(fdt, node, "size", 0x04000000U);
  return ret < 0 ? ret : 1;
}
