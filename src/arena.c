/*****************************************************************************
 *  arena.c - the procedural archipelago the squadron flies over.
 *
 *  The world is a grid of 16x16 tiles covering 8192x6144 pixels. A fractal
 *  value-noise height field is quantised into a handful of bands: water is
 *  painted straight from the palette, land is painted with Kenney's
 *  grass/sand tiles, and trees and rocks are scattered on top as
 *  bullet-blocking cover.
 *
 *  Three noise layers are mixed to get a readable world at this size:
 *      cont   low frequency "continents", decides where land is at all
 *      ridge  mid frequency coastlines and capes
 *      micro  high frequency beaches, lagoons and speckle
 *  There is no radial falloff any more (that only makes sense for a single
 *  small island) - instead the outer rim of the map is sunk into deep water
 *  so the edge of the world reads as open ocean.
 *****************************************************************************/
#include "game.h"

#include <math.h>

static uint8_t s_terrain[WORLD_TILES_Y][WORLD_TILES_X];
static uint8_t s_decor[WORLD_TILES_Y][WORLD_TILES_X];
static float   s_height[WORLD_TILES_Y][WORLD_TILES_X];

/* decor kinds */
enum {
    DECOR_NONE,
    DECOR_TREE_A,   /* single pine   */
    DECOR_TREE_B,   /* pine pair     */
    DECOR_TREE_C,   /* lone pine     */
    DECOR_ROCK_A,   /* red crag      */
    DECOR_ROCK_B,   /* red peak      */
};

/* Height bands. Land starts at LAND_LEVEL; everything below it is water. */
#define LAND_LEVEL 0.435f
#define SAND_LEVEL 0.472f

/* --------------------------------------------------------------------------
 *  Noise
 * ----------------------------------------------------------------------- */
static unsigned Hash2(int x, int y, unsigned seed)
{
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float Noise01(int x, int y, unsigned seed)
{
    return (float)(Hash2(x, y, seed) & 0xFFFFFFu) / (float)0xFFFFFF;
}

/* Classic smoothstep-interpolated value noise. */
static float ValueNoise(float x, float y, unsigned seed)
{
    int xi = (int)floorf(x);
    int yi = (int)floorf(y);
    float fx = x - (float)xi;
    float fy = y - (float)yi;
    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);

    float a = Noise01(xi,     yi,     seed);
    float b = Noise01(xi + 1, yi,     seed);
    float c = Noise01(xi,     yi + 1, seed);
    float d = Noise01(xi + 1, yi + 1, seed);

    return Lerp(Lerp(a, b, ux), Lerp(c, d, ux), uy);
}

/* Fractal value noise. The octave count is a parameter so the cheap detail
   layers can stay cheap - this loop runs a few hundred thousand times when a
   new world is generated. */
static float Fbm(float x, float y, unsigned seed, int octaves)
{
    float sum = 0.0f, amp = 0.5f, freq = 1.0f, norm = 0.0f;
    for (int octave = 0; octave < octaves; octave++) {
        sum  += ValueNoise(x * freq, y * freq, seed + (unsigned)octave * 101u) * amp;
        norm += amp;
        amp  *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

/* --------------------------------------------------------------------------
 *  Generation
 * ----------------------------------------------------------------------- */
void ArenaGenerate(unsigned seed)
{
    /* The outer rim of the map is drowned so the world does not end on a
       hard cut; the ramp is this many tiles wide. */
    const float rimX = (float)WORLD_TILES_X * 0.055f;
    const float rimY = (float)WORLD_TILES_Y * 0.055f;

    for (int ty = 0; ty < WORLD_TILES_Y; ty++) {
        for (int tx = 0; tx < WORLD_TILES_X; tx++) {
            float cont  = Fbm(tx * 0.0230f, ty * 0.0230f, seed, 5);
            float ridge = Fbm(tx * 0.0550f, ty * 0.0550f, seed + 7717u, 3);
            float micro = ValueNoise(tx * 0.180f, ty * 0.180f, seed + 991u);

            float h = 0.470f
                    + (cont  - 0.5f) * 1.75f
                    + (ridge - 0.5f) * 0.55f
                    + (micro - 0.5f) * 0.16f;

            /* sink the map border into deep water */
            float ex = fminf((float)tx, (float)(WORLD_TILES_X - 1 - tx)) / rimX;
            float ey = fminf((float)ty, (float)(WORLD_TILES_Y - 1 - ty)) / rimY;
            float edge = fminf(fminf(ex, ey), 1.0f);
            h -= (1.0f - edge) * 0.60f;

            s_height[ty][tx] = h;

            if (h < LAND_LEVEL) {
                s_terrain[ty][tx] = TER_WATER;
            } else if (h < SAND_LEVEL) {
                s_terrain[ty][tx] = TER_SAND;
            } else {
                s_terrain[ty][tx] = TER_GRASS;
            }
        }
    }

    /* Scatter cover on grass. A little clustering noise keeps the trees in
       groves instead of sprinkling them evenly. */
    for (int ty = 1; ty < WORLD_TILES_Y - 1; ty++) {
        for (int tx = 1; tx < WORLD_TILES_X - 1; tx++) {
            s_decor[ty][tx] = DECOR_NONE;
            if (s_terrain[ty][tx] != TER_GRASS) continue;

            float grove = ValueNoise(tx * 0.11f, ty * 0.11f, seed + 4242u);
            float roll  = Noise01(tx, ty, seed + 991u);

            if (grove > 0.70f && roll > 0.72f) {
                if (roll > 0.86f)      s_decor[ty][tx] = DECOR_TREE_B;
                else if (roll > 0.71f) s_decor[ty][tx] = DECOR_TREE_C;
                else                   s_decor[ty][tx] = DECOR_TREE_A;
            } else if (s_height[ty][tx] > 0.62f && roll > 0.93f) {
                s_decor[ty][tx] = (roll > 0.965f) ? DECOR_ROCK_B : DECOR_ROCK_A;
            }
        }
    }
}

TerrainKind ArenaAt(int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= WORLD_TILES_X || ty >= WORLD_TILES_Y) return TER_WATER;
    return (TerrainKind)s_terrain[ty][tx];
}

bool ArenaBlocksAt(Vector2 world)
{
    int tx = (int)floorf(world.x / TILE_SIZE);
    int ty = (int)floorf(world.y / TILE_SIZE);
    if (tx < 0 || ty < 0 || tx >= WORLD_TILES_X || ty >= WORLD_TILES_Y) return false;
    return s_decor[ty][tx] != DECOR_NONE;
}

/* A random point that is comfortably on dry land - used to place drops. */
Vector2 ArenaRandomLandPos(void)
{
    for (int attempt = 0; attempt < 512; attempt++) {
        int tx = GetRandomValue(4, WORLD_TILES_X - 5);
        int ty = GetRandomValue(4, WORLD_TILES_Y - 5);
        if (s_terrain[ty][tx] == TER_GRASS && s_decor[ty][tx] == DECOR_NONE) {
            return V2((tx + 0.5f) * TILE_SIZE, (ty + 0.5f) * TILE_SIZE);
        }
    }
    return V2(WORLD_W * 0.5f, WORLD_H * 0.5f);
}

/* Sweep the map on a coarse lattice and return the centre of the window with
   the best mix of grass and beach. Dropping the squadron there means a run
   starts over terrain instead of empty ocean, and the title screen has
   something to fly past. */
Vector2 ArenaScenicPos(void)
{
    const int step = 16;   /* tiles between candidate windows */
    const int win  = 8;    /* half window size, in tiles      */

    int bestX = WORLD_TILES_X / 2;
    int bestY = WORLD_TILES_Y / 2;
    int best  = -1;

    for (int ty = win; ty < WORLD_TILES_Y - win; ty += step) {
        for (int tx = win; tx < WORLD_TILES_X - win; tx += step) {
            int score = 0;
            for (int y = ty - win; y <= ty + win; y += 2) {
                for (int x = tx - win; x <= tx + win; x += 2) {
                    if (s_terrain[y][x] == TER_GRASS)      score += 2;
                    else if (s_terrain[y][x] == TER_SAND)  score += 3;
                }
            }
            if (score > best) { best = score; bestX = tx; bestY = ty; }
        }
    }

    return V2((bestX + 0.5f) * TILE_SIZE, (bestY + 0.5f) * TILE_SIZE);
}

/* --------------------------------------------------------------------------
 *  Drawing
 * ----------------------------------------------------------------------- */
static Color WaterShade(float h)
{
    if (h < 0.10f) return COL_DEEP;
    if (h < 0.24f) return COL_SEA;
    if (h < 0.34f) return COL_WATER;
    return COL_SHALLOW;
}

static int DecorSprite(int decor)
{
    switch (decor) {
        case DECOR_TREE_A: return TILE_TREE_A;
        case DECOR_TREE_B: return TILE_TREE_B;
        case DECOR_TREE_C: return TILE_TREE_C;
        case DECOR_ROCK_A: return TILE_ROCK;
        case DECOR_ROCK_B: return TILE_ROCK_ALT;
        default:           return -1;
    }
}

/* The sheet only ships a couple of plain grass and sand stamps, so the same
   16x16 image repeats and the grid becomes painfully obvious. Each tile picks
   one of the two variants and one of four mirrorings from a cheap hash, which
   scatters the repeat instead of laying it out as a visible checker. */
static unsigned TileVariation(int tx, int ty)
{
    unsigned h = (unsigned)tx * 73856093u ^ (unsigned)ty * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

static void DrawTerrainTile(int sprite, float px, float py, bool flipX, bool flipY)
{
    Rectangle src = AtlasSource(sprite, TILE_COLS, TILE_CELL);

    /* NOTE: raylib anchors a negative source width at source.x and a negative
       height at source.y (it applies source.y -= source.height itself), so the
       top-left corner must be left untouched - moving it as well would sample
       the neighbouring cell. */
    if (flipX) src.width  = -TILE_CELL;
    if (flipY) src.height = -TILE_CELL;

    DrawTexturePro(assets.tiles, src, (Rectangle){ px, py, TILE_CELL, TILE_CELL },
                   V2(0.0f, 0.0f), 0.0f, WHITE);
}

void ArenaDraw(Camera2D cam)
{
    float halfW = RENDER_W * 0.5f / cam.zoom;
    float halfH = RENDER_H * 0.5f / cam.zoom;

    int x0 = (int)floorf((cam.target.x - halfW) / TILE_SIZE) - 2;
    int y0 = (int)floorf((cam.target.y - halfH) / TILE_SIZE) - 2;
    int x1 = (int)ceilf((cam.target.x + halfW) / TILE_SIZE) + 2;
    int y1 = (int)ceilf((cam.target.y + halfH) / TILE_SIZE) + 2;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > WORLD_TILES_X - 1) x1 = WORLD_TILES_X - 1;
    if (y1 > WORLD_TILES_Y - 1) y1 = WORLD_TILES_Y - 1;

    /* --- pass 1: land and water ------------------------------------- */
    for (int ty = y0; ty <= y1; ty++) {
        for (int tx = x0; tx <= x1; tx++) {
            float px = (float)(tx * TILE_SIZE);
            float py = (float)(ty * TILE_SIZE);
            unsigned h = TileVariation(tx, ty);

            switch (s_terrain[ty][tx]) {
                case TER_WATER:
                    DrawRectangle((int)px, (int)py, TILE_SIZE, TILE_SIZE,
                                  WaterShade(s_height[ty][tx]));
                    break;
                case TER_SAND:
                    DrawTerrainTile((h & 4u) ? TILE_SAND_ALT : TILE_SAND, px, py,
                                    (h & 1u) != 0, (h & 2u) != 0);
                    break;
                default:
                    DrawTerrainTile((h & 4u) ? TILE_GRASS_ALT : TILE_GRASS, px, py,
                                    (h & 1u) != 0, (h & 2u) != 0);
                    break;
            }
        }
    }

    /* --- pass 2: surf where land meets water ------------------------- */
    const int surf = 3;
    for (int ty = y0; ty <= y1; ty++) {
        for (int tx = x0; tx <= x1; tx++) {
            if (s_terrain[ty][tx] == TER_WATER) continue;

            float px = (float)(tx * TILE_SIZE);
            float py = (float)(ty * TILE_SIZE);

            if (ArenaAt(tx, ty - 1) == TER_WATER)
                DrawRectangle((int)px, (int)(py - surf), TILE_SIZE, surf, COL_FOAM);
            if (ArenaAt(tx, ty + 1) == TER_WATER)
                DrawRectangle((int)px, (int)(py + TILE_SIZE), TILE_SIZE, surf, COL_FOAM);
            if (ArenaAt(tx - 1, ty) == TER_WATER)
                DrawRectangle((int)(px - surf), (int)py, surf, TILE_SIZE, COL_FOAM);
            if (ArenaAt(tx + 1, ty) == TER_WATER)
                DrawRectangle((int)(px + TILE_SIZE), (int)py, surf, TILE_SIZE, COL_FOAM);
        }
    }

    /* --- pass 3: cover ---------------------------------------------- */
    for (int ty = y0; ty <= y1; ty++) {
        for (int tx = x0; tx <= x1; tx++) {
            int sprite = DecorSprite(s_decor[ty][tx]);
            if (sprite < 0) continue;
            DrawTerrainTile(sprite,
                            (float)(tx * TILE_SIZE), (float)(ty * TILE_SIZE),
                            (TileVariation(tx, ty) & 1u) != 0, false);
        }
    }
}
