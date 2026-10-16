# Pixel Squadron

A small top-down twin-stick arena shooter written in C with
[raylib](https://www.raylib.com/). Fly a fighter over procedurally generated
islands, sink the enemy waves, pick up perks - and keep an eye on your three
wingmen, who chat, bicker and (usually) have your back.

![screenshot](docs/screenshot.png)

## Features

- **Twin-stick combat** - move with WASD, aim with the mouse, fire with LMB.
  Gamepads (Xbox-style) are supported too.
- **Three AI wingmen** with distinct ships, names, personalities and special
  attacks. They escort you, call out threats, get perks from kills and
  respawn 15 seconds after going down.
- **Squadron chatter** - speech bubbles pop up over whoever is talking, with a
  translucent message log down the right-hand side.
- **Procedural island arenas** built from a 16x16 tile atlas.
- **Boost** - hold Shift to spend a rocket plume's worth of extra speed.
- **Wave-based survival** with a score, pickups, three weapons levels, rapid
  fire and shields.
- **Pixel-perfect rendering** at a 480x270 internal resolution, scaled to the
  window with nearest-neighbour filtering (no shimmer, no letterboxing).

## Controls

| Action | Keyboard / Mouse | Gamepad |
| --- | --- | --- |
| Move | `W` `A` `S` `D` / arrow keys | Left stick |
| Aim | Mouse | Right stick |
| Fire | `LMB` / `Space` | `RT` |
| Boost | `Shift` | - |
| Pause | `P` | - |
| Fullscreen | `F11` | - |
| Mute | `M` | - |
| Quit to title | `Esc` | - |

## Building (desktop)

Requires CMake 3.16+ and a C11 compiler. raylib 5.5 is fetched and built
automatically, or an installed copy is used if one is found.

```sh
cmake -S . -B build
cmake --build build -j
./build/pixel_squadron
```

Run from the repository root (or from `build/`, where a copy of `assets/` is
placed next to the binary) so the asset lookup can find what it needs.

### Command-line flags

| Flag | Effect |
| --- | --- |
| `--windowed` | Start in a window instead of fullscreen |
| `--fullscreen` | Force fullscreen |
| `--demo N` | Autopilot for `N` seconds, then exit (used for screenshots) |
| `--shot FILE` | Save a PNG screenshot when a `--demo` run finishes |
| `--title` | With `--demo`, capture the title screen instead |
| `--seed N` | Use a fixed world seed |

## Building for the web

The game also builds to WebAssembly with the
[Emscripten SDK](https://emscripten.org/):

```sh
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh          # or emsdk_env.bat on Windows

cd ..
emcmake cmake -S . -B build-web -DPLATFORM=Web
cmake --build build-web -j
```

The output lands in `build-web/`. Copy `index.html`, `*.js`, `*.wasm` and the
`.data` bundle into `docs/` to publish it - GitHub Pages can serve that folder
directly (see `.github/` for the workflow).

## Credits

All art, sound and the UI font are free / open assets - see
[`assets/CREDITS.md`](assets/CREDITS.md) for the full list. raylib is zlib
licensed. The game's own code is MIT licensed (see [`LICENSE`](LICENSE)).
