# Pixel Squadron

A small top-down twin-stick arena shooter written in C with
[raylib](https://www.raylib.com/). Fly a fighter over an endless procedural
archipelago, sink the enemy waves, grab the supply crates - and keep an eye on
your three wingmen, who chat, bicker and (usually) have your back.

![screenshot](docs/screenshot.png)

## Play in the browser

<https://cflixdev-projects.github.io/pixel-squadron/>

## Features

- **Twin-stick combat** - move with WASD, aim with the mouse, fire with LMB.
  Xbox-style gamepads are fully supported: sticks to move and aim, triggers to
  fire and boost, bumpers to recall the wing.
- **Three AI wingmen** with distinct ships, names, personalities and special
  attacks. They escort you, call out threats, get perks from kills and
  respawn 15 seconds after going down.
- **A wing that learns** - every kill the squadron scores together makes the
  whole wing a little better: quicker on the stick, quicker on the trigger,
  more hull. On top of that they are formally rearmed a few percent every five
  waves, so they keep pace with a swarm that is getting nastier at the same
  time. Their progress is shown in the chat and on the hull bars.
- **Recall** - press `Q` and the whole wing breaks off, sprints back and fights
  on station around you until the danger has passed. They shoot the entire way
  home, so a recall is backup, not a hug.
- **Five biomes** across the endless map: verdant isles, dust reach, frost
  shelf, ash barrens and deep verge. Each has its own palette, waterline,
  wildlife and hazards, and they blend into one another at the borders. The
  HUD names the one you are flying over.
- **Seven enemy types**: scouts, raiders, heavy gunships, darting wasps,
  charged-up snipers, splitters that burst into wasps, and tenders that heal
  the whole swarm. New kinds join the fight as the waves climb.
- **Squadron chatter** - speech bubbles pop up over whoever is talking, with a
  translucent message log down the right-hand side. The wing has opinions, and
  most of them are not suitable for work.
- **An endless procedural world** - terrain and biomes are pure functions of
  the tile coordinate, so there is no map to reach the edge of and no two runs
  look alike. Fly in any direction for as long as you live.
- **Supply crates** dropped onto the islands at the top of every wave: better
  weapons, hull repairs and shields. They are marked on the screen edge when
  off screen, and a wingman will happily take one before you get there -
  especially with a hole in their hull or nothing left in the rack.
- **Critical hits** - one shot in five lands hard for 2.5x damage and flashes
  `CRIT` over the target.
- **Boost** - hold Shift to spend a rocket plume's worth of extra speed.
- **Wave-based survival** with a score, three weapon levels, rapid fire and
  shields.
- **Fills the screen** - the game renders into a pixel-perfect virtual screen
  whose zoom is always a whole number, then grows that screen until it covers
  the window. No letterbox bars, no cropping, no shimmer; a wider window simply
  shows more of the world.

## Controls

| Action | Keyboard / Mouse | Gamepad |
| --- | --- | --- |
| Start | `Enter` / `Space` | `A` / `Start` |
| Move | `W` `A` `S` `D` / arrow keys | Left stick |
| Aim | Mouse | Right stick |
| Fire | `LMB` / `Space` | `RT` / `RB` |
| Boost | `Shift` | `LT` / `Y` |
| Recall the wing | `Q` | `LB` / `D-pad down` |
| Pause | `P` / `Esc` | `Start` |
| Fullscreen | `F11` | - |
| Mute | `M` | - |
| Quit | `Esc` (from the title, pause or end screen) | - |

The mouse cursor is hidden in flight: a reticle sits exactly where the aim
point is, so what is inside the box is what you hit.

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
| `--endcard` | Dev aid: jump straight to the end-of-run report |
| `--titlecard` | Dev aid: screenshot the title card and quit |
| `--biome N` | Dev aid: pin the whole world to one biome (0-4) |
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
`.data` bundle into `docs/`, then commit and push `main`: GitHub Pages serves
that folder directly, so every push republishes the game.

## Credits

All art, sound and the UI font are free / open assets - see
[`assets/CREDITS.md`](assets/CREDITS.md) for the full list. raylib is zlib
licensed. The game's own code is MIT licensed (see [`LICENSE`](LICENSE)).
