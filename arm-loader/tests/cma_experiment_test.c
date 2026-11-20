#include <assert.h>
#include <stdint.h>
#include "cma_experiment.h"

int main(void) {
  char blob[4096];
  assert(fdt_create_empty_tree(blob, sizeof(blob)) == 0);
  int rmem = fdt_add_subnode(blob, 0, "reserved-memory");
  assert(rmem >= 0);
  int cma = fdt_add_subnode(blob, rmem, "linux,cma");
  assert(cma >= 0);
  assert(fdt_setprop_u32(blob, cma, "size", 0x10000000U) == 0);
  assert(dsi_cma_reference_experiment(blob, 0) == 0);
  assert(dsi_cma_reference_experiment(blob, 1) == 1);
  cma = fdt_path_offset(blob, "/reserved-memory/linux,cma");
  int len;
  const fdt32_t *size = fdt_getprop(blob, cma, "size", &len);
  assert(size && len == 4 && fdt32_to_cpu(*size) == 0x04000000U);
  assert(dsi_cma_reference_experiment(blob, 1) == 0);
  assert(fdt_setprop_u32(blob, cma, "size", 0x10000000U) == 0);
  cma = fdt_path_offset(blob, "/reserved-memory/linux,cma");
  assert(fdt_setprop_u32(blob, cma, "alloc-ranges", 0) == 0);
  assert(dsi_cma_reference_experiment(blob, 1) == 0);
  return 0;
}
