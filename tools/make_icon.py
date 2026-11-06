"""Draws the WWMA icon: a mixing desk in a laundry basket ("washable and withstanding").

    python tools/make_icon.py

Writes assets/icon/wwma-icon.png (1024 px, app icon) and assets/icon/wwma-icon-small.png
(256 px, fewer details, for 16-48 px icon sizes and the plugin's top bar). Needs Pillow.
"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets/icon"

BG = (27, 34, 38)          # colours::panel
EDGE = (47, 58, 64)        # colours::line
DESK = (52, 64, 71)
DESK_EDGE = (86, 101, 107) # colours::faint
SLOT = (20, 25, 28)        # colours::background
ACCENT = (127, 209, 199)   # colours::accent (the basket)
BASKET_DARK = (78, 150, 142)
HOLE = (27, 34, 38)
CAPS = [(226, 87, 76), (232, 163, 58), (214, 201, 73), (92, 193, 122), (111, 143, 179)]
KNOB = (196, 207, 210)

SS = 4  # supersampling


def desk_layer(S, k, small):
    """The mixing desk, drawn upright on its own layer (rotated afterwards)."""
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    x0, y0, x1, y1 = 250 * k, 150 * k, 774 * k, 560 * k
    d.rounded_rectangle((x0, y0, x1, y1), radius=36 * k, fill=DESK, outline=DESK_EDGE, width=int(10 * k))

    channels = 4 if small else 5
    pad = 60 * k
    step = (x1 - x0 - 2 * pad) / (channels - 1)
    capsY = [0.55, 0.25, 0.7, 0.4, 0.15] if not small else [0.55, 0.2, 0.65, 0.35]
    for i in range(channels):
        cx = x0 + pad + step * i
        # knobs on top
        if not small:
            r = 22 * k
            ky = y0 + 62 * k
            d.ellipse((cx - r, ky - r, cx + r, ky + r), fill=KNOB)
            d.line((cx, ky, cx + r * 0.6, ky - r * 0.6), fill=DESK, width=int(7 * k))
        # fader slot and cap
        top, bot = y0 + (115 if not small else 60) * k, y1 - 40 * k
        w = (12 if not small else 18) * k
        d.rounded_rectangle((cx - w / 2, top, cx + w / 2, bot), radius=w / 2, fill=SLOT)
        cy = top + (bot - top) * capsY[i]
        cw, ch = (64 if not small else 84) * k, (30 if not small else 40) * k
        d.rounded_rectangle((cx - cw / 2, cy - ch / 2, cx + cw / 2, cy + ch / 2), radius=8 * k, fill=CAPS[i % len(CAPS)])
    return layer


def draw(size, small):
    S = size * SS
    k = S / 1024.0
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # rounded tile
    pad = 24 * k
    d.rounded_rectangle((pad, pad, S - pad, S - pad), radius=210 * k, fill=BG, outline=EDGE, width=int(14 * k))

    # the desk, leaning in the basket
    desk = desk_layer(S, k, small).rotate(-9, resample=Image.BICUBIC, center=(512 * k, 600 * k))
    img.alpha_composite(desk)
    d = ImageDraw.Draw(img)

    # the basket: wider at the top, a thick rim with handle holes, rows of slots
    top, bottom = 470 * k, 880 * k
    lt, rt, lb, rb = 140 * k, 884 * k, 220 * k, 804 * k
    d.polygon([(lt, top), (rt, top), (rb, bottom), (lb, bottom)], fill=ACCENT)
    d.rounded_rectangle((lb, bottom - 60 * k, rb, bottom), radius=30 * k, fill=ACCENT)  # rounded bottom corners
    rim = 78 * k
    d.rounded_rectangle((lt - 22 * k, top - 10 * k, rt + 22 * k, top + rim), radius=28 * k, fill=ACCENT)
    # handle holes in the rim
    hw, hh = 120 * k, 28 * k
    for hx in (lt + 70 * k, rt - 70 * k - hw):
        d.rounded_rectangle((hx, top + 12 * k, hx + hw, top + 12 * k + hh), radius=hh / 2, fill=BASKET_DARK)
    # the weave: rows of slots, narrowing with the basket
    rows = 3 if small else 4
    y = top + rim + (34 if small else 30) * k
    rowH = (bottom - 40 * k - y) / rows
    for row in range(rows):
        ry = y + rowH * row
        t = (ry - top) / (bottom - top)
        left = lt + (lb - lt) * t + 60 * k
        right = rt + (rb - rt) * t - 60 * k
        n = 4 if small else 6
        gap = (26 if small else 22) * k
        sw = (right - left - gap * (n - 1)) / n
        sh = rowH * 0.5
        for i in range(n):
            sx = left + i * (sw + gap)
            d.rounded_rectangle((sx, ry, sx + sw, ry + sh), radius=sh / 2, fill=HOLE)

    return img.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    draw(1024, small=False).save(OUT / "wwma-icon.png", optimize=True)
    draw(256, small=True).save(OUT / "wwma-icon-small.png", optimize=True)
    print("wrote", OUT)
