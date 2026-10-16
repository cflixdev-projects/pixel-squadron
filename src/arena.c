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

/* Height bands. Land starts at the land level; everything below it is water.
   The exact heights differ per biome (a desert has wide sand and no beach to
   speak of, a tundra drops straight from ice to water), so these are the
   temperate defaults and live in the biome table below. */
#define LAND_LEVEL 0.435f
#define SAND_LEVEL 0.472f

/* --------------------------------------------------------------------------
 *  Biomes
 *
 *  A second, much lower frequency noise field decides which *kind* of country
 *  a tile belongs to. The two neighbouring biomes are blended across the
 *  seam - palette and waterline both - so crossing from one into the next
 *  reads as weather rather than as a tile boundary.
 * ----------------------------------------------------------------------- */
typedef struct {
    float landLevel;    /* below this is water          */
    float sandLevel;    /* ...and between the two, sand */
    Color grassTint;    /* multiplied over the grass stamp */
    Color sandTint;
    Color ground;       /* ...then this is laid over the whole tile. A tint can
                           only ever darken the art, and the ground here is a
                           bright yellow-green, so the pale biomes need a wash
                           of colour on top to come out pale at all. */
    Color water[4];     /* deep, sea, water, shallow    */
    Color foam;         /* the surf line                */
    int   groveRoll;    /* grove tiles rolling above this grow trees  */
    int   rockRoll;     /* high ground rolling above this grows rock  */
    int   id;           /* which biome this sample is closest to      */
} Biome;

/* The three tones the crag stamps are painted in - highlight, mid, dark -
   repainted for each biome. A tint cannot do this: multiplying an orange can
   only make a darker orange, so the crags have to be baked, and this is the
   palette each bake uses. */
const Color kRockRamp[BIOME_COUNT][3] = {
    /* TEMPERATE - the sheet's own hot orange, untouched */
    { { 244, 172, 102, 255 }, { 252, 104,  59, 255 }, { 222,  75,  30, 255 } },
    /* DESERT    - sun-bleached sandstone */
    { { 250, 208, 152, 255 }, { 238, 168, 106, 255 }, { 204, 124,  74, 255 } },
    /* SNOW      - bare grey rock under the ice */
    { { 208, 220, 234, 255 }, { 150, 166, 186, 255 }, { 104, 118, 140, 255 } },
    /* ASH       - banked down to a dull char */
    { { 168,  86,  52, 255 }, { 118,  52,  34, 255 }, {  78,  34,  26, 255 } },
    /* JUNGLE    - overgrown, mossy boulders */
    { { 150, 178, 110, 255 }, { 104, 132,  72, 255 }, {  66,  88,  50, 255 } },
};

/* All the decorations on the sheet are pines and red crags, so the palette
   carries the identity: the same stamp reads as scrub in the desert, as a
   frosted pine in the snow and as a stand of jungle in the green. */
static const Biome kBiomes[BIOME_COUNT] = {
    /* TEMPERATE - the original green archipelago, exactly as it always was */
    { 0.435f, 0.472f,
      { 255, 255, 255, 255 }, { 255, 255, 255, 255 },
      { 0, 0, 0, 0 },
      { {  16,  66,  96, 255 }, {  26, 108, 148, 255 },
        {  44, 197, 246, 255 }, { 126, 226, 250, 255 } },
      { 223, 246, 245, 255 }, 72, 93, 0 },

    /* DESERT - wide sand, and grass that has gone to straw */
    { 0.410f, 0.505f,
      { 255, 255, 255, 255 }, { 255, 255, 255, 255 },
      { 240, 175, 120, 115 },
      { {  22,  84, 118, 255 }, {  38, 142, 170, 255 },
        {  74, 206, 226, 255 }, { 154, 240, 246, 255 } },
      { 246, 250, 238, 255 }, 200, 88, 1 },

    /* SNOW - a flat, pale country, thin ice shelves, frosted pines */
    { 0.428f, 0.452f,
      { 255, 235, 255, 255 }, { 255, 240, 255, 255 },
      { 208, 220, 248, 209 },
      { {  22,  48,  82, 255 }, {  38,  84, 126, 255 },
        {  70, 140, 186, 255 }, { 140, 196, 224, 255 } },
      { 240, 250, 255, 255 }, 80, 90, 2 },

    /* ASH - basalt underfoot and burning shallows, nothing green anywhere */
    { 0.448f, 0.462f,
      { 255, 230, 255, 255 }, { 255, 235, 240, 255 },
      {  48,  48,  54, 219 },
      { {  26,  16,  18, 255 }, {  58,  26,  24, 255 },
        { 122,  46,  28, 255 }, { 226, 118,  46, 255 } },
      { 255, 176,  96, 255 }, 200, 86, 3 },

    /* JUNGLE - deep, wet, overgrown */
    { 0.440f, 0.466f,
      { 170, 230, 160, 255 }, { 180, 225, 165, 255 },
      {  30,  80,  40,  64 },
      { {  14,  58,  50, 255 }, {  22,  92,  74, 255 },
        {  40, 132, 104, 255 }, { 104, 178, 140, 255 } },
      { 214, 240, 220, 255 }, 52, 97, 4 },
};

static Color MixColor(Color a, Color b, float t)
{
    return (Color){
        (unsigned char)((float)a.r + ((float)b.r - (float)a.r) * t),
        (unsigned char)((float)a.g + ((float)b.g - (float)a.g) * t),
        (unsigned char)((float)a.b + ((float)b.b - (float)a.b) * t),
        (unsigned char)((float)a.a + ((float)b.a - (float)a.a) * t),
    };
}

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

static Biome s_force;          /* dev: one biome for the whole world */
static bool  s_forced = false;

static const char *const kBiomeNames[BIOME_COUNT] = {
    "VERDANT ISLES", "DUST REACH", "FROST SHELF", "ASH BARRENS", "DEEP VERGE",
};

static void BiomeSample(int tx, int ty, Biome *out);

void ArenaForceBiome(int id)
{
    if (id < 0 || id >= BIOME_COUNT) { s_forced = false; return; }
    s_force  = kBiomes[id];
    s_force.id = id;
    s_forced = true;
}

const char *ArenaBiomeNameAt(Vector2 world)
{
    int tx = (int)floorf(world.x / TILE_SIZE);
    int ty = (int)floorf(world.y / TILE_SIZE);

    Biome b;
    BiomeSample(tx, ty, &b);
    return kBiomeNames[b.id];
}

/* Blend the two neighbouring biomes at this tile into `out`. The whole world
   is a pure function of the coordinate, so this is as well - no lookup, no
   storage, no seams. */
static void BiomeSample(int tx, int ty, Biome *out)
{
    if (s_forced) { *out = s_force; return; }

    float bf = Fbm(tx * 0.0042f, ty * 0.0042f, s_seed + 50505u, 2);

    /* stretch the midband out to the ends so every biome really does turn up */
    bf = Clamp((bf - 0.5f) * 2.2f + 0.5f, 0.0f, 0.9999f) * (float)BIOME_COUNT;

    int   i0 = (int)floorf(bf);
    if (i0 < 0) i0 = 0;
    if (i0 > BIOME_COUNT - 1) i0 = BIOME_COUNT - 1;
    int   i1 = (i0 + 1 < BIOME_COUNT) ? i0 + 1 : i0;
    float frac = bf - (float)i0;

    /* Only the tail of each band blends with the next one. The middle of a
       band is a single biome - pure palette, pure waterline - and the two meet
       over a wide, soft edge, so a border reads as a coastline changing
       character rather than as a fence across the world. */
    const float blend = 0.16f;
    float t = 0.0f;
    if (i1 != i0 && frac > 1.0f - blend) t = (frac - (1.0f - blend)) / blend;

    const Biome *a = &kBiomes[i0];
    const Biome *b = &kBiomes[i1];

    /* Decor follows whichever biome owns the tile rather than a blend: a tree
       cannot be 40% desert. The roll thresholds switch at the halfway point. */
    const Biome *near = (t < 0.5f) ? a : b;
    out->groveRoll = near->groveRoll;
    out->rockRoll  = near->rockRoll;
    out->id        = (t < 0.5f) ? i0 : i1;

    out->landLevel = Lerp(a->landLevel, b->landLevel, t);
    out->sandLevel = Lerp(a->sandLevel, b->sandLevel, t);
    out->grassTint = MixColor(a->grassTint, b->grassTint, t);
    out->sandTint  = MixColor(a->sandTint,  b->sandTint,  t);
    out->foam      = MixColor(a->foam,      b->foam,      t);

    /* the wash fades out with its own alpha, so a clear biome really is clear */
    out->ground = MixColor(a->ground, b->ground, t);
    out->ground.a = (unsigned char)((float)a->ground.a +
                                   ((float)b->ground.a - (float)a->ground.a) * t);

    for (int i = 0; i < 4; i++) out->water[i] = MixColor(a->water[i], b->water[i], t);
}

static TerrainKind KindOf(float h, const Biome *b)
{
    if (h < b->landLevel) return TER_WATER;
    if (h < b->sandLevel) return TER_SAND;
    return TER_GRASS;
}

/* Cover only ever grows on grass, and only where a second noise layer says a
   grove belongs - so the trees clump into woodland instead of being sprinkled
   evenly. How thick that woodland is, and whether there is one at all, is the
   biome's business. */
static int DecorAt(int tx, int ty, float h, const Biome *b)
{
    if (h < b->sandLevel) return DECOR_NONE;

    float grove = ValueNoise(tx * 0.11f, ty * 0.11f, s_seed + 4242u);
    float roll  = Noise01(tx, ty, s_seed + 991u) * 100.0f;
    float low   = (float)b->groveRoll;

    if (grove > 0.70f && roll > low) {
        if (roll > low + 14.0f) return DECOR_TREE_B;
        if (roll > low + 4.0f)  return DECOR_TREE_C;
        return DECOR_TREE_A;
    }
    if (h > 0.62f && roll > (float)b->rockRoll) {
        return (roll > (float)b->rockRoll + 3.5f) ? DECOR_ROCK_B : DECOR_ROCK_A;
    }
    return DECOR_NONE;
}

void ArenaGenerate(unsigned seed)
{
    s_seed = seed ? seed : 1337u;
}

TerrainKind ArenaAt(int tx, int ty)
{
    Biome b;
    BiomeSample(tx, ty, &b);
    return KindOf(TileHeight(tx, ty), &b);
}

bool ArenaBlocksAt(Vector2 world)
{
    int tx = (int)floorf(world.x / TILE_SIZE);
    int ty = (int)floorf(world.y / TILE_SIZE);

    Biome b;
    BiomeSample(tx, ty, &b);

    float h = TileHeight(tx, ty);
    if (h < b.sandLevel) return false;
    return DecorAt(tx, ty, h, &b) != DECOR_NONE;
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

        Biome b;
        BiomeSample(tx, ty, &b);

        float h = TileHeight(tx, ty);
        if (h < b.sandLevel || DecorAt(tx, ty, h, &b) != DECOR_NONE) continue;

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
                    Biome b;
                    BiomeSample(x, y, &b);

                    TerrainKind k = KindOf(TileHeight(x, y), &b);
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
    uint8_t terrain;
    uint8_t decor;
    Color   grassTint;   /* biome tints, resolved once per tile per frame */
    Color   sandTint;
    Color   ground;      /* the wash laid over the ground tile            */
    Color   water;
    Color   foam;
    uint8_t biome;       /* which biome owns the tile, for the crag stamps */
} TileInfo;

/* The visible rectangle, cached for one frame. Sized for the largest window
   the game will ever draw into (see the clamp in main.c's UpdateViewport). */
#define VIEW_MAX_TILES_X 96
#define VIEW_MAX_TILES_Y 64
static TileInfo s_view[VIEW_MAX_TILES_Y][VIEW_MAX_TILES_X];

static Color WaterShade(float h, const Biome *b)
{
    if (h < 0.10f) return b->water[0];
    if (h < 0.24f) return b->water[1];
    if (h < 0.34f) return b->water[2];
    return b->water[3];
}

/* Which atlas cell to stamp for a decoration. The crags come in a per-biome
   flavour - the sheet only has the hot orange ones - while the pines are the
   same art everywhere and get their colour from the local light instead. */
static int DecorSprite(int decor, int biome)
{
    switch (decor) {
        case DECOR_TREE_A: return TILE_TREE_A;
        case DECOR_TREE_B: return TILE_TREE_B;
        case DECOR_TREE_C: return TILE_TREE_C;
        case DECOR_ROCK_A: return TILE_ROCK_BIOME     + biome;
        case DECOR_ROCK_B: return TILE_ROCK_ALT_BIOME + biome;
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

static void DrawTerrainTile(int sprite, float px, float py, bool flipX, bool flipY,
                            Color tint)
{
    Rectangle src = AtlasSource(sprite, TILE_COLS, TILE_CELL);

    /* NOTE: raylib anchors a negative source width at source.x and a negative
       height at source.y (it applies source.y -= source.height itself), so the
       top-left corner must be left untouched - moving it as well would sample
       the neighbouring cell. */
    if (flipX) src.width  = -TILE_CELL;
    if (flipY) src.height = -TILE_CELL;

    DrawTexturePro(assets.tiles, src, (Rectangle){ px, py, TILE_CELL, TILE_CELL },
                   V2(0.0f, 0.0f), 0.0f, tint);
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

            Biome b;
            BiomeSample(tx, ty, &b);

            float h = TileHeight(tx, ty);
            TerrainKind k = KindOf(h, &b);

            s_view[j][i].terrain   = (uint8_t)k;
            s_view[j][i].decor     = (uint8_t)((k == TER_GRASS)
                                               ? DecorAt(tx, ty, h, &b) : DECOR_NONE);
            s_view[j][i].grassTint = b.grassTint;
            s_view[j][i].sandTint  = b.sandTint;
            s_view[j][i].ground    = b.ground;
            s_view[j][i].foam      = b.foam;
            s_view[j][i].water     = WaterShade(h, &b);
            s_view[j][i].biome     = (uint8_t)b.id;
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
                                  s_view[j][i].water);
                    break;
                case TER_SAND:
                    DrawTerrainTile((v & 4u) ? TILE_SAND_ALT : TILE_SAND, px, py,
                                    (v & 1u) != 0, (v & 2u) != 0,
                                    s_view[j][i].sandTint);
                    if (s_view[j][i].ground.a > 0)
                        DrawRectangle((int)px, (int)py, TILE_SIZE, TILE_SIZE,
                                      s_view[j][i].ground);
                    break;
                default:
                    DrawTerrainTile((v & 4u) ? TILE_GRASS_ALT : TILE_GRASS, px, py,
                                    (v & 1u) != 0, (v & 2u) != 0,
                                    s_view[j][i].grassTint);
                    if (s_view[j][i].ground.a > 0)
                        DrawRectangle((int)px, (int)py, TILE_SIZE, TILE_SIZE,
                                      s_view[j][i].ground);
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
            Color foam = s_view[j][i].foam;

            if (CachedWater(i, j - 1, nx, ny))
                DrawRectangle((int)px, (int)(py - surf), TILE_SIZE, surf, foam);
            if (CachedWater(i, j + 1, nx, ny))
                DrawRectangle((int)px, (int)(py + TILE_SIZE), TILE_SIZE, surf, foam);
            if (CachedWater(i - 1, j, nx, ny))
                DrawRectangle((int)(px - surf), (int)py, surf, TILE_SIZE, foam);
            if (CachedWater(i + 1, j, nx, ny))
                DrawRectangle((int)(px + TILE_SIZE), (int)py, surf, TILE_SIZE, foam);
        }
    }

    /* --- pass 3: cover ---------------------------------------------- */
    for (int j = 0; j < ny; j++) {
        for (int i = 0; i < nx; i++) {
            int sprite = DecorSprite(s_view[j][i].decor, s_view[j][i].biome);
            if (sprite < 0) continue;

            int tx = x0 + i;
            int ty = y0 + j;

            /* A pine picks up the local light, so it reads as frosted in the
               snow and as dry scrub in the sand. A crag does not: it already
               carries its biome's own rock colour, baked in at load, and the
               land it stands on is whatever the biome painted. */
            bool  tree = (s_view[j][i].decor >= DECOR_TREE_A &&
                          s_view[j][i].decor <= DECOR_TREE_C);
            Color tint = tree
                       ? MixColor(s_view[j][i].grassTint, (Color){ 255, 255, 255, 255 }, 0.45f)
                       : WHITE;

            DrawTerrainTile(sprite, (float)(tx * TILE_SIZE), (float)(ty * TILE_SIZE),
                            (TileVariation(tx, ty) & 1u) != 0, false, tint);
        }
    }
}
