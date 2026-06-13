# sspm-player

A playable, open-source reader for **Rhythia** maps (`.sspm` format), written in **C99 + raylib**.

Rhythia (formerly Sound Space) is a rhythm game by NaN Studios where notes fly toward the player and must be hit with the cursor. This project is an independent reimplementation — it reads the same community map format and reproduces the core gameplay, with a focus on **running well on low-end hardware**: no dynamic lights, no shadows, no post-processing, O(visible notes) rendering per frame.

Runs on **Windows** and **Linux (Debian/Ubuntu)**.

---

## Features

- Full `.sspm` v2 support — loads any community Rhythia map
- Subdirectory scanning — organize your `maps/` folder however you like
- Playable or **autoplay** (visualizer) mode
- Drag-and-drop a `.sspm` directly onto the window to play it
- Configurable note speed, approach distance, palette, note shape, hitsound
- Custom cursor skins (PNG) with a configurable trail
- Custom note meshes (`.obj`, `.glb`, `.gltf`, `.iqm`, `.vox`, `.m3d`) with auto-scaling and palette tinting
- Tablet / stylus support (absolute pointing mode)
- **Aim Trainer** mode — procedural sessions with configurable patterns and difficulty
- Mods: Hard Rock, Hidden, No Fail, Sudden Death, speed rate
- Audio calibration tool (offset correction)
- Favorites, personal bests, and map filtering
- Low-res internal render target option for very weak GPUs (854×480 or 1280×720 upscaled)
- All settings persisted to `settings.cfg`

---

## Requirements

### Windows

No installation required. The release `.zip` is self-contained — just extract and run `sspm-player.exe`.

### Linux (Debian / Ubuntu)

Install the build dependencies:

```bash
sudo apt update
sudo apt install build-essential pkg-config libraylib-dev
```

> If `libraylib-dev` is not available on your distribution, build raylib from source (one-time):
>
> ```bash
> sudo apt install git cmake libasound2-dev libx11-dev libxrandr-dev \
>      libxi-dev libgl1-mesa-dev libglu1-mesa-dev libxcursor-dev libxinerama-dev
> git clone --depth 1 https://github.com/raysan5/raylib
> cd raylib/src
> make PLATFORM=PLATFORM_DESKTOP
> sudo make install
> sudo ldconfig
> ```

---

## Installation

### Windows — pre-built binary

1. Download `sspm-player-windows.zip` from the [Releases](../../releases) page.
2. Extract anywhere.
3. Drop your `.sspm` files into the `maps/` folder.
4. Run `sspm-player.exe`.

### Linux — build from source

```bash
git clone https://github.com/lolopada/sspm-player
cd sspm-player
make
./sspm-player
```

Drop your `.sspm` files into `maps/` next to the binary.

---

## Usage

```
./sspm-player [path] [WxH] [--fullscreen] [--autoplay] [--info]
```

| Argument | Description |
|---|---|
| `path` | A `.sspm` file (plays it directly) or a folder (opens the menu on that folder). Defaults to `maps/`, then `.` |
| `WxH` | Window resolution, e.g. `1280x720`. Defaults to `960x540` |
| `--fullscreen` | Start in fullscreen (toggle with F11 at any time) |
| `--autoplay` | Visualizer mode — notes are hit automatically |
| `--info` | Print map metadata to stdout and exit (no window) |

Arguments can be given in any order.

---

## Controls

### Menu

| Key / Action | Effect |
|---|---|
| ↑ / ↓ or scroll | Navigate the map list |
| Enter or click | Play selected map |
| S | Open Options |
| F5 | Rescan the maps folder |
| Drag & drop | Play a `.sspm` file directly |
| Esc | Quit |

### In-game (after the 3-2-1 countdown)

| Key / Action | Effect |
|---|---|
| Mouse / stylus | Aim — a note is hit when the cursor is over it as it crosses the hit plane |
| Space | Pause / resume |
| R | Restart (replays the countdown) |
| F11 | Toggle fullscreen |
| Esc | Return to menu |

### Options screen (S)

↑ / ↓ to select a setting, ← / → to change its value. Changes are saved automatically to `settings.cfg` on exit.

---

## Options

| Setting | Description |
|---|---|
| Approach distance | How far away notes spawn (20–120) |
| Approach time | Time for a note to reach the hit plane, in ms (200–1500). Lower = faster |
| Mouse sensitivity | Cursor speed in relative (mouse) mode |
| Tablet mode | Absolute pointing for drawing tablets and styluses |
| Audio offset | Shift note timing to compensate for audio latency (use the calibration tool) |
| Palette | Color scheme for notes. Includes presets and a fully custom mode |
| Note shape | Cube (default) or any 3D model from `meshes/` |
| Hitsound | Sound played on a hit — any file from `hitsounds/` |
| Cursor skin | Custom PNG cursor from `cursors/`, with trail options |
| Cursor in menus | Use the custom cursor skin in menus as well (hides the system cursor) |
| Resolution | Internal render resolution (for low-end GPUs) |
| Juice | Visual feedback intensity on hit (particles, pulse, neon) |

---

## Adding content

All content lives in folders next to the binary. Subfolders are supported.

### Maps — `maps/`

Place `.sspm` files here. Any folder structure works:

```
maps/
  pack-name/
    map1.sspm
    map2.sspm
  artist/
    hard/
      map3.sspm
```

Up to **8 192 maps** are loaded. Press **F5** in the menu to rescan after adding files.

### Note meshes — `meshes/`

Place 3D models here to use as note shapes instead of the default cube.
Accepted formats: `.obj`, `.glb`, `.gltf`, `.iqm`, `.vox`, `.m3d`

The model is auto-scaled to note size and tinted by the active palette.
**For `.obj` files: the mesh must be fully triangulated** (no n-gons). In Blender, enable "Triangulated Mesh" at export, or use `.glb` which is always triangulated.

Up to 64 meshes are loaded.

### Cursor skins — `cursors/`

Place `.png` files here. The cursor is scaled automatically. Configure trail color, length, and opacity in Options.

### Hitsounds — `hitsounds/`

Place audio files here (`.wav`, `.ogg`, `.mp3`, `.flac`). The selected sound plays on every hit.

### Color palettes — `colors/`

Place `.txt` files here to add custom note color palettes. Each file defines a named palette:

```
name=My Palette
colors=ff0000,00ff00,0000ff
```

- `name` — displayed in the Options palette picker
- `colors` — comma-separated hex RGB values cycled across notes

Several palettes are included by default (`aurora.txt`, `cyberpunk.txt`, `sunset.txt`, …).

---

## License

[GPL v3](LICENSE) — not affiliated with NaN Studios or the Rhythia project.
