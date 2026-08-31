#!/usr/bin/env python3
"""Convert a PNG into the raw RGB565 data that LVGL draws directly.

The firmware embeds the output with EMBED_FILES and wraps it in an
lv_image_dsc_t, so no decoding happens on the device and no PNG decoder has to
be linked in.

    python tools/png_to_lvgl.py assets/energy-owl.png assets/energy-owl-bringup.bin
    python tools/png_to_lvgl.py --crop 219,10,637,478 assets/energy-owl.png OUT.bin

The optional crop takes x,y,width,height in the source image and is applied
before scaling. Keeping the crop here rather than in a cut-down PNG means the
original illustration stays in the repository and the framing can be changed
by editing one number.

The source PNG may be any size. It is scaled to the screen of the
ESP32-S3-BOX-3, which is 320 x 240. The aspect ratio is kept, and whatever
space is left over is filled with the colour of the source image's top-left
corner, so a letterbox does not show up as a black bar.

Needs Pillow. This runs on the development machine, not in the build, so the
generated .bin is committed next to the PNG it came from.
"""

import struct
import sys

from PIL import Image

USAGE = "usage: png_to_lvgl.py [--crop X,Y,W,H] INPUT.png OUTPUT.bin"


SCREEN_WIDTH = 320
SCREEN_HEIGHT = 240


def fit_to_screen(image):
    """Scale to the screen, keeping the shape, and pad what is left over."""
    if (image.width, image.height) == (SCREEN_WIDTH, SCREEN_HEIGHT):
        return image

    scale = min(SCREEN_WIDTH / image.width, SCREEN_HEIGHT / image.height)
    width = max(1, round(image.width * scale))
    height = max(1, round(image.height * scale))

    scaled = image.resize((width, height), Image.LANCZOS)

    if (width, height) == (SCREEN_WIDTH, SCREEN_HEIGHT):
        return scaled

    #  Pad with the top-left colour rather than black: on an image with a light
    #  background a black bar reads as a fault, a matching bar reads as margin.
    canvas = Image.new("RGB", (SCREEN_WIDTH, SCREEN_HEIGHT), image.getpixel((0, 0)))
    canvas.paste(scaled, ((SCREEN_WIDTH - width) // 2, (SCREEN_HEIGHT - height) // 2))

    return canvas


def to_rgb565(image):
    """Pack an RGB image into little-endian RGB565, the order LVGL expects."""
    packed = bytearray()
    for r, g, b in image.getdata():
        value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        packed += struct.pack("<H", value)
    return bytes(packed)


def parse_crop(text):
    parts = text.split(",")
    if len(parts) != 4:
        sys.exit(USAGE)
    try:
        x, y, width, height = (int(part) for part in parts)
    except ValueError:
        sys.exit(USAGE)
    if width < 1 or height < 1:
        sys.exit("crop width and height must be positive")
    return x, y, x + width, y + height


def main():
    argv = sys.argv[1:]
    box = None

    if argv and argv[0] == "--crop":
        if len(argv) < 2:
            sys.exit(USAGE)
        box = parse_crop(argv[1])
        argv = argv[2:]

    if len(argv) != 2:
        sys.exit(USAGE)

    source, target = argv[0], argv[1]

    original = Image.open(source).convert("RGB")

    if box is not None:
        if box[2] > original.width or box[3] > original.height:
            sys.exit(f"crop reaches outside the image, which is "
                     f"{original.width} x {original.height}")
        print(f"crop: {box[0]},{box[1]} {box[2]-box[0]} x {box[3]-box[1]}")
        original = original.crop(box)

    image = fit_to_screen(original)
    data = to_rgb565(image)

    with open(target, "wb") as handle:
        handle.write(data)

    if (original.width, original.height) != (image.width, image.height):
        print(f"{source}: {original.width} x {original.height} "
              f"-> {image.width} x {image.height}")
    else:
        print(f"{source}: {image.width} x {image.height}")
    print(f"{target}: {len(data)} bytes RGB565")
    print(f"header: .w = {image.width}, .h = {image.height}, "
          f".stride = {image.width * 2}")


if __name__ == "__main__":
    main()
