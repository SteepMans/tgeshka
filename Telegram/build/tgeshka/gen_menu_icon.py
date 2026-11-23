# This file is part of TGeshka, an unofficial client
# based on Telegram Desktop (GPLv3).
#
# Generates the settings menu icon (white mask on black, as the upstream
# menu icons): Telegram/Resources/icons/menu/tgeshka{,@2x,@3x}.png
# The plane is the one from logo.svg inside a ring.
#
#   python Telegram/build/tgeshka/gen_menu_icon.py

import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'Resources', 'icons', 'menu')

# logo.svg plane, viewBox 0..100.
PLANE = [(16, 48), (80, 22), (68, 78), (50, 66), (40, 76), (39, 59)]
FOLD = [(39, 59), (73, 30), (50, 66), (40, 76)]

def render(size):
    k = 8
    big = size * k
    image = Image.new('L', (big, big), 0)
    draw = ImageDraw.Draw(image)
    # Ring: 20 of 24 logical pixels, 1.5 px stroke.
    unit = big / 24.0
    margin = 2.0 * unit
    stroke = 1.5 * unit
    draw.ellipse(
        [margin, margin, big - margin, big - margin],
        outline=255,
        width=int(round(stroke)))
    # Plane inside the ring, logo coordinates mapped to 4..20.
    def map_point(p):
        x, y = p
        return (
            (4.0 + x / 100.0 * 16.0) * unit,
            (4.0 + y / 100.0 * 16.0) * unit)
    draw.polygon([map_point(p) for p in PLANE], fill=255)
    draw.polygon([map_point(p) for p in FOLD], fill=0)
    draw.line(
        [map_point(FOLD[0]), map_point(FOLD[1])],
        fill=255,
        width=int(round(0.9 * unit)))
    image = image.resize((size, size), Image.LANCZOS)
    return Image.merge('RGB', (image, image, image))

for suffix, scale in (('', 1), ('@2x', 2), ('@3x', 3)):
    render(24 * scale).save(os.path.join(OUT, 'tgeshka' + suffix + '.png'))
print('ok')
