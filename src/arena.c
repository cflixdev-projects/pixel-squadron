/*****************************************************************************
 *  arena.c - the procedural archipelago the squadron flies over.
 *
 *  There is no map. The height of a tile is a pure function of its (x, y)
 *  coordinate and the run's seed, so the world simply goes on in every
 *  direction for as long as the squadron lives: the player can hold one
 *  heading for five minutes and never meet an edge, a wall or a repeating
 *  pattern. Nothing is stored, so there is nothing to stream and nothing to
 *  run out of.
 *
 *  Three noise layers are mixed to get a readable world at this scale:
 *      cont   low frequency "continents", decides where land is at all
 *      ridge  mid frequency coastlines and capes
 *      micro  high frequency beaches, lagoons and speckle
 *  Everything below LAND_LEVEL is water; a band above it is beach, and the
 *  rest is grass with trees and rocks scattered on top as bullet-blocking
 *  cover.
 *
 *  The only cost of the endless approach is that a tile is always computed,
 *  never looked up, so ArenaDraw fills a small cache for the visible rectangle
 *  once per frame and every later pass reads that.
 *****************************************************************************/
#include "game.h"

#include <math.h>

static unsigned s_seed = 1337u;

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
   layers can stay cheap - this is called for every tile on screen, every
   frame. */
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
 *  Generation - everything below is a pure function of the tile coordinate
 * ----------------------------------------------------------------------- */
static float TileHeight(int tx, int ty)
{
    float cont  = Fbm(tx * 0.0230f, ty * 0.0230f, s_seed, 5);
    float ridge = Fbm(tx * 0.0550f, ty * 0.0550f, s_seed + 7717u, 3);
    float micro = ValueNoise(tx * 0.180f, ty * 0.180f, s_seed + 991u);

    return 0.470f
         + (cont  - 0.5f) * 1.75f
         + (ridge - 0.5f) * 0.55f
         + (micro - 0.5f) * 0.16f;
}

static TerrainKind KindOf(float h)
{
    if (h < LAND_LEVEL) return TER_WATER;
    if (h < SAND_LEVEL) return TER_SAND;
    return TER_GRASS;
}

/* Cover is only ever planted on grass, and only where a second noise layer
   says a grove belongs - so the trees clump into woodland instead of being
   sprinkled evenly. */
static int DecorAt(int tx, int ty, float h)
{
    if (h < SAND_LEVEL) return DECOR_NONE;

    float grove = ValueNoise(tx * 0.11f, ty * 0.11f, s_seed + 4242u);
    float roll  = Noise01(tx, ty, s_seed + 991u);

    if (grove > 0.70f && roll > 0.72f) {
        if (roll > 0.86f)      return DECOR_TREE_B;
        else if (roll > 0.71f) return DECOR_TREE_C;
        else                   return DECOR_TREE_A;
    }
    if (h > 0.62f && roll > 0.93f) {
        return (roll > 0.965f) ? DECOR_ROCK_B : DECOR_ROCK_A;
    }
    return DECOR_NONE;
}

void ArenaGenerate(unsigned seed)
{
    s_seed = seed ? seed : 1337u;
}

TerrainKind ArenaAt(int tx, int ty)
{
    return KindOf(TileHeight(tx, ty));
}

bool ArenaBlocksAt(Vector2 world)
{
    int tx = (int)floorf(world.x / TILE_SIZE);
    int ty = (int)floorf(world.y / TILE_SIZE);

    float h = TileHeight(tx, ty);
    if (h < SAND_LEVEL) return false;
    return DecorAt(tx, ty, h) != DECOR_NONE;
}

/* A random point that is comfortably on dry land - used to place drops. */
Vector2 ArenaRandomLandNear(Vector2 centre, float rmin, float rmax)
{
    for (int attempt = 0; attempt < 220; attempt++) {
        float ang = (float)GetRandomValue(0, 359) * DEG2RAD;
        float r   = rmin + (rmax - rmin) * (float)GetRandomValue(0, 1000) / 1000.0f;

        Vector2 p = V2(centre.x + cosf(ang) * r, centre.y + sinf(ang) * r);
        int tx = (int)floorf(p.x / TILE_SIZE);
        int ty = (int)floorf(p.y / TILE_SIZE);

        float h = TileHeight(tx, ty);
        if (h < SAND_LEVEL || DecorAt(tx, ty, h) != DECOR_NONE) continue;

        return V2((tx + 0.5f) * TILE_SIZE, (ty + 0.5f) * TILE_SIZE);
    }
    return centre;
}

/* Sweep a coarse lattice around the origin and return the centre of the window
   with the best mix of grass and beach. Dropping the squadron there means a
   run starts over terrain instead of empty ocean. Ties go to whichever window
   the sweep reaches first, so the same seed always starts in the same place. */
Vector2 ArenaScenicPos(void)
{
    const int step = 20;   /* tiles between candidate windows */
    const int win  = 7;    /* half window size, in tiles      */
    const int span = 12;   /* candidates each way from origin */

    int bestX = 0, bestY = 0, best = -1;

    for (int cy = -span; cy <= span; cy++) {
        for (int cx = -span; cx <= span; cx++) {
            int tx = cx * step;
            int ty = cy * step;
            int score = 0;

            for (int y = ty - win; y <= ty + win; y += 2) {
                for (int x = tx - win; x <= tx + win; x += 2) {
                    TerrainKind k = KindOf(TileHeight(x, y));
                    if (k == TER_GRASS)     score += 2;
                    else if (k == TER_SAND) score += 3;
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
typedef struct {
    float   h;
    uint8_t terrain;
    uint8_t decor;
} TileInfo;

/* The visible rectangle, cached for one frame. Sized for the largest window
   the game will ever draw into (see the clamp in main.c's UpdateViewport). */
#define VIEW_MAX_TILES_X 96
#define VIEW_MAX_TILES_Y 64
static TileInfo s_view[VIEW_MAX_TILES_Y][VIEW_MAX_TILES_X];

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

/* Is the cached neighbour at (i, j) water? Anything off the edge of the cache
   is treated as water, which only ever suppresses surf that would have been
   drawn off screen anyway. */
static bool CachedWater(int i, int j, int nx, int ny)
{
    if (i < 0 || j < 0 || i >= nx || j >= ny) return true;
    return s_view[j][i].terrain == TER_WATER;
}

void ArenaDraw(Camera2D cam)
{
    float halfW = RENDER_W * 0.5f / cam.zoom;
    float halfH = RENDER_H * 0.5f / cam.zoom;

    int x0 = (int)floorf((cam.target.x - halfW) / TILE_SIZE) - 2;
    int y0 = (int)floorf((cam.target.y - halfH) / TILE_SIZE) - 2;
    int x1 = (int)ceilf((cam.target.x + halfW) / TILE_SIZE) + 2;
    int y1 = (int)ceilf((cam.target.y + halfH) / TILE_SIZE) + 2;

    int nx = x1 - x0 + 1;
    int ny = y1 - y0 + 1;
    if (nx > VIEW_MAX_TILES_X) nx = VIEW_MAX_TILES_X;
    if (ny > VIEW_MAX_TILES_Y) ny = VIEW_MAX_TILES_Y;
    if (nx <= 0 || ny <= 0) return;

    /* --- pass 0: work out what is actually here ---------------------- */
    for (int j = 0; j < ny; j++) {
        for (int i = 0; i < nx; i++) {
            int tx = x0 + i;
            int ty = y0 + j;
            float h = TileHeight(tx, ty);
            TerrainKind k = KindOf(h);

            s_view[j][i].h       = h;
            s_view[j][i].terrain = (uint8_t)k;
            s_view[j][i].decor   = (uint8_t)((k == TER_GRASS) ? DecorAt(tx, ty, h)
                                                              : DECOR_NONE);
        }
    }

    /* --- pass 1: land and water ------------------------------------- */
    for (int j = 0; j < ny; j++) {
        for (int i = 0; i < nx; i++) {
            int tx = x0 + i;
            int ty = y0 + j;
            float px = (float)(tx * TILE_SIZE);
            float py = (float)(ty * TILE_SIZE);
            unsigned v = TileVariation(tx, ty);

            switch ((TerrainKind)s_view[j][i].terrain) {
                case TER_WATER:
                    DrawRectangle((int)px, (int)py, TILE_SIZE, TILE_SIZE,
                                  WaterShade(s_view[j][i].h));
                    break;
                case TER_SAND:
                    DrawTerrainTile((v & 4u) ? TILE_SAND_ALT : TILE_SAND, px, py,
                                    (v & 1u) != 0, (v & 2u) != 0);
                    break;
                default:
                    DrawTerrainTile((v & 4u) ? TILE_GRASS_ALT : TILE_GRASS, px, py,
                                    (v & 1u) != 0, (v & 2u) != 0);
                    break;
            }
        }
    }

    /* --- pass 2: surf where land meets water ------------------------- */
    const int surf = 3;
    for (int j = 0; j < ny; j++) {
        for (int i = 0; i < nx; i++) {
            if (s_view[j][i].terrain == TER_WATER) continue;

            float px = (float)((x0 + i) * TILE_SIZE);
            float py = (float)((y0 + j) * TILE_SIZE);

            if (CachedWater(i, j - 1, nx, ny))
                DrawRectangle((int)px, (int)(py - surf), TILE_SIZE, surf, COL_FOAM);
            if (CachedWater(i, j + 1, nx, ny))
                DrawRectangle((int)px, (int)(py + TILE_SIZE), TILE_SIZE, surf, COL_FOAM);
            if (CachedWater(i - 1, j, nx, ny))
                DrawRectangle((int)(px - surf), (int)py, surf, TILE_SIZE, COL_FOAM);
            if (CachedWater(i + 1, j, nx, ny))
                DrawRectangle((int)(px + TILE_SIZE), (int)py, surf, TILE_SIZE, COL_FOAM);
        }
    }

    /* --- pass 3: cover ---------------------------------------------- */
    for (int j = 0; j < ny; j++) {
        for (int i = 0; i < nx; i++) {
            int sprite = DecorSprite(s_view[j][i].decor);
            if (sprite < 0) continue;

            int tx = x0 + i;
            int ty = y0 + j;
            DrawTerrainTile(sprite, (float)(tx * TILE_SIZE), (float)(ty * TILE_SIZE),
                            (TileVariation(tx, ty) & 1u) != 0, false);
        }
    }
}
