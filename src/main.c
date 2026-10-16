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

Game       g     = {0};
InputState input = {0};

/* --------------------------------------------------------------------------
 *  Presentation state
 * ----------------------------------------------------------------------- */
static RenderTexture2D target;
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
 *  Viewport
 *
 *  The window is created without FLAG_WINDOW_HIGHDPI, so its framebuffer is
 *  exactly the size we asked for in pixels: on a scaled Windows desktop GLFW
 *  still creates a DPI-aware window, Windows does not stretch it, and raylib
 *  leaves its internal screen scaling at identity. That means screen size ==
 *  framebuffer size, the arithmetic below is honest, and the blit lands on
 *  whole pixels with no DPI guesswork anywhere.
 * ----------------------------------------------------------------------- */
static void UpdateViewport(void)
{
    float w = (float)GetScreenWidth();
    float h = (float)GetScreenHeight();

    float s = fminf(w / RENDER_W, h / RENDER_H);
    if (s >= 1.0f) s = floorf(s);   /* whole-number zoom keeps pixels square */
    if (s < 0.05f) s = 0.05f;

    /* The offsets must be whole pixels too: a half-pixel here would land the
       blit between two texels and smear the whole picture. */
    viewScale  = s;
    viewOffset = V2(floorf((w - RENDER_W * s) * 0.5f),
                    floorf((h - RENDER_H * s) * 0.5f));
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

    bool    firing   = IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsKeyDown(KEY_SPACE);
    bool    gamepad  = false;
    Vector2 aim      = V2(0, 0);

    if (IsGamepadAvailable(0)) {
        Vector2 ls = V2(GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X),
                        GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y));
        Vector2 rs = V2(GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X),
                        GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y));

        if (V2Len(ls) > 0.22f) { move = ls; gamepad = true; }

        if (V2Len(rs) > 0.28f) {
            aim     = V2Norm(rs);
            firing  = true;
            gamepad = true;
        }
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) ||
            IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) {
            firing  = true;
            gamepad = true;
        }
    }

    Vector2 mouseVirtual = ScreenToVirtual(GetMousePosition());

    if (!gamepad) {
        Vector2 mouseWorld = GetScreenToWorld2D(mouseVirtual, camera);
        Vector2 toMouse    = V2Sub(mouseWorld, g.player.pos);
        if (V2Len(toMouse) > 1.0f) aim = V2Norm(toMouse);
    }

    input.move         = move;
    input.aim          = aim;
    input.firing       = firing;
    input.boost        = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    input.usingGamepad = gamepad;
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
    desired.x = Clamp(desired.x, 80.0f, WORLD_W - 80.0f);
    desired.y = Clamp(desired.y, 80.0f, WORLD_H - 80.0f);
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
    input.usingGamepad = true;   /* suppresses the mouse reticle */
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

    want.x = Clamp(want.x, RENDER_W * 0.5f, WORLD_W - RENDER_W * 0.5f);
    want.y = Clamp(want.y, RENDER_H * 0.5f, WORLD_H - RENDER_H * 0.5f);

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

    /* hit-stop: freeze the simulation for a couple of frames on a big kill */
    float step = dt;
    if (g.hitStop > 0.0f) {
        g.hitStop -= dt;
        step = dt * 0.15f;
    }

    PlayerUpdate(step);
    EnemiesUpdate(step);
    AlliesUpdate(step);
    BulletsUpdate(step);
    PickupsUpdate(step);
    ParticlesUpdate(step);
    BurstsUpdate(step);

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
    AlliesDraw();
    PlayerDraw();
    BurstsDraw();
    ParticlesDraw();
}

static void DrawFrame(void)
{
    BeginTextureMode(target);
    ClearBackground(COL_DEEP);

    if (g.state == STATE_TITLE) {
        /* slow pan over the land the next run will start on, plus a few idle
           particles */
        camera.target = V2(g.player.pos.x + sinf(g.timer * 0.11f) * 150.0f,
                           g.player.pos.y + cosf(g.timer * 0.15f) * 90.0f);
        camera.target.x = Clamp(camera.target.x, RENDER_W * 0.5f, WORLD_W - RENDER_W * 0.5f);
        camera.target.y = Clamp(camera.target.y, RENDER_H * 0.5f, WORLD_H - RENDER_H * 0.5f);
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

    /* blit the virtual screen, letterboxed and pixel-aligned */
    UpdateViewport();

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

/* The window is always sized so that its framebuffer is an exact whole
   multiple of the 480x270 virtual screen. Otherwise the integer zoom factor
   cannot fill the window and black letterbox bars appear everywhere, even
   though the aspect ratio matches. */
static void FitWindowToIntegerScale(int flags)
{
    int monW = GetMonitorWidth(GetCurrentMonitor());
    int monH = GetMonitorHeight(GetCurrentMonitor());

    if (monW <= 0 || monH <= 0) {          /* no monitor info, keep what we have */
        monW = RENDER_W * 4;
        monH = RENDER_H * 4;
    }

    int n = 8;
    while (n > 1 && (RENDER_W * n > (int)(monW * 0.92f) ||
                     RENDER_H * n > (int)(monH * 0.92f))) n--;

    SetWindowSize(RENDER_W * n, RENDER_H * n);

    if (flags & START_CENTER) {
        SetWindowPosition((monW - RENDER_W * n) / 2, (monH - RENDER_H * n) / 2);
    }
}

/* "Fullscreen" is a borderless window covering the monitor rather than a real
   video-mode switch. raylib's fullscreen path letterboxes its own viewport
   when the display aspect does not match the requested one, and it keeps that
   offset internal - our blit cannot see it, so the picture ends up cropped.
   A borderless window keeps the framebuffer identical to the windowed case. */
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

/* --------------------------------------------------------------------------
 *  Global keys
 * ----------------------------------------------------------------------- */
static void HandleGlobalKeys(void)
{
    if (IsKeyPressed(KEY_F11)) SetFullscreen(!fullscreenOn);

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
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) ||
                IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
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
            if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ENTER) ||
                IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) {
                g.state = STATE_PLAY;
            }
            if (IsKeyPressed(KEY_ESCAPE)) {
                g.state = STATE_TITLE;
            }
            break;

        case STATE_GAMEOVER:
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) ||
                IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
                StartNewRun();
            }
            if (IsKeyPressed(KEY_ESCAPE)) {
                g.state = STATE_TITLE;
            }
            break;
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
    bool captureTitle = false;
    float captureTime = 0.0f;
    int startFlags = START_CENTER | START_FULLSCREEN;   /* play big by default */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--demo") == 0 && i + 1 < argc) {
            demoSeconds = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shotPath = argv[++i];
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--title") == 0) {
            captureTitle = true;
        } else if (strcmp(argv[i], "--fullscreen") == 0) {
            startFlags |= START_FULLSCREEN;
        } else if (strcmp(argv[i], "--windowed") == 0) {
            startFlags &= ~START_FULLSCREEN;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Pixel Squadron\n"
                   "  --seed N          start from a specific terrain seed\n"
                   "  --demo SECONDS    play itself, then screenshot and quit\n"
                   "  --shot FILE       where the demo writes its screenshot\n"
                   "  --title           demo the title screen instead of gameplay\n"
                   "  --windowed        start in a window (default is fullscreen)\n"
                   "  --fullscreen      force fullscreen\n"
                   "\n"
                   "  F11 toggles fullscreen at any time.\n"
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
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(RENDER_W * 3, RENDER_H * 3, "Pixel Squadron - raylib twin-stick shooter");
    InitAudioDevice();

    /* A headless demo should not hijack the desktop. */
    if (demoSeconds > 0.0f) startFlags &= ~START_FULLSCREEN;

    FitWindowToIntegerScale(startFlags);
    if (startFlags & START_FULLSCREEN) SetFullscreen(true);

    SetExitKey(KEY_NULL);          /* we handle Escape ourselves */
    SetTargetFPS(60);
    SetRandomSeed(seed ? seed : (unsigned)time(NULL));

    LoadHighScore();
    AssetsLoad();

    target = LoadRenderTexture(RENDER_W, RENDER_H);
    SetTextureFilter(target.texture, TEXTURE_FILTER_POINT);

    camera = (Camera2D){
        .offset   = V2(RENDER_W * 0.5f, RENDER_H * 0.5f),
        .target   = V2(WORLD_W * 0.5f, WORLD_H * 0.5f),
        .rotation = 0.0f,
        .zoom     = 1.0f,
    };
    cameraPos = camera.target;

    ArenaGenerate(seed ? seed : 1337u);
    EntitiesReset();
    g.state = STATE_TITLE;

    bool capturing = (demoSeconds > 0.0f);

    if (capturing && !captureTitle) {
        StartNewRun();
        g.autoPilot = true;
    }

    while (!WindowShouldClose() && !wantQuit) {
        float dt = fminf(GetFrameTime(), 0.05f);
        g.timer += dt;

        if (g.state != STATE_TITLE) HandleGlobalKeys();
        else HandleGlobalKeys();

        if (g.damageFlash > 0.0f) g.damageFlash -= dt * 3.0f;
        if (g.shake > 0.0f)       g.shake       -= dt * 26.0f;
        if (g.messageTimer > 0.0f) g.messageTimer -= dt;

        if (g.autoPilot && g.state == STATE_PLAY) AutoPilotInput(dt);
        else if (g.state == STATE_PLAY)           GatherInput();
        else                                      input = (InputState){ 0 };

        if (g.state == STATE_PLAY) {
            UpdateGameCamera(dt);
            UpdatePlaying(dt);
            ChatUpdate(dt);
        } else if (g.state == STATE_TITLE) {
            UpdateViewport();
        }

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
            TraceLog(LOG_INFO, "DEMO: captured after %.1fs (score %d, wave %d)",
                     g.autoPilotTime, g.score, g.wave);
            break;
        }

        if (capturing && captureTitle) {
            captureTime += dt;
            if (captureTime >= demoSeconds) {
                CaptureVirtualScreen(shotPath ? shotPath : "pixel_squadron.png");
                TraceLog(LOG_INFO, "DEMO: captured title screen");
                break;
            }
        }
    }

    UnloadRenderTexture(target);
    AssetsUnload();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
