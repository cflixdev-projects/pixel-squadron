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
    const AllyProfile *pro = pr;
    float ready = a->alive
                ? Clamp(1.0f - a->specialTimer / pro->cooldown, 0.0f, 1.0f)
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
    DrawPanel((Rectangle){ 6, 38, 124, 64 });
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

    /* --- boost meter and active buffs, bottom left -------------------- */
    float y = RENDER_H - 30.0f;

    /* Boost is a core control, so its meter is always on. */
    DrawTextPixShadow(assets.font, "BOOST", V2(8, y), 1, COL_SHALLOW);
    DrawTimerBar((Rectangle){ 8, y + 13, 72, 5 }, p->boostFuel / PLAYER_BOOST_FUEL,
                 (Color){ 255, 236, 159, 230 });
    y -= 22.0f;

    if (p->powerTimer > 0.0f) {
        DrawTextPixShadow(assets.font, "RAPID FIRE", V2(8, y), 1,
                          (Color){ 255, 200, 80, 255 });
        DrawTimerBar((Rectangle){ 8, y + 13, 72, 5 }, p->powerTimer / 10.0f,
                     (Color){ 126, 226, 250, 230 });
        y -= 22.0f;
    }
    if (p->shieldTimer > 0.0f) {
        DrawTextPixShadow(assets.font, "SHIELD", V2(8, y), 1, COL_SHALLOW);
        DrawTimerBar((Rectangle){ 8, y + 13, 72, 5 }, p->shieldTimer / 6.0f,
                     (Color){ 126, 226, 250, 230 });
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

    /* --- reticle, only when the mouse is doing the aiming ------------- */
    if (!input.usingGamepad && g.player.alive) {
        Vector2 m = input.reticle;
        DrawRectangleLines((int)m.x - 5, (int)m.y - 5, 10, 10,
                           (Color){ 223, 246, 245, 120 });
        DrawRectangle((int)m.x, (int)m.y, 1, 1, (Color){ 255, 130, 140, 200 });
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
 *  Title card
 * ----------------------------------------------------------------------- */
void HudDrawTitle(void)
{
    DrawRectangle(0, 0, RENDER_W, RENDER_H, (Color){ 12, 20, 28, 130 });

    /* subtle banner behind the title */
    DrawRectangle(0, 44, RENDER_W, 68, (Color){ 12, 20, 28, 175 });
    DrawRectangle(0, 44, RENDER_W, 1, (Color){ 126, 226, 250, 90 });
    DrawRectangle(0, 111, RENDER_W, 1, (Color){ 126, 226, 250, 90 });

    DrawTextCentered("PIXEL SQUADRON", 50, 2, assets.font, COL_FOAM);
    DrawTextCentered("T W I N - S T I C K   A R E N A", 92, 1, assets.font, COL_SHALLOW);

    /* the squadron flies past, each in their own colours */
    for (int i = 0; i < MAX_ALLIES; i++) {
        const AllyProfile *pr = AllyProfileOf(i);
        float t = fmodf(g.timer * (26.0f + (float)i * 9.0f) + (float)i * 60.0f,
                        RENDER_W + 120.0f) - 60.0f;

        DrawShip(pr->ship, V2(t, 132.0f + (float)i * 16.0f), ART_FORWARD + 4.0f,
                 0.5f, pr->hull);
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
        DrawTextPix(assets.font, pr->name, V2(x, 186.0f), 1, pr->text);
        x += w[i];
        if (i < ALLY_ROLE_COUNT - 1) {
            DrawTextPix(assets.font, "-", V2(x + 4.0f, 186.0f), 1,
                        (Color){ 126, 226, 250, 140 });
            x += 14.0f;
        }
    }

    /* blink on a slow cycle rather than a hard flicker */
    float blink = 0.55f + 0.45f * sinf(g.timer * 4.0f);
    Color prompt = COL_FOAM;
    prompt.a = (unsigned char)(120.0f + 135.0f * blink);
    DrawTextCentered("PRESS ENTER OR [A] TO FLY", 206, 1, assets.font, prompt);

    DrawTextCentered("WASD / LEFT STICK  -  MOVE", 226, 1, assets.font,
                     (Color){ 126, 226, 250, 190 });
    DrawTextCentered("MOUSE / RIGHT STICK  -  AIM", 238, 1, assets.font,
                     (Color){ 126, 226, 250, 190 });
    DrawTextCentered("LMB / RT - FIRE    SHIFT - BOOST    F11 - FULLSCREEN",
                     250, 1, assets.font, (Color){ 126, 226, 250, 190 });

    if (g.highScore > 0) {
        char best[48];
        snprintf(best, sizeof best, "BEST  %07d", g.highScore);
        DrawTextCentered(best, 26, 1, assets.font, (Color){ 255, 200, 80, 220 });
    }
}

/* --------------------------------------------------------------------------
 *  Game over
 * ----------------------------------------------------------------------- */
void HudDrawGameOver(void)
{
    DrawRectangle(0, 0, RENDER_W, RENDER_H, (Color){ 12, 20, 28, 190 });

    DrawTextCentered("SQUADRON DOWN", 46, 2, assets.font, COL_HP);

    DrawPanel((Rectangle){ RENDER_W * 0.5f - 90, 92, 180, 82 });
    DrawTextPixShadow(assets.font, "SCORE", V2(RENDER_W * 0.5f - 80, 100), 1, COL_SHALLOW);
    DrawPixelNumber((long)g.score, V2(RENDER_W * 0.5f - 80, 112), 7, 1.0f, WHITE);

    char line[64];
    snprintf(line, sizeof line, "WAVES SURVIVED   %d", g.wave - 1);
    DrawTextPixShadow(assets.font, line, V2(RENDER_W * 0.5f - 80, 132), 1, COL_FOAM);

    snprintf(line, sizeof line, "KILLS            %d", g.kills);
    DrawTextPixShadow(assets.font, line, V2(RENDER_W * 0.5f - 80, 146), 1, COL_FOAM);

    snprintf(line, sizeof line, "TIME           %02d:%02d",
             (int)g.survived / 60, (int)g.survived % 60);
    DrawTextPixShadow(assets.font, line, V2(RENDER_W * 0.5f - 80, 160), 1, COL_FOAM);

    if (g.score >= g.highScore && g.score > 0) {
        float blink = 0.6f + 0.4f * sinf(g.timer * 6.0f);
        Color c = { 255, 200, 80, (unsigned char)(255.0f * blink) };
        DrawTextCentered("NEW RECORD", 182, 1, assets.font, c);
    }

    float blink = 0.55f + 0.45f * sinf(g.timer * 4.0f);
    Color prompt = COL_FOAM;
    prompt.a = (unsigned char)(120.0f + 135.0f * blink);
    DrawTextCentered("PRESS ENTER TO FLY AGAIN", 212, 1, assets.font, prompt);
    DrawTextCentered("ESC  -  QUIT", 228, 1, assets.font,
                     (Color){ 126, 226, 250, 160 });
}

/* --------------------------------------------------------------------------
 *  Pause
 * ----------------------------------------------------------------------- */
void HudDrawPause(void)
{
    DrawRectangle(0, 0, RENDER_W, RENDER_H, (Color){ 12, 20, 28, 170 });
    DrawTextCentered("PAUSED", 104, 2, assets.font, COL_FOAM);
    DrawTextCentered("P  -  RESUME", 146, 1, assets.font, COL_SHALLOW);
    DrawTextCentered("ESC  -  QUIT TO TITLE", 160, 1, assets.font,
                     (Color){ 126, 226, 250, 160 });
}
