#!/usr/bin/env python3
"""Generate multi-resolution .ico files and optimized PNGs for PortalVNC from
the source art in assets/. Run: python tools/make-icons.py"""
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
SIZES = [16, 24, 32, 48, 64, 128, 256]

JOBS = [
    ("vnc-server-icon.png", "pvnc-server.ico", "pvnc-server-256.png"),
    ("pnv-viewer-icon.png", "pvnc-viewer.ico", "pvnc-viewer-256.png"),
]

# The Agent Manager has no dedicated art; derive it from the server icon with a
# subtle blue accent so it reads as a related-but-distinct "admin" tool.
TINTED = [
    ("vnc-server-icon.png", "pvnc-admin.ico", "pvnc-admin-256.png", (0x3B, 0x82, 0xF6), 0.28),
]


def tint(im, rgb, amount):
    """Blend opaque pixels toward `rgb` by `amount`, preserving alpha."""
    r, g, b, a = im.split()
    overlay = Image.new("RGB", im.size, rgb)
    base = Image.merge("RGB", (r, g, b))
    blended = Image.blend(base, overlay, amount)
    br, bg, bb = blended.split()
    return Image.merge("RGBA", (br, bg, bb, a))


for src, ico, png in JOBS:
    im = Image.open(os.path.join(ASSETS, src)).convert("RGBA")
    im.save(os.path.join(ASSETS, ico), format="ICO", sizes=[(s, s) for s in SIZES])
    im.resize((256, 256), Image.LANCZOS).save(
        os.path.join(ASSETS, png), format="PNG", optimize=True)
    print("wrote", ico, "and", png)

for src, ico, png, rgb, amount in TINTED:
    im = tint(Image.open(os.path.join(ASSETS, src)).convert("RGBA"), rgb, amount)
    im.save(os.path.join(ASSETS, ico), format="ICO", sizes=[(s, s) for s in SIZES])
    im.resize((256, 256), Image.LANCZOS).save(
        os.path.join(ASSETS, png), format="PNG", optimize=True)
    print("wrote", ico, "and", png)
print("done")
