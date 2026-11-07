# TNA iMPACT! Reloaded — PlayStation 5

**TNA iMPACT!** (Xbox 360, 2008) recompiled and running natively on a jailbroken PS5.

- **Title ID:** `PPSA94007`
- **Platform:** jailbroken PS5, firmware 13.60
- **Download:** `PPSA94007.zip` (GitHub Releases)
- **License:** GPL-3.0

## Description

TNA iMPACT! Reloaded is a native PS5 port of Midway's 2008 Xbox 360 wrestling game. The game is
statically recompiled with ReXGlue — it isn't emulated — and draws through Mesa RADV on the PS5's
GPU. It opens on its own hub, where you pick your game files and tune the picture with the
controller, before starting the game.

**No game files are included.** You need your own Xbox 360 copy of TNA iMPACT! as an `.iso` (or
the extracted files with `default.xex`). The hub finds it on your PS5, reads the `.iso` in place,
and remembers your choice.

## Features

- 60 fps at 720p or 1440p (the game's own 60 fps mode); 1080p output also available.
- FSR 1 or CAS upscaling to 4K, 16x anisotropic filtering, optional FXAA and 2x–8x MSAA.
- DualSense vibration levels (Off / Light / Medium / Full) and a light bar colour.
- A settings hub (Home / Settings / About) with menu music, an FPS counter and your own music.
- Game-dump picker: searches the app folder, `/data` and USB drives; Triangle rescans; several
  games can be cycled in Settings.
- PlayStation controller in the hub; game menus stay as they were.

## Requirements

- A jailbroken PS5 on firmware 13.60 with a homebrew launcher for `/data/homebrew/`.
- Your own Xbox 360 copy of **TNA iMPACT!** as an `.iso` or extracted files with `default.xex`.
- ~2 GB free for the app (the game itself is read from your own dump).

## Install

1. Download and unzip `PPSA94007.zip`.
2. FTP the `PPSA94007` folder to `/data/homebrew/`.
3. Copy your `TNA iMPACT!.iso` (or extracted disc files) to `/data/homebrew/PPSA94007/`,
   `/data/`, or a USB drive.
4. Start **TNA iMPACT! Reloaded** from your homebrew launcher. Press **Triangle** on the Home
   page if the hub hasn't found your game yet.

## Credits

- **kamb205** — recompiled TNA iMPACT! and ported it to PS5.
  [GitHub](https://github.com/kamb205) · Discord `kamsb205`
- **Midway Studios Los Angeles** — made TNA iMPACT! (2008).
- **@elripalda** — the ReXGlue PS5 runtime and the hub base (GPL-3.0).
- **ReXGlue**, **Xenia**, **PS5 Payload SDK**, **Mesa RADV**, **plume**.
- **UnleashedRecomp-Prospero** — DualSense vibration-mode approach.
- **AMD FidelityFX FSR 1** (MIT), **Dear ImGui**, **stb**, **dr_mp3/dr_flac**,
  **Oswald/Inter fonts** (OFL).

TNA iMPACT! and all related names belong to their owners. This is an unofficial fan project, not
affiliated with or endorsed by TNA Entertainment, Midway, Microsoft or Sony.

## Support

If you enjoy the port: **https://buymeacoffee.com/kamb205**

## Known issues

- Online / Xbox LIVE is not available.
- Xbox button prompts remain in places (PlayStation button art is partial).
- CAW/Story black character lighting may still occur (open investigation).

## License

GPL-3.0 — because the hub is based on GPL-3.0 PS5 port code. See [LICENSE](../LICENSE).
