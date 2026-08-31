#!/usr/bin/env python3
"""Convert a PNG into the raw RGB565 data that LVGL draws directly.

The firmware embeds the output with EMBED_FILES and wraps it in an
lv_image_dsc_t, so no decoding happens on the device and no PNG decoder has to
be linked in.

    python tools/png_to_lvgl.py assets/energy-owl-bringup.png assets/energy-owl-bringup.bin

The image must match the screen it is drawn on, or LVGL scales it at runtime.
The ESP32-S3-BOX-3 screen is 320 x 240.

Needs Pillow. This runs on the development machine, not in the build, so the
generated .bin is committed next to the PNG it came from.
"""

import struct
import sys

from PIL import Image

USAGE = "usage: png_to_lvgl.py INPUT.png OUTPUT.bin"


def to_rgb565(image):
    """Pack an RGB image into little-endian RGB565, the order LVGL expects."""
    packed = bytearray()
    for r, g, b in image.getdata():
        value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        packed += struct.pack("<H", value)
    return bytes(packed)


def main():
    if len(sys.argv) != 3:
        sys.exit(USAGE)

    source, target = sys.argv[1], sys.argv[2]

    image = Image.open(source).convert("RGB")
    data = to_rgb565(image)

    with open(target, "wb") as handle:
        handle.write(data)

    print(f"{source}: {image.width} x {image.height}")
    print(f"{target}: {len(data)} bytes RGB565")
    print(f"header: .w = {image.width}, .h = {image.height}, "
          f".stride = {image.width * 2}")


if __name__ == "__main__":
    main()
