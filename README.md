# TNA iMPACT! Reloaded — PS5

**TNA iMPACT!** (Xbox 360, Midway, 2008) statically recompiled and running natively on a
jailbroken **PlayStation 5**, with a hub made for the DualSense.

This is not an emulator: the Xbox 360 game is recompiled ahead of time with
[ReXGlue](https://github.com/rexglue/rexglue-sdk) and draws through Mesa RADV on the PS5's GPU.

> [!IMPORTANT]
> **No game files are included.** You need your **own Xbox 360 copy** of TNA iMPACT! as a disc
> image (`.iso`). The app finds it on your PS5 and reads it in place.

- Title ID: `PPSA94007`
- Version: see the release page
- Platform: jailbroken PS5, firmware 13.60

## Features

- **60 fps**, using the game's own 60 fps mode, at **720p** (the Xbox 360's picture) or **1440p**
  (drawn at 2x). 1080p output is also available.
- **FSR 1** or **CAS** upscaling to 4K, 16x anisotropic filtering, optional FXAA / 2x–8x MSAA.
- **DualSense**: vibration levels (Off / Light / Medium / Full) and a light bar colour (TNA gold
  by default).
- **The hub**: Home, Settings and About pages, menu music (R3 for the next song) and an FPS counter.
- **Bring your own disc image**: the hub searches `/data/homebrew`, `/data` and your USB drives
  for an `.iso` or an extracted folder with `default.xex`, and remembers your choice.

## Install

You need a jailbroken PS5 that runs homebrew apps from `/data/homebrew/`.

1. Download `PPSA94007.zip` from [Releases](../../releases/latest) and unzip it.
2. FTP the whole `PPSA94007` folder to `/data/homebrew/PPSA94007/`.
3. Put your own disc image (or extracted disc files) where the hub can find it:
   - copy `TNA iMPACT!.iso` into `/data/homebrew/PPSA94007/`, **or**
   - copy it to `/data/` or a USB drive, **or**
   - extract it (leave out `$SystemUpdate`) into `/data/homebrew/PPSA94007/Assets/`, so you have
     `Assets/default.xex`.
4. Start **TNA iMPACT! Reloaded** from your homebrew launcher.
5. If the game isn't found, press **Triangle** on the Home page to search again. With several
   games found, **Settings › Game files** cycles between them.

The `.iso` is read in place; you don't have to extract it.

## Controls (hub)

| Button | Action |
| --- | --- |
| L1 / R1 | previous / next page |
| D-pad | move; Left / Right changes a setting |
| Cross | Start Game (Home) / change a setting (Settings) |
| Triangle | search for your game again (Home) |
| Circle | back to Home |
| Options | start the game from any page |
| R3 | next menu song |

## Settings

All settings are saved as you change them in
`/data/homebrew/PPSA94007/UserData/settings.toml` and take effect when the game starts:

| Setting | Values |
| --- | --- |
| Resolution | 720p (original), 1080p, 1440p (recommended) |
| Frame Rate | 30 fps, 60 fps (original) |
| Anti-Aliasing | Off (original), FXAA, 2x, 4x, 8x MSAA |
| Upscaling | FSR 1 (recommended), CAS sharpen, bilinear |
| Anisotropic Filter | Off, 2x, 4x, 8x, 16x |
| FPS Counter | On, Off |
| Vibration | Off, Light, Medium, Full |
| Light Bar | TNA Gold, Red, Blue, White, System |
| Menu Music | Off … 100% |
| Game files | your disc image / game folder |

Your own menu music (MP3, WAV, FLAC or OGG) goes in
`/data/homebrew/PPSA94007/hub/music/`.

## Updating

Replace the `PPSA94007` folder with the new release's folder (`eboot.bin`, `sce_sys/`,
`sce_module/`, `hub/`). Keep your `UserData/` and your game files. After changing the home-screen
art, the PS5 may keep the old cached icon: also copy `icon0`/`pic0`/`pic1`/`pic2` (`.dds` and
`.png`) to `/user/appmeta/PPSA94007/`.

## Known issues

- Online / Xbox LIVE is not available.
- Some Xbox button prompts remain in the game's menus (PlayStation button art is partial).
- CAW/Story black character lighting can still occur (under investigation).

## Reporting problems

Open an issue with the newest log from `/data/homebrew/PPSA94007/UserData/logs/` and, if there is
one, the crash from `UserData/crashes/`. Say what you were doing and your firmware version.

## Credits

- **kamb205** — recompiled TNA iMPACT! and ported it to PS5.
  [GitHub](https://github.com/kamb205) · Discord `kamsb205` ·
  [Support the port](https://buymeacoffee.com/kamb205)
- **Midway Studios Los Angeles** — made TNA iMPACT! (2008).
- **@elripalda** — brought ReXGlue to PS5 and made the hub this port is based on
  (SvR2008-Recomp-PS5, GPL-3.0).
- **ReXGlue** and **Xenia** — the static recompiler and runtime.
- **PS5 Payload SDK**, **Mesa RADV**, **plume**.
- **UnleashedRecomp-Prospero** — the DualSense vibration-mode approach.
- **AMD FidelityFX FSR 1** (MIT).
- **Dear ImGui**, **stb**, **dr_mp3/dr_flac**, **Oswald/Inter fonts** (OFL).

TNA iMPACT! is Midway Studios Los Angeles' game; all names belong to their owners. This is an
unofficial fan project, not affiliated with or endorsed by TNA Entertainment, Midway, Microsoft
or Sony.

## License

GPL-3.0. The hub is based on elripalda's GPL-3.0 PS5 port work; see [LICENSE](LICENSE).
