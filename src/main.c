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
static void SetVirtualScreen(int w, int h)
{
    if (target.id != 0 && w == viewW && h == viewH) return;

    if (target.id != 0) UnloadRenderTexture(target);

    target = LoadRenderTexture(w, h);
    SetTextureFilter(target.texture, TEXTURE_FILTER_POINT);

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

        /* Aiming out of the deadzone takes the crosshair; only a proper push
           opens fire, so a worn stick that never quite centres cannot spray
           the map on its own. */
        float rsLen = V2Len(rs);
        /* A high deadzone: a worn or phantom stick must never be able to
           steal the aim from the mouse. Only a deliberate push counts. */
        if (rsLen > 0.55f) {
            aim    = V2Norm(rs);
            padAim = true;
        }
        if (rsLen > 0.62f) firing = true;

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
        Vector2 mouseWorld = V2Add(camera.target, V2Sub(mouseVirtual, V2(BASE_W * 0.5f, BASE_H * 0.5f)));
        Vector2 toMouse    = V2Sub(mouseWorld, g.player.pos);
        if (V2Len(toMouse) > 1.0f) aim = V2Norm(toMouse);
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

    /* blit the virtual screen, filling the window exactly */
    BeginDrawing();
    ClearBackground((Color){ 8, 12, 18, 255 });

    Rectangle src = { 0.0f, 0.0f, (float)target.texture.width, (float)-target.texture.height };
    Rectangle dst = { viewOffset.x, viewOffset.y,
                      RENDER_W * viewScale, RENDER_H * viewScale };
    DrawTexturePro(target.texture, src, dst, V2(0, 0), 0.0f, WHITE);

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

            if (IsKeyPressed(KEY_C)) { remapActive = true; remapArmed = false; break; }

            if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) ||
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
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(BASE_W * 3, BASE_H * 3, "Pixel Squadron - raylib twin-stick shooter");
    InitAudioDevice();
#else
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(BASE_W * 3, BASE_H * 3, "Pixel Squadron - raylib twin-stick shooter");
    InitAudioDevice();

    /* A headless demo should not hijack the desktop. */
    if (demoSeconds > 0.0f || endCard) startFlags &= ~START_FULLSCREEN;

    FitWindowToIntegerScale(startFlags);
    if (startFlags & START_FULLSCREEN) SetFullscreen(true);
#endif

    SetExitKey(KEY_NULL);          /* we handle Escape ourselves */
    SetTargetFPS(60);
    SetRandomSeed(seed ? seed : (unsigned)time(NULL));
    ArenaForceBiome(forceBiome);   /* no-op unless --biome was passed */

    LoadHighScore();
    BindsLoad();
    AssetsLoad();

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

    UnloadRenderTexture(target);
    AssetsUnload();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
