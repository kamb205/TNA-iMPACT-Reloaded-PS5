#!/usr/bin/env python3
"""PS5 home-screen and hub art from the user's own disc (never committed: ps5/art/local/ is ignored).

    ~/ps5-deps/artvenv/bin/python ps5/art/make_ps5_art.py [private/extract]

Needs Pillow, numpy and etcpak (BC7). Writes into ps5/art/local/:
  icon0.dds (512x512) and pic0/pic1/pic2.dds (3840x2160), BC7 like the PS5's own apps (the home
  screen shows the background on hover from these); pic0.png / pic1.png;
  hub/hero.jpg (the hub's backdrop) and hub/logo.png (the TNA logo, cut out of Splash_end).
"""
import struct
import sys
from pathlib import Path

import etcpak
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "local"
GAME = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT.parent.parent / "private" / "extract")
SPLASH = GAME / "Splash"


def upscale(im, w, h):
    """720p art to 4K: Lanczos, then a light unsharp mask so it doesn't look soft."""
    big = im.resize((w, h), Image.LANCZOS)
    return big.filter(ImageFilter.UnsharpMask(radius=2.2, percent=70, threshold=2))


def write_dds_bc7(im, path):
    im = im.convert("RGBA")
    w, h = im.size
    data = etcpak.compress_bc7(im.tobytes(), w, h)
    header = struct.pack("<4s7I44x8I4I4x", b"DDS ", 124, 0xA1007, h, w, w * h, 0, 1,
                         32, 4, 0x30315844, 0, 0, 0, 0, 0,  # pixel format: FourCC "DX10"
                         0x1000, 0, 0, 0)                   # caps: texture
    header += struct.pack("<5I", 98, 3, 0, 1, 0)            # BC7_UNORM, 2D, 1 array slice
    assert len(header) == 148, len(header)
    path.write_bytes(header + data)


def logo():
    """The TNA logo from Splash_end (on a dark crowd): keyed by brightness and saturation."""
    im = Image.open(SPLASH / "Splash_end.bmp").convert("RGB")
    a = np.asarray(im.crop((280, 60, 1000, 200))).astype(float)
    mx, mn = a.max(2), a.min(2)
    alpha = np.clip(np.maximum(mx - 95, (mx - mn) * 1.5 - 75) / 70, 0, 1)
    alpha[alpha < 0.12] = 0
    out = Image.fromarray(np.dstack([a, alpha * 255]).astype(np.uint8), "RGBA")
    out = out.crop(out.getchannel("A").point(lambda v: 255 if v > 60 else 0).getbbox())
    return out.resize((out.width * 2, out.height * 2), Image.LANCZOS)


# ------------------------------------------------------------- Reloaded art

FONT = ROOT.parent / "fonts" / "oswald-latin-700-normal.ttf"


def original_logo():
    """The plain title logo (T009's hub/logo.png), kept as logo_original.png."""
    out = OUT / "hub"
    out.mkdir(parents=True, exist_ok=True)
    orig = out / "logo_original.png"
    if orig.is_file():
        return Image.open(orig).convert("RGBA")
    if (out / "logo.png").is_file():
        im = Image.open(out / "logo.png").convert("RGBA")
        im.save(orig, optimize=True)
        return im
    return logo()  # (no T009 art: cut it out of the splash again)


def logo_colours(base):
    """Red and gold sampled from the logo's own pixels (the metallic gradient)."""
    a = np.asarray(base).astype(float)
    rgb, alpha = a[..., :3], a[..., 3]
    hsv = np.asarray(base.convert("HSV")).astype(float)
    h, s, v = hsv[..., 0], hsv[..., 1], hsv[..., 2]
    red = (alpha > 180) & (s > 140) & ((h < 18) | (h > 235)) & (v > 60)
    gold = (alpha > 180) & (s > 100) & (h > 25) & (h < 60) & (v > 120)
    r = rgb[red].mean(0) if red.sum() > 50 else np.array([200.0, 16.0, 46.0])
    g = rgb[gold].mean(0) if gold.sum() > 50 else np.array([212.0, 160.0, 23.0])
    return r, g


def make_reloaded(base):
    """The original logo with RELOADED in matching heavy caps and a metal gradient."""
    font_path = FONT if FONT.is_file() else None
    if not font_path:
        raise SystemExit("missing ps5/fonts/oswald-latin-700-normal.ttf")
    target = base.width * 0.60
    size = int(base.height * 0.95)
    while size > 24:
        font = ImageFont.truetype(str(font_path), size)
        tracking = max(2, size // 18)
        w = sum(font.getlength(c) for c in "RELOADED") + tracking * 7
        if w <= target:
            break
        size -= 4
    font = ImageFont.truetype(str(font_path), size)
    tracking = max(2, size // 18)
    bbox = font.getbbox("RELOADED")
    tw = int(sum(font.getlength(c) for c in "RELOADED") + tracking * 7)
    th = bbox[3] - bbox[1]
    margin = max(12, size // 5)
    mask = Image.new("L", (tw + margin * 2, th + margin * 2), 0)
    d = ImageDraw.Draw(mask)
    x = margin
    for c in "RELOADED":
        d.text((x, margin - bbox[1]), c, 255, font=font)
        x += font.getlength(c) + tracking

    red, gold = logo_colours(base)
    light = np.clip(gold * 0.45 + 255 * 0.55, 0, 255)
    dark = np.clip(red * 0.45, 0, 255)
    stops = np.array([dark, red, gold, light])
    t = np.linspace(0, 1, mask.height)
    pos = np.linspace(0, 3, len(t))
    grad = np.zeros((mask.height, 3))
    for c in range(3):
        grad[:, c] = np.interp(pos, np.arange(4), stops[:, c])
    grad = np.repeat(grad[:, None, :], mask.width, axis=1)
    # bevel: brighter at the top, darker at the bottom
    shade = np.ones(mask.height)
    shade[: max(1, mask.height // 5)] = 1.22
    shade[-max(1, mask.height // 5):] = 0.78
    grad = np.clip(grad * shade[:, None, None], 0, 255)
    fill = Image.fromarray(np.dstack([grad, np.asarray(mask)]).astype(np.uint8), "RGBA")

    # dark outline, bevel shadow, then the gradient face
    outline = mask.filter(ImageFilter.MaxFilter(9))
    shadow = outline.filter(ImageFilter.GaussianBlur(max(3, size // 14)))
    word_y = base.height - int(th * 0.12)
    canvas = Image.new("RGBA", (max(base.width, mask.width), word_y + mask.height + margin), (0, 0, 0, 0))
    canvas.alpha_composite(base, ((canvas.width - base.width) // 2, 0))
    sh = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    sh.paste((5, 5, 7, 170), ((canvas.width - mask.width) // 2, word_y + max(3, size // 16)), shadow)
    canvas.alpha_composite(sh)
    ol = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    ol.paste((16, 8, 10, 255), ((canvas.width - mask.width) // 2, word_y), outline)
    canvas.alpha_composite(ol)
    canvas.alpha_composite(fill, ((canvas.width - mask.width) // 2, word_y))
    return canvas.crop(canvas.getbbox())


def composite(bg, mark):
    """The Reloaded logo on the TNA_Load background's left third, legible on the tile."""
    bg = bg.convert("RGBA")
    w = bg.width // 3 - 140
    h = int(w * mark.height / mark.width)
    x, y = 120, (bg.height - h) // 2
    shade = Image.new("RGBA", bg.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(shade)
    d.rounded_rectangle((x - 60, y - 40, x + w + 60, y + h + 40), radius=40, fill=(0, 0, 0, 150))
    bg = Image.alpha_composite(bg, shade.filter(ImageFilter.GaussianBlur(45)))
    m = mark.resize((w, h), Image.LANCZOS)
    bg.alpha_composite(m, (x, y))
    return bg.convert("RGB")


def make_icon(mark):
    """512x512 icon: the Reloaded logo on the hub's dark plate."""
    a = np.zeros((512, 512, 3), dtype=float)
    yy, xx = np.mgrid[0:512, 0:512]
    r = np.sqrt((xx - 256) ** 2 + (yy - 256) ** 2) / 362
    glow = np.clip(1 - r, 0, 1) ** 2
    a[..., 0] = 12 + 26 * glow
    a[..., 1] = 12 + 17 * glow
    a[..., 2] = 15 + 11 * glow
    plate = Image.fromarray(a.astype(np.uint8), "RGB").convert("RGBA")
    w = 468
    h = int(w * mark.height / mark.width)
    m = mark.resize((w, h), Image.LANCZOS)
    plate.alpha_composite(m, ((512 - w) // 2, (512 - h) // 2))
    return plate.convert("RGB")


def contact(mark, icon, bg):
    """private/analysis/reloaded_check.jpg: the logo, the icon and the background."""
    sheet = Image.new("RGB", (2000, 1280), (16, 17, 20))
    m = mark.copy()
    m.thumbnail((1180, 520), Image.LANCZOS)
    sheet.paste(m, (50, 50), m)
    sheet.paste(icon, (1370, 50))
    b = bg.copy()
    b.thumbnail((1900, 620), Image.LANCZOS)
    sheet.paste(b, (50, 620))
    out = ROOT.parent.parent / "private" / "analysis"
    out.mkdir(parents=True, exist_ok=True)
    sheet.save(out / "reloaded_check.jpg", quality=92)


def main():
    (OUT / "hub").mkdir(parents=True, exist_ok=True)
    base = Image.open(SPLASH / "TNA_Load.bmp").convert("RGB")
    big = upscale(base, 3840, 2160)
    plain = original_logo()
    mark = make_reloaded(plain)
    mark.save(OUT / "hub" / "logo.png", optimize=True)
    plain.save(OUT / "hub" / "logo_original.png", optimize=True)
    pic_big = composite(big, mark)
    pic_big.save(OUT / "pic0.png", optimize=True)
    pic_small = pic_big.resize((1920, 1080), Image.LANCZOS)
    pic_small.save(OUT / "pic1.png", optimize=True)
    big.save(OUT / "hub" / "hero.jpg", quality=92)
    for name, im in (("pic0", pic_big), ("pic1", pic_big), ("pic2", pic_big)):
        write_dds_bc7(im, OUT / (name + ".dds"))
    icon = make_icon(mark)
    icon.save(OUT / "icon0.png", optimize=True)
    write_dds_bc7(icon.resize((512, 512), Image.LANCZOS), OUT / "icon0.dds")
    contact(mark, icon, pic_big)
    for p in sorted(OUT.rglob("*")):
        if p.is_file():
            print("%-24s %9d" % (p.relative_to(OUT), p.stat().st_size))


if __name__ == "__main__":
    main()
