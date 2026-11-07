#!/usr/bin/env bash
# Build the release kit: home-screen/hub art from the disc, the PS5 app folder,
# and dist/release/PPSA94007.zip with its sha256 and release notes.
# PREPARE ONLY: this script never uploads, pushes or creates a GitHub release.
#
#   tools/make_release.sh [extract dir]
#
# Needs the PS5 build already made (ps5/out/ps5) and ~/ps5-deps/env.sh sourced
# (PS5_TRIANGLE, PS5_PAYLOAD_SDK, RADV_ARCHIVE), as ps5/tools/ps5_package.sh.
# No game files go in the zip: Assets/ is left empty and UserData/ is excluded.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
extract=${1:-$root/private/extract}
app=$root/dist/PPSA94007
out=$root/dist/release

art_py=${ART_PYTHON:-$HOME/ps5-deps/artvenv/bin/python}
[[ -x $art_py ]] || art_py=python3

echo "==> art (Reloaded logo, icon and backgrounds from your own disc)"
"$art_py" "$root/ps5/art/make_ps5_art.py" "$extract"
"$art_py" "$root/ps5/art/apply_custom_art.py"  # (kamb205's own Reloaded art, if present)

echo "==> package the PS5 app folder"
"$root/ps5/tools/ps5_package.sh"

[[ -f $app/eboot.bin ]] || { echo "no $app/eboot.bin" >&2; exit 2; }
rm -rf -- "$out"
mkdir -p "$out"

echo "==> zip (no Assets content, no UserData, no logs)"
(
    cd "$root/dist"
    zip -r -X release/PPSA94007.zip PPSA94007 \
        -x 'PPSA94007/Assets/*' 'PPSA94007/assets/*' 'PPSA94007/UserData/*' \
           'PPSA94007/*.log' 'PPSA94007/hub/music/*'
)
(cd "$out" && shasum -a 256 PPSA94007.zip > PPSA94007.zip.sha256)

cat > "$out/release-notes.md" <<'MD'
# TNA iMPACT! Reloaded for PS5 — PPSA94007

TNA iMPACT! (Xbox 360, 2008) statically recompiled and running natively on a jailbroken PS5,
with the **Reloaded** hub, 60 fps, FSR 1 upscaling and DualSense support.

## Features

- 60 fps at 720p or 1440p (1080p output also available), with FSR 1 or CAS upscaling to 4K.
- 16x anisotropic filtering and optional FXAA / 2x–8x MSAA.
- DualSense vibration levels (Off / Light / Medium / Full) and a TNA gold light bar.
- A settings hub (Home / Settings / About) with menu music and an FPS counter.
- Bring your own game: the hub finds your .iso or extracted disc files on the PS5's drives.

## Requirements

- A jailbroken PS5 on firmware 13.60, able to run homebrew from /data/homebrew/.
- **Your own** Xbox 360 copy of TNA iMPACT! as an .iso (or extracted files with default.xex).
  No game files are included in this release.

## Install

1. Copy the `PPSA94007` folder from the zip to `/data/homebrew/` on your PS5 (over FTP).
2. Copy your own `TNA iMPACT!.iso` (or an extracted folder with `default.xex`) anywhere the hub
   searches: `/data/homebrew/PPSA94007/`, `/data/`, or a USB drive.
3. Start **TNA iMPACT! Reloaded** from your homebrew launcher. If the game isn't found, press
   **Triangle** on the Home page to search again.

## Support

If you enjoy the port: **https://buymeacoffee.com/kamb205**

## Known issues

- Online / Xbox LIVE is not available.
- Xbox button prompts remain in places (PlayStation button art is partial).
- CAW/Story black character lighting may still occur (open investigation).
MD

echo
echo "==> $out/PPSA94007.zip"
ls -lh "$out/PPSA94007.zip"
cat "$out/PPSA94007.zip.sha256"
