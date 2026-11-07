#!/usr/bin/env bash
# [ps5 port] Link the PS5 build of TNA iMPACT! and package it as a PS5
# homebrew app folder (eboot.bin, sce_module/libc.prx, sce_sys/param.json).
#
#   ps5/tools/ps5_package.sh [cmake build dir] [ps5-plume-triangle checkout] [title id]
#
# Defaults: ps5/out/ps5, $PS5_TRIANGLE from ~/ps5-deps/env.sh, PPSA94007.
# Writes ../dist/<title id>/ (repository root) - see docs/reports/T004.md.
#
# The link and packaging reuse the PS5 tooling of Nazky's ps5-plume-triangle
# / UnleashedRecomp-Prospero (mihawk-99's RADV link recipe, app CRT, libc.prx
# builder and FSELF signer): build that project once first (its build/ holds
# app_crt.o, app_cpp_runtime.o, the libSceAgc stubs and ps5-native-tool).
# No game files are packaged: copy your installed game folder into the app
# folder on the PS5 (see docs/PS5_PORT_PLAN.md in PS5-SvR2010-Native).
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build=$(realpath "${1:-$root/ps5/out/ps5}")
tri=$(realpath "${2:-${PS5_TRIANGLE:?set PS5_TRIANGLE or pass the ps5-plume-triangle checkout}}")
title_id=${3:-PPSA94007}
port=$root/ps5

sdk_root=${PS5_PAYLOAD_SDK:-$tri/.deps/native/ps5-payload-sdk}
radv_archive=${RADV_ARCHIVE:-$tri/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a}
tool=$tri/build/host/ps5-native-tool
for f in "$sdk_root/bin/prospero-lld" "$radv_archive" "$tool" "$tri/build/obj/app_crt.o" \
         "$tri/build/obj/app_cpp_runtime.o" "$tri/build/stubs/libSceAgc.so" "$tri/runtime/libc.prx"; do
    [[ -e $f ]] || { echo "missing $f (build ps5-plume-triangle first)" >&2; exit 2; }
done

# Staging root for radv_link_recipe: it reads tooling/ and vendor/ and writes build/.
stage=$build/ps5-link
mkdir -p "$stage/build"
ln -sfn "$tri/tooling" "$stage/tooling"
ln -sfn "$tri/vendor" "$stage/vendor"

# The objects and static libraries CMake links into tnaimpact, in its order.
# (bash 3.2 on macOS has no mapfile; read the words into an array.)
link_cmd=$(ninja -C "$build" -t commands tnaimpact | tail -1)
inputs=()
for w in $link_cmd; do
    case $w in
        *.o|*.a)
            if [ "${w#/}" != "$w" ]; then
                inputs+=("$w")
            else
                inputs+=("$build/$w")
            fi
            ;;
    esac
done
echo "==> [link] ${#inputs[@]} objects and libraries from CMake"

export PS5_CLANG=${PS5_CLANG:-$(command -v clang-19 || command -v clang)}
source "$tri/tools/radv-link.sh"
radv_link_recipe "$stage" "$sdk_root" "$radv_archive"
# (the recipe's layout, with a fixed gap for the process parameters)
radv_linker_script=(-T "$port/tools/ps5-pie-tnaimpact.ld")

echo "==> [link] TNA iMPACT! + RADV into a PS5 PIE ELF"
"$sdk_root/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$tri/tooling/native/app-symbols.map" --exclude-libs=ALL \
    -e _start -o "$stage/build/llvm-pie.elf" \
    "$tri/build/obj/app_crt.o" "$tri/build/obj/app_cpp_runtime.o" \
    --start-group "${inputs[@]}" --end-group \
    "$tri/build/stubs/libSceAgc.so" "$tri/build/stubs/libSceAgcDriver.so" \
    "${radv_link_inputs[@]}" \
    --as-needed "$sdk_root"/target/lib/*.so

echo "==> [package] eboot.bin for $title_id"
"$tool" link --in "$stage/build/llvm-pie.elf" --out "$stage/build/eboot.elf" \
    --stub-dir "$sdk_root/target/lib" --stub "$tri/build/stubs/libSceAgc.so" \
    --stub "$tri/build/stubs/libSceAgcDriver.so" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf
app=$root/dist/$title_id
rm -rf -- "$app"
mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$stage/build/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
cp "$tri/runtime/libc.prx" "$app/sce_module/libc.prx"
cat > "$app/sce_sys/param.json" <<EOF
{
  "ageLevel": { "default": 0 },
  "applicationCategoryType": 0,
  "applicationDrmType": "free",
  "attribute": 0,
  "attribute2": 0,
  "attribute3": 0,
  "conceptId": "${title_id:4}",
  "contentBadgeType": 1,
  "contentId": "UP9000-${title_id}_00-TNAIMPACTPS50000",
  "contentVersion": "01.000.000",
  "downloadDataSize": 256,
  "gameIntent": { "permittedIntents": [ { "intentType": "launchActivity" } ] },
  "localizedParameters": {
    "defaultLanguage": "en-US",
    "en-US": { "titleName": "TNA iMPACT! Reloaded" }
  },
  "masterVersion": "01.00",
  "pubtools": { "creationDate": "2026-11-06 00:00:00", "loudnessSnd0": "-28.00", "toolVersion": "2.00" },
  "requiredSystemSoftwareVersion": "0x0000000000000000",
  "sdkVersion": "0x0000000000000000",
  "titleId": "${title_id}",
  "versionFileUri": ""
}
EOF
# The game's files are never touched here: no default.xex, .pak or patched
# textures are copied into the app folder. The player puts their own disc
# image's files in /data/homebrew/PPSA94007/Assets (or the .iso in the app
# folder).
cat > "$app/HOW TO INSTALL.txt" <<TXT
TNA iMPACT! Reloaded for PS5 (PPSA94007)

No game files are included. You need your own Xbox 360 copy of the game as a disc image (.iso).

INSTALL
1. Copy this PPSA94007 folder into /data/homebrew/ on your PS5 (over FTP).
2. Start TNA iMPACT! Reloaded from your homebrew launcher. The hub looks for your game in
   /data/homebrew/PPSA94007/ (an .iso or an Assets folder with default.xex), then /data/ and your
   USB drives. If it isn't found, copy your .iso (or the extracted files with default.xex) to
   /data/homebrew/PPSA94007/, /data/, or a USB drive and press Triangle on the Home page.
   (The .iso is read in place; extracting isn't required.)

THE HUB
L1 / R1        the pages (Home, Settings, About)
D-pad          move; Left / Right changes a setting
Cross          Start Game (Home) / change a setting (Settings)
Triangle       search for your game again (Home)
Circle         back to Home
Options        start the game from any page
R3             the next menu song
Settings > Game files cycles between the disc images / game folders found, if there are several.
Your own music (MP3, WAV, FLAC or OGG) goes in /data/homebrew/PPSA94007/hub/music/.
The hub's settings (resolution, frame rate, anti-aliasing, anisotropic filtering,
FPS counter, menu music) are in /data/homebrew/PPSA94007/UserData/settings.toml.

PROBLEMS
Send the newest file from /data/homebrew/PPSA94007/UserData/logs/ (and UserData/crashes/ if
there is one). Support the port: https://buymeacoffee.com/kamb205
TXT
# The hub's own art and menu music (art/local/hub: logo.png, hero.jpg,
# buttons/, music/ - your own, never in the repo) -> <app>/hub
if [[ -d $port/art/local/hub ]]; then
    cp -R "$port/art/local/hub" "$app/hub"
fi
# BC7 DDS versions (ps5/art/make_ps5_art.py): the PS5 home screen shows the
# background on hover from pic0/pic1/pic2.dds, as the system's own apps have them.
for asset in icon0.dds pic0.dds pic1.dds pic2.dds; do
    if [[ -f $port/art/local/$asset ]]; then
        cp "$port/art/local/$asset" "$app/sce_sys/$asset"
    fi
done
# Home-screen art from your own disc (ps5/art/make_art.py writes art/local/):
# icon0.png 512x512, pic0.png 3840x2160, pic1.png 1920x1080.
for asset in icon0.png pic0.png pic1.png; do
    if [[ -f $port/art/local/$asset ]]; then
        cp "$port/art/local/$asset" "$app/sce_sys/$asset"
    elif [[ -f $port/art/$asset ]]; then
        cp "$port/art/$asset" "$app/sce_sys/$asset"
    fi
done
"$tool" self --inspect --file "$app/eboot.bin" | tail -3
echo "==> $app ($(stat -f %z "$app/eboot.bin") bytes)"
