#!/usr/bin/env python3
"""Use kamb205's own TNA iMPACT! Reloaded art (ps5/art/local/custom/) over the generated art.

    ~/ps5-deps/artvenv/bin/python ps5/art/apply_custom_art.py

custom/logo_src.*        -> hub/logo.png (white surround removed: flood fill from the edges)
custom/icon_src.*        -> icon0.png 512x512 + icon0.dds
custom/background_src.*  -> pic0.png 3840x2160, pic1.png 1920x1080, pic0/pic1/pic2.dds
Runs after make_ps5_art.py (tools/make_release.sh does both). Missing files are skipped.
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parent
LOCAL = ROOT / "local"
CUSTOM = LOCAL / "custom"
sys.path.insert(0, str(ROOT))
from make_ps5_art import write_dds_bc7  # noqa: E402


def find(stem):
    for p in sorted(CUSTOM.glob(stem + ".*")):
        return p
    return None


def cover(im, w, h):
    """Scale to cover w x h, centre-crop."""
    s = max(w / im.width, h / im.height)
    im = im.resize((round(im.width * s), round(im.height * s)), Image.LANCZOS)
    x, y = (im.width - w) // 2, (im.height - h) // 2
    return im.crop((x, y, x + w, y + h))


def remove_white(im):
    """Alpha = 0 for near-white pixels connected to the border; soft edge."""
    rgb = np.asarray(im.convert("RGB")).astype(np.int16)
    white = (rgb.min(axis=2) > 225)
    h, w = white.shape
    seen = np.zeros_like(white)
    stack = [(y, x) for x in range(w) for y in (0, h - 1)] + [(y, x) for y in range(h) for x in (0, w - 1)]
    while stack:
        y, x = stack.pop()
        if y < 0 or x < 0 or y >= h or x >= w or seen[y, x] or not white[y, x]:
            continue
        seen[y, x] = True
        stack += [(y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)]
    alpha = Image.fromarray(np.where(seen, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2))
    out = im.convert("RGBA")
    out.putalpha(alpha)
    return out.crop(out.getchannel("A").point(lambda v: 255 if v > 20 else 0).getbbox())


def main():
    hub = LOCAL / "hub"
    hub.mkdir(parents=True, exist_ok=True)
    if (p := find("logo_src")):
        src = Image.open(p)
        has_alpha = src.mode in ("RGBA", "LA") or "transparency" in src.info
        if has_alpha and np.asarray(src.convert("RGBA"))[..., 3].min() < 20:
            logo = src.convert("RGBA")  # (already transparent)
            logo = logo.crop(logo.getchannel("A").point(lambda v: 255 if v > 20 else 0).getbbox())
        else:
            logo = remove_white(src)
        logo.save(hub / "logo.png", optimize=True)
        print("logo", logo.size)
    if (p := find("icon_src")):
        icon = cover(Image.open(p).convert("RGB"), 512, 512)
        icon.save(LOCAL / "icon0.png", optimize=True)
        write_dds_bc7(icon, LOCAL / "icon0.dds")
        print("icon 512x512")
    if (p := find("background_src")):
        bg = cover(Image.open(p).convert("RGB"), 3840, 2160)
        bg = bg.filter(ImageFilter.UnsharpMask(radius=2, percent=50, threshold=2))
        bg.save(LOCAL / "pic0.png", optimize=True)
        bg.resize((1920, 1080), Image.LANCZOS).save(LOCAL / "pic1.png", optimize=True)
        for n in ("pic0", "pic1", "pic2"):
            write_dds_bc7(bg, LOCAL / (n + ".dds"))
        print("background 3840x2160")


if __name__ == "__main__":
    main()
