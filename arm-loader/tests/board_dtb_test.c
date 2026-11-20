#include <assert.h>
#include "../arm_chainloader/board_dtb.h"

int main(void) {
  assert(board_is_pi3bplus(0x00a020d3));
  assert(!board_is_pi3bplus(0x00a02082));
  assert(!board_is_pi3bplus(0x002020d3));
  assert(!board_is_pi3bplus(0x00a030d3));
  assert(board_is_pi3b(0x00a02082));
  assert(!board_is_pi3b(0x00a020d3));
  assert(!board_is_pi3b(0x00202082));
  return 0;
}
