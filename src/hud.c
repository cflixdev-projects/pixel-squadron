/*****************************************************************************
 *  hud.c - the in-game overlay, the title card and the end screens.
 *
 *  The pack ships a full 0-9 digit set as tiles, so numbers in the HUD are
 *  drawn from the very same sheet as the ships; everything else uses Kenney
 *  Pixel, drawn through the pixel-snapped helpers in assets.c. Using the
 *  font's own native sizes is what keeps the lettering from looking chewed.
 *****************************************************************************/
#include "game.h"

#include <math.h>
#include <stdio.h>

/* --------------------------------------------------------------------------
 *  Pixel text helpers
 * ----------------------------------------------------------------------- */

/* One loaded weapon crate on the HUD: what it is called, how much of it is
   left, and the colour it is known by everywhere else. */
typedef struct {
    const char *label;
    float       frac;
    Color       color;
} WeaponChip;

/* Advance between glyph cells. The digits carry a little transparent padding
   around the actual glyph, so 12px reads tighter than the raw 16px cell. */
#define GLYPH_ADV 12.0f

static int DigitTile(int digit)
{
    return (digit < 5) ? TILE_DIGIT_0 + digit
                       : TILE_DIGIT_5 + (digit - 5);
}

/* Blit one 16x16 tile with its top-left corner at `at`. */
static void DrawGlyph(int tileIndex, Vector2 at, float scale, Color tint)
{
    Rectangle src = AtlasSource(tileIndex, TILE_COLS, TILE_CELL);
    Rectangle dst = { at.x, at.y, TILE_CELL * scale, TILE_CELL * scale };
    DrawTexturePro(assets.tiles, src, dst, V2(0.0f, 0.0f), 0.0f, tint);
}

void DrawPixelNumber(long value, Vector2 pos, int digits, float scale, Color tint)
{
    if (value < 0) value = 0;

    for (int i = digits - 1; i >= 0; i--) {
        DrawGlyph(DigitTile((int)(value % 10)),
                  V2(pos.x + GLYPH_ADV * scale * (float)i, pos.y),
                  scale, tint);
        value /= 10;
    }
}

static void DrawTextCentered(const char *text, float y, int scale, Font font, Color color)
{
    float w = TextWidthPix(font, text, scale);
    float at = floorf((RENDER_W - w) * 0.5f);
    DrawTextPixShadow(font, text, V2(at, y), scale, color);
}

/* A dark rounded panel with a cyan hairline - used for every HUD cluster. */
static void DrawPanel(Rectangle r)
{
    DrawRectangleRec(r, COL_UI_BG);
    DrawRectangleLinesEx(r, 1.0f, (Color){ 126, 226, 250, 120 });
    DrawRectangle((int)r.x + 1, (int)r.y + 1, (int)r.width - 2, 1,
                  (Color){ 223, 246, 245, 40 });
}

/* --------------------------------------------------------------------------
 *  In-game overlay
 * ----------------------------------------------------------------------- */
static Color HealthColor(float ratio)
{
    if (ratio > 0.6f) return (Color){ 146, 220, 90, 255 };
    if (ratio > 0.3f) return (Color){ 255, 200, 80, 255 };
    return COL_HP;
}

/* Draw a small bar that drains over `t` seconds. */
static void DrawTimerBar(Rectangle r, float ratio, Color fill)
{
    ratio = Clamp(ratio, 0.0f, 1.0f);
    DrawRectangleRec(r, (Color){ 12, 20, 28, 200 });
    fill.a = 230;
    DrawRectangleRec((Rectangle){ r.x, r.y, r.width * ratio, r.height }, fill);
    DrawRectangleLinesEx(r, 1.0f, (Color){ 223, 246, 245, 90 });
}

/* The wing badge: who they are, what they fly, what state they are in. */
static void DrawWingmanRow(int i, float y)
{
    Ally  *a  = &g.allies[i];
    const AllyProfile *pr = AllyProfileOf(a->role);

    /* ship icon in their own colours, washed out while they are down */
    Color hull = a->alive ? pr->hull : (Color){ 62, 78, 96, 255 };
    DrawShip(pr->ship, V2(17.0f, y + 5.0f), ART_FORWARD, 0.4f, hull);

    Color name = a->alive ? pr->text : (Color){ 74, 92, 110, 255 };
    DrawTextPix(assets.font, pr->name, V2(27.0f, y), 1, name);

    /* hull bar (or a rebuild timer while they are down) */
    Rectangle bar = { 63.0f, y + 2.0f, 40.0f, 5.0f };
    DrawRectangle((int)bar.x, (int)bar.y, (int)bar.width, (int)bar.height,
                  (Color){ 12, 20, 28, 255 });

    if (a->alive) {
        float ratio = Clamp(a->hp / a->maxHp, 0.0f, 1.0f);
        DrawRectangle((int)bar.x, (int)bar.y, (int)(bar.width * ratio), (int)bar.height,
                      pr->hull);
    } else {
        float ratio = Clamp(1.0f - a->respawnTimer / ALLY_RESPAWN, 0.0f, 1.0f);
        DrawRectangle((int)bar.x, (int)bar.y, (int)(bar.width * ratio), (int)bar.height,
                      (Color){ 74, 92, 110, 255 });
    }
    DrawRectangleLinesEx(bar, 1.0f, (Color){ 223, 246, 245, 70 });

    /* special-weapon pip: lit when the special is charged and ready */
    float ready = a->alive
                ? Clamp(1.0f - a->specialTimer / pr->cooldown, 0.0f, 1.0f)
                : 0.0f;

    Rectangle pip = { 107.0f, y + 2.0f, 5.0f, 5.0f };
    DrawRectangle((int)pip.x, (int)pip.y, (int)pip.width, (int)pip.height,
                  (Color){ 12, 20, 28, 255 });
    if (ready >= 0.999f) {
        DrawRectangle((int)pip.x, (int)pip.y, 5, 5, (Color){ 255, 236, 159, 255 });
    } else if (ready > 0.0f) {
        DrawRectangle((int)pip.x, (int)pip.y, 5, (int)(5.0f * ready),
                      (Color){ 126, 226, 250, 150 });
    }
    DrawRectangleLinesEx(pip, 1.0f, (Color){ 223, 246, 245, 70 });

    /* the move itself, named so it is obvious that everyone has one. It
       brightens when it is charged, and flashes when it actually fires. */
    float namePulse = a->alive ? ready : 0.0f;
    if (a->specialFlash > 0.0f) namePulse = 1.0f;
    Color spec = a->alive
               ? (Color){ (unsigned char)(126.0f + 129.0f * namePulse),
                          (unsigned char)(226.0f + 10.0f * namePulse),
                          (unsigned char)(250.0f - 5.0f * namePulse),
                          (unsigned char)(120.0f + 135.0f * namePulse) }
               : (Color){ 74, 92, 110, 255 };
    DrawTextPix(assets.font, pr->special, V2(118.0f, y), 1, spec);
}

void HudDraw(Camera2D cam)
{
    Player *p = &g.player;
    float hpRatio = Clamp(p->hp / PLAYER_MAX_HP, 0.0f, 1.0f);

    /* --- hull + power, top left --------------------------------------- */
    DrawPanel((Rectangle){ 6, 6, 96, 26 });
    DrawTextPixShadow(assets.font, "HULL", V2(10, 9), 1, COL_SHALLOW);
    DrawRectangle(8, 20, 92, 8, (Color){ 12, 20, 28, 255 });
    DrawRectangle(9, 21, (int)(90.0f * hpRatio), 6, HealthColor(hpRatio));
    DrawRectangleLinesEx((Rectangle){ 8, 20, 92, 8 }, 1.0f, COL_INK);

    /* weapon level pips */
    for (int i = 0; i < 3; i++) {
        bool lit = (p->weaponLevel > i);
        Color c  = lit ? (Color){ 255, 236, 159, 255 } : (Color){ 60, 76, 92, 255 };
        DrawRectangle(10 + i * 7, 30, 5, 3, c);
    }
    if (p->powerTimer > 0.0f) {
        DrawRectangle(24, 30, 5, 3, (Color){ 255, 130, 140, 255 });
    }

    /* --- the squadron, below the hull panel --------------------------- */
    /* Taller rows with a clear gap under the heading: at the old 11px pitch
       the first name ran straight into the "WINGMEN" title. */
    DrawPanel((Rectangle){ 6, 38, 150, 64 });
    DrawTextPixShadow(assets.font, "WINGMEN", V2(10, 42), 1, COL_SHALLOW);

    for (int i = 0; i < MAX_ALLIES; i++)
        DrawWingmanRow(i, 58.0f + (float)i * 14.0f);

    /* --- score, top right --------------------------------------------- */
    /* 7 digits on a 12px pitch span 88px; give the panel 8px of padding on
       each side, sit it 8px off the window edge, and leave a clear gap under
       the label so the two do not look squashed together. */
    const int scoreW = 104;
    const int scoreX = RENDER_W - 8 - scoreW;
    DrawPanel((Rectangle){ scoreX, 6, scoreW, 36 });
    DrawTextPixShadow(assets.font, "SCORE", V2(scoreX + 8, 10), 1, COL_SHALLOW);
    DrawPixelNumber((long)g.score, V2((float)(scoreX + 8), 24), 7, 1.0f, WHITE);

    /* --- wave, top centre --------------------------------------------- */
    char waveLabel[24];
    snprintf(waveLabel, sizeof waveLabel, "WAVE %d", g.wave);
    float waveW = TextWidthPix(assets.font, waveLabel, 1);
    DrawPanel((Rectangle){ floorf((RENDER_W - waveW) * 0.5f) - 8.0f, 6, waveW + 16.0f, 20 });
    DrawTextPixShadow(assets.font, waveLabel,
                      V2(floorf((RENDER_W - waveW) * 0.5f), 10), 1, COL_FOAM);

    /* --- and which country we are over, just under it ------------------ */
    const char *biome = ArenaBiomeNameAt(p->pos);
    float biomeW = TextWidthPix(assets.font, biome, 1);
    DrawTextPixShadow(assets.font, biome, V2(floorf((RENDER_W - biomeW) * 0.5f), 30), 1,
                      (Color){ 168, 196, 204, 200 });

    /* --- boost, recall and ultimate meters, bottom left --------------- */
    char rallyLabel[32], specialLabel[32];
    snprintf(rallyLabel,   sizeof rallyLabel,   "RALLY Q/%s",   BindName(BIND_RECALL));
    snprintf(specialLabel, sizeof specialLabel, "BLAST E/%s",   BindName(BIND_SPECIAL));

    /* The pad button in the label can be as long as two characters, so the
       three meters are laid out from their measured widths rather than from
       guesses - otherwise the labels collide on a narrow window. */
    const char *boostLabel = "BOOST";
    float bx = 8.0f;
    float bw = fmaxf(72.0f, TextWidthPix(assets.font, boostLabel,    1));
    float rx = bx + bw + 12.0f;
    float rw = fmaxf(72.0f, TextWidthPix(assets.font, rallyLabel,    1));
    float sx = rx + rw + 12.0f;
    float sw = fmaxf(72.0f, TextWidthPix(assets.font, specialLabel,  1));

    float y = RENDER_H - 30.0f;

    /* Boost is a core control, so its meter is always on. */
    DrawTextPixShadow(assets.font, boostLabel, V2(bx, y), 1, COL_SHALLOW);
    DrawTimerBar((Rectangle){ bx, y + 13, bw, 5 }, p->boostFuel / PLAYER_BOOST_FUEL,
                 (Color){ 255, 236, 159, 230 });

    /* The recall radio needs a moment to recharge between calls. */
    bool rallyReady = (g.rallyCool <= 0.0f);
    DrawTextPixShadow(assets.font, rallyLabel, V2(rx, y), 1,
                      rallyReady ? COL_ALLY_UI : (Color){ 96, 116, 134, 255 });
    DrawTimerBar((Rectangle){ rx, y + 13, rw, 5 },
                 1.0f - g.rallyCool / RALLY_COOLDOWN,
                 rallyReady ? (Color){ 126, 226, 250, 230 }
                            : (Color){ 90, 122, 152, 220 });

    /* ...and the player's own ultimate. */
    bool blastReady = (p->specialTimer <= 0.0f);
    DrawTextPixShadow(assets.font, specialLabel, V2(sx, y), 1,
                      blastReady ? (Color){ 255, 186, 92, 255 }
                                 : (Color){ 96, 116, 134, 255 });
    DrawTimerBar((Rectangle){ sx, y + 13, sw, 5 },
                 1.0f - p->specialTimer / SPECIAL_COOLDOWN,
                 blastReady ? (Color){ 255, 186, 92, 230 }
                            : (Color){ 90, 122, 152, 220 });

    /* --- what the guns are loaded with --------------------------------
       Every crate that is running gets a chip down at the bottom, so the
       player can see at a glance what is loaded and how long is left. */
    {
        WeaponChip chip[5];
        int n = 0;

        if (p->powerTimer  > 0.0f) chip[n++] = (WeaponChip){ "RAPID",  p->powerTimer  / POWER_DURATION,    (Color){ 255, 200,  80, 255 } };
        if (p->shieldTimer > 0.0f) chip[n++] = (WeaponChip){ "SHIELD", p->shieldTimer / SHIELD_DURATION,   (Color){ 126, 226, 250, 255 } };
        if (p->quadTimer   > 0.0f) chip[n++] = (WeaponChip){ "QUAD",   p->quadTimer   / PLAYER_QUAD_TIME,   (Color){ 198, 150, 255, 255 } };
        if (p->spreadTimer > 0.0f) chip[n++] = (WeaponChip){ "SPREAD", p->spreadTimer / PLAYER_SPREAD_TIME, (Color){ 255, 186,  92, 255 } };
        if (p->homingTimer > 0.0f) chip[n++] = (WeaponChip){ "SEEKER", p->homingTimer / PLAYER_HOMING_TIME, (Color){ 140, 235, 150, 255 } };

        if (n > 0) {
            const float cw = 54.0f, gap = 4.0f;
            float total = (float)n * cw + (float)(n - 1) * gap;
            float cx    = floorf((RENDER_W - total) * 0.5f);
            float cy    = RENDER_H - 62.0f;

            for (int i = 0; i < n; i++) {
                DrawRectangle((int)cx, (int)cy, (int)cw, 15, (Color){ 12, 20, 28, 200 });
                DrawRectangle((int)cx, (int)cy, (int)cw, 1, chip[i].color);
                DrawTextPix(assets.font, chip[i].label, V2(cx + 4.0f, cy + 3.0f), 1, chip[i].color);
                DrawTimerBar((Rectangle){ cx, cy + 15.0f, cw, 3 }, chip[i].frac, chip[i].color);
                cx += cw + gap;
            }
        }
    }

    /* --- the wing's rearm banner --------------------------------------
       Every other wave the wing gets measurably better. That is worth a
       shout, because otherwise it just looks like they stopped dying. */
    if (g.levelUpFlash > 0.0f) {
        char up[40];
        snprintf(up, sizeof up, "WINGMEN LEVEL %d", g.allyLevel + 1);

        /* fade in over the first third of a second, hold, then fade out over
           the last half second of the three */
        float k = Clamp(g.levelUpFlash / 3.0f, 0.0f, 1.0f);
        float a = (k > 0.9f) ? (1.0f - k) / 0.1f : Clamp(k * 6.0f, 0.0f, 1.0f);
        a = Clamp(a, 0.0f, 1.0f);
        float w = TextWidthPix(assets.font, up, 1);

        DrawRectangle(0, 42, RENDER_W, 19, (Color){ 255, 186, 92, (unsigned char)(95.0f * a) });
        DrawRectangle(0, 42, RENDER_W, 1,  (Color){ 255, 236, 159, (unsigned char)(230.0f * a) });
        DrawRectangle(0, 60, RENDER_W, 1,  (Color){ 255, 236, 159, (unsigned char)(230.0f * a) });

        Color c = { 255, 236, 159, (unsigned char)(255.0f * a) };
        DrawTextPixShadow(assets.font, up, V2(floorf((RENDER_W - w) * 0.5f), 46), 1, c);
    }

    /* --- floating message --------------------------------------------- */
    if (g.messageTimer > 0.0f && g.message[0]) {
        float a = Clamp(g.messageTimer / 0.6f, 0.0f, 1.0f);
        Color c = COL_FOAM;
        c.a = (unsigned char)(255.0f * a);
        DrawTextCentered(g.message, RENDER_H - 46.0f, 1, assets.font, c);
    }

    /* --- off-screen enemy markers ------------------------------------- */
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g.enemies[i];
        if (!e->alive) continue;

        Vector2 screen = GetWorldToScreen2D(e->pos, cam);
        if (screen.x > 12 && screen.x < RENDER_W - 12 &&
            screen.y > 12 && screen.y < RENDER_H - 12) continue;

        Vector2 edge = V2(
            Clamp(screen.x, 8.0f, RENDER_W - 8.0f),
            Clamp(screen.y, 8.0f, RENDER_H - 8.0f));
        DrawRectangle((int)edge.x - 1, (int)edge.y - 1, 3, 3,
                      (Color){ 255, 130, 140, 170 });
    }

    /* --- supply crates, marked on the edge while they are off screen --- */
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *p = &g.pickups[i];
        if (!p->alive) continue;

        Vector2 sp = GetWorldToScreen2D(p->pos, cam);
        if (sp.x > 10 && sp.x < RENDER_W - 10 &&
            sp.y > 10 && sp.y < RENDER_H - 10) continue;

        Color c = (p->kind == PICK_POWER)   ? (Color){ 255, 236, 159, 210 }
                : (p->kind == PICK_HEALTH)  ? (Color){ 140, 245, 170, 210 }
                : (p->kind == PICK_QUAD)    ? (Color){ 198, 150, 255, 210 }
                : (p->kind == PICK_SPREAD)  ? (Color){ 255, 186,  92, 210 }
                : (p->kind == PICK_HOMING)  ? (Color){ 140, 235, 150, 210 }
                                            : (Color){ 126, 226, 250, 210 };

        Vector2 edge = V2(Clamp(sp.x, 6.0f, RENDER_W - 6.0f),
                          Clamp(sp.y, 6.0f, RENDER_H - 6.0f));
        DrawRectangle((int)edge.x - 2, (int)edge.y - 2, 4, 4, c);
    }

    /* --- wingmen in trouble, marked on the edge of the screen ---------- */
    /* If one of them is shouting for help we put a big red arrow on the map
       pointing at them, so the player can go and pull them out. */
    for (int i = 0; i < MAX_ALLIES; i++) {
        Ally *a = &g.allies[i];
        if (!a->alive || a->distress <= 0.0f) continue;

        const AllyProfile *pr = AllyProfileOf(a->role);
        Vector2 sp = GetWorldToScreen2D(a->pos, cam);

        bool offscreen = (sp.x < 18.0f || sp.x > RENDER_W - 18.0f ||
                          sp.y < 18.0f || sp.y > RENDER_H - 18.0f);
        if (!offscreen) continue;

        Vector2 edge = V2(Clamp(sp.x, 16.0f, RENDER_W - 16.0f),
                          Clamp(sp.y, 16.0f, RENDER_H - 16.0f));

        float ang   = atan2f(sp.y - edge.y, sp.x - edge.x);
        float pulse = 0.5f + 0.5f * sinf(g.time * 9.0f);

        Color c = { 255, 74, 74, (unsigned char)(180.0f + 75.0f * pulse) };

        Vector2 tip = V2(edge.x + cosf(ang) * 11.0f, edge.y + sinf(ang) * 11.0f);
        Vector2 l   = V2(edge.x + cosf(ang + 2.5f) * 8.0f, edge.y + sinf(ang + 2.5f) * 8.0f);
        Vector2 r   = V2(edge.x + cosf(ang - 2.5f) * 8.0f, edge.y + sinf(ang - 2.5f) * 8.0f);

        DrawTriangle(tip, l, r, c);
        DrawTriangle(r, l, tip, c);   /* both windings, so it always fills */

        float w = TextWidthPix(assets.font, pr->name, 1);
        DrawTextPix(assets.font, pr->name,
                    V2(floorf(edge.x - w * 0.5f), floorf(edge.y + 12.0f)), 1, c);
    }

    /* --- reticle -------------------------------------------------------
       The crosshair is always up, whichever hand is driving. On the mouse it
       sits on the pointer; on the pad it rides out along the aim vector, so
       there is still something on screen saying where the guns point - and
       it is drawn in the colour of whatever the guns are loaded with. */
    if (g.player.alive) {
        Vector2 aimAt = input.reticle;

        if (input.usingGamepad) {
            Vector2 ray = (V2Len(input.aim) > 0.01f) ? input.aim
                                                      : V2(cosf(p->angle), sinf(p->angle));
            aimAt = WorldToScreen(V2Add(p->pos, V2Scale(ray, 54.0f)));
        }

        /* Warm halo + star outburst, kept compact so it reads as a sight. */
        {
            float pulse = 0.5f + 0.5f * sinf(g.timer * 4.0f);
            /* soft warm glow */
            DrawCircleV(aimAt, 9.0f + pulse * 2.0f, (Color){ 245, 200, 140, 30 });
            DrawCircleV(aimAt, 6.0f + pulse * 1.0f, (Color){ 245, 200, 140, 50 });
            /* star outburst: 4 long rays + 4 short diagonals */
            for (int i = 0; i < 8; i++) {
                float ang = (float)i * 45.0f * DEG2RAD;
                float len = (i % 2 == 0) ? 10.0f : 6.0f;
                Vector2 tip = V2Add(aimAt, V2(cosf(ang) * len, sinf(ang) * len));
                DrawLineEx(aimAt, tip, 1.2f, (Color){ 245, 200, 140, (unsigned char)(140 + 60 * pulse) });
            }
            /* crosshair ticks */
            DrawLineEx(V2(aimAt.x - 4, aimAt.y), V2(aimAt.x - 10, aimAt.y), 1.0f, (Color){ 255, 240, 210, 180 });
            DrawLineEx(V2(aimAt.x + 4, aimAt.y), V2(aimAt.x + 10, aimAt.y), 1.0f, (Color){ 255, 240, 210, 180 });
            DrawLineEx(V2(aimAt.x, aimAt.y - 4), V2(aimAt.x, aimAt.y - 10), 1.0f, (Color){ 255, 240, 210, 180 });
            DrawLineEx(V2(aimAt.x, aimAt.y + 4), V2(aimAt.x, aimAt.y + 10), 1.0f, (Color){ 255, 240, 210, 180 });
            /* bright red center dot */
            DrawCircleV(aimAt, 2.0f, (Color){ 255, 80, 60, 240 });
            DrawCircleV(aimAt, 1.0f, (Color){ 255, 200, 180, 255 });
        }
    }

    /* --- fps counter, bottom right, with a black box behind it ---------- */
    {
        char fpsBuf[16];
        snprintf(fpsBuf, sizeof fpsBuf, "%d FPS", GetFPS());
        float fw = TextWidthPix(assets.font, fpsBuf, 1);
        DrawRectangle((int)(RENDER_W - fw - 8.0f), (int)(RENDER_H - 13.0f),
                      (int)(fw + 6.0f), 11, (Color){ 0, 0, 0, 160 });
        DrawTextPix(assets.font, fpsBuf, V2(RENDER_W - fw - 5.0f, RENDER_H - 11.0f), 1,
                    (Color){ 200, 220, 240, 220 });
    }

    /* --- damage vignette ---------------------------------------------- */
    if (g.damageFlash > 0.0f) {
        unsigned char a = (unsigned char)(90.0f * g.damageFlash);
        DrawRectangle(0, 0, RENDER_W, 6,  (Color){ 222, 75, 30, a });
        DrawRectangle(0, RENDER_H - 6, RENDER_W, 6, (Color){ 222, 75, 30, a });
        DrawRectangle(0, 0, 6, RENDER_H,  (Color){ 222, 75, 30, a });
        DrawRectangle(RENDER_W - 6, 0, 6, RENDER_H, (Color){ 222, 75, 30, a });
    }
}

/* --------------------------------------------------------------------------
 *  Title / attract card
 *
 *  Sits over the live arena pan, so the first thing anyone sees is the game
 *  itself. The whole block is measured from the middle of the screen at a
 *  fixed pitch, which keeps it centred on any window shape.
 * ----------------------------------------------------------------------- */
void HudDrawTitle(void)
{
    DrawRectangle(0, 0, RENDER_W, RENDER_H, (Color){ 12, 20, 28, 130 });

    const float block = 184.0f;                       /* total layout height */
    float top = floorf((RENDER_H - block) * 0.5f);
    if (top < 6.0f) top = 6.0f;

    /* subtle banner behind the title */
    DrawRectangle(0, (int)top - 8, RENDER_W, 56, (Color){ 12, 20, 28, 175 });
    DrawRectangle(0, (int)top - 8, RENDER_W, 1, (Color){ 126, 226, 250, 90 });
    DrawRectangle(0, (int)top + 47, RENDER_W, 1, (Color){ 126, 226, 250, 90 });

    DrawTextCentered("PIXEL SQUADRON", (int)top, 2, assets.font, COL_FOAM);
    DrawTextCentered("T W I N - S T I C K   A R E N A", (int)top + 30, 1,
                     assets.font, COL_SHALLOW);

    /* the squadron flies past, each in their own colours */
    for (int i = 0; i < MAX_ALLIES; i++) {
        const AllyProfile *pr = AllyProfileOf(i);
        float t = fmodf(g.timer * (26.0f + (float)i * 9.0f) + (float)i * 60.0f,
                        RENDER_W + 120.0f) - 60.0f;

        DrawShip(pr->ship, V2(t, top + 62.0f + (float)i * 15.0f),
                 ART_FORWARD + 4.0f, 0.5f, pr->hull);
    }

    /* who is riding with you, in their own colours */
    float w[ALLY_ROLE_COUNT];
    float total = 0.0f;
    for (int i = 0; i < ALLY_ROLE_COUNT; i++) {
        const AllyProfile *pr = AllyProfileOf(i);
        w[i] = TextWidthPix(assets.font, pr->name, 1);
        total += w[i] + 10.0f;
    }
    total -= 10.0f;

    float x = floorf((RENDER_W - total) * 0.5f);
    for (int i = 0; i < ALLY_ROLE_COUNT; i++) {
        const AllyProfile *pr = AllyProfileOf(i);
        DrawTextPix(assets.font, pr->name, V2(x, top + 112.0f), 1, pr->text);
        x += w[i];
        if (i < ALLY_ROLE_COUNT - 1) {
            DrawTextPix(assets.font, "-", V2(x + 4.0f, top + 112.0f), 1,
                        (Color){ 126, 226, 250, 140 });
            x += 14.0f;
        }
    }

    /* blink on a slow cycle rather than a hard flicker */
    float blink = 0.55f + 0.45f * sinf(g.timer * 4.0f);
    Color prompt = COL_FOAM;
    prompt.a = (unsigned char)(120.0f + 135.0f * blink);
    DrawTextCentered("PRESS ENTER OR [A] TO FLY", (int)top + 132, 1,
                     assets.font, prompt);

    DrawTextCentered("WASD / LEFT STICK  -  MOVE", (int)top + 150, 1, assets.font,
                     (Color){ 126, 226, 250, 190 });
    DrawTextCentered("MOUSE / RIGHT STICK  -  AIM", (int)top + 162, 1, assets.font,
                     (Color){ 126, 226, 250, 190 });
    DrawTextCentered("LMB / RT - FIRE   SHIFT / B - BOOST   Q / X - RECALL",
                     (int)top + 174, 1, assets.font, (Color){ 126, 226, 250, 190 });
    DrawTextCentered("E / Y - BLAST   C - PAD CONTROLS",
                     (int)top + 186, 1, assets.font, (Color){ 126, 226, 250, 190 });

    if (g.highScore > 0) {
        char best[48];
        snprintf(best, sizeof best, "BEST  %07d", g.highScore);
        DrawTextCentered(best, 10, 1, assets.font, (Color){ 255, 200, 80, 220 });
    }
}

/* --------------------------------------------------------------------------
 *  Game over
 * ----------------------------------------------------------------------- */
void HudDrawGameOver(void)
{
    DrawRectangle(0, 0, RENDER_W, RENDER_H, (Color){ 12, 20, 28, 190 });

    /* The block is laid out around the middle of the screen and every line is
       measured against a fixed pitch, so the last entry of the report always
       sits a clear 15px above the bottom of the panel - on any window shape.
       The record tag lives on the SCORE line rather than below the panel so it
       cannot push the prompts off the bottom. */
    float top = floorf(RENDER_H * 0.5f) - 88.0f;
    if (top < 6.0f) top = 6.0f;

    DrawTextCentered("SQUADRON DOWN", (int)top, 2, assets.font, COL_HP);

    const float panelW = 232.0f;
    const float panelH = 116.0f;
    const float panelX = floorf((RENDER_W - panelW) * 0.5f);
    const float panelY = top + 30.0f;
    const float textX  = panelX + 16.0f;

    DrawPanel((Rectangle){ panelX, panelY, panelW, panelH });

    DrawTextPixShadow(assets.font, "SCORE", V2(textX, panelY + 10), 1, COL_SHALLOW);

    if (g.score >= g.highScore && g.score > 0) {
        float blink = 0.6f + 0.4f * sinf(g.timer * 6.0f);
        Color c = { 255, 200, 80, (unsigned char)(255.0f * blink) };
        const char *tag = "NEW RECORD";
        DrawTextPixShadow(assets.font, tag,
                          V2(panelX + panelW - 16.0f - TextWidthPix(assets.font, tag, 1),
                             panelY + 10), 1, c);
    }

    DrawPixelNumber((long)g.score, V2(textX, panelY + 26), 7, 1.0f, WHITE);

    char line[64];

    snprintf(line, sizeof line, "WAVES SURVIVED  %d", g.wave - 1);
    DrawTextPixShadow(assets.font, line, V2(textX, panelY + 46), 1, COL_FOAM);

    snprintf(line, sizeof line, "KILLS           %d", g.kills);
    DrawTextPixShadow(assets.font, line, V2(textX, panelY + 60), 1, COL_FOAM);

    snprintf(line, sizeof line, "CRITICAL HITS   %d", g.crits);
    DrawTextPixShadow(assets.font, line, V2(textX, panelY + 74), 1, COL_FOAM);

    snprintf(line, sizeof line, "TIME         %02d:%02d",
             (int)g.survived / 60, (int)g.survived % 60);
    DrawTextPixShadow(assets.font, line, V2(textX, panelY + 88), 1, COL_FOAM);

    float below = panelY + panelH + 8.0f;

    float blink = 0.55f + 0.45f * sinf(g.timer * 4.0f);
    Color prompt = COL_FOAM;
    prompt.a = (unsigned char)(120.0f + 135.0f * blink);
    DrawTextCentered("PRESS ENTER OR [A] TO FLY AGAIN", (int)below, 1, assets.font, prompt);
    DrawTextCentered("ESC  -  QUIT", (int)below + 15.0f, 1, assets.font,
                     (Color){ 126, 226, 250, 160 });
}

/* --------------------------------------------------------------------------
 *  Pause
 * ----------------------------------------------------------------------- */
void HudDrawPause(void)
{
    DrawRectangle(0, 0, RENDER_W, RENDER_H, (Color){ 12, 20, 28, 170 });

    int centre = (int)floorf(RENDER_H * 0.5f);

    if (RemapActive()) {
        /* --- the pad controls panel ----------------------------------- */
        static const char *const kLabel[BIND_COUNT] = {
            "FIRE", "BOOST", "RECALL", "SPECIAL"
        };

        DrawTextCentered("PAD CONTROLS", centre - 76, 2, assets.font, COL_FOAM);
        DrawTextCentered("LEFT STICK MOVE    RIGHT STICK AIM + SHOOT",
                         centre - 52, 1, assets.font, (Color){ 126, 226, 250, 170 });

        float x = floorf((float)RENDER_W * 0.5f) - 70.0f;
        float y = (float)centre - 30.0f;

        for (int i = 0; i < BIND_COUNT; i++) {
            bool sel = (i == RemapSelection());

            if (sel) {
                DrawRectangle((int)x - 10, (int)y - 4, 160, 15, (Color){ 255, 186, 92, 60 });
                DrawRectangle((int)x - 10, (int)y - 4, 2, 15, (Color){ 255, 236, 159, 255 });
            }

            char line[48];
            snprintf(line, sizeof line, "%-9s %s", kLabel[i], BindName(i));
            DrawTextPix(assets.font, line, V2(x, y), 1,
                        sel ? (Color){ 255, 236, 159, 255 } : COL_SHALLOW);

            y += 16.0f;
        }

        float blink = 0.55f + 0.45f * sinf(g.timer * 4.0f);
        Color prompt = COL_FOAM;
        prompt.a = (unsigned char)(120.0f + 135.0f * blink);

        if (RemapArmed()) {
            DrawTextCentered("PRESS A PAD BUTTON  -  ESC TO CANCEL",
                             centre + 44, 1, assets.font, prompt);
        } else {
            DrawTextCentered("W / S  -  CHOOSE    ENTER  -  REBIND",
                             centre + 44, 1, assets.font, prompt);
            DrawTextCentered("R  -  DEFAULTS      C / ESC / START  -  DONE",
                             centre + 58, 1, assets.font, (Color){ 126, 226, 250, 160 });
        }
        return;
    }

    DrawTextCentered("PAUSED", centre - 34, 2, assets.font, COL_FOAM);
    DrawTextCentered("P / ESC / START  -  RESUME", centre - 2, 1, assets.font, COL_SHALLOW);
    DrawTextCentered("WASD / LEFT STICK - MOVE    MOUSE / RIGHT STICK - AIM",
                     centre + 14, 1, assets.font, (Color){ 126, 226, 250, 160 });
    DrawTextCentered("LMB / RT - FIRE    SHIFT / B - BOOST",
                     centre + 28, 1, assets.font, (Color){ 126, 226, 250, 160 });
    DrawTextCentered("Q / X - RECALL    E / Y - BLAST    C - PAD CONTROLS",
                     centre + 42, 1, assets.font, (Color){ 126, 226, 250, 160 });
    DrawTextCentered("F - CYCLE FPS CAP (30/60/120/144/OFF)",
                      centre + 56, 1, assets.font, (Color){ 126, 226, 250, 120 });
}
