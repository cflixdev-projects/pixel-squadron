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
   sampling at a cell edge can never reach the neighbouring sprite. */
static Image PadAtlas(Image sheet, int cell, int cols, int rows)
{
    const int stride = cell + ATLAS_PAD * 2;
    Image padded = GenImageColor(stride * cols, stride * rows, BLANK);

    for (int r = 0; r < rows; r++) {
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

    Image padded = PadAtlas(sheet, cell, cols, rows);
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

/* --------------------------------------------------------------------------
 *  Loading
 * ----------------------------------------------------------------------- */
void AssetsLoad(void)
{
    assets.ships = LoadAtlas("sprites/ships.png", SHIP_CELL, SHIP_COLS, 6);
    assets.tiles = LoadAtlas("sprites/tiles.png", TILE_CELL, TILE_COLS, 10);

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
