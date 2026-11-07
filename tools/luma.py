#!/usr/bin/env python3
"""Compute mean luminance of raw R8G8B8X8 frame dumps (T014).

Usage: python3 -I tools/luma.py <frame_dir> [--region cx cy w h]

Reads size.txt ("width height stride") and every frame_*.rgba, then prints the
mean luma (mean of R,G,B, 0..255) per frame. With --region, also prints the
mean luma of a centred rectangle (normalised 0..1 coords by default, or px if
--px is given) for the character region.
"""
import glob
import os
import sys


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    d = args[0]
    region = None
    px = False
    i = 1
    while i < len(args):
        if args[i] == "--region":
            region = tuple(map(float, args[i + 1:i + 5]))
            i += 5
        elif args[i] == "--px":
            px = True
            i += 1
        else:
            i += 1

    size_path = os.path.join(d, "size.txt")
    with open(size_path) as f:
        w, h, stride = map(int, f.read().split())
    npx = w * h

    frames = sorted(glob.glob(os.path.join(d, "frame_*.rgba")),
                    key=lambda p: int(p.split("frame_")[1].split(".")[0]))
    print(f"# dir={d} {w}x{h} stride={stride} frames={len(frames)}")
    for p in frames:
        name = os.path.basename(p).replace(".rgba", "")
        with open(p, "rb") as f:
            data = f.read()
        if len(data) < npx * 4:
            print(f"{name}: short ({len(data)} bytes)")
            continue
        b = memoryview(data)[: npx * 4]
        # R,G,B,X interleaved; sum R,G,B. Process as bytes (little-endian host).
        # Sum over all pixels: accumulate R+G+B.
        total = 0
        step = 4
        # Fast path via slicing per channel.
        r = sum(b[0::4])
        g = sum(b[1::4])
        bb = sum(b[2::4])
        total = r + g + bb
        mean = total / (npx * 3)
        out = f"{name}: mean_luma={mean:.2f}"
        if region:
            cx, cy, rw, rh = region
            if px:
                x0, y0 = int(cx), int(cy)
                x1, y1 = x0 + int(rw), y0 + int(rh)
            else:
                x0 = int((cx - rw / 2) * w)
                x1 = int((cx + rw / 2) * w)
                y0 = int((cy - rh / 2) * h)
                y1 = int((cy + rh / 2) * h)
            x0, x1 = max(0, x0), min(w, x1)
            y0, y1 = max(0, y0), min(h, y1)
            s = 0
            cnt = 0
            for y in range(y0, y1):
                row = y * w * 4
                r = sum(b[row + x0 * 4: row + x1 * 4: 4])
                g = sum(b[row + x0 * 4 + 1: row + x1 * 4: 4])
                bb = sum(b[row + x0 * 4 + 2: row + x1 * 4: 4])
                s += r + g + bb
                cnt += (x1 - x0) * 3
            out += f" region_mean={s / cnt:.2f}" if cnt else " region_mean=na"
        print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
