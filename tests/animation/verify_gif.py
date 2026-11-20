#!/usr/bin/env python3
"""Compare gif_test's 14 raw ARGB frames with the coalesced source GIF."""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

asset = Path(__file__).resolve().parents[2] / "platform/bcm28xx/animation/wo046_animation.gif"
width, height = int(sys.argv[2]), int(sys.argv[3])
actual = np.fromfile(sys.argv[1], dtype="<u4").reshape((14, height, width))
gif = Image.open(asset)
x = np.arange(width) * gif.width // width
y = np.arange(height) * gif.height // height
for frame in range(14):
    gif.seek(frame % gif.n_frames)
    rgb = np.asarray(gif.convert("RGB"), dtype=np.uint32)[y[:, None], x]
    expected = 0xff000000 | rgb[:, :, 0] << 16 | rgb[:, :, 1] << 8 | rgb[:, :, 2]
    if len(sys.argv) > 4 and int(sys.argv[4]):
        expected = expected[::-1, :]
    if not np.array_equal(actual[frame], expected):
        raise SystemExit(f"frame {frame} does not match the source GIF")
print(f"All 14 frames match the GIF at {width}x{height}, including loop restart")
