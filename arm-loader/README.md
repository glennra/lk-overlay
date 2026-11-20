# ARM loader

This is the ARM chainloader and its runtime dependencies, incorporated from
the workspace's `rpi-open-firmware` checkout for the LK Pi 3 boot path. The
import includes the uncommitted handoff and board-selection changes present at
the time of incorporation. The originating checkout is not a build input.
The original license is retained in `LICENSE` and the source directories.

Build with `nix-build --no-out-link` from this directory. The production build
also runs `make static-test` and embeds the resulting `arm_chainloader.bin`.
