/*****************************************************************************
 *  main.c - bootstrap, input, camera, frame loop and the state machine.
 *
 *  Rendering goes through a 480x270 render texture which is then blitted to
 *  the window with nearest-neighbour filtering and an integer scale factor,
 *  which is what keeps the pixel art crisp at any window size.
 *****************************************************************************/
#include "game.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>   /* EM_ASM, for keeping the pad bindings across reloads */
#endif

Game       g     = {0};
InputState input = {0};
int fpsCap = 0;  /* 0 = uncapped */

Graphics gfx = { .quality = 1, .bloom = 1, .crt = 0, .vignette = 1,
                  .particles = 1, .smoke = 1, .motionBlur = 0 };
bool    g_autoTargetOn = false;   /* auto-shoot: a target is being tracked */
Vector2 g_autoTarget    = { 0, 0 };
int gfxPanelMode = 0;   /* pause-menu graphics screen */
int gfxPanelSel  = 0;
int pauseMenuSel = 0;

#define GFX_SETTINGS_FILE "settings.cfg"

/* Load quality settings from disk. Missing or junk values fall back to the
   defaults above; the game never hard-fails over a settings file. */
static void GfxSettingsLoad(void)
{
    char *text = LoadFileText(GFX_SETTINGS_FILE);
    if (!text) return;

    int v;
    if (sscanf(text, "quality %d", &v) == 1 && v >= 0 && v <= 2) gfx.quality = v;
    if (strstr(text, "bloom 1")            != NULL) gfx.bloom       = 1;
    else if (strstr(text, "bloom 0")       != NULL) gfx.bloom       = 0;
    /* the old hard scanline overlay grew into the CRT pass; honour both */
    if (strstr(text, "crt 1")              != NULL) gfx.crt         = 1;
    else if (strstr(text, "crt 0")         != NULL) gfx.crt         = 0;
    else if (strstr(text, "scanlines 1")   != NULL) gfx.crt         = 1;
    else if (strstr(text, "scanlines 0")   != NULL) gfx.crt         = 0;
    if (strstr(text, "vignette 1")         != NULL) gfx.vignette    = 1;
    else if (strstr(text, "vignette 0")    != NULL) gfx.vignette    = 0;
    if (strstr(text, "particles 1")        != NULL) gfx.particles   = 1;
    else if (strstr(text, "particles 0")   != NULL) gfx.particles   = 0;
    if (strstr(text, "smoke 1")            != NULL) gfx.smoke       = 1;
    else if (strstr(text, "smoke 0")       != NULL) gfx.smoke       = 0;
    if (strstr(text, "motionBlur 1")       != NULL) gfx.motionBlur  = 1;
    else if (strstr(text, "motionBlur 0")  != NULL) gfx.motionBlur  = 0;
    if (sscanf(text, "fpsCap %d", &v) == 1)
        fpsCap = (v == 0 || v == 30 || v == 60 || v == 120 || v == 144) ? v : 0;

    MemFree(text);
}

static void GfxSettingsSave(void)
{
    char text[256];
    snprintf(text, sizeof text,
             "quality %d\nbloom %d\ncrt %d\nvignette %d\n"
             "particles %d\nsmoke %d\nmotionBlur %d\nfpsCap %d\n",
             gfx.quality, gfx.bloom, gfx.crt, gfx.vignette,
             gfx.particles, gfx.smoke, gfx.motionBlur, fpsCap);
    SaveFileText(GFX_SETTINGS_FILE, text);
}

/* a preset picks a sensible bundle so nobody has to flip five toggles */
static void GfxApplyQuality(int q)
{
    gfx.quality = q;
    switch (q) {
        case 0:  /* LOW: every post effect off, shortest particle list */
            gfx.bloom = 0; gfx.crt = 0; gfx.vignette = 0; gfx.particles = 0;
            gfx.smoke = 0; gfx.motionBlur = 0;
            break;
        case 1:  /* MEDIUM: the game's intended look without tube tricks */
            gfx.bloom = 1; gfx.crt = 0; gfx.vignette = 1; gfx.particles = 1;
            gfx.smoke = 1; gfx.motionBlur = 0;
            break;
        default: /* HIGH: everything, softly */
            gfx.bloom = 1; gfx.crt = 1; gfx.vignette = 1; gfx.particles = 1;
            gfx.smoke = 1; gfx.motionBlur = 1;
            break;
    }
}

/* --------------------------------------------------------------------------
 *  Presentation state
 * ----------------------------------------------------------------------- */
static RenderTexture2D target;
static int     viewW        = BASE_W;
static int     viewH        = BASE_H;
int            gViewW       = BASE_W;
int            gViewH       = BASE_H;

static float   viewScale  = 2.0f;
static Vector2 viewOffset = { 0, 0 };

static Camera2D   camera;
static Vector2    cameraPos;
static bool       wantQuit = false;
static bool       muted    = false;

/* --------------------------------------------------------------------------
 *  Persistence - a single integer, kept next to the executable.
 * ----------------------------------------------------------------------- */
#define SAVE_FILE "pixel_squadron.sav"

static void LoadHighScore(void)
{
    FILE *f = fopen(SAVE_FILE, "r");
    if (!f) return;
    if (fscanf(f, "%d", &g.highScore) != 1) g.highScore = 0;
    fclose(f);
}

static void SaveHighScore(void)
{
    FILE *f = fopen(SAVE_FILE, "w");
    if (!f) return;
    fprintf(f, "%d\n", g.highScore);
    fclose(f);
}

/* --------------------------------------------------------------------------
 *  Pad bindings
 *
 *  Which button shoots is a personal matter, so the pad is remappable and the
 *  choice outlives the session: a small key=value file next to the executable
 *  on the desktop, and the browser's own storage on the web. The keyboard is
 *  deliberately not remappable - there are enough keys that the defaults are
 *  never in the way, and a half-remapped keyboard is just somewhere to get
 *  stuck.
 * ----------------------------------------------------------------------- */
Bindings binds;

#define PAD_BUTTON_FIRST GAMEPAD_BUTTON_LEFT_FACE_UP
#define PAD_BUTTON_MAX   (GAMEPAD_BUTTON_RIGHT_THUMB + 1)

static const char *const kBindKeys[BIND_COUNT] = {
    "fire", "boost", "recall", "special"
};

void BindsLoadDefaults(void)
{
    /* The trigger shoots, B boosts, X calls the wing and Y is the ultimate.
       Nothing else on the pad fires: a stray face button should never start
       a fight. */
    binds.button[BIND_FIRE]    = GAMEPAD_BUTTON_RIGHT_TRIGGER_2;  /* RT    */
    binds.button[BIND_BOOST]   = GAMEPAD_BUTTON_RIGHT_FACE_RIGHT; /* B     */
    binds.button[BIND_RECALL]  = GAMEPAD_BUTTON_RIGHT_FACE_LEFT;  /* X     */
    binds.button[BIND_SPECIAL] = GAMEPAD_BUTTON_RIGHT_FACE_UP;    /* Y     */
}

/* A human name for a pad button, for the controls panel. */
const char *PadButtonName(int button)
{
    switch (button) {
        case GAMEPAD_BUTTON_LEFT_FACE_UP:      return "D-UP";
        case GAMEPAD_BUTTON_LEFT_FACE_RIGHT:   return "D-RIGHT";
        case GAMEPAD_BUTTON_LEFT_FACE_DOWN:    return "D-DOWN";
        case GAMEPAD_BUTTON_LEFT_FACE_LEFT:    return "D-LEFT";
        case GAMEPAD_BUTTON_RIGHT_FACE_UP:     return "Y";
        case GAMEPAD_BUTTON_RIGHT_FACE_RIGHT:  return "B";
        case GAMEPAD_BUTTON_RIGHT_FACE_DOWN:   return "A";
        case GAMEPAD_BUTTON_RIGHT_FACE_LEFT:   return "X";
        case GAMEPAD_BUTTON_LEFT_TRIGGER_1:    return "LB";
        case GAMEPAD_BUTTON_LEFT_TRIGGER_2:    return "LT";
        case GAMEPAD_BUTTON_RIGHT_TRIGGER_1:   return "RB";
        case GAMEPAD_BUTTON_RIGHT_TRIGGER_2:   return "RT";
        case GAMEPAD_BUTTON_MIDDLE_LEFT:       return "BACK";
        case GAMEPAD_BUTTON_MIDDLE:            return "GUIDE";
        case GAMEPAD_BUTTON_MIDDLE_RIGHT:      return "START";
        case GAMEPAD_BUTTON_LEFT_THUMB:        return "L3";
        case GAMEPAD_BUTTON_RIGHT_THUMB:       return "R3";
        default:                               return "?";
    }
}

const char *BindName(int action)
{
    if (action < 0 || action >= BIND_COUNT) return "?";
    return PadButtonName(binds.button[action]);
}

void BindsLoad(void)
{
    BindsLoadDefaults();

    char blob[256] = { 0 };
#if defined(__EMSCRIPTEN__)
    EM_ASM({
        var s = "";
        try { s = window.localStorage.getItem("pixel_squadron_controls") || ""; }
        catch (e) { s = ""; }
        stringToUTF8(s, $0, $1);
    }, blob, (int)sizeof blob);
#else
    FILE *f = fopen("pixel_squadron.cfg", "r");
    if (!f) return;
    size_t n = fread(blob, 1, sizeof blob - 1, f);
    blob[n] = '\0';
    fclose(f);
#endif

    for (int i = 0; i < BIND_COUNT; i++) {
        char key[24];
        snprintf(key, sizeof key, "%s=", kBindKeys[i]);

        char *at = strstr(blob, key);
        if (!at) continue;

        /* The stored buttons are small positive ids; a lookup also keeps
           anything else in the file from smuggling a bogus binding in. */
        int v = atoi(at + strlen(key));
        if (v > 0 && v < PAD_BUTTON_MAX) binds.button[i] = v;
    }
}

void BindsSave(void)
{
    char blob[256];
    int n = 0;

    for (int i = 0; i < BIND_COUNT; i++) {
        n += snprintf(blob + n, sizeof blob - n, "%s=%d\n",
                      kBindKeys[i], binds.button[i]);
    }

#if defined(__EMSCRIPTEN__)
    EM_ASM({
        try { window.localStorage.setItem("pixel_squadron_controls", UTF8ToString($0)); }
        catch (e) {}
    }, blob);
#else
    FILE *f = fopen("pixel_squadron.cfg", "w");
    if (!f) return;
    fputs(blob, f);
    fclose(f);
#endif
}

/* --------------------------------------------------------------------------
 *  The controls panel on the pause screen.
 *  main.c owns the state, hud.c draws it.
 * ----------------------------------------------------------------------- */
static bool remapActive = false;
static int  remapSel    = 0;
static bool remapArmed  = false;

bool RemapActive(void)    { return remapActive; }
int  RemapSelection(void) { return remapSel; }
bool RemapArmed(void)     { return remapArmed; }

/* Which buttons may be bound. Start is the pause button and the guide button
   belongs to the console, so neither is offered. */
static bool BindableButton(int b)
{
    return b >= PAD_BUTTON_FIRST && b < PAD_BUTTON_MAX &&
           b != GAMEPAD_BUTTON_MIDDLE_RIGHT && b != GAMEPAD_BUTTON_MIDDLE;
}

/* Returns true while the panel is up, so the caller knows the pause key was
   spent on it rather than on resuming. */
static bool RemapPanelKeys(void)
{
    if (remapArmed) {
        for (int b = PAD_BUTTON_FIRST; b < PAD_BUTTON_MAX; b++) {
            if (!BindableButton(b)) continue;
            if (IsGamepadButtonPressed(0, b)) {
                binds.button[remapSel] = b;
                BindsSave();
                remapArmed = false;
                break;
            }
        }
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_C) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) {
            remapArmed = false;
        }
        return true;
    }

    if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP) ||
        IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP)) {
        remapSel = (remapSel + BIND_COUNT - 1) % BIND_COUNT;
    }
    if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN) ||
        IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) {
        remapSel = (remapSel + 1) % BIND_COUNT;
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) ||
        IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
        remapArmed = true;
    }
    if (IsKeyPressed(KEY_R)) {   /* back to how it shipped */
        BindsLoadDefaults();
        BindsSave();
    }
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_C) ||
        IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) {
        remapActive = false;
        BindsSave();
    }

    return true;
}

/* --------------------------------------------------------------------------
 *  Viewport
 *
 *  The window is created without FLAG_WINDOW_HIGHDPI, so its framebuffer is
 *  exactly the size we asked for in pixels: on a scaled Windows desktop GLFW
 *  still creates a DPI-aware window, Windows does not stretch it, and raylib
 *  leaves its internal screen scaling at identity. That means screen size ==
 *  framebuffer size, the arithmetic below is honest, and the blit lands on
 *  whole pixels with no DPI guesswork anywhere.
 * ----------------------------------------------------------------------- */

/* Point the render target at a virtual screen of `w` x `h` pixels. The blit
   is a whole-number zoom, so the picture is only ever resized by re-creating
   the target - and only when the window has actually changed shape. */
void RebuildBloomRT(int w, int h);   /* post FX: half-res bloom feed */

static void SetVirtualScreen(int w, int h)
{
    if (target.id != 0 && w == viewW && h == viewH) return;

    if (target.id != 0) UnloadRenderTexture(target);

    target = LoadRenderTexture(w, h);
    SetTextureFilter(target.texture, TEXTURE_FILTER_POINT);

    RebuildBloomRT(w, h);

    viewW  = w;
    viewH  = h;
    gViewW = w;
    gViewH = h;

    camera.offset = V2((float)w * 0.5f, (float)h * 0.5f);
}

/* Fit the virtual screen to the window.
 *
 * The zoom is a whole number chosen from the window's height, so a 1920x1080
 * window gives exactly the 480x270 design and everything stays pixel-square.
 * The virtual screen is then grown until it *covers* the window: the blit has
 * no letterbox bars to sit in and nothing worth seeing is cropped - a taller
 * or wider window simply reveals more of the world. */
static void UpdateViewport(void)
{
    float w = (float)GetScreenWidth();
    float h = (float)GetScreenHeight();

    int s = (int)floorf(h / (float)BASE_H + 0.5f);
    if (s < 1) s = 1;

    int vw = (int)ceilf(w / (float)s);
    int vh = (int)ceilf(h / (float)s);

    /* Keep the target sane on absurd window sizes, and inside the tile cache
       the arena keeps for one frame. A minimum only ever reveals *more* of
       the world, and guarantees the overlay still has room to lay itself
       out. */
    if (vw < 300) vw = 300;
    if (vh < 200) vh = 200;
    if (vw > 1440) vw = 1440;
    if (vh > 900)  vh = 900;

    if (s < 1) s = 1;
    viewScale  = (float)s;
    viewOffset = V2(0.0f, 0.0f);

    SetVirtualScreen(vw, vh);
}

/* Window coordinates -> the 480x270 space everything else thinks in. */
static Vector2 ScreenToVirtual(Vector2 screen)
{
    return V2((screen.x - viewOffset.x) / viewScale,
              (screen.y - viewOffset.y) / viewScale);
}

/* --------------------------------------------------------------------------
 *  Input
 * ----------------------------------------------------------------------- */
static void GatherInput(void)
{
    Vector2 move = V2(0, 0);
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  move.x -= 1.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) move.x += 1.0f;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    move.y -= 1.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  move.y += 1.0f;

#if defined(__EMSCRIPTEN__)
    /* The JS tracker reports in VIRTUAL space (0..480, 0..270), so no
       scaling guesswork is needed. Fall back to raylib's value if JS has
       not seen a move yet. */
    Vector2 rawMouse;
    int jsmx = EM_ASM_INT({ return (window.__mx >= 0) ? window.__mx : -1; });
    int jsmy = EM_ASM_INT({ return (window.__my >= 0) ? window.__my : -1; });
    if (jsmx >= 0 && jsmy >= 0) {
        rawMouse = V2((float)jsmx, (float)jsmy);
    } else {
        rawMouse = ScreenToVirtual(GetMousePosition());
    }
    Vector2 mouseVirtual = rawMouse;
#else
    Vector2 rawMouse = GetMousePosition();
    Vector2 mouseVirtual = ScreenToVirtual(rawMouse);
#endif

    bool firing  = IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsKeyDown(KEY_SPACE);
    bool boosting = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool rally    = IsKeyPressed(KEY_Q);
    bool special  = IsKeyPressed(KEY_E);
    bool padAim   = false;
    Vector2 aim   = V2(0, 0);

    /* --- Xbox pad: left stick flies, right stick shoots, shoulders act -----
       Left stick moves, right stick aims and opens up, the trigger the player
       bound to FIRE shoots, BOOST burns, RECALL brings the wing home and
       SPECIAL fires the ultimate. Nothing else on the pad shoots - a stray
       bumper should never start a fight. */
    if (IsGamepadAvailable(0)) {
        Vector2 ls = V2(GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X),
                        GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y));
        Vector2 rs = V2(GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X),
                        GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y));

        if (V2Len(ls) > 0.22f) move = ls;

        /* Aiming out of the deadzone takes the crosshair. The stick NEVER
           opens fire - a worn stick that never quite centres cannot spray
           the map on its own, and turning to look at something is not the
           same as shooting at it. Only the button bound to FIRE shoots. */
        float rsLen = V2Len(rs);
        if (rsLen > 0.55f) {
            aim    = V2Norm(rs);
            padAim = true;
        }

        if (IsGamepadButtonDown(0, binds.button[BIND_FIRE]))  firing   = true;
        if (IsGamepadButtonDown(0, binds.button[BIND_BOOST])) boosting = true;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1) ||
            GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_TRIGGER) > 0.45f) {
            boosting = true;   /* the left trigger stays a boost whatever
                                  the right one is bound to */
        }
        if (IsGamepadButtonPressed(0, binds.button[BIND_RECALL]))  rally   = true;
        if (IsGamepadButtonPressed(0, binds.button[BIND_SPECIAL])) special = true;
    }

    /* --- toggles ---------------------------------------------------------
       AUTO-SHOOT: guns take care of themselves - aim snaps to the nearest
       enemy and they keep talking, while the specials and movement stay
       entirely the player's. V on the keyboard, A on the pad, and a badge on
       the HUD says when it is running. The chat bubbles over the wingmen can
       be muted the same way (H / BACK) without touching the radio log. */
    static bool autoShoot  = false;
    static bool chatPopups = true;
    static bool chatLog    = true;
    if (IsKeyPressed(KEY_V) ||
        IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
        autoShoot  = !autoShoot;
        if (autoShoot) { TextCopy(g.message, "AUTO-SHOOT ON");  g.messageTimer = 1.2f; }
        else           { TextCopy(g.message, "AUTO-SHOOT OFF"); g.messageTimer = 1.2f; }
    }
    if (IsKeyPressed(KEY_H) ||
        IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_LEFT)) {
        chatPopups = !chatPopups;
        TextCopy(g.message, chatPopups ? "CHAT POPUPS ON" : "CHAT POPUPS OFF");
        g.messageTimer = 1.2f;
    }
    /* and the radio log on the right gets its own switch (L / menu button),
       so the whole squadron can be silenced without turning off either. */
    if (IsKeyPressed(KEY_L) ||
        IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) {
        chatLog = !chatLog;
        TextCopy(g.message, chatLog ? "CHAT LOG ON" : "CHAT LOG OFF");
        g.messageTimer = 1.2f;
    }

    /* --- the special carousel -------------------------------------------
       Every special move in the squadron - the player's own shockwave and
       the wingmen's vitamins - sits in one pool. One is loaded at a time,
       each reloads at its own pace, and TAB (or the dpad) flips through the
       bag; 1-8 loads a move straight away. E / the SPECIAL button fires it. */
    {
        int   count = PlayerSpecialCount();
        int   sel   = g.player.specialSel;
        int   dir   = 0;
        bool  changed = false;

        if (IsKeyPressed(KEY_TAB))                        dir = 1;
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) dir = 1;
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP))    dir = 1;
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT))  dir = -1;
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN))  dir = -1;

        if (dir != 0) {
            sel = (sel + dir + count) % count;
            changed = true;
        }
        for (int k = 1; k <= 8; k++) {
            if (k > count) break;
            if (IsKeyPressed(KEY_ONE + (k - 1))) { sel = k - 1; changed = true; }
        }

        if (changed && sel != g.player.specialSel) {
            char label[40];
            g.player.specialSel = sel;
            snprintf(label, sizeof label, "LOADED: %s", PlayerSpecialName(sel));
            TextCopy(g.message, label);
            g.messageTimer = 1.4f;
        }
    }

    /* --- auto-shoot takes over the guns, nothing else ---------------------
       the ship turns itself toward the nearest enemy and keeps firing; the
       player only steers. The HUD can draw thanks to the exported target. */
    g_autoTargetOn = false;
    if (autoShoot) {
        /* no range limit: auto-shoot is supposed to take over the aim as
           soon as anything hostile exists on the map */
        Enemy *best = NULL;
        float bestD = 1.0e30f;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy *e = &g.enemies[i];
            if (!e->alive || e->spawnFade > 0.0f) continue;
            float d = Dist2(e->pos, g.player.pos);
            if (d < bestD) { bestD = d; best = e; }
        }
        if (best) {
            aim    = V2Norm(V2Sub(best->pos, g.player.pos));
            firing = true;

            g_autoTarget    = best->pos;
            g_autoTargetOn  = true;
        }
    }

    /* --- which hand is driving? ------------------------------------------
       The two never fight over the crosshair. Moving the mouse - or holding
       the button - takes the aim away from the pad *there and then*; only a
       right stick actually pushed past its deadzone hands it back. A resting
       stick, or a worn one that never quite centres, can never steal the aim
       from the hand holding the mouse.
       The decision is sticky, and the aim below follows the *decision*, not
       the raw stick reading - otherwise a single held stick would freeze the
       guns on the last pad angle while the reticle happily went with the
       mouse. */
    static bool    padMode   = false;
    static Vector2 lastMouse = { -1.0f, -1.0f };

    bool mouseMoved = (lastMouse.x >= 0.0f) &&
                      (fabsf(mouseVirtual.x - lastMouse.x) > 0.1f ||
                       fabsf(mouseVirtual.y - lastMouse.y) > 0.1f);
    lastMouse = mouseVirtual;

    /* The mouse wins the moment it moves and keeps winning for a short while
       afterwards, so a drifting gamepad stick cannot reclaim the aim between
       two mouse events. */
    static float mouseLinger = 0.0f;
    if (mouseMoved || IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        padMode      = false;
        mouseLinger  = 0.35f;
    } else if (mouseLinger > 0.0f) {
        mouseLinger -= GetFrameTime();
        padMode = false;
    } else if (padAim) {
        padMode = true;
    }

    if (!padMode) {
        /* Aim straight at the mouse: the direction from the screen centre to
           the cursor. No camera, no world space, no deadzone - the mouse
           always wins here. EXCEPT while auto-shoot has a target: then the
           turret lock owns the aim and the (hidden) cursor has no say. */
        if (!autoShoot || !g_autoTargetOn) {
            Vector2 toMouse = V2Sub(mouseVirtual, V2(BASE_W * 0.5f, BASE_H * 0.5f));
            if (V2Len(toMouse) > 1.0f) aim = V2Norm(toMouse);
        }
    }

    /* A tapped button can go down and up between two frames. A press always
       buys a short burst of fire, so a quick click never does nothing. */
    static float fireLatch = 0.0f;
    if (firing) fireLatch = 0.10f;
    else if (fireLatch > 0.0f) fireLatch -= GetFrameTime();

    input.move         = move;
    input.aim          = aim;
    input.firing       = firing || fireLatch > 0.0f;
    input.boost        = boosting;
    input.rally        = rally;
    input.special      = special;
    input.usingGamepad = padMode;
    input.autoAim      = autoShoot;
    input.chatPopups   = chatPopups;
    input.chatLog      = chatLog;
    if (mouseVirtual.x != mouseVirtual.x || mouseVirtual.y != mouseVirtual.y) {
        mouseVirtual = V2(BASE_W * 0.5f, BASE_H * 0.5f);
    }
    if (mouseVirtual.x < 0.0f) mouseVirtual.x = 0.0f;
    if (mouseVirtual.x > (float)BASE_W) mouseVirtual.x = (float)BASE_W;
    if (mouseVirtual.y < 0.0f) mouseVirtual.y = 0.0f;
    if (mouseVirtual.y > (float)BASE_H) mouseVirtual.y = (float)BASE_H;
    input.reticle      = mouseVirtual;
}

/* Drive the game from a script so it can render itself to a PNG and exit.
   Used by tools and CI; harmless in normal play. */
static void AutoPilotInput(float dt)
{
    /* remember where we started so the lap is centred on ground, not ocean */
    static Vector2 centre = { 0, 0 };
    if (g.autoPilotTime <= 0.0f) centre = g.player.pos;

    g.autoPilotTime += dt;
    float t = g.autoPilotTime;

    /* fly a wide lap around the starting position */
    float   radius  = 210.0f;
    Vector2 desired = V2(centre.x + cosf(t * 0.45f) * radius,
                         centre.y + sinf(t * 0.45f) * radius);
    input.move = V2ClampLen(V2Scale(V2Sub(desired, g.player.pos), 0.05f), 1.0f);

    /* aim at the closest enemy that is actually in the fight, otherwise look
       where we are heading */
    Enemy *best = NULL;
    float bestD = 430.0f * 430.0f;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!g.enemies[i].alive) continue;
        float d = Dist2(g.enemies[i].pos, g.player.pos);
        if (d < bestD) { bestD = d; best = &g.enemies[i]; }
    }

    Vector2 aim = (best) ? V2Norm(V2Sub(best->pos, g.player.pos))
                         : V2Norm(input.move);
    input.aim = aim;
    input.firing = true;
    /* let the demo show off the boost every few seconds */
    input.boost  = fmodf(g.autoPilotTime, 7.0f) < 4.0f;
    /* ...and call the wing back once a lap, so the recall gets exercised */
    input.rally  = fmodf(g.autoPilotTime, 3.5f) < 0.02f;
    /* fire the ultimate once a lap too */
    input.special = fmodf(g.autoPilotTime, 4.0f) < 0.02f;
    input.usingGamepad = true;   /* the reticle rides the aim, not the mouse */
    input.reticle = V2(RENDER_W * 0.5f, RENDER_H * 0.5f);
}

/* Save exactly what the game drew, straight out of the 480x270 render
   target. Reading the window back instead would drag in HiDPI scaling and
   letterboxing, which is not what we want for documentation shots. */
static void CaptureVirtualScreen(const char *path)
{
    Image image = LoadImageFromTexture(target.texture);
    ImageFlipVertical(&image);
    ExportImage(image, path);
    UnloadImage(image);
}

/* --------------------------------------------------------------------------
 *  Camera
 * ----------------------------------------------------------------------- */
static void UpdateGameCamera(float dt)
{
    Vector2 lookAhead = V2Scale(g.player.vel, 0.22f);
    Vector2 want      = V2Add(g.player.pos, lookAhead);

    float k = 1.0f - expf(-9.0f * dt);
    cameraPos = V2Add(cameraPos, V2Scale(V2Sub(want, cameraPos), k));

    /* Snap the camera to whole pixels so the world does not shimmer as it
       scrolls. This is standard practice for pixel art and it also keeps
       every tile blit perfectly texel-aligned. */
    Vector2 snapped = V2(floorf(cameraPos.x), floorf(cameraPos.y));

    camera.offset   = V2(RENDER_W * 0.5f, RENDER_H * 0.5f);
    camera.zoom     = 1.0f;
    camera.rotation = 0.0f;

    if (g.shake > 0.05f) {
        camera.target = V2(snapped.x + (float)GetRandomValue(-100, 100) * 0.01f * g.shake,
                           snapped.y + (float)GetRandomValue(-100, 100) * 0.01f * g.shake);
    } else {
        camera.target = snapped;
    }
}

/* World position as it lands on the virtual screen. The camera is always
 * zoom 1 with no rotation, so this is a plain translate - but it goes through
 * the camera so the bubbles track the screen exactly as the ships do. */
Vector2 WorldToScreen(Vector2 world)
{
    return V2Add(V2Sub(world, camera.target), camera.offset);
}

/* --------------------------------------------------------------------------
 *  State machine
 * ----------------------------------------------------------------------- */
static void StartNewRun(void)
{
    /* Generate the terrain first: the squadron is placed on the best land the
       fresh map has to offer. */
    ArenaGenerate((unsigned)GetRandomValue(1, 1000000));
    EntitiesReset();

    g.score        = 0;
    g.wave         = 0;
    g.kills        = 0;
    g.survived     = 0.0f;
    g.difficulty   = 0.0f;
    g.shake        = 0.0f;
    g.hitStop      = 0.0f;
    g.damageFlash  = 0.0f;
    g.message[0]   = '\0';
    g.messageTimer = 0.0f;
    g.waveBreak    = false;
    g.waveTimer    = 0.0f;
    g.stateTimer   = 0.0f;

    ChatReset();

    cameraPos = g.player.pos;

    StartWave(1);
    g.state = STATE_PLAY;
#if defined(__EMSCRIPTEN__)
    EM_ASM({ if (!document.fullscreenElement && !document.webkitFullscreenElement) {
        var el = document.getElementById('canvas');
        (el.requestFullscreen || el.webkitRequestFullscreen || function(){}).call(el);
    } });
#endif
}

static void UpdatePlaying(float dt)
{
    g.survived += dt;
    if (g.rallyCool  > 0.0f) g.rallyCool  -= dt;
    if (g.rallyTimer > 0.0f) g.rallyTimer -= dt;

    /* hit-stop: freeze the simulation for a couple of frames on a big kill */
    float step = dt;
    if (g.hitStop > 0.0f) {
        g.hitStop -= dt;
        step = dt * 0.15f;
    }

    /* The recall is a player order, so it is read before the wing thinks. */
    if (input.rally && g.player.alive) AlliesRally();
    if (input.special && g.player.alive) PlayerSpecial();

    PlayerUpdate(step);
    EnemiesUpdate(step);
    FieldUpdate(step);
    AlliesUpdate(step);
    BulletsUpdate(step);
    PickupsUpdate(step);
    ParticlesUpdate(step);
    BurstsUpdate(step);
    PopupsUpdate(step);

    if (g.levelUpFlash > 0.0f) g.levelUpFlash -= dt;

    if (g.player.alive) {
        if (g.wavePending > 0) {
            WaveSpawnStep(step);
        } else if (EnemiesAliveCount() == 0) {
            if (!g.waveBreak) {
                g.waveBreak = true;
                g.waveTimer = 3.0f;
                TextCopy(g.message, "AREA CLEAR");
                g.messageTimer = 1.5f;
            }
            g.waveTimer -= dt;
            if (g.waveTimer <= 0.0f) {
                g.waveBreak = false;
                StartWave(g.wave + 1);
            }
        } else {
            g.waveBreak = false;
        }
    }

    /* death -> short beat, then the score screen */
    if (!g.player.alive) {
        g.stateTimer += dt;
        if (g.stateTimer > 1.6f) {
            g.survived -= 1.6f;
            if (g.score > g.highScore) {
                g.highScore = g.score;
                SaveHighScore();
            }
            g.state     = STATE_GAMEOVER;
            g.stateTimer = 0.0f;
        }
    }
}

/* --------------------------------------------------------------------------
 *  Drawing
 * ----------------------------------------------------------------------- */
static void DrawWorld(void)
{
    ArenaDraw(camera);
    PickupsDraw();
    BulletsDraw();
    EnemiesDraw();
    FieldDraw();          /* JUNO's dome sits over the fight, under the ships */
    AlliesDraw();
    PlayerDraw();
    RallyDraw();
    BurstsDraw();
    ParticlesDraw();
    PopupsDraw();
}

/* Attract camera: a slow drift over the ground the run will start on. The
   endless arena means there is nothing to clamp against - it just flies. */
static void UpdateTitleCamera(void)
{
    camera.offset   = V2(RENDER_W * 0.5f, RENDER_H * 0.5f);
    camera.zoom     = 1.0f;
    camera.rotation = 0.0f;
    camera.target   = V2(floorf(g.player.pos.x + sinf(g.timer * 0.11f) * 150.0f),
                         floorf(g.player.pos.y + cosf(g.timer * 0.15f) * 90.0f));
}

/* --------------------------------------------------------------------------
 *  Post effects
 *
 *  An optional bloom pass (the glow so bright areas bleed a little), plus two
 *  cheap full-screen overlays: ever-other-scanline dimming and a vignette.
 *  Everything is switchable from the pause menu's graphics panel and the
 *  presets, and every stage fails soft: no shader, no bloom, game runs.
 * ----------------------------------------------------------------------- */
static Shader        bloomShader = { 0 };
static Shader        crtShader   = { 0 };
static Shader        trailShader = { 0 };
static RenderTexture bloomRT     = { 0 };
static RenderTexture trailRT[2]  = { { 0 }, { 0 } };
static int           trailCur    = 0;
static Texture2D     vignTex;
static int           uTexel     = -1;
static int           uStrength  = -1;
static int           uCrtTime   = -1;
static int           uCrtScan   = -1;
static int           uPrevSam   = -1;
static int           uDecay     = -1;
static bool          postLoaded = false;
static bool          bloomOK    = false;
static bool          crtOK      = false;
static bool          trailOK    = false;

/* GLSL dual-header: Emscripten runs WebGL (GLSL ES 100), desktop GPUs get
   core 330. Everything else stays shared below. */
#if defined(__EMSCRIPTEN__)
#define GLSL_FS_HEADER \
    "#version 100\n" \
    "precision mediump float;\n" \
    "varying vec2 fragTexCoord;\n" \
    "#define SAMPLE(t,uv) texture2D(t, uv)\n"
#define GLSL_FS_OUT "gl_FragColor"
#else
#define GLSL_FS_HEADER \
    "#version 330\n" \
    "in vec2 fragTexCoord;\n" \
    "#define SAMPLE(t,uv) texture(t, uv)\n" \
    "out vec4 finalColor;\n"
#define GLSL_FS_OUT "finalColor"
#endif

/* ---- bloom ----
   Bright-pass first, blur second: only pixels that are already glowing
   (rounds, muzzles, explosions - R/G/B near or at 1.0) survive the
   threshold, so ground, hulls and HUD stay matte and only light sources
   bleed a halo. Strength is deliberately low; bloom should be felt, not
   seen. */
static const char *kBloomFs =
    GLSL_FS_HEADER
    "uniform sampler2D texture0;\n"
    "uniform vec2 texelSize;\n"
    "uniform float strength;\n"
    "void main() {\n"
    "    vec3 sum = vec3(0.0);\n"
    "    for (int i = 0; i < 8; i++) {\n"
    "        float ang = 6.2831853 * 0.125 * float(i);\n"
    "        vec2  off = vec2(cos(ang), sin(ang)) * texelSize * 1.8;\n"
    "        vec3  s   = SAMPLE(texture0, fragTexCoord + off).rgb;\n"
    "        float m   = max(s.r, max(s.g, s.b));\n"
    "        sum      += s * smoothstep(0.88, 1.02, m);\n"
    "    }\n"
    "    " GLSL_FS_OUT " = vec4(sum * 0.125 * strength, 1.0);\n"
    "}\n";

/* ---- CRT screen ----
   A cheap tube, done softly: slight barrel curve, edge-only colour
   separation, a gentle sine shading per line (no hard stripes), faint
   phosphor shimmer, static grain and a falloff baked into the glass. Every
   few seconds one horizontal band hiccups sideways for a single tick -
   the glitch, subtle by default. */
static const char *kCrtFs =
    GLSL_FS_HEADER
    "uniform sampler2D texture0;\n"
    "uniform float uTime;\n"
    "uniform float uScan;\n"
    "float hash(vec2 p) {\n"
    "    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);\n"
    "}\n"
    "vec2 curve(vec2 uv) {\n"
    "    uv = uv * 2.0 - 1.0;\n"
    "    vec2 o = abs(uv.yx) / vec2(7.5, 5.5);\n"
    "    uv += uv * o * o;\n"
    "    return uv * 0.5 + 0.5;\n"
    "}\n"
    "void main() {\n"
    "    vec2 uv = curve(fragTexCoord);\n"
    "    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {\n"
    "        " GLSL_FS_OUT " = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    float tt   = floor(uTime * 9.0);\n"
    "    float band = floor(uv.y * 44.0);\n"
    "    float g    = step(0.985, hash(vec2(tt, band)));\n"
    "    float dx   = (hash(vec2(tt + 7.1, band)) - 0.5) * 0.020 * g;\n"
    "    vec2  ca   = uv - 0.5;\n"
    "    float amt  = 0.0012 + dot(ca, ca) * 0.008 + g * 0.004;\n"
    "    vec3  col;\n"
    "    col.r = SAMPLE(texture0, uv + vec2(dx + amt, 0.0)).r;\n"
    "    col.g = SAMPLE(texture0, uv + vec2(dx,        0.0)).g;\n"
    "    col.b = SAMPLE(texture0, uv + vec2(dx - amt, 0.0)).b;\n"
    "    float sc = sin(uv.y * uScan * 3.14159265) * 0.5 + 0.5;\n"
    "    col *= 0.955 + 0.045 * sc;\n"
    "    col *= 1.0 + 0.02 * sin(uTime * 2.3 + uv.y * 9.0);\n"
    "    col += (hash(uv + fract(uTime)) - 0.5) * 0.028;\n"
    "    float vig = smoothstep(1.18, 0.38, length(uv - 0.5) * 1.55);\n"
    "    col *= 0.74 + 0.26 * vig;\n"
    "    " GLSL_FS_OUT " = vec4(col, 1.0);\n"
    "}\n";

/* ---- motion blur ----
   Feedback trails: each frame the previous composite decays and the
   brighter of the two wins, so fast lights draw a fading tail behind
   them. The decay uniform is recomputed per frame from the real dt, so
   the trail length is the same at any frame rate. */
static const char *kTrailFs =
    GLSL_FS_HEADER
    "uniform sampler2D texture0;\n"
    "uniform sampler2D prev;\n"
    "uniform float decay;\n"
    "void main() {\n"
    "    vec3 c = SAMPLE(texture0, fragTexCoord).rgb;\n"
    "    vec3 h = SAMPLE(prev, fragTexCoord).rgb * decay;\n"
    "    " GLSL_FS_OUT " = vec4(max(c, h), 1.0);\n"
    "}\n";

void RebuildBloomRT(int w, int h)
{
    if (!postLoaded) return;

    if (bloomRT.id != 0) UnloadRenderTexture(bloomRT);
    bloomRT = LoadRenderTexture((w + 1) / 2, (h + 1) / 2);
    SetTextureFilter(bloomRT.texture, TEXTURE_FILTER_BILINEAR);

    for (int i = 0; i < 2; i++) {
        if (trailRT[i].id != 0) UnloadRenderTexture(trailRT[i]);
        trailRT[i] = LoadRenderTexture(w, h);
        SetTextureFilter(trailRT[i].texture, TEXTURE_FILTER_POINT);
    }
    trailCur = 0;
}

/* overlay art built once: a scanline mask and a soft vignette at the game's
   own virtual resolution - they stretch with the blit, so they scale clean */
static void BuildOverlayTextures(void)
{
    /* The hard scanline overlay is gone - a CRT belongs in a shader, where
       the lines can be soft sine shading instead ofa few black stripes. */
    Image img;
    /* the corners sit at d~0.71, so the ramp has to actually reach there -
       the first version faded out before the screen edge began */
    img = GenImageColor(BASE_W, BASE_H, BLANK);
    for (int y = 0; y < BASE_H; y++) {
        for (int x = 0; x < BASE_W; x++) {
            float dx = (x / (BASE_W - 1.0f)) * 2.0f - 1.0f;
            float dy = (y / (BASE_H - 1.0f)) * 2.0f - 1.0f;
            float d  = sqrtf(dx * dx + dy * dy) * 0.5f;   /* 0 ... ~0.71 */
            float t  = (d - 0.42f) / 0.30f;
            if (t <= 0.0f) continue;
            if (t > 1.0f) t = 1.0f;
            ImageDrawPixel(&img, x, y, (Color){ 2, 6, 12,
                                (unsigned char)(168.0f * t * t) });
        }
    }
    vignTex = LoadTextureFromImage(img);
    SetTextureFilter(vignTex, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);
}

static void PostFxInit(void)
{
    if (postLoaded) return;

    bloomShader = LoadShaderFromMemory(NULL, kBloomFs);
    uTexel    = GetShaderLocation(bloomShader, "texelSize");
    uStrength = GetShaderLocation(bloomShader, "strength");

    crtShader = LoadShaderFromMemory(NULL, kCrtFs);
    uCrtTime  = GetShaderLocation(crtShader, "uTime");
    uCrtScan  = GetShaderLocation(crtShader, "uScan");

    trailShader = LoadShaderFromMemory(NULL, kTrailFs);
    uPrevSam    = GetShaderLocation(trailShader, "prev");
    uDecay      = GetShaderLocation(trailShader, "decay");

    BuildOverlayTextures();
    postLoaded = true;

    /* a driver that rejects our GLSL must never take the game down with it */
    if (bloomShader.id == 0) bloomOK = false;
    else {
        float zero[2] = { 0.0f, 0.0f };
        SetShaderValue(bloomShader, uTexel, zero, SHADER_UNIFORM_VEC2);
        float strength = 0.6f;
        SetShaderValue(bloomShader, uStrength, &strength, SHADER_UNIFORM_FLOAT);
        bloomOK = true;
    }
    crtOK   = (crtShader.id   != 0);
    trailOK = (trailShader.id != 0);

    RebuildBloomRT(viewW, viewH);   /* half window res: softer blur, cheaper */
}

static void PostFxUnload(void)
{
    if (!postLoaded) return;
    UnloadShader(bloomShader);
    UnloadShader(crtShader);
    UnloadShader(trailShader);
    if (bloomRT.id != 0) UnloadRenderTexture(bloomRT);
    for (int i = 0; i < 2; i++)
        if (trailRT[i].id != 0) UnloadRenderTexture(trailRT[i]);
    UnloadTexture(vignTex);
    postLoaded = false;
}

/* trails first, then the bloom feed off the *scene* - so lights bloom even
   while they leave motion trails, and the trail already carries the glow */
static void PostFxRender(void)
{
    if (!postLoaded) return;

    if (trailOK && gfx.motionBlur && uDecay >= 0 && uPrevSam >= 0) {
        int   nxt    = 1 - trailCur;
        float decay  = expf(-GetFrameTime() / 0.10f);   /* ~0.1 s tail */

        BeginTextureMode(trailRT[nxt]);
        ClearBackground(BLANK);
        SetShaderValue(trailShader, uDecay, &decay, SHADER_UNIFORM_FLOAT);
        SetShaderValueTexture(trailShader, uPrevSam, trailRT[trailCur].texture);
        BeginShaderMode(trailShader);
        DrawTexturePro(target.texture,
                       (Rectangle){ 0.0f, 0.0f, (float)viewW, -(float)viewH },
                       (Rectangle){ 0.0f, 0.0f, (float)viewW, (float)viewH },
                       V2(0, 0), 0.0f, WHITE);
        EndShaderMode();
        EndTextureMode();
        trailCur = nxt;
    }

    if (!bloomOK || !gfx.bloom || bloomRT.id == 0) return;

    float texel[2] = { 1.0f / (float)viewW, 1.0f / (float)viewH };
    float strength = 0.6f;

    BeginTextureMode(bloomRT);
    ClearBackground(BLANK);
    SetShaderValue(bloomShader, uTexel, texel, SHADER_UNIFORM_VEC2);
    SetShaderValue(bloomShader, uStrength, &strength, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(bloomShader);
    DrawTexturePro(target.texture,
                   (Rectangle){ 0.0f, 0.0f, (float)viewW, -(float)viewH },
                   (Rectangle){ 0.0f, 0.0f,
                                (float)bloomRT.texture.width,
                                (float)bloomRT.texture.height },
                   V2(0, 0), 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();
}

/* stacked on the window blit so the overlays follow the picture, not the UI */
static void PostFxPresent(const Rectangle *dst, bool crtActive)
{
    if (!postLoaded) return;

    if (bloomOK && gfx.bloom && bloomRT.id != 0) {
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTexturePro(bloomRT.texture,
                       (Rectangle){ 0.0f, 0.0f, (float)bloomRT.texture.width,
                                    -(float)bloomRT.texture.height },
                       *dst, V2(0, 0), 0.0f, WHITE);
        EndBlendMode();
    }
    if (gfx.vignette && !crtActive) {
        Rectangle src = { 0.0f, 0.0f, (float)BASE_W, (float)BASE_H };
        DrawTexturePro(vignTex, src, *dst, V2(0, 0), 0.0f, WHITE);
    }
}

static void DrawFrame(void)
{
    /* The virtual screen is re-fitted before anything is drawn into it, so a
       window that changed shape this frame is already using the new target. */
    UpdateViewport();

    BeginTextureMode(target);
    ClearBackground(COL_DEEP);

    if (g.state == STATE_TITLE) {
        /* Attract card: the land the run will start on drifts past behind the
           title, so the first thing anyone sees is the game itself. */
        UpdateTitleCamera();

        BeginMode2D(camera);
        ArenaDraw(camera);
        EndMode2D();

        HudDrawTitle();
    } else {
        BeginMode2D(camera);
        DrawWorld();
        EndMode2D();

        switch (g.state) {
            case STATE_PAUSED:   HudDrawPause();    break;
            case STATE_GAMEOVER: HudDrawGameOver(); break;
            default:             HudDraw(camera);   ChatDraw(); break;
        }
    }

    EndTextureMode();

    PostFxRender();

    /* blit the virtual screen, filling the window exactly. With the CRT
       setting on, the blit itself goes through the tube shader so the whole
       picture (scene, HUD, overlays) is drawn on one screen. */
    BeginDrawing();
    ClearBackground((Color){ 8, 12, 18, 255 });

    Rectangle dst = { viewOffset.x, viewOffset.y,
                      RENDER_W * viewScale, RENDER_H * viewScale };

    bool useTrail = (postLoaded && trailOK && gfx.motionBlur &&
                     trailRT[trailCur].id != 0);
    Texture2D presentTex = useTrail ? trailRT[trailCur].texture : target.texture;
    Rectangle src = { 0.0f, 0.0f, (float)presentTex.width,
                      -(float)presentTex.height };

    bool crtActive = (crtOK && gfx.crt);
    if (crtActive) {
        float ut   = fmodf(g.timer, 3600.0f);
        float scan = (float)viewH;
        SetShaderValue(crtShader, uCrtTime, &ut, SHADER_UNIFORM_FLOAT);
        SetShaderValue(crtShader, uCrtScan, &scan, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(crtShader);
        DrawTexturePro(presentTex, src, dst, V2(0, 0), 0.0f, WHITE);
        EndShaderMode();
    } else {
        DrawTexturePro(presentTex, src, dst, V2(0, 0), 0.0f, WHITE);
    }

    PostFxPresent(&dst, crtActive);

    EndDrawing();
}

/* --------------------------------------------------------------------------
 *  Window and display
 * ----------------------------------------------------------------------- */
#define START_CENTER     (1 << 0)
#define START_FULLSCREEN (1 << 1)

/* The window is opened at a whole-number multiple of the design size, so at
   1080p the picture is exactly the 480x270 design scaled 3x. Any other window
   shape is fine too - the viewport simply reveals more of the world - this is
   just the nicest place to start. Desktop only: on the web the canvas belongs
   to the page, and raylib's own resize callback keeps it pinned to the
   viewport instead. */
#if !defined(__EMSCRIPTEN__)
static void FitWindowToIntegerScale(int flags)
{
    int monW = GetMonitorWidth(GetCurrentMonitor());
    int monH = GetMonitorHeight(GetCurrentMonitor());

    if (monW <= 0 || monH <= 0) {          /* no monitor info, keep what we have */
        monW = BASE_W * 4;
        monH = BASE_H * 4;
    }

    int n = 8;
    while (n > 1 && (BASE_W * n > (int)(monW * 0.92f) ||
                     BASE_H * n > (int)(monH * 0.92f))) n--;

    SetWindowSize(BASE_W * n, BASE_H * n);

    if (flags & START_CENTER) {
        SetWindowPosition((monW - BASE_W * n) / 2, (monH - BASE_H * n) / 2);
    }
}
#endif

/* "Fullscreen" is a borderless window covering the monitor rather than a real
   video-mode switch. raylib's fullscreen path letterboxes its own viewport
   when the display aspect does not match the requested one, and it keeps that
   offset internal - our blit cannot see it, so the picture ends up cropped.
   A borderless window keeps the framebuffer identical to the windowed case.
   Not needed on the web, where the canvas already fills the page. */
#if !defined(__EMSCRIPTEN__)
static bool    fullscreenOn = false;
static Vector2 windowedPos  = { 0, 0 };
static int     windowedW    = 0, windowedH = 0;

static void SetFullscreen(bool on)
{
    if (on == fullscreenOn) return;

    int mon = GetCurrentMonitor();

    if (on) {
        windowedPos = GetWindowPosition();
        windowedW   = GetScreenWidth();
        windowedH   = GetScreenHeight();

        int monW = GetMonitorWidth(mon);
        int monH = GetMonitorHeight(mon);
        if (monW <= 0 || monH <= 0) return;

        Vector2 monPos = GetMonitorPosition(mon);

        SetWindowState(FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_TOPMOST);
        SetWindowSize(monW, monH);
        SetWindowPosition((int)monPos.x, (int)monPos.y);
    } else {
        ClearWindowState(FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_TOPMOST);
        SetWindowSize(windowedW, windowedH);
        SetWindowPosition((int)windowedPos.x, (int)windowedPos.y);
    }

    fullscreenOn = on;
}
#endif

/* --------------------------------------------------------------------------
 *  Global keys
 * ----------------------------------------------------------------------- */
static void HandleGlobalKeys(void)
{
#if !defined(__EMSCRIPTEN__)
    if (IsKeyPressed(KEY_F11)) SetFullscreen(!fullscreenOn);
#endif

    if (IsKeyPressed(KEY_M)) {
        muted = !muted;
        if (muted) {
            StopSound(assets.engine);
            SetMasterVolume(0.0f);
        } else {
            SetMasterVolume(1.0f);
        }
    }

    if (IsKeyPressed(KEY_F12)) TakeScreenshot("pixel_squadron.png");

    /* F cycles the FPS cap anywhere the flight is live: uncapped (FREE),
       30, 60, 120, 144. The counter in the corner and the little message
       both confirm it, so the key is never silent. */
    if ((g.state == STATE_PLAY || g.state == STATE_PAUSED) &&
        IsKeyPressed(KEY_F)) {
        static const int fpsOpts[] = { 0, 30, 60, 120, 144 };
        static int fpsIdx = 0;
        for (int i = 0; i < 5; i++)
            if (fpsOpts[i] == fpsCap) { fpsIdx = i; break; }
        fpsIdx = (fpsIdx + 1) % 5;
        fpsCap = fpsOpts[fpsIdx];
        SetTargetFPS((fpsCap <= 0) ? 1000 : fpsCap);

        char label[24];
        if (fpsCap <= 0) snprintf(label, sizeof label, "FPS CAP: FREE");
        else             snprintf(label, sizeof label, "FPS CAP: %d", fpsCap);
        TextCopy(g.message, label);
        g.messageTimer = 1.4f;
    }

    switch (g.state) {
        case STATE_TITLE:
            /* Enter (or A on a pad) ships out. Escape backs out of the game
               altogether, which is what a title screen is for. */
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) ||
                IsKeyPressed(KEY_SPACE) ||
                IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) ||
                IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) {
                StartNewRun();
            }
            if (IsKeyPressed(KEY_ESCAPE)) wantQuit = true;
            break;

        case STATE_PLAY:
            if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE) ||
                IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) {
                g.state = STATE_PAUSED;
            }
            break;

        case STATE_PAUSED:
            /* The controls panel takes the whole pause keymap while it is up,
               so Escape means "close the panel", not "resume". */
            if (remapActive) { RemapPanelKeys(); break; }

            /* ...and so does the graphics panel. */
            if (gfxPanelMode) {
                enum { GFX_ROWS = 8 };
                if (IsKeyPressed(KEY_G) || IsKeyPressed(KEY_ESCAPE)) {
                    gfxPanelMode = 0;
                    break;
                }
                if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP) ||
                    IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP))
                    gfxPanelSel = (gfxPanelSel + GFX_ROWS - 1) % GFX_ROWS;
                if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN) ||
                    IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN))
                    gfxPanelSel = (gfxPanelSel + 1) % GFX_ROWS;

                int dir = 0;
                if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT) ||
                    IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT))  dir = -1;
                if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT) ||
                    IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) dir = 1;
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) ||
                    IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) dir = 1;

                if (dir != 0) {
                    switch (gfxPanelSel) {
                        case 0: GfxApplyQuality((gfx.quality + dir + 3) % 3); break;
                        case 1: gfx.bloom      = !gfx.bloom;     break;
                        case 2: gfx.crt        = !gfx.crt;       break;
                        case 3: gfx.vignette   = !gfx.vignette;  break;
                        case 4: gfx.particles  = !gfx.particles; break;
                        case 5: gfx.smoke      = !gfx.smoke;     break;
                        case 6: gfx.motionBlur = !gfx.motionBlur; break;
                        case 7: {
                            static const int caps[] = { 0, 30, 60, 120, 144 };
                            int idx = 0;
                            for (int i = 0; i < 5; i++)
                                if (caps[i] == fpsCap) idx = i;
                            fpsCap = caps[(idx + dir + 5) % 5];
                            SetTargetFPS((fpsCap <= 0) ? 1000 : fpsCap);
                            break;
                        }
                    }
                    GfxSettingsSave();
                }
                break;
            }

            /* pause main menu: three entries, gamepad friendly */
            if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP) ||
                IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP))
                pauseMenuSel = (pauseMenuSel + 2) % 3;
            if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN) ||
                IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN))
                pauseMenuSel = (pauseMenuSel + 1) % 3;

            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) ||
                IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
                switch (pauseMenuSel) {
                    case 0: g.state = STATE_PLAY; break;
                    case 1: gfxPanelMode = 1; gfxPanelSel = 0; break;
                    case 2: remapActive = true; remapArmed = false; break;
                }
                break;
            }


            /* Escape: if we are still in fullscreen, the first press already
               paused; the second one drops out of fullscreen instead of
               unpausing. After that, Escape unpauses normally. */
            if (IsKeyPressed(KEY_ESCAPE)) {
#if defined(__EMSCRIPTEN__)
                int inFs = EM_ASM_INT({ return (document.fullscreenElement || document.webkitFullscreenElement) ? 1 : 0; });
                if (inFs) {
                    EM_ASM({ if (document.exitFullscreen) document.exitFullscreen();
                             else if (document.webkitExitFullscreen) document.webkitExitFullscreen(); });
                    break;   /* stay paused, just left fullscreen */
                }
#endif
                g.state = STATE_PLAY;
                break;
            }

            if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ENTER) ||
                IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) {
                g.state = STATE_PLAY;
            }
            break;

        case STATE_GAMEOVER:
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) ||
                IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
                StartNewRun();
            }
            if (IsKeyPressed(KEY_ESCAPE)) wantQuit = true;
            break;

        default: break;
    }
}

/* --------------------------------------------------------------------------
 *  Entry point
 * ----------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    float demoSeconds = 0.0f;
    const char *shotPath = NULL;
    unsigned seed = 0;
    bool endCard = false;
    bool titleCard = false;
    int  forceBiome = -1;
    int  wingUp = -1;
    bool mouseTest = false;
#if !defined(__EMSCRIPTEN__)
    int startFlags = START_CENTER | START_FULLSCREEN;   /* play big by default */
#endif
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--demo") == 0 && i + 1 < argc) {
            demoSeconds = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shotPath = argv[++i];
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (unsigned)strtoul(argv[++i], NULL, 10);
#if !defined(__EMSCRIPTEN__)
        } else if (strcmp(argv[i], "--fullscreen") == 0) {
            startFlags |= START_FULLSCREEN;
        } else if (strcmp(argv[i], "--windowed") == 0) {
            startFlags &= ~START_FULLSCREEN;
#endif
        } else if (strcmp(argv[i], "--endcard") == 0) {
            endCard = true;      /* dev: screenshot the end-of-run report */
        } else if (strcmp(argv[i], "--titlecard") == 0) {
            titleCard = true;    /* dev: screenshot the title card */
        } else if (strcmp(argv[i], "--biome") == 0 && i + 1 < argc) {
            forceBiome = atoi(argv[++i]);   /* dev: pin the world to one biome */
        } else if (strcmp(argv[i], "--wingup") == 0 && i + 1 < argc) {
            wingUp = atoi(argv[++i]);       /* dev: show the wing rearm banner */
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            fpsCap = atoi(argv[++i]);       /* 0 = uncapped */
        } else if (strcmp(argv[i], "--mousetest") == 0) {
            mouseTest = true;              /* dev: scripted mouse, log aim */
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Pixel Squadron\n"
                   "  --seed N          start from a specific terrain seed\n"
                   "  --demo SECONDS    play itself, then screenshot and quit\n"
                   "  --shot FILE       where the demo writes its screenshot\n"
                   "  --biome N         dev: force one biome (0 verdant, 1 dust,\n"
                   "                                 2 frost, 3 ash, 4 verge)\n"
                   "  --endcard         dev: jump straight to the end-of-run report\n"
                   "  --titlecard       dev: screenshot the title card and quit\n"
                   "  --wingup N        dev: show the wing's rearm banner at level N\n"
                   "  --windowed        start in a window (default is fullscreen)\n"
                   "  --fullscreen      force fullscreen\n"
                   "\n"
                   "  F11 toggles fullscreen      F12 saves a screenshot\n"
                   "  Q   recall the wing to you\n"
                   "  TAB/1-8 load a special move, E fires it\n"
                   "  V   auto-shoot toggle       H   chat popups toggle\n"
                   "  F   caps the fps (FREE/30/60/120/144)\n"
                   "  VEX/JUNO/BIT fly your wing: each has a special weapon, and\n"
                   "  they talk. Answer a red ! on the map to bail one out.\n");
            return 0;
        }
    }

    /* No FLAG_WINDOW_HIGHDPI on purpose. With it, GLFW scales the requested
       window size by the monitor scale, and raylib then draws through a
       scaled matrix while its framebuffer bookkeeping can go stale after a
       resize - which is what crops the right edge of the picture. Without it
       the window, the framebuffer and our blit all agree, pixel for pixel. */
#if defined(__EMSCRIPTEN__)
    /* On the web the canvas *is* the window, and raylib/GLFW will happily
       resize it to a monitor-sized slab (which a fixed-aspect CSS canvas then
       letterboxes inside the viewport - the black bars). Leaving
       FLAG_WINDOW_RESIZABLE set hands that sizing to raylib's own resize
       callback, which pins the canvas to the browser viewport, and there is no
       monitor to fit or to take over either: the browser owns fullscreen. */
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(BASE_W * 3, BASE_H * 3, "Pixel Squadron - raylib twin-stick shooter");
    InitAudioDevice();
#else
    /* No VSYNC hint: with it on the swap blocks and "FREE" never beats the
       monitor's refresh by a pixel. The FPS-cap system regulates the loop
       itself, so the driver is left alone. */
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(BASE_W * 3, BASE_H * 3, "Pixel Squadron - raylib twin-stick shooter");
    InitAudioDevice();

    /* A headless demo should not hijack the desktop. */
    if (demoSeconds > 0.0f || endCard) startFlags &= ~START_FULLSCREEN;

    FitWindowToIntegerScale(startFlags);
    if (startFlags & START_FULLSCREEN) SetFullscreen(true);
#endif

    SetExitKey(KEY_NULL);          /* we handle Escape ourselves */
#ifndef __EMSCRIPTEN__
    /* the game draws its own aiming cursor (or none at all during auto
       shoot), so the OS arrow would just hover around on top of art */
    HideCursor();
#endif
    GfxSettingsLoad();
    SetTargetFPS((fpsCap <= 0) ? 1000 : fpsCap);  /* 0 = uncapped */
    SetRandomSeed(seed ? seed : (unsigned)time(NULL));
    ArenaForceBiome(forceBiome);   /* no-op unless --biome was passed */

    LoadHighScore();
    BindsLoad();
    AssetsLoad();
    PostFxInit();      /* bloom pass + CRT overlays; failures fall back nice */

    camera = (Camera2D){
        .offset   = V2(BASE_W * 0.5f, BASE_H * 0.5f),
        .target   = V2(0.0f, 0.0f),
        .rotation = 0.0f,
        .zoom     = 1.0f,
    };

    SetVirtualScreen(BASE_W, BASE_H);
    cameraPos = camera.target;

    /* Generate the run the title card will sit over, so the attract view is
       the actual map the first wave starts on. */
    StartNewRun();

    /* Open on the title card - the land the run will start on drifts past
       behind it - unless a dev flag wants to be in the fight immediately. */
    if (demoSeconds <= 0.0f && !mouseTest) g.state = STATE_TITLE;
    if (mouseTest) g.state = STATE_PLAY;

    /* Dev aid: jump to the end-of-run report with a plausible run behind it,
       so the layout can be checked without playing for ten minutes first. */
    if (endCard) {
        g.score    = 48250;
        g.kills    = 137;
        g.crits    = 41;
        g.wave     = 12;
        g.survived = 371.0f;
        g.highScore = 60000;
        g.state    = STATE_GAMEOVER;
    }

    if (demoSeconds > 0.0f) g.autoPilot = true;
    if (mouseTest) { g.autoPilot = false;
#if !defined(__EMSCRIPTEN__)
        startFlags &= ~START_FULLSCREEN;
#endif
    }

    /* Dev aid: hold the wing-rearm banner on screen so it can be eyeballed. */
    if (wingUp >= 0) {
        g.allyLevel    = wingUp;
        g.levelUpFlash = 2.5f;
    }

    /* Dev aid: sit on the title card, screenshot it and quit. */
    if (titleCard) {
        float shotAt = (demoSeconds > 0.0f) ? demoSeconds : 0.7f;

        while (!WindowShouldClose() && g.timer < shotAt) {
            float dt = fminf(GetFrameTime(), 0.05f);
            g.timer += dt;
            DrawFrame();
        }

        CaptureVirtualScreen(shotPath ? shotPath : "title.png");
        TraceLog(LOG_INFO, "DEMO: captured title card");

        UnloadRenderTexture(target);
        AssetsUnload();
        CloseAudioDevice();
        CloseWindow();
        return 0;
    }

    while (!WindowShouldClose() && !wantQuit) {
        float dt = fminf(GetFrameTime(), 0.05f);
        g.timer += dt;

        HandleGlobalKeys();

        if (g.damageFlash > 0.0f) g.damageFlash -= dt * 3.0f;
        if (g.shake > 0.0f)       g.shake       -= dt * 26.0f;
        if (g.messageTimer > 0.0f) g.messageTimer -= dt;

        if (g.autoPilot && g.state == STATE_PLAY) AutoPilotInput(dt);
        else if (g.state == STATE_PLAY)           GatherInput();
        else                                      input = (InputState){ 0 };

#if defined(__EMSCRIPTEN__)
        /* Hide the OS cursor during play; show it in menus. */
        if (g.state == STATE_PLAY) {
            EM_ASM({ document.getElementById('canvas').style.cursor = 'none'; });
        } else {
            EM_ASM({ document.getElementById('canvas').style.cursor = 'crosshair'; });
        }
#endif

#if defined(__EMSCRIPTEN__)
        if (g.state == STATE_PLAY) {
            EM_ASM({
                var dbg = document.getElementById('debug');
                if (dbg) dbg.textContent = 'jsMouse:' + window.__mx + ',' + window.__my +
                    ' reticle:' + $0 + ',' + $1 + ' aim:' + $2.toFixed(2) + ',' + $3.toFixed(2);
            }, input.reticle.x, input.reticle.y, input.aim.x, input.aim.y);
        }
#endif

        /* The game draws its own crosshair on the aim point, so the OS arrow
           would only be a second, differently-placed cursor on top of it. It
           comes back the moment we are in a menu. */
        {
            /* On the web the OS cursor is kept visible: HideCursor is not
               reliably implemented there, and if the game's own reticle ever
               fails to track (a stuck mouse, a drifting pad) the player
               would be aiming blind. The game reticle still draws on top. */
#if !defined(__EMSCRIPTEN__)
            static bool cursorHidden = false;
            bool wantHidden = (g.state == STATE_PLAY || g.state == STATE_TITLE) &&
                              !g.autoPilot;
            if (wantHidden != cursorHidden) {
                if (wantHidden) HideCursor(); else ShowCursor();
                cursorHidden = wantHidden;
            }
#endif
        }

        if (g.state == STATE_PLAY) {
            UpdateGameCamera(dt);
            UpdatePlaying(dt);
            ChatUpdate(dt);
        }

        /* Dev: scripted mouse, so the aim pipeline can be exercised without a
           hand on the device. Moves the cursor to a known spot, then logs what
           the game actually saw. */
        if (mouseTest) {
            static float mt = 0.0f;
            mt += dt;
            int phase = (int)(mt * 2.0f) % 4;
            Vector2 target = (phase == 0) ? V2(120.0f, 90.0f)
                          : (phase == 1) ? V2(400.0f, 200.0f)
                          : (phase == 2) ? V2(240.0f, 135.0f)
                                         : V2( 60.0f, 220.0f);
            SetMousePosition((int)target.x, (int)target.y);

            static float logt = 0.0f;
            logt += dt;
            if (logt >= 0.4f) {
                logt = 0.0f;
                Vector2 mp = GetMousePosition();
                TraceLog(LOG_INFO, "MOUSETEST: mouse=(%.1f,%.1f) virtual aim=(%.2f,%.2f) pad=%d",
                         mp.x, mp.y, input.aim.x, input.aim.y, input.usingGamepad);
            }
            if (mt >= 2.2f) { CaptureVirtualScreen("mousetest.png"); return 0; }
        }

        /* Dev aid: keep the wing-rearm banner pinned open for a screenshot. */
        if (wingUp >= 0) g.levelUpFlash = 2.5f;

        /* looping engine hum, louder the faster we fly */
        if (!muted && g.state == STATE_PLAY && g.player.alive) {
            if (!IsSoundPlaying(assets.engine)) {
                SetSoundVolume(assets.engine, 0.06f);
                PlaySound(assets.engine);
            }
        } else if (IsSoundPlaying(assets.engine)) {
            StopSound(assets.engine);
        }

        DrawFrame();

        if (g.autoPilot && g.autoPilotTime >= demoSeconds) {
            CaptureVirtualScreen(shotPath ? shotPath : "pixel_squadron.png");
            TraceLog(LOG_INFO, "DEMO: captured after %.1fs (score %d, wave %d, kills %d, crits %d)",
                     g.autoPilotTime, g.score, g.wave, g.kills, g.crits);

            /* Census of whatever is still in the air, so a run's composition
               can be checked without squinting at a screenshot. */
            int census[ENEMY_KIND_COUNT] = { 0 };
            int allyShots = 0;
            for (int i = 0; i < MAX_ENEMIES; i++)
                if (g.enemies[i].alive) census[g.enemies[i].kind]++;
            for (int i = 0; i < MAX_BULLETS; i++)
                if (g.bullets[i].alive && g.bullets[i].owner == BULLET_ALLY) allyShots++;

            TraceLog(LOG_INFO, "DEMO: enemies S%d R%d H%d dart%d lancer%d split%d tender%d"
                               " | wing kills %d/%d/%d, ally shots %d, rally %.1f",
                     census[ENEMY_SCOUT], census[ENEMY_RAIDER], census[ENEMY_HEAVY],
                     census[ENEMY_DART], census[ENEMY_LANCER], census[ENEMY_SPLITTER],
                     census[ENEMY_TENDER],
                     g.allyKills[0], g.allyKills[1], g.allyKills[2], allyShots, g.rallyTimer);
            break;
        }

        if (endCard && g.timer >= demoSeconds) {
            CaptureVirtualScreen(shotPath ? shotPath : "pixel_squadron.png");
            TraceLog(LOG_INFO, "DEMO: captured end card");
            break;
        }
    }

    PostFxUnload();
    UnloadRenderTexture(target);
    GfxSettingsSave();
    AssetsUnload();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
