#!/usr/bin/env python3
"""Generates icons/app.ico from the game's own player gunship sprite.

Design: dusk-horizon app tile, banked blue wing gunship popping off,
orange tracers up-right, cyan engine flare down-left, faint scanlines.
Run from the project root:  python icons/make_icon.py
"""
import os
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CELL = 32                      # gunship cell in assets/sprites/ships.png
S = 256                        # master canvas


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def build_background():
    """Dusk gradient + low sun + horizon glow + speed streaks."""
    top, mid1, mid2, low = (24, 16, 44), (52, 30, 82), (104, 52, 106), (226, 108, 66)
    bg = Image.new("RGBA", (S, S), top)
    px = bg.load()
    for y in range(S):
        t = y / (S - 1)
        c = lerp(mid1, lerp(mid2, low, max(0.0, (t - 0.55) / 0.45)), min(1.0, t / 0.55)[0] if False else t / 0.55) \
            if t < 0.55 else lerp(mid2, low, (t - 0.55) / 0.45)
        c = lerp(top, c, min(1.0, t * 55.0))
        for x in range(S):
            px[x, y] = (c[0], c[1], c[2], 255)

    d = ImageDraw.Draw(bg)
    # sun disc low on the horizon with soft glow rings
    cx, cy, rx, ry = 128, 208, 118, 74
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    for rr, a in ((180, 26), (150, 40), (120, 58)):
        gd.ellipse((cx - rx * rr / 120, cy - ry * rr / 120, cx + rx * rr / 120, cy + ry * rr / 120),
                   fill=(255, 160, 80, a))
    bg = Image.alpha_composite(bg, glow.filter(ImageFilter.GaussianBlur(6)))
    d = ImageDraw.Draw(bg)
    d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=(255, 190, 105, 255))
    d.ellipse((cx - rx * 0.82, cy - ry * 0.82, cx + rx * 0.82, cy + ry * 0.82), fill=(255, 214, 140, 255))
    # horizon band: crisp warm line just under the ship
    d.rectangle((0, 214, S, 222), fill=(255, 150, 82, 255))
    d.rectangle((0, 222, S, 256), fill=(52, 26, 52, 255))

    # diagonal speed streaks (fixed pattern, keeps icon family look)
    for (x0, y0, x1, y1, a, w) in (
            (18, 52, 96, 46, 34, 2), (36, 88, 132, 80, 26, 2), (20, 128, 88, 124, 22, 2),
            (168, 36, 244, 30, 30, 2), (150, 96, 234, 92, 20, 2), (188, 150, 246, 146, 24, 2),
            (60, 176, 150, 170, 18, 2)):
        d.line((x0, y0, x1, y1), fill=(206, 232, 255, a), width=w)
    return bg


def build_ship():
    """Blue gunship cell, upscaled, banked, plus drop shadow."""
    sheet = Image.open(os.path.join(ROOT, "assets", "sprites", "ships.png")).convert("RGBA")
    cell = sheet.crop((0, 0, CELL, CELL)).resize((CELL * 5, CELL * 5), Image.NEAREST)  # 160
    ship = cell.rotate(-18, expand=True, resample=Image.NEAREST)                       # bank CW, nose right

    shadow = ship.split()[3].point(lambda a: int(a * 0.35))
    sh = Image.merge("RGBA", (Image.new("L", ship.size, 8), Image.new("L", ship.size, 6),
                              Image.new("L", ship.size, 20), shadow))
    return ship, sh


NOSE = (151, 79)                                   # ship nose tip on the 256 canvas
DIR = (0.31, -0.95)                                # flight direction (18 deg bank)


def build_tracers():
    """A stream of tracer bolts flying off the nose, aligned with the bank."""
    bolts = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    for dist, ln, w in ((24, 34, 8), (50, 26, 6), (74, 16, 5)):
        cx, cy = NOSE[0] + DIR[0] * dist, NOSE[1] + DIR[1] * dist
        b = Image.new("RGBA", (ln + 20, w + 20), (0, 0, 0, 0))
        bd = ImageDraw.Draw(b)
        y = w // 2 + 10
        # tail-glow body -> hot core -> white-hot tip
        bd.line((10, y, 10 + ln, y), fill=(255, 122, 54, 205), width=w + 3)
        bd.line((12, y, 10 + ln, y), fill=(255, 212, 120, 255), width=w)
        bd.line((13, y, 12 + ln - 2, y), fill=(255, 250, 238, 255), width=max(2, w - 3))
        b = b.rotate(-18, expand=True, resample=Image.BILINEAR)
        bolts.alpha_composite(b, (int(cx - b.width / 2), int(cy - b.height / 2)))
    return bolts


def build_nose_flash():
    """Small muzzle flash right on the nose."""
    fl = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(fl)
    cx, cy = NOSE[0] + DIR[0] * 3, NOSE[1] + DIR[1] * 3
    d.ellipse((cx - 9, cy - 9, cx + 9, cy + 9), fill=(120, 236, 255, 90))
    d.ellipse((cx - 5, cy - 5, cx + 5, cy + 5), fill=(238, 252, 255, 235))
    return fl


def build_engine_flare():
    """Cyan exhaust streaming from the lower-left tail."""
    fl = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(fl)
    tx, ty = 104, 150                                   # ship tail
    for dist, ln, w, c, a in ((10, 18, 6, (150, 238, 255), 210),
                              (26, 22, 4, (66, 210, 255), 170),
                              (46, 26, 3, (44, 176, 235), 120),
                              (68, 28, 3, (36, 140, 205), 70)):
        x0, y0 = tx - DIR[0] * dist - DIR[1] * ln / 2, ty - DIR[1] * dist + DIR[0] * ln / 2
        x1, y1 = x0 + DIR[1] * ln,                      y0 - DIR[0] * ln
        d.line((x0, y0, x1, y1), fill=c + (a,), width=w)
    # soft glow hugging the tail
    gl = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gd = ImageDraw.Draw(gl)
    gx, gy = tx - DIR[0] * 12, ty - DIR[1] * 12
    for r, a in ((26, 40), (16, 70)):
        gd.ellipse((gx - r, gy - r, gx + r, gy + r), fill=(120, 226, 255, a))
    return Image.alpha_composite(gl.filter(ImageFilter.GaussianBlur(3)), fl)


def main():
    base = build_background()
    ship, sh = build_ship()

    ic = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ic.alpha_composite(base)

    # shadow then ship, centered so the ship dominates the tile
    sw, shh = sh.size
    ic.alpha_composite(sh, (128 - sw // 2, 114 - shh // 2 + 34))
    ic.alpha_composite(ship, (128 - sw // 2, 114 - shh // 2))

    ic.alpha_composite(build_engine_flare())
    ic.alpha_composite(build_tracers())
    ic.alpha_composite(build_nose_flash())

    # frame: navy ring + inner top highlight + faint scanlines
    d = ImageDraw.Draw(ic)
    d.rounded_rectangle((4, 4, S - 5, S - 5), radius=44, outline=(14, 18, 34, 255), width=10)
    hl = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    hd = ImageDraw.Draw(hl)
    hd.arc((9, 9, S - 10, S - 10), start=190, end=340, fill=(150, 226, 255, 70), width=3)
    for y in range(6, S - 6, 3):
        hd.line((6, y, S - 7, y), fill=(0, 0, 0, 16))
    ic.alpha_composite(hl)

    # crop to the rounded shape so corners are transparent
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, S - 1, S - 1), radius=48, fill=255)
    ic.putalpha(Image.composite(ic.split()[3], Image.new("L", (S, S), 0), mask))

    out32 = ic.resize((32, 32), Image.LANCZOS)
    out32.save(os.environ["TEMP"] + r"\opencode\icon_preview32.png")
    ic.save(os.environ["TEMP"] + r"\opencode\icon_preview256.png")

    sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    imgs = [ic.resize(sz, Image.LANCZOS) for sz in sizes]
    imgs[-1].save(os.path.join(ROOT, "icons", "app.ico"), format="ICO",
                  sizes=sizes, append_images=imgs[:-1])
    print("app.ico written")


if __name__ == "__main__":
    main()
