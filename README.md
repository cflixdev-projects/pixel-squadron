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
  Xbox-style gamepads are fully supported: sticks to move and aim, the right
  trigger to fire, `B` to boost, `X` to recall the wing and `Y` for your own
  ultimate. Nothing else on the pad shoots, and every one of those buttons can
  be rebound from the pause menu - the choice is remembered between sessions.
  The aim reticle is drawn in both modes, so you can always see where the guns
  are pointing.
- **Three AI wingmen** with distinct ships, names, personalities and special
  attacks. They escort you, call out threats, get perks from kills and
  respawn 15 seconds after going down.
- **A wing that learns** - every kill the squadron scores together makes the
  whole wing a little better: quicker on the stick, quicker on the trigger,
  more hull. On top of that they are formally rearmed **every two waves**, with
  a banner across the screen so you can see it happen. They also hunt down
  supply crates themselves and will fly a weapon crate's worth of distance to
  get one - then use it.
- **Recall** - press `Q` and the whole wing breaks off, sprints back and fights
  on station around you until the danger has passed. They shoot the entire way
  home, so a recall is backup, not a hug.
- **Your own ultimate** - press `E` for **OVERSURGE**: a radial blast that
  shoves everything nearby, shreds it, wipes enemy fire out of the air and
  leaves you untouchable for a beat. The HUD meter shows when it is back.
- **Five biomes** across the endless map: verdant isles, dust reach, frost
  shelf, ash barrens and deep verge. Each has its own palette, waterline,
  wildlife and hazards, and they blend into one another at the borders. The
  HUD names the one you are flying over. Trees and crags are repainted for
  whichever biome they are standing in, so nothing arrives wearing a lawn.
- **Seven enemy types**: scouts, raiders, heavy gunships, darting wasps,
  charged-up snipers, splitters that burst into wasps, and tenders that heal
  the whole swarm. New kinds join the fight as the waves climb.
- **Squadron chatter** - speech bubbles pop up over whoever is talking, with a
  translucent message log down the right-hand side. The wing has opinions, and
  most of them are not suitable for work.
- **An endless procedural world** - terrain and biomes are pure functions of
  the tile coordinate, so there is no map to reach the edge of and no two runs
  look alike. Fly in any direction for as long as you live.
- **Supply crates** dropped onto the islands at the top of every wave: hull
  repairs, shields, rapid fire, and three weapon crates - **quad cannons**, a
  **spread gun** and **seeker rounds** - each of which replaces your gun for a
  few seconds and is drawn as its own icon. They are marked on the screen edge
  when off screen, and a wingman will happily take one before you get there -
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
| Fire | `LMB` / `Space` | `RT` |
| Boost | `Shift` | `B` |
| Recall the wing | `Q` | `X` |
| Ultimate (OVERSURGE) | `E` | `Y` |
| Rebind the pad | `C` (from the pause menu) | - |
| Pause | `P` / `Esc` | `Start` |
| Fullscreen | `F11` | - |
| Mute | `M` | - |
| Quit | `Esc` (from the title, pause or end screen) | - |

In flight the OS cursor is replaced by the game's own reticle, which sits
exactly where the aim point is: what is inside the box is what you hit. On a
pad the reticle rides out along the aim vector instead, so it is just as
obvious where the guns are pointed. Pushing the right stick aims and opens
fire; moving the mouse takes the aim straight back, so both hands work and
neither steals the crosshair from the other.

The pause menu's **pad controls** panel (`C`) rebinds fire, boost, recall and
the ultimate to any pad button; `R` there restores the defaults. The keyboard
is not remappable - there are enough keys that the defaults are never in the
way.

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
| `--wingup N` | Dev aid: hold the wing's rearm banner on screen |
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
