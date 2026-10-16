# Credits & third-party assets

Pixel Squadron's own source code is released under the MIT license (see the
`LICENSE` file in the repository root). Everything it loads at runtime comes
from the free packs below. No attribution is legally required for the CC0
assets, but giving credit is the decent thing to do.

## Art

| Asset | File | Author | License |
| --- | --- | --- | --- |
| Spaceship sprites | `assets/sprites/ships.png` | [Kenney](https://kenney.nl/) — *Pixel Shmup* | CC0 1.0 |
| Terrain tiles | `assets/sprites/tiles.png` | [Kenney](https://kenney.nl/) — *Pixel Shmup* | CC0 1.0 |

## Audio

All sound effects come from the **Kenney Sci-Fi Sounds** pack.

| File | Original |
| --- | --- |
| `shoot.ogg` | `laserSmall_000.ogg` |
| `shoot_heavy.ogg` | `laserLarge_002.ogg` |
| `enemy_shoot.ogg` | `laserRetro_003.ogg` |
| `explosion.ogg` | `explosionCrunch_001.ogg` |
| `explosion_big.ogg` | `lowFrequency_explosion_000.ogg` |
| `hit.ogg` | `impactMetal_002.ogg` |
| `pickup.ogg` | `forceField_000.ogg` |
| `shield.ogg` | `forceField_003.ogg` |
| `engine.ogg` | `spaceEngineSmall_000.ogg` |

[Kenney Sci-Fi Sounds](https://kenney.nl/assets/sci-fi-sounds) — CC0 1.0.

## Font

| Asset | Author | License |
| --- | --- | --- |
| `assets/fonts/departure.otf` | [Departure Mono](https://github.com/rektdeckard/departure-mono) by Tobias Fried | SIL Open Font License 1.1 |

## Engine

| Library | License |
| --- | --- |
| [raylib](https://www.raylib.com/) 5.5 | zlib |

## Re-fetching the raw packs

`tools/fetch_assets.ps1` downloads the original CC0 packs and copies the exact
files the game uses into `assets/`. The raw downloads are cached in
`assets/_download/` and are **not** committed to the repository.
