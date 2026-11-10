# SPDX-License-Identifier: GPL-3.0-or-later
"""Draws the application icon and writes every size the packages need.

Run from the repository root:  python3 packaging/icons/make_icons.py
Needs Pillow. Writes resources/icons/twisted-harness-designer.{png,ico,icns}.
The outputs are committed, so builds never run this script.
"""
import math
import pathlib

from PIL import Image, ImageDraw

SIZE = 1024
SS = 4  # supersampling factor for smooth edges
OUT = pathlib.Path(__file__).resolve().parents[2] / "resources" / "icons"

BACKGROUND = (30, 42, 56)
CONNECTOR = (196, 204, 214)
CONNECTOR_EDGE = (120, 130, 142)
PIN_HOLE = (60, 70, 82)
WIRE_A = (232, 92, 40)   # orange
WIRE_B = (52, 140, 230)  # blue


def draw(size):
    s = size * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # Rounded-square background, inset like macOS/Windows app icons.
    inset = s * 0.06
    d.rounded_rectangle([inset, inset, s - inset, s - inset], radius=s * 0.18,
                        fill=BACKGROUND)

    # Connector housing on the left with two pin holes.
    cx0, cx1 = s * 0.14, s * 0.36
    cy0, cy1 = s * 0.30, s * 0.70
    d.rounded_rectangle([cx0, cy0, cx1, cy1], radius=s * 0.03, fill=CONNECTOR,
                        outline=CONNECTOR_EDGE, width=int(s * 0.012))
    hole_r = s * 0.04
    hx = (cx0 + cx1) / 2
    for hy in (s * 0.42, s * 0.58):
        d.ellipse([hx - hole_r, hy - hole_r, hx + hole_r, hy + hole_r], fill=PIN_HOLE)

    # Twisted pair leaving the connector: two sine waves half a period apart.
    x_start, x_end = cx1, s * 0.88
    mid, amp = s * 0.50, s * 0.09
    turns = 1.75  # ends a quarter turn short so the two stripped ends sit apart

    def points(phase):
        pts = []
        steps = 400
        for i in range(steps + 1):
            t = i / steps
            x = x_start + (x_end - x_start) * t
            # Leave the connector at the pin heights, then twist.
            blend = min(1.0, t / 0.18)
            pin_y = mid + (s * 0.08 if phase == 0 else -s * 0.08)
            wave = mid + amp * math.sin(2 * math.pi * turns * t + phase)
            pts.append((x, pin_y * (1 - blend) + wave * blend))
        return pts

    a = points(0.0)
    b = points(math.pi)

    # Split both wires halfway between crossings so each piece holds one
    # crossing; pieces alternate which wire is drawn on top.
    crossings = [i for i in range(1, len(a))
                 if (a[i - 1][1] - b[i - 1][1]) * (a[i][1] - b[i][1]) <= 0]
    cuts = [0] + [(c0 + c1) // 2 for c0, c1 in zip(crossings, crossings[1:])] + [len(a) - 1]
    r = s * 0.032
    edge = s * 0.008

    def stroke(pts, lo, hi, color):
        # The fill starts a little before the outline so a piece's outline
        # does not leave a dark ring where it joins the previous piece.
        for x, y in pts[lo:hi + 1]:
            d.ellipse([x - r - edge, y - r - edge, x + r + edge, y + r + edge], fill=BACKGROUND)
        for x, y in pts[max(0, lo - 12):hi + 1]:
            d.ellipse([x - r, y - r, x + r, y + r], fill=color)

    for i, (lo, hi) in enumerate(zip(cuts, cuts[1:])):
        under, over = ((b, WIRE_B), (a, WIRE_A)) if i % 2 == 0 else ((a, WIRE_A), (b, WIRE_B))
        stroke(under[0], lo, hi, under[1])
        stroke(over[0], lo, hi, over[1])

    # Stripped wire ends (copper) at the right.
    copper = (214, 160, 80)
    for pts in (a, b):
        x, y = pts[-1]
        d.rounded_rectangle([x, y - r * 0.55, x + s * 0.05, y + r * 0.55],
                            radius=r * 0.4, fill=copper)

    return img.resize((size, size), Image.LANCZOS)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    icon = draw(SIZE)
    icon.resize((256, 256), Image.LANCZOS).save(OUT / "twisted-harness-designer.png")
    icon.save(OUT / "twisted-harness-designer.ico",
              sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    icon.save(OUT / "twisted-harness-designer.icns")


if __name__ == "__main__":
    main()
