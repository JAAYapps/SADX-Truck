# SADX Truck

Drive the trucks from **Cabela's 4x4 Off-Road Adventure 3** in **Sonic Adventure DX** (PC).
The mod runs Cabela's own truck physics engine (`EngineDll.dll`) inside SADX: all 14 trucks,
with their engine sounds, gears, fuel, part damage, dents and snorkels.
It also brings Cabela's 16 maps into SADX, with their rocks, trees, grass, buildings, bridges,
ramps, water, sky and fog; Sonic and the trucks can go to any of them from Station Square.

This repository holds only the mod's own code. No Cabela's files are in it.
You bring your own copy of Cabela's 3, and `setup.exe` builds the truck and map data from it on your PC.

## What you need

| Thing | Where it comes from |
|---|---|
| Sonic Adventure DX (PC) with [SA Mod Manager](https://github.com/X-Hax/SA-Mod-Manager) | your own copy |
| Cabela's 4x4 Off-Road Adventure 3, installed | your own copy (2003, Activision Value) |
| The Cabela's 3 **Enhancement Pack** zip, `cora3_enhancement_pack.zip` | a free patch; the official download is gone, a copy is kept at [archive.org](https://archive.org/download/CabelasORA3/cora3_enhancement_pack.zip) |
| Cabela's 4x4 Off-Road Adventure (the first one, 2001), installed | optional: only for the engine-cranking sounds; without it the cranking is silent |

The Enhancement Pack matters: the mod only works with its `EngineDll.dll`
(4,374,528 bytes). The one from the 2003 disc crashes. You do **not** need to run the
pack's installer (it is a 16-bit program and will not start on 64-bit Windows);
`setup.exe` reads the zip directly.

## Install

1. Download `SADX-Truck.zip` from the [Releases page](https://github.com/JAAYapps/SADX-Truck/releases)
   and unzip it into SADX's `mods` folder, so you have `mods\SADX Truck\` with
   `sadxtruck.dll`, `mod.ini`, `configschema.xml`, `setup.exe` and `sounds\crowd_panic.wav` in it.
2. Put `cora3_enhancement_pack.zip` in that same folder (or anywhere; setup will ask for it).
3. Double-click `setup.exe`. It looks for Cabela's 3 and Cabela's 1 in Program Files and asks
   for the folder when it cannot find one. When it is done it says so, and writes what it did
   to `setup_log.txt`.
4. Turn on **SADX Truck** in SA Mod Manager and play.

What setup writes into the mod folder:

```
SADX Truck\
  EngineDll.dll, msvcp60.dll    Cabela's physics engine, from the Enhancement Pack
  trucks.bin                    the 14 trucks: models, sounds list, tuning
  sounds\                       88 sounds
  textures\SADXTRUCK\           90 textures, including the truck picker pictures
  maps\                         the 16 Cabela's maps: ground, objects, collision, grass, sky
  textures\SXMAP_*, SXOBJ_*,     the maps' pictures (ground, objects, sky),
    SXSKY_*, SXWATER, SXMAPMENU   the water and the map menu
  setup_log.txt                 what setup found and wrote
```

Setup also runs from a command prompt, with no windows:

```
setup.exe "<Cabela's 3 folder>" "<Enhancement Pack zip>" ["<Cabela's 1 folder>"]
```

## Controls

| Where | Button | Does |
|---|---|---|
| on foot | Y | get in (the parked truck if Sonic is within about 13 feet of it, otherwise a new truck) |
| on foot | D-pad left / right | pick the previous / next truck (its picture shows for 2 seconds) |
| on foot | D-pad down | snorkel for the picked truck: none, Watermaster, Watermaster Ultra |
| on foot | D-pad up, hold 2 seconds | repair the picked truck and fill its tank |
| on foot in Station Square | L + R, hold 2 seconds | open the map menu |
| map menu | D-pad up / down | pick a map (its overview picture shows) |
| map menu | A / B | go to the picked map / close the menu |
| on foot on a map | L + R, hold 2 seconds | back to Station Square, where Sonic left from |
| driving | X | crank the engine (in Ice Cap, hold it) |
| driving | R or A or right trigger | gas |
| driving | L or B or left trigger | brake; held while stopped, it shifts into reverse |
| driving | left stick | steer |
| driving | D-pad up | headlights on / off |
| driving | D-pad down | traction: 2x4, 4x4, 4x4 Low |
| driving | D-pad left / right | radio: OFF, Crush 40 Rock, Station Square FM, Action Stage, Chao Chill, Your Music |
| driving | Y | get out |

## Radio

While driving, D-pad left / right changes the station; the station's name and the song show at the
top for a few seconds. The stations play SADX's own soundtrack from your game folder
(`system\sounddata\bgm\wma\`); Your Music plays any files you put in `mods\SADX Truck\radio\`
(MP3, OGG, WAV or WMA). Stations keep going while you are away, so you tune back in mid-song.
While the radio plays, the level's music waits. On foot, the radio is heard from the truck: full
within 15 feet, fading out by 100 feet, where the level's music comes back. Turning the radio OFF
brings the level's music back at once. The radio needs SA Mod Manager's "Enable BASS music" (on by
default).

## Options

In SA Mod Manager, select SADX Truck and click **Configure**:

| Option | Default | Does |
|---|---|---|
| Keep dents per truck | on | each truck keeps its dents when you switch trucks |
| Dent strength (x) | 1 | 1 = Cabela's own dent depth, up to 10 |
| Save damage between games | on | damage, dents, fuel and odometer saved to `damage_Car1.bin` to `damage_Car14.bin` |
| Landing damage (x) | 1 | 1 = Cabela's own landing damage, up to 50 |
| Use fuel | on | off: the tank never runs dry and the fuel gauge is hidden |
| Truck volume | 100 | 0 to 100, also scaled by the game's sound effect volume |

## Building

Everything is built with LLVM (`clang` and `lld-link`) for 32-bit Windows, on Linux or Windows.
No Visual Studio and no C runtime are needed; the `.lib` files in the repository are small
import libraries made from the `.def` files next to them
(`llvm-dlltool -m i386 -d msvcrt.def -l msvcrt.lib`; add `-k` for `kernel32.def`).

The mod:

```
clang --target=i686-pc-windows-msvc -O1 -fno-stack-protector -mno-stack-arg-probe -c sadxtruck/sadxtruck.c -o sadxtruck/sadxtruck.obj
lld-link /dll /noimplib /nodefaultlib /entry:DllMainCRTStartup sadxtruck/sadxtruck.obj truckprobe/kernel32.lib /out:sadxtruck.dll
```

The setup program:

```
clang --target=i686-pc-windows-msvc -O1 -msse2 -mfpmath=sse -fno-math-errno -fno-builtin -fno-stack-protector -mno-stack-arg-probe -c setup/setup.c -o setup/setup.obj
lld-link /nodefaultlib /entry:start /subsystem:windows setup/setup.obj setup/kernel32.lib setup/msvcrt.lib /out:setup.exe
```

The test host (runs the mod's driving code without SADX, in a console; it needs
`EngineDll.dll`, `msvcp60.dll` and `trucks.bin` next to it, from a setup run):

```
clang --target=i686-pc-windows-msvc -O1 -fno-stack-protector -mno-stack-arg-probe -c sadxtruck/testhost.c -o sadxtruck/testhost.obj
lld-link /nodefaultlib /entry:mainCRTStartup /subsystem:console sadxtruck/testhost.obj truckprobe/kernel32.lib sadxtruck/msvcrt.lib /out:truckprobe/run/testhost.exe
```

## Other files

| File | What it is |
|---|---|
| `skin_to_c.py` | the Python version of what `setup.exe` does for `trucks.bin`; the two give identical files |
| `arc_list.py`, `arc_extract.py` | list and unpack Cabela's 3 `Cora3.arc` |
| `res_list.py` | list Cabela's 1 `data1.res` |
| `model_bones.py` | print a truck model's parts and bones |
| `heightmap_to_obj.py`, `colormap_to_png.py` | turn a Cabela's map into a 3D model and a picture |
| `map_to_bin.py` | the Python version of what `setup.exe` does for the maps (`setup/maps.c`); the two give identical files |
| `map_to_obj.py` | one Cabela's map's ground as an OBJ model and picture, for looking at in Blender; `map_to_bin.py` uses its readers |
| `truckprobe/truckprobe.c` | the first experiment: loads Cabela's engine on its own and drives one truck |

## Credits

| Who | What |
|---|---|
| Joshua Vanderzee | the mod's idea and design choices, testing |
| Claude, by Anthropic (https://claude.ai) | wrote most of the code (the mod, `setup.exe` and the Python tools), with me directing it and testing every step |
| FUNLabs and Activision Value | Cabela's 4x4 Off-Road Adventure 3, whose trucks, physics engine, sounds and maps the mod uses from your own copy (none are included here) |
| Mark Adler | `puff`, the base of `setup.exe`'s unzip code (see `THIRD_PARTY_NOTICES.md`) |
| Pillow's authors | the picture-resize code in `setup/textures.c` (see `THIRD_PARTY_NOTICES.md`) |
| X-Hax (SA Mod Manager and the SADX Mod Loader) | the mod loader, and the SDK headers the mod's SADX addresses come from |
| unshield | how InstallShield cabinets are laid out: `setup.exe` reads the Enhancement Pack's cabinet the way unshield 1.6.2 does |
| freesound_community, on Pixabay | "crowdpanic", the crowd sound the Past's echidnas make when they see the truck (`sadxtruck/sounds/crowd_panic.wav`, a 12-second loop cut from it; Pixabay Content License) |

## License

The mod's code is under the MIT License (see `LICENSE`), except the two adapted parts listed in
`THIRD_PARTY_NOTICES.md`, which keep their own licenses. That covers only this repository's
own code; Cabela's files that `setup.exe` reads or writes on your PC stay under their owners' terms.

`sadxtruck/sounds/crowd_panic.wav` is not under the MIT License: it is under the Pixabay Content
License (https://pixabay.com/service/license-summary/).

Cabela's 4x4 Off-Road Adventure is a trademark of its owners. Sonic Adventure DX is a trademark of SEGA.
This is a fan mod and is not affiliated with either.
