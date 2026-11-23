# This file is part of TGeshka, an unofficial client
# based on Telegram Desktop (GPLv3).
#
# Draws the TGeshka logo (lavender circle, white paper plane) and writes every
# icon the Windows build uses into Telegram/Resources/art, plus the SVG
# source next to this script. Run: python Telegram/build/tgeshka/gen_icons.py
# Requires Pillow.

import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.normpath(os.path.join(HERE, '..', '..', 'Resources', 'art'))

# Colors: gradient from top-left to bottom-right.
TOP = (188, 170, 247)
BOTTOM = (132, 112, 222)
PLANE = (255, 255, 255)
FOLD = (230, 223, 255)

# Plane in a 100x100 box, circle is centered at (50, 50) with radius 50.
BODY = [(16, 48), (80, 22), (68, 78), (50, 66), (40, 76), (39, 59)]
FOLD_PART = [(39, 59), (73, 30), (50, 66), (40, 76)]

SUPERSAMPLE = 8


def lerp(a, b, t):
    return tuple(int(round(x + (y - x) * t)) for x, y in zip(a, b))


def draw_logo(size, margin):
    # margin: part of the size left transparent on each side.
    big = size * max(1, min(SUPERSAMPLE, 4096 // size))
    image = Image.new('RGBA', (big, big), (0, 0, 0, 0))

    inner = big - 2 * int(round(margin * big))
    offset = (big - inner) // 2

    steps = 256
    gradient = Image.new('RGBA', (steps, steps))
    pixels = gradient.load()
    for y in range(steps):
        for x in range(steps):
            t = (x + y) / (2.0 * (steps - 1))
            pixels[x, y] = lerp(TOP, BOTTOM, t) + (255,)
    gradient = gradient.resize((inner, inner), Image.BICUBIC)
    mask = Image.new('L', (inner, inner), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, inner - 1, inner - 1), fill=255)
    image.paste(gradient, (offset, offset), mask)

    draw = ImageDraw.Draw(image)

    def scale(points):
        return [
            (offset + x * inner / 100.0, offset + y * inner / 100.0)
            for x, y in points
        ]

    draw.polygon(scale(BODY), fill=PLANE + (255,))
    draw.polygon(scale(FOLD_PART), fill=FOLD + (255,))
    return image.resize((size, size), Image.LANCZOS)


def svg():
    def path(points):
        return 'M' + ' L'.join('%g %g' % p for p in points) + ' Z'
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">\n'
        '  <defs>\n'
        '    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">\n'
        '      <stop offset="0" stop-color="#%02x%02x%02x"/>\n'
        '      <stop offset="1" stop-color="#%02x%02x%02x"/>\n'
        '    </linearGradient>\n'
        '  </defs>\n'
        '  <circle cx="50" cy="50" r="50" fill="url(#bg)"/>\n'
        '  <path d="%s" fill="#ffffff"/>\n'
        '  <path d="%s" fill="#%02x%02x%02x"/>\n'
        '</svg>\n'
    ) % (TOP + BOTTOM + (path(BODY), path(FOLD_PART)) + FOLD)


def main():
    if not os.path.isdir(ART):
        sys.exit('Resources/art not found: ' + ART)

    # Same transparent border as the upstream icons (10px of 256).
    margin = 10.0 / 256.0

    for base in (16, 32, 48, 64, 128, 256, 512):
        small_margin = 0.0 if base <= 32 else margin
        draw_logo(base, small_margin).save(
            os.path.join(ART, 'icon%d.png' % base))
        draw_logo(base * 2, small_margin).save(
            os.path.join(ART, 'icon%d@2x.png' % base))

    draw_logo(256, margin).save(os.path.join(ART, 'logo_256.png'))
    draw_logo(256, 0.0).save(os.path.join(ART, 'logo_256_no_margin.png'))
    draw_logo(1024, 50.0 / 1024.0).save(
        os.path.join(ART, 'icon_round512@2x.png'))

    ico_sizes = [16, 24, 32, 48, 64, 128, 256]
    frames = [draw_logo(s, 0.0 if s <= 32 else margin) for s in ico_sizes]
    frames[-1].save(
        os.path.join(ART, 'icon256.ico'),
        sizes=[(s, s) for s in ico_sizes],
        append_images=frames[:-1])

    with open(os.path.join(HERE, 'logo.svg'), 'w', encoding='utf-8',
              newline='\n') as f:
        f.write(svg())

    print('Icons written to ' + ART)


if __name__ == '__main__':
    main()
