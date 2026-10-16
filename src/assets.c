/*****************************************************************************
 *  assets.c - locating, loading and drawing the game's sprite atlases.
 *****************************************************************************/
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Assets assets = {0};

/* --------------------------------------------------------------------------
 *  Path resolution
 *  The game may be launched from the repository root, from build/, or from
 *  an installed directory, so probe a handful of likely roots and pick the
 *  first one that actually contains the file.
 * ----------------------------------------------------------------------- */
static char g_assetRoot[512] = "";

static bool TryPath(const char *root, const char *rel, char *out, size_t outSize)
{
    snprintf(out, outSize, "%s%s", root, rel);
    return FileExists(out);
}

static const char *AssetPath(const char *rel)
{
    static char full[512];
    char candidate[512];

    /* 1. explicit override for people who move the assets elsewhere */
    const char *env = getenv("PIXEL_SQUADRON_ASSETS");
    if (env && *env) {
        snprintf(candidate, sizeof candidate, "%s/%s", env, rel);
        if (FileExists(candidate)) return TextFormat("%s", candidate);
    }

    /* 2. cache the first working root */
    if (g_assetRoot[0] && TryPath(g_assetRoot, rel, full, sizeof full)) {
        return full;
    }

    /* 3. relative to the working directory, then walk upwards */
    static const char *roots[] = {
        "assets/",
        "../assets/",
        "../../assets/",
        "../../../assets/",
    };
    for (int i = 0; i < (int)(sizeof roots / sizeof roots[0]); i++) {
        if (TryPath(roots[i], rel, full, sizeof full)) {
            snprintf(g_assetRoot, sizeof g_assetRoot, "%s", roots[i]);
            return full;
        }
    }

    /* 4. next to the executable, and one level up from it */
    const char *exe = GetApplicationDirectory();
    static const char *exeSuffix[] = { "assets/", "../assets/" };
    for (int i = 0; i < 2; i++) {
        snprintf(candidate, sizeof candidate, "%s%s", exe, exeSuffix[i]);
        if (TryPath(candidate, rel, full, sizeof full)) {
            snprintf(g_assetRoot, sizeof g_assetRoot, "%s", candidate);
            return full;
        }
    }

    TraceLog(LOG_WARNING, "ASSETS: could not locate '%s'", rel);
    snprintf(full, sizeof full, "assets/%s", rel); /* let raylib report it */
    return full;
}

/* --------------------------------------------------------------------------
 *  Atlas preparation
 * ----------------------------------------------------------------------- */

/* Rebuild a grid sheet with a replicated border around every cell, so that
   sampling at a cell edge can never reach the neighbouring sprite. Rows past
   `srcRows` are left blank for whatever wants to fill them in later. */
static Image PadAtlas(Image sheet, int cell, int cols, int srcRows, int rows)
{
    const int stride = cell + ATLAS_PAD * 2;
    Image padded = GenImageColor(stride * cols, stride * rows, BLANK);

    for (int r = 0; r < srcRows; r++) {
        for (int c = 0; c < cols; c++) {
            /* source cell and its destination slot */
            Rectangle cellRec = { (float)(c * cell), (float)(r * cell),
                                  (float)cell, (float)cell };
            Rectangle slot    = { (float)(c * stride + ATLAS_PAD),
                                  (float)(r * stride + ATLAS_PAD),
                                  (float)cell, (float)cell };

            ImageDraw(&padded, sheet, cellRec, slot, WHITE);

            for (int p = 1; p <= ATLAS_PAD; p++) {
                const float o = (float)p;

                /* replicate the top and bottom rows outwards */
                ImageDraw(&padded, sheet,
                          (Rectangle){ cellRec.x, cellRec.y, (float)cell, 1.0f },
                          (Rectangle){ slot.x, slot.y - o, (float)cell, 1.0f }, WHITE);
                ImageDraw(&padded, sheet,
                          (Rectangle){ cellRec.x, cellRec.y + cell - 1.0f, (float)cell, 1.0f },
                          (Rectangle){ slot.x, slot.y + cell + o - 1.0f, (float)cell, 1.0f }, WHITE);

                /* replicate the left and right columns outwards */
                ImageDraw(&padded, sheet,
                          (Rectangle){ cellRec.x, cellRec.y, 1.0f, (float)cell },
                          (Rectangle){ slot.x - o, slot.y, 1.0f, (float)cell }, WHITE);
                ImageDraw(&padded, sheet,
                          (Rectangle){ cellRec.x + cell - 1.0f, cellRec.y, 1.0f, (float)cell },
                          (Rectangle){ slot.x + cell + o - 1.0f, slot.y, 1.0f, (float)cell }, WHITE);
            }

            /* and the four corner pixels */
            const float ex = cellRec.x + cell - 1.0f;
            const float ey = cellRec.y + cell - 1.0f;
            ImageDraw(&padded, sheet, (Rectangle){ cellRec.x, cellRec.y, 1.0f, 1.0f },
                      (Rectangle){ slot.x - 1.0f, slot.y - 1.0f, 1.0f, 1.0f }, WHITE);
            ImageDraw(&padded, sheet, (Rectangle){ ex, cellRec.y, 1.0f, 1.0f },
                      (Rectangle){ slot.x + cell, slot.y - 1.0f, 1.0f, 1.0f }, WHITE);
            ImageDraw(&padded, sheet, (Rectangle){ cellRec.x, ey, 1.0f, 1.0f },
                      (Rectangle){ slot.x - 1.0f, slot.y + cell, 1.0f, 1.0f }, WHITE);
            ImageDraw(&padded, sheet, (Rectangle){ ex, ey, 1.0f, 1.0f },
                      (Rectangle){ slot.x + cell, slot.y + cell, 1.0f, 1.0f }, WHITE);
        }
    }

    return padded;
}

/* Load a grid sheet and return the padded, point-filtered texture for it. */
static Texture2D LoadAtlas(const char *relative, int cell, int cols, int rows)
{
    Image sheet = LoadImage(AssetPath(relative));
    if (sheet.data == NULL) {
        TraceLog(LOG_ERROR, "ASSETS: failed to load '%s'", relative);
        return (Texture2D){ 0 };
    }

    Image padded = PadAtlas(sheet, cell, cols, rows, rows);
    UnloadImage(sheet);

    Texture2D texture = LoadTextureFromImage(padded);
    UnloadImage(padded);

    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    return texture;
}

Rectangle AtlasSource(int idx, int cols, int cell)
{
    const int stride = cell + ATLAS_PAD * 2;
    return (Rectangle){
        (float)((idx % cols) * stride + ATLAS_PAD),
        (float)((idx / cols) * stride + ATLAS_PAD),
        (float)cell,
        (float)cell,
    };
}

/* The key pass runs later in this file; the atlas builder needs it declared. */
static void KeyOutTileBackground(Image *img, int cell, int cols,
                                 const int *cells, int count);

/* Repaint one crag stamp into one biome's colours, and drop the copy in a
   spare cell of the atlas. The sheet's crags are a single hot orange: a tint
   can only ever darken a sprite, so no amount of tinting turns them grey, and
   an orange crag on a snowfield was the one thing the biome work never fixed.
   There are only three rock tones in the art, so this is a straight swap. */
static void BakeBiomeCrag(Image *img, int cols, int cell, int src, int dst,
                          const Color ramp[3])
{
    static const Color kFrom[3] = {
        { 244, 172, 102, 255 },   /* highlight */
        { 252, 104,  59, 255 },   /* mid       */
        { 222,  75,  30, 255 },   /* dark      */
    };
    const int stride = cell + ATLAS_PAD * 2;
    const int sc = src % cols, sr = src / cols;
    const int dc = dst % cols, dr = dst / cols;

    /* the padding is copied too, so the baked cell's border replicates the
       repainted pixels rather than the old ones */
    for (int y = -ATLAS_PAD; y < cell + ATLAS_PAD; y++) {
        for (int x = -ATLAS_PAD; x < cell + ATLAS_PAD; x++) {
            int sx = sc * stride + ATLAS_PAD + x;
            int sy = sr * stride + ATLAS_PAD + y;
            int dx = dc * stride + ATLAS_PAD + x;
            int dy = dr * stride + ATLAS_PAD + y;

            if (sx < 0 || sy < 0 || sx >= img->width  || sy >= img->height) continue;
            if (dx < 0 || dy < 0 || dx >= img->width  || dy >= img->height) continue;

            Color at = GetImageColor(*img, sx, sy);

            for (int i = 0; i < 3; i++) {
                if (at.r == kFrom[i].r && at.g == kFrom[i].g && at.b == kFrom[i].b) {
                    at = ramp[i];
                    break;
                }
            }

            /* everything else - the ink outline, and the transparent ground
               the key pass left behind - is copied across untouched */
            ImageDrawPixel(img, dx, dy, at);
        }
    }
}

/* The tilesheet, with the cover stamps cut out so the biome underneath shows,
   and the crags baked out once per biome. */
static Texture2D LoadTilesAtlas(void)
{
    Image sheet = LoadImage(AssetPath("sprites/tiles.png"));
    if (sheet.data == NULL) {
        TraceLog(LOG_ERROR, "ASSETS: failed to load 'sprites/tiles.png'");
        return (Texture2D){ 0 };
    }

    Image padded = PadAtlas(sheet, TILE_CELL, TILE_COLS, 10, TILE_ROWS);
    UnloadImage(sheet);

    static const int cover[] = {
        TILE_TREE_A, TILE_TREE_B, TILE_TREE_C, TILE_ROCK, TILE_ROCK_ALT,
    };
    KeyOutTileBackground(&padded, TILE_CELL, TILE_COLS, cover, 5);

    for (int b = 0; b < BIOME_COUNT; b++) {
        BakeBiomeCrag(&padded, TILE_COLS, TILE_CELL,
                      TILE_ROCK,     TILE_ROCK_BIOME     + b, kRockRamp[b]);
        BakeBiomeCrag(&padded, TILE_COLS, TILE_CELL,
                      TILE_ROCK_ALT, TILE_ROCK_ALT_BIOME + b, kRockRamp[b]);
    }

    Texture2D texture = LoadTextureFromImage(padded);
    UnloadImage(padded);

    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    return texture;
}

/* The cover stamps on the tilesheet - every tree and every crag - are drawn
   sitting on a patch of the base grass, so each one carried its own little
   green square around with it. That is invisible on a green island and glaring
   everywhere else. Clearing that green to transparent lets whatever ground the
   biome actually painted show through instead, which is what makes a pine in
   the snow look like a pine in the snow rather than a pine in a lawn. */
static void KeyOutTileBackground(Image *img, int cell, int cols,
                                 const int *cells, int count)
{
    /* the three greens the base grass is made of, plus the sand tone the sheet
       bleeds in along the cell borders */
    static const Color bg[] = {
        { 182, 213,  60, 255 },
        { 166, 196,  46, 255 },
        { 201, 231,  80, 255 },
        { 203, 129,  94, 255 },
    };
    const int stride = cell + ATLAS_PAD * 2;

    for (int n = 0; n < count; n++) {
        int c = cells[n] % cols;
        int r = cells[n] / cols;

        for (int y = -ATLAS_PAD; y < cell + ATLAS_PAD; y++) {
            for (int x = -ATLAS_PAD; x < cell + ATLAS_PAD; x++) {
                int px = c * stride + ATLAS_PAD + x;
                int py = r * stride + ATLAS_PAD + y;
                if (px < 0 || py < 0 || px >= img->width || py >= img->height) continue;

                Color at = GetImageColor(*img, px, py);
                for (int i = 0; i < (int)(sizeof bg / sizeof bg[0]); i++) {
                    if (at.r == bg[i].r && at.g == bg[i].g && at.b == bg[i].b) {
                        ImageDrawPixel(img, px, py, BLANK);
                        break;
                    }
                }
            }
        }
    }
}

/* --------------------------------------------------------------------------
 *  Loading
 * ----------------------------------------------------------------------- */
void AssetsLoad(void)
{
    assets.ships = LoadAtlas("sprites/ships.png", SHIP_CELL, SHIP_COLS, 6);
    assets.tiles = LoadTilesAtlas();

    /* Departure Mono. Loaded at 14 so the rasterised em lands on the font's
       native 11px grid (see the note on Assets.font). */
    assets.font = LoadFontEx(AssetPath("fonts/departure.otf"), 14, NULL, 0);
    SetTextureFilter(assets.font.texture, TEXTURE_FILTER_POINT);

    assets.shoot        = LoadSound(AssetPath("audio/shoot.ogg"));
    assets.shootHeavy   = LoadSound(AssetPath("audio/shoot_heavy.ogg"));
    assets.enemyShoot   = LoadSound(AssetPath("audio/enemy_shoot.ogg"));
    assets.explosion    = LoadSound(AssetPath("audio/explosion.ogg"));
    assets.explosionBig = LoadSound(AssetPath("audio/explosion_big.ogg"));
    assets.hit          = LoadSound(AssetPath("audio/hit.ogg"));
    assets.pickup       = LoadSound(AssetPath("audio/pickup.ogg"));
    assets.shield       = LoadSound(AssetPath("audio/shield.ogg"));
    assets.engine       = LoadSound(AssetPath("audio/engine.ogg"));

    TraceLog(LOG_INFO, "ASSETS: loaded from '%s'", g_assetRoot[0] ? g_assetRoot : ".");
}

void AssetsUnload(void)
{
    UnloadTexture(assets.ships);
    UnloadTexture(assets.tiles);
    UnloadFont(assets.font);

    UnloadSound(assets.shoot);
    UnloadSound(assets.shootHeavy);
    UnloadSound(assets.enemyShoot);
    UnloadSound(assets.explosion);
    UnloadSound(assets.explosionBig);
    UnloadSound(assets.hit);
    UnloadSound(assets.pickup);
    UnloadSound(assets.shield);
    UnloadSound(assets.engine);
}

/* Fire a sound with a little random detune so repeated shots do not turn
   into a machine-gun of identical samples. */
void PlaySfx(Sound s, float volume, float pitch)
{
    if (s.frameCount == 0) return;
    SetSoundVolume(s, volume);
    SetSoundPitch(s, pitch * (0.94f + GetRandomValue(0, 12) / 100.0f));
    PlaySound(s);
}

/* --------------------------------------------------------------------------
 *  Drawing
 * ----------------------------------------------------------------------- */
void DrawCell(Texture2D atlas, int idx, int cols, int cell, Vector2 pos,
              float rotation, float scale, Color tint)
{
    if (atlas.id == 0 || idx < 0) return;

    Rectangle src = AtlasSource(idx, cols, cell);
    float size = cell * scale;

    /* Keep the quad's top-left corner on a whole pixel, and step the rotation
       so a turning sprite keeps the same pixel pattern between frames. */
    Rectangle dst = { SnapPix(pos.x - size * 0.5f) + size * 0.5f,
                      SnapPix(pos.y - size * 0.5f) + size * 0.5f, size, size };
    Vector2 origin = { size * 0.5f, size * 0.5f };

    DrawTexturePro(atlas, src, dst, origin, SnapAngle(rotation), tint);
}

void DrawShip(int idx, Vector2 pos, float rotDeg, float scale, Color tint)
{
    DrawCell(assets.ships, idx, SHIP_COLS, SHIP_CELL, pos, rotDeg, scale, tint);
}

void DrawTileSprite(int idx, Vector2 pos, float rotDeg, float scale, Color tint)
{
    DrawCell(assets.tiles, idx, TILE_COLS, TILE_CELL, pos, rotDeg, scale, tint);
}

/* --------------------------------------------------------------------------
 *  Pixel-perfect text
 *
 *  raylib's DrawTextEx walks the string on a float cursor, so a glyph can land
 *  on x = 41.7 and get sampled half a texel off - which, with a point-filtered
 *  pixel font, eats a column of pixels. The routines below do the same walk
 *  but keep every glyph, and every advance, on whole pixels.
 * ----------------------------------------------------------------------- */
float TextWidthPix(Font font, const char *text, int scale)
{
    if (!text || font.texture.id == 0) return 0.0f;

    /* MeasureTextEx uses the same advance rules, so it agrees with the walk
       below as long as the spacing is zero. */
    return MeasureTextEx(font, text, (float)font.baseSize * (float)scale, 0.0f).x;
}

void DrawTextPix(Font font, const char *text, Vector2 at, int scale, Color tint)
{
    if (!text || font.texture.id == 0 || scale < 1) return;

    Vector2 pen = V2(floorf(at.x), floorf(at.y));
    const float s = (float)scale;
    int cpSize = 0;

    for (const char *p = text; *p != '\0'; ) {
        int cp = GetCodepointNext(p, &cpSize);
        p += cpSize;
        if (cp == '\n') { pen.x = floorf(at.x); pen.y += (float)font.baseSize * s; continue; }

        int idx = GetGlyphIndex(font, cp);
        if (idx < 0) continue;

        GlyphInfo  gi  = font.glyphs[idx];
        Rectangle  rec = font.recs[idx];

        if (rec.width > 0.0f && rec.height > 0.0f) {
            Rectangle dst = {
                pen.x + floorf((float)gi.offsetX * s),
                pen.y + floorf((float)gi.offsetY * s),
                floorf(rec.width * s),
                floorf(rec.height * s),
            };
            DrawTexturePro(font.texture, rec, dst, V2(0, 0), 0.0f, tint);
        }

        float adv = (gi.advanceX == 0) ? (rec.width + 1.0f) : (float)gi.advanceX;
        pen.x += floorf(adv * s);
    }
}

void DrawTextPixCentered(Font font, const char *text, float y, int scale, Color tint)
{
    float w = TextWidthPix(font, text, scale);
    DrawTextPix(font, text, V2(floorf((RENDER_W - w) * 0.5f), y), scale, tint);
}

void DrawTextPixShadow(Font font, const char *text, Vector2 at, int scale, Color tint)
{
    Vector2 snapped = V2(floorf(at.x), floorf(at.y));
    DrawTextPix(font, text, V2Add(snapped, V2(1.0f, 1.0f)), scale,
                (Color){ 10, 16, 22, (unsigned char)fminf(220.0f, (float)tint.a) });
    DrawTextPix(font, text, snapped, scale, tint);
}
