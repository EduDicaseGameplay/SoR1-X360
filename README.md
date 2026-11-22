# SoR1-X360

*English | [Português](LEIAME.md)*

Streets of Rage (1991) running **natively** on a modded Xbox 360: 60 fps, 1280x720 with the wider 16:9 view, picture filters, button remapping, save states and the settings menu.

It isn't an emulator running the game one instruction at a time. It starts from [Streets-Of-Rage-Recomp](https://github.com/OriginalSwayze/Streets-Of-Rage-Recomp) by OriginalSwayze, which translates the game's own 68000 code to C, and I ported it to the Xbox 360: video on the console's GPU (D3D9), XInput, XAudio2, its files and its menus. The translation is made on your PC from **your** ROM and compiled into the `.xex`. The sound chips and the video chip are still emulated around it.

> **There is no game in here.** No ROM, no translated code, no prebuilt `.xex`, and no Microsoft SDK. You need your own ROM and the tools below to build it. Please don't ask me for ROMs.

---

## What works

- 60 fps, one game frame per TV refresh
- 1280x720, 16:9 with the wider play area (or the original 4:3)
- Filters: scanlines, CRT, LCD (on the GPU)
- Button remapping, rumble, two controllers
- Save states (9 slots) and settings saved to `settings.ini`
- Sound on its own core

Played on a real console through the first stages, with save states, at 60 fps.

## What you need

### On the console
- A modded Xbox 360 (RGH/JTAG) with Aurora, FreeStyle Dash or XeXMenu

### On the PC (Windows 10/11)
1. **WSL** with Ubuntu. The Xbox build runs there.
2. **[OXDK](https://github.com/MrMilenko/OXDK)** in WSL, with its LLVM built with the Xenon patches from `OXDK/xbox360/llvm`, plus the one in `port/tools/patches/llvm-lld-thunk-r0.patch` (a long-branch thunk for lld that keeps r12 free).
3. **Xbox 360 SDK (XDK)** files where OXDK looks for them (`~/xdk360/XDK`).
4. **Visual Studio 2022 Build Tools** with C++ and its clang, for the PC build of the base.
5. **Node.js**, for the base's translator.
6. **Git**.

### The ROM
**Streets of Rage (World)**, SHA-1 `3d74dbc81f3472a5bde45bf265e636a72a314667`.

It has to be the first release. The **Rev A** dump (serial ending in -01) does not work: the code is translated by address, and Rev A has it in other places.

## Building

**1. Download this project and the base**, in a folder without spaces in the path:

```bash
git clone https://github.com/EduDicaseGameplay/SoR1-X360.git
cd SoR1-X360
git clone --recursive https://github.com/OriginalSwayze/Streets-Of-Rage-Recomp.git referencias/Streets-Of-Rage-Recomp
git -C referencias/Streets-Of-Rage-Recomp checkout 006ff0f
```

The port was made on that commit of the base.

**2. Translate the game.** Put your ROM in `rom\` and create `referencias\Streets-Of-Rage-Recomp\local.json` with its path:

```json
{ "roms": { "sor1": "C:\\path\\to\\SoR1-X360\\rom\\your_rom.md" } }
```

Then, in that folder:

```
set GAME=sor1
node tools\recomp.mjs
```

**3. Build the base for PC** once. The Xbox build uses the config, the Musashi tables and the fonts it makes:

```
port\tools\pcbuild.bat
```

**4. Build the `.xex`** in WSL:

```bash
bash port/tools/build.sh
```

`make` doesn't take spaces in paths. If the project is under `C:\Users\...\Desktop\My folder`, make a link first: `ln -s "/mnt/c/.../SoR1-X360" ~/sor1` and run `bash ~/sor1/port/tools/build.sh`.

The result is `port/build/xex/default.xex`.

## On the console

Make a folder (for example `Hdd1:\Games\SoR1X360`) with:

```
default.xex
sor1.bin       your ROM (.bin, .md, .gen or .smd)
```

Controls: D-pad or left stick to move, A attack, B jump, X: special (the police car), Start pause. **Back** opens the menu (LB/RB change tabs): Controls, save states, Widescreen, Filter, sound. Back + Start held for 3 seconds quits.

If something goes wrong, `sor1_log.txt` next to the `.xex` says where it stopped.

## Credits

- [Streets-Of-Rage-Recomp](https://github.com/OriginalSwayze/Streets-Of-Rage-Recomp) by OriginalSwayze (MIT): the translator and the runtime this port changes
- [OXDK](https://github.com/MrMilenko/OXDK) by MrMilenko (public domain): the toolchain
- Musashi, z80 by superzazu and ymfm by Aaron Giles, through the base

See `THIRD_PARTY.txt` and `LICENSE`. Streets of Rage belongs to SEGA.
