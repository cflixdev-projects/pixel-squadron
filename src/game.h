/*****************************************************************************
 *  PIXEL SQUADRON
 *  A small top-down twin-stick arena shooter built with raylib.
 *
 *  game.h - shared tuning constants, types and module interfaces.
 *
 *  Modules
 *      assets.c    sprite atlas helpers, loading, sound
 *      arena.c     procedural island terrain
 *      entities.c  player, enemies, bullets, particles, pickups
 *      hud.c       in-game overlay + pixel number rendering
 *      main.c      bootstrap, state machine, frame loop
 *****************************************************************************/
#ifndef GAME_H
#define GAME_H

#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 *  Presentation
 *  Everything is drawn into a render target and blown up with nearest-
 *  neighbour filtering, so the game keeps a crisp pixel-art look.
 *
 *  BASE_W x BASE_H is the design size - what you get on a 1920x1080 window.
 *  The real render target is then sized to the window: the zoom is always a
 *  whole number and the target grows until it covers the window completely, so
 *  there are never letterbox bars and nothing is ever cropped. A wide window
 *  simply sees more of the world, and because the zoom is a whole number the
 *  pixels stay square.
 *
 *  Anything that lays itself out against the screen (the HUD, the radio log,
 *  the camera) reads RENDER_W / RENDER_H, which follow the current target.
 * ---------------------------------------------------------------------- */
#define BASE_W 480
#define BASE_H 270

extern int gViewW;   /* virtual screen size for the current window */
extern int gViewH;

#define RENDER_W (gViewW)
#define RENDER_H (gViewH)

#define INTERNAL_ZOOM 1.0f

/* A sprite drawn at a fractional position is sampled against a different part
   of its texel every single frame, so its outline crawls. Snapping the draw
   position to whole pixels - and turning sprites in fixed angular steps
   instead of continuously - is what keeps moving pixel art looking solid. */
#define ROT_STEPS 32.0f
#define ROT_STEP  (360.0f / ROT_STEPS)

static inline float SnapPix(float v) { return floorf(v); }
static inline float SnapAngle(float deg)
{
    return floorf(deg / ROT_STEP + 0.5f) * ROT_STEP;
}

/* The archipelago has no edges: the terrain is a pure function of the tile
   coordinate, so the squadron can fly in any direction for as long as it
   lives. Tiles are only ever looked up, never stored. */
#define TILE_SIZE 16

/* -------------------------------------------------------------------------
 *  Sprite atlases
 *  assets/sprites/ships.png : 4 columns x 6 rows of 32x32 cells
 *  assets/sprites/tiles.png : 12 columns x 10 rows of 16x16 cells
 *  Both sheets draw their sprites pointing "up", so a sprite that should
 *  aim along `a` degrees of the world's X axis is rotated by a + 90.
 * ---------------------------------------------------------------------- */
#define SHIP_CELL 32
#define SHIP_COLS 4
#define TILE_CELL 16
#define TILE_COLS 12
#define ART_FORWARD 90.0f

/* Kenney packs cells edge to edge, so a sprite drawn at a fractional screen
   position can sample the neighbouring cell and smear a line of its colour
   along the edge. Every atlas is rebuilt at load time with a one pixel
   replicated border around each cell, which makes that impossible. */
#define ATLAS_PAD 1

/* Cell indices inside ships.png */
enum ShipSprite {
    SHIP_PLAYER       = 0,  /* blue fighter          */
    SHIP_PLAYER_ALT   = 2,  /* green fighter         */
    SHIP_WINGMAN      = 20, /* pale fighter - the wingmen tint it per pilot  */
    SHIP_ALLY         = 8,  /* blue twin - wingmen. Blue reads clearly on both
                               grass and sand, and the player is always drawn
                               dead centre so the two can never be confused. */
    SHIP_ENEMY_SCOUT  = 5,  /* red, swept wings      */
    SHIP_ENEMY_RAIDER = 9,  /* red flying wing       */
    SHIP_ENEMY_HEAVY  = 12, /* grey gunship          */
};

/* Cell indices inside tiles.png */
enum TileSprite {
    TILE_BOLT         = 0,  /* orange plasma bolt    */
    TILE_BOLT_DOUBLE  = 1,
    TILE_BOLT_WHITE   = 2,  /* enemy bolt            */
    TILE_BOMB         = 3,
    TILE_FX_BURST1    = 4,  /* explosion animation   */
    TILE_FX_BURST2    = 5,
    TILE_FX_BURST3    = 6,
    TILE_FX_SPARKS    = 7,
    TILE_FX_SMOKE     = 8,
    TILE_PIXEL        = 9,  /* single lit pixel      */
    TILE_MINE         = 16,
    TILE_DIGIT_0      = 19, /* digits 0..4 live at 19..23 */
    TILE_DIGIT_5      = 31, /* digits 5..9 live at 31..35 */
    TILE_PICK_HEALTH  = 24,
    TILE_PICK_POWER   = 25,
    TILE_PICK_SHIELD  = 26,
    TILE_TREE_A       = 48,
    TILE_TREE_B       = 60,
    TILE_TREE_C       = 36,
    TILE_GRASS        = 50,
    TILE_GRASS_ALT    = 110, /* same green, different speckle layout */
    TILE_ROCK         = 84,
    TILE_ROCK_ALT     = 72,
    TILE_SAND         = 56,
    TILE_SAND_ALT     = 116,

    /* The sheet's two crags are painted in a single hot orange, which is right
       on a green island and wrong almost everywhere else - and a tint can only
       darken, so it can never make them grey. Instead the two stamps are baked
       out again once per biome at load time, into two spare rows of the atlas
       (see BakeBiomeCrag in assets.c). */
    TILE_ROCK_BIOME     = 120,  /* + biome index, 5 cells */
    TILE_ROCK_ALT_BIOME = 132,  /* ...the peak variant   */
};

/* The atlas grows two rows so the crags can be baked out once per biome. */
#define TILE_ROWS 12

/* Which country a tile belongs to. */
typedef enum {
    BIOME_TEMPERATE = 0,
    BIOME_DESERT,
    BIOME_SNOW,
    BIOME_ASH,
    BIOME_JUNGLE,
    BIOME_COUNT
} BiomeId;

/* The three tones the crag stamps are painted in, repainted per biome:
   light, mid, dark, in the order the sheet uses them. Arena owns the table. */
extern const Color kRockRamp[BIOME_COUNT][3];

/* -------------------------------------------------------------------------
 *  Palette - sampled straight out of the Kenney tilesheet so that code
 *  drawn elements (water, foam, HUD) sit next to the art seamlessly.
 * ---------------------------------------------------------------------- */
#define COL_DEEP    CLITERAL(Color){ 16,  66,  96, 255 }
#define COL_SEA     CLITERAL(Color){ 26, 108, 148, 255 }
#define COL_WATER   CLITERAL(Color){ 44, 197, 246, 255 }
#define COL_SHALLOW CLITERAL(Color){ 126, 226, 250, 255 }
#define COL_FOAM    CLITERAL(Color){ 223, 246, 245, 255 }
#define COL_INK     CLITERAL(Color){ 52,  85,  81, 255 } /* outline colour in the art */
#define COL_UI_BG   CLITERAL(Color){ 24,  34,  46, 230 }
#define COL_UI_LINE CLITERAL(Color){ 126, 226, 250, 255 }
#define COL_HP      CLITERAL(Color){ 252, 104,  59, 255 }
#define COL_HEAL    CLITERAL(Color){ 140, 245, 170, 255 } /* JUNO's repair field */
#define COL_HP_LOW  CLITERAL(Color){ 222,  75,  30, 255 }
#define COL_ALLY    CLITERAL(Color){ 126, 200, 255, 255 } /* wingman blue       */
#define COL_ALLY_UI CLITERAL(Color){ 176, 226, 255, 255 }

/* -------------------------------------------------------------------------
 *  Pools
 * ---------------------------------------------------------------------- */
#define MAX_BULLETS 768
#define MAX_ENEMIES 160
#define MAX_PARTICLES 1536
#define MAX_PICKUPS 48
#define MAX_BURSTS 80
#define MAX_POPUPS 40
#define MAX_ALLIES 3
#define ALLY_RESPAWN 15.0f  /* seconds before a downed wingman rejoins */
#define ALLY_POWER_TIME  9.0f    /* a weapon crate keeps a wingman hot   */
#define ALLY_SHIELD_TIME 8.0f    /* a shield crate keeps a wingman alive  */
#define ALLY_CRATE_RANGE 430.0f  /* how far they will detour for loot    */
#define ALLY_CRATE_RUSH  190.0f  /* ...and how close a crate has to be to
                                    make them break off a fight for it     */

#define PLAYER_MAX_HP 100.0f

/* Player buff durations, so the HUD can draw a meter for each. */
#define POWER_DURATION    10.0f   /* seconds of the rapid-fire buff  */
#define SHIELD_DURATION    6.0f

/* Weapon crates the player can find out on the map. */
#define PLAYER_QUAD_TIME   7.0f
#define PLAYER_SPREAD_TIME 7.0f
#define PLAYER_HOMING_TIME 8.0f

/* The player's ultimate: a radial shove that clears the deck and makes the
   next second and a bit free. */
#define SPECIAL_COOLDOWN 15.0f
#define SPECIAL_RADIUS   150.0f
#define SPECIAL_DAMAGE   90.0f
#define SPECIAL_SHIELD   1.4f

/* JUNO's dome. */
#define FIELD_LIFE      4.5f
#define FIELD_RADIUS    86.0f
#define FIELD_SHOVE     250.0f  /* speed enemies are pushed out at         */
#define FIELD_HEAL      9.0f    /* hull per second to everyone inside      */
#define FIELD_COOLDOWN  16.0f

/* Holding Shift spends boost fuel for a short burst of extra top speed. */
#define PLAYER_BOOST_FUEL 1.6f   /* seconds of boost available          */
#define PLAYER_BOOST_MULT 1.75f  /* top-speed multiplier while boosting */

/* The player can call the wing back at any time; the radio takes a moment to
   recover before it can be used again. A recall is an order to come home *and*
   fight, so the wing sprints in and then holds station around the player,
   shooting whatever comes near. */
#define RALLY_TIME 5.5f
#define RALLY_COOLDOWN 7.0f
#define ALLY_RALLY_ARRIVE 86.0f  /* inside this of the slot = on station    */

/* Chance that any given friendly hit lands critically, for extra hurt. */
#define CRIT_CHANCE 20      /* percent */
#define CRIT_MULT   2.5f

/* -------------------------------------------------------------------------
 *  Terrain
 * ---------------------------------------------------------------------- */
typedef enum {
    TER_WATER,
    TER_SAND,
    TER_GRASS,
} TerrainKind;

/* -------------------------------------------------------------------------
 *  Entities
 * ---------------------------------------------------------------------- */
typedef struct {
    Vector2 pos, vel;
    float angle;       /* radians, 0 = +X */
    float hp;
    float fireTimer;
    float powerTimer;  /* remaining seconds of the power-up  */
    float shieldTimer; /* remaining seconds of invulnerability */
    float engineTimer; /* thruster particle accumulator */
    int weaponLevel;   /* 1..3 */
    float hitFlash;
    float boostFuel;   /* seconds of boost left (0..PLAYER_BOOST_FUEL) */
    float specialTimer;/* counts down to the next special attack      */
    int   specialSel;  /* which special move is loaded from the pool  */
    float quadTimer;   /* weapon crates that are running right now    */
    float spreadTimer;
    float homingTimer;
    bool alive;
} Player;

typedef enum {
    ENEMY_SCOUT,     /* the baseline: circles and pecks                  */
    ENEMY_RAIDER,    /* fast, fragile, eager                             */
    ENEMY_HEAVY,     /* slow gunship, three-wide volleys                 */
    ENEMY_DART,      /* tiny, very fast, dives in and rams               */
    ENEMY_LANCER,    /* hangs back and snipes; charges in plain sight    */
    ENEMY_SPLITTER,  /* bursts into two darts when it dies               */
    ENEMY_TENDER,    /* patches up everything around it - kill it first  */
    ENEMY_KIND_COUNT
} EnemyKind;

typedef struct {
    bool alive;
    EnemyKind kind;
    Vector2 pos, vel;
    float angle;
    float hp, maxHp;
    float fireTimer;
    float radius;
    float orbit;      /* preferred distance from the player */
    float wobble;     /* per-enemy phase offset             */
    float hitFlash;
    float spawnFade;  /* 1 -> 0 while materialising         */
    float charge;     /* lancer wind-up, >0 while lining up a shot */
    int score;
    bool elite;       /* the plated variant: more hull, worth a lot more */
} Enemy;

/* Who fired a bullet: enemies hurt the squadron, the squadron hurts enemies. */
typedef enum {
    BULLET_ENEMY = 0,
    BULLET_PLAYER,
    BULLET_ALLY,
} BulletOwner;

/* The three wingmen are individuals, not clones: each one flies a different
   special weapon, wears a different paint job and talks like their own
   person. The profile table lives in entities.c. */
typedef enum {
    ALLY_VEX = 0,   /* hot-headed  - scattergun   */
    ALLY_JUNO,      /* steady       - repair field */
    ALLY_BIT,       /* nervous kid  - seeker swarm */
    ALLY_ROLE_COUNT
} AllyRole;

typedef struct {
    const char *name;
    Color       hull;    /* tint for the ship sprite          */
    Color       text;    /* readable version of it for text   */
    int         ship;    /* cell in ships.png                 */
    float       cooldown;/* seconds between specials          */
    const char *special; /* name shown in the HUD             */
} AllyProfile;

const AllyProfile *AllyProfileOf(int role);

/* A friendly wingman. It flies formation off the player's wing, breaks off to
   engage anything that comes close, and is rebuilt after a short while if it
   gets shot down. */
typedef struct {
    bool    alive;
    Vector2 pos, vel;
    float   angle;
    float   hp, maxHp;
    float   fireTimer;
    float   hitFlash;
    float   spawnFade;
    float   respawnTimer;  /* only meaningful while !alive */
    int     slot;          /* formation slot, also the HUD column */
    int     role;          /* AllyRole */
    float   specialTimer;  /* counts down to the next special attack */
    int     specialSel;    /* which special from their own bag is up */
    float   specialFlash;  /* >0 for a moment after the special fires */
    float   distress;      /* >0 while calling for help (drives the beacon) */
    float   helpCool;      /* quiet period between two calls for help */
    float   assistTimer;   /* >0 while running to rescue a buddy */
    Vector2 assistAt;      /* where the buddy in trouble was last seen */
    float   rallyTimer;    /* >0 while answering the player's recall */
    float   powerTimer;    /* >0 while their guns are hot from a crate */
    float   shieldTimer;   /* >0 while a shield crate is holding      */
    float   quadTimer;     /* >0 while they are carrying the other crates */
    float   spreadTimer;
    float   homingTimer;
} Ally;

/* The shared special-move pool. Every pilot - the player and the wingmen -
   draws from one table: each move has its own name and its own cooldown,
   scaled to how strong it is, and anyone can only ever have one loaded. */
int         PlayerSpecialCount(void);
const char *PlayerSpecialName(int idx);
float       PlayerSpecialCooldown(int idx);
const char *AllySpecialName(const Ally *a);
float       AllySpecialCooldown(const Ally *a);

typedef struct {
    bool alive;
    Vector2 pos, vel;
    float angle;
    float life;
    float damage;
    int sprite;
    float scale;
    int owner;   /* BulletOwner */
    int slot;    /* which wingman fired it, -1 otherwise */
    float homing;/* turn rate in rad/s, 0 = flies straight */
} Bullet;

typedef struct {
    bool alive;
    Vector2 pos, vel;
    float life, maxLife;
    float size;
    float angle, spin;
    Color color;
} Particle;

typedef struct {
    bool alive;
    Vector2 pos;
    float t;
    float scale;
} Burst;

typedef enum {
    PICK_HEALTH,     /* hull repair                                   */
    PICK_POWER,      /* hot guns and a weapon level                    */
    PICK_SHIELD,     /* a few seconds of not being hit at all          */
    PICK_QUAD,       /* four barrels at once                          */
    PICK_SPREAD,     /* a wide close-range fan                        */
    PICK_HOMING,     /* rounds that steer themselves                  */
    PICK_KIND_COUNT
} PickupKind;

typedef struct {
    bool alive;
    PickupKind kind;
    Vector2 pos, vel;
    float t;
    float life;
} Pickup;

/* Short-lived floating text - "CRIT" over a shot that landed hard. */
typedef struct {
    bool    alive;
    Vector2 pos;
    float   t, life;
    char    text[12];
    Color   color;
} Popup;

/* JUNO's answer to being surrounded: a bubble she drops around herself that
   shoves enemies and their fire out of the way, patching up everyone standing
   inside it while it holds. The idea is that the squadron gets a few seconds
   to breathe, look at the board and take the fight back up on its own terms. */
typedef struct {
    bool    alive;
    Vector2 pos;      /* follows the caster while it holds */
    float   life;     /* seconds left                      */
    float   radius;
    float   pulse;    /* animation phase                   */
    bool    dropped;  /* a supply crate was handed out     */
} AegisField;

typedef enum {
    STATE_TITLE,     /* attract card over the land the next run will start on */
    STATE_PLAY,
    STATE_PAUSED,
    STATE_GAMEOVER,
} GameState;

/* Filled in by main.c each frame (keyboard/mouse on one hand, gamepad on
   the other) and consumed by the entity update code. */
typedef struct {
    Vector2 move;         /* -1..1 on each axis */
    Vector2 aim;          /* unit vector, world space */
    Vector2 reticle;      /* mouse position in virtual-screen space */
    bool firing;
    bool boost;           /* Shift held - spend boost fuel for speed */
    bool rally;           /* Q / left shoulder - call the wing home   */
    bool special;         /* the player's own ultimate                */
    bool usingGamepad;
    bool autoAim;         /* auto-shoot mode is toggled on   */
    bool chatPopups;      /* chat speech bubbles shown       */
} InputState;

extern InputState input;

/* -------------------------------------------------------------------------
 *  Controls
 *  The pad answers to whatever the player tells it to. Only the pad is
 *  remappable: the keyboard is roomy enough that the defaults are never in
 *  anyone's way, and a half-remapped keyboard is just a way to get stuck.
 * ---------------------------------------------------------------------- */
typedef enum {
    BIND_FIRE = 0,   /* hold to shoot  */
    BIND_BOOST,      /* burn for speed  */
    BIND_RECALL,     /* bring the wing  */
    BIND_SPECIAL,    /* ultimate        */
    BIND_COUNT
} BindAction;

typedef struct {
    int button[BIND_COUNT];  /* GAMEPAD_BUTTON_* ids */
} Bindings;

extern Bindings binds;

void BindsLoadDefaults(void);
void BindsLoad(void);
void BindsSave(void);
const char *BindName(int action);
const char *PadButtonName(int button);

/* The controls panel on the pause screen: main.c owns the state, hud.c draws
   it. RemapSelection is the action being edited, RemapArmed is "waiting for a
   button to be pressed". */
bool RemapActive(void);
int  RemapSelection(void);
bool RemapArmed(void);

/* -------------------------------------------------------------------------
 *  Global game state (defined in main.c)
 * ---------------------------------------------------------------------- */
/* -------------------------------------------------------------------------
 *  Radio chatter
 *  The wingmen talk. Every line is kept in a ring buffer: the speaker gets a
 *  speech bubble floating over their own ship, so any number of them can talk
 *  at once without the radio flip-flopping between voices, and the same
 *  buffer feeds the message log down the right-hand edge of the screen.
 * ---------------------------------------------------------------------- */
#define CHAT_SLOTS 14
#define CHAT_TEXT  72
#define CHAT_LOG   8      /* how many lines the side log keeps          */
#define CHAT_ROW   13     /* vertical pitch of a log row                 */

typedef enum {
    CHAT_KILL,     /* just shot something down      */
    CHAT_BUDDY,    /* a wingman went down           */
    CHAT_HURT,     /* taking heavy damage           */
    CHAT_SWARMED,  /* too many contacts on me       */
    CHAT_IDLE,     /* quiet-moment banter           */
    CHAT_WAVE,     /* new wave inbound              */
    CHAT_CLEAR,    /* wave wiped out                */
    CHAT_REPLY,    /* answering whoever just spoke  */
    CHAT_RALLY,    /* answering the player's recall */
    CHAT_LOOT,     /* just grabbed something off the ground */
    CHAT_ELITE,    /* a plated escort pack just landed     */
    CHAT_EVENTS
} ChatEvent;

typedef struct {
    bool  used;
    int   slot;    /* which wingman is speaking, -1 for a system line */
    char  who[16];
    Color color;
    char  text[CHAT_TEXT];
    float t;       /* seconds since the line appeared */
    float life;    /* total seconds it stays up       */
} ChatLine;

typedef struct {
    GameState state;
    float time;

    Player player;
    Enemy enemies[MAX_ENEMIES];
    Ally  allies[MAX_ALLIES];
    Bullet bullets[MAX_BULLETS];
    Particle particles[MAX_PARTICLES];
    Pickup pickups[MAX_PICKUPS];
    Burst bursts[MAX_BURSTS];
    Popup popups[MAX_POPUPS];

    /* run statistics */
    int score;
    int highScore;
    int wave;
    float waveTimer;     /* >0 -> counting down to the next wave */
    int wavePending;     /* enemies still to spawn this wave      */
    int waveScout;       /* remaining composition of the wave     */
    int waveRaider;
    int waveHeavy;
    int waveDart;
    int waveLancer;
    int waveSplitter;
    int waveTender;
    float spawnTimer;
    float difficulty;
    bool  waveBreak;     /* true during the breather between waves     */
    float stateTimer;    /* generic timer for state transitions        */
    float rallyCool;     /* >0 while the recall radio recharges        */
    float rallyTimer;    /* >0 while the squadron is answering a recall */
    int   crits;         /* how many shots landed critically this run  */
    int   allyKills[MAX_ALLIES];  /* wing kills, per slot - the wing's rank */
    int   allyLevel;     /* how many times the wing has been rearmed    */
    float levelUpFlash;  /* >0 while the "WINGMEN LEVEL UP" banner runs */
    AegisField field;    /* JUNO's dome, when it is up                  */

    /* feel */
    float shake;
    float hitStop;
    float damageFlash;
    float timer;         /* generic free-running clock for animations */

    /* stats shown on the game-over screen */
    int kills;
    float survived;

    char message[32];
    float messageTimer;

    /* wingman radio */
    ChatLine chat[CHAT_SLOTS];
    int      chatNext;      /* ring buffer cursor            */
    float    chatIdle;      /* counts up to the next banter  */

    bool autoPilot;      /* set by --demo so the game can screenshot itself */
    float autoPilotTime;
} Game;

extern Game g;

/* -------------------------------------------------------------------------
 *  assets.c
 * ---------------------------------------------------------------------- */
typedef struct {
    Texture2D ships;
    Texture2D tiles;
    /* Departure Mono is a monospaced pixel font whose native design size is an
       11px em. raylib scales glyphs by (ascent - descent) rather than the em
       square, and this font's (ascent - descent) is 700 units against a 550
       unit em, so it is loaded at 14 to land exactly on that 11px grid and
       rasterise with no anti-aliasing. It must only ever be drawn at whole
       multiples of the native size (scale 1 = 11px, scale 2 = 22px headings):
       any other size resamples the glyphs and chews the lettering up. */
    Font font;       /* 11px native - HUD, radio, help and headings */

    Sound shoot;
    Sound shootHeavy;
    Sound enemyShoot;
    Sound explosion;
    Sound explosionBig;
    Sound hit;
    Sound pickup;
    Sound shield;
    Sound engine;
} Assets;

extern int fpsCap;
extern Assets assets;

void AssetsLoad(void);
void AssetsUnload(void);
void PlaySfx(Sound s, float volume, float pitch);

/* Text drawn from a pixel font has to land on whole pixels. raylib's
   DrawTextEx places glyphs on fractional coordinates, and any font size that
   is not a whole multiple of the font's native size resamples every glyph -
   the letters come out with dropped rows and look glitched. These helpers
   snap each glyph to the pixel grid instead. `scale` must be a whole number. */
float TextWidthPix(Font font, const char *text, int scale);
void  DrawTextPix(Font font, const char *text, Vector2 at, int scale, Color tint);
void  DrawTextPixCentered(Font font, const char *text, float y, int scale, Color tint);
/* Same, plus a 1px drop shadow so the text survives busy terrain. */
void  DrawTextPixShadow(Font font, const char *text, Vector2 at, int scale, Color tint);

/* Source rectangle of cell `idx` inside an atlas whose cells are `cell`
   pixels tall and `cols` wide. Accounts for the replicated padding border. */
Rectangle AtlasSource(int idx, int cols, int cell);

/* Draw cell `idx` of an atlas whose cells are `cell` pixels and `cols` wide.
   `rotation` is degrees (clockwise); provide ART_FORWARD + angle for art
   that points up. `pos` is the centre of the destination rectangle. */
void DrawCell(Texture2D atlas, int idx, int cols, int cell, Vector2 pos,
              float rotation, float scale, Color tint);
void DrawShip(int idx, Vector2 pos, float rotDeg, float scale, Color tint);
void DrawTileSprite(int idx, Vector2 pos, float rotDeg, float scale, Color tint);

/* -------------------------------------------------------------------------
 *  arena.c
 * ---------------------------------------------------------------------- */
void ArenaGenerate(unsigned seed);
TerrainKind ArenaAt(int tx, int ty);
bool ArenaBlocksAt(Vector2 world);
/* Dev switch: force the entire world to be one biome, by index. Anything out
   of range goes back to the normal mix. Used by --biome. */
void ArenaForceBiome(int id);
/* The name of the biome covering a world position - the HUD names the country
   the player is flying over, and the biomes are worth naming. */
const char *ArenaBiomeNameAt(Vector2 world);
/* A random spot on clear land between `rmin` and `rmax` pixels from `centre`.
   Supplies are dropped into the world this way. */
Vector2 ArenaRandomLandNear(Vector2 centre, float rmin, float rmax);
/* A spot in the middle of the most land-rich area near the origin, used to
   place the squadron so a run does not start in the middle of the ocean. */
Vector2 ArenaScenicPos(void);
void ArenaDraw(Camera2D cam);

/* -------------------------------------------------------------------------
 *  entities.c
 * ---------------------------------------------------------------------- */
void EntitiesReset(void);
void PlayerUpdate(float dt);
void PlayerDraw(void);
void PlayerDamage(float amount);
/* The player's own ultimate, fired from the HUD meter. */
void PlayerSpecial(void);
/* JUNO's bubble: cast it, tick it, draw it. */
void FieldCast(Ally *caster);
void FieldUpdate(float dt);
void FieldDraw(void);

void EnemiesUpdate(float dt);
void EnemiesDraw(void);
void EnemiesClear(void);
int  EnemiesAliveCount(void);

/* Wingmen */
void AlliesUpdate(float dt);
void AlliesDraw(void);
/* The recall beacon: a ring on the player plus a tether to everyone still
   inbound. Drawn over the ships so it is obvious what the button did. */
void RallyDraw(void);
void AllyDamage(Ally *a, float amount);
void AllySpecial(Ally *a);
int  AlliesAliveCount(void);
Vector2 AllyFormationPos(int slot);
/* Puts a beacon on the map: the wingman names itself as being in trouble, the
   others turn to help, and the HUD shows a red marker. */
void AllyCallForHelp(Ally *a);
/* The player's recall: every wingman breaks off, comes home and holds station,
   shooting anything that follows them in. */
void AlliesRally(void);
/* Hands the wing out better gear as the waves get meaner. */
void AlliesUpgrade(void);

/* -------------------------------------------------------------------------
 *  chat.c - wingman radio chatter
 * ---------------------------------------------------------------------- */
void ChatReset(void);
void ChatUpdate(float dt);
void ChatDraw(void);
Vector2 WorldToScreen(Vector2 world);   /* main.c - world -> virtual screen */

void ChatSay(int slot, ChatEvent ev);                 /* a wingman speaks     */
void ChatSayText(int slot, const char *text);
void ChatSystem(const char *who, Color color, const char *text);

void SpawnEnemy(EnemyKind kind, Vector2 pos);
void SpawnEnemyElite(EnemyKind kind, Vector2 pos);
/* Returns the bullet so callers can tag it (seeker missiles set homing). */
Bullet *SpawnBullet(Vector2 pos, Vector2 vel, int sprite, float damage, float life,
                    float scale, int owner, int slot);
void SpawnPickup(PickupKind kind, Vector2 pos);
void SpawnBurst(Vector2 pos, float scale);
void SpawnBurstParticles(Vector2 pos, float power, Color tint);
void SpawnPopup(Vector2 pos, const char *text, Color color);

void BulletsUpdate(float dt);
void BulletsDraw(void);
void PickupsUpdate(float dt);
void PickupsDraw(void);
void ParticlesUpdate(float dt);
void ParticlesDraw(void);
void BurstsUpdate(float dt);
void BurstsDraw(void);
void PopupsUpdate(float dt);
void PopupsDraw(void);

void DamageEnemy(Enemy *e, float amount);
void StartWave(int wave);
void WaveSpawnStep(float dt);

/* -------------------------------------------------------------------------
 *  hud.c
 * ---------------------------------------------------------------------- */
void DrawPixelNumber(long value, Vector2 pos, int digits, float scale, Color tint);
void HudDraw(Camera2D cam);
void HudDrawGameOver(void);
void HudDrawPause(void);
void HudDrawTitle(void);

/* -------------------------------------------------------------------------
 *  helpers
 * ---------------------------------------------------------------------- */
static inline Vector2 V2(float x, float y) { return (Vector2){ x, y }; }
static inline Vector2 V2Add(Vector2 a, Vector2 b) { return V2(a.x + b.x, a.y + b.y); }
static inline Vector2 V2Sub(Vector2 a, Vector2 b) { return V2(a.x - b.x, a.y - b.y); }
static inline Vector2 V2Scale(Vector2 a, float s) { return V2(a.x * s, a.y * s); }
static inline float V2Len(Vector2 a) { return sqrtf(a.x * a.x + a.y * a.y); }

static inline Vector2 V2Norm(Vector2 a)
{
    float l = V2Len(a);
    return (l > 0.0001f) ? V2(a.x / l, a.y / l) : V2(0.0f, 0.0f);
}

static inline Vector2 V2ClampLen(Vector2 a, float maxLen)
{
    float l = V2Len(a);
    return (l > maxLen) ? V2(a.x / l * maxLen, a.y / l * maxLen) : a;
}

static inline float WrapAngle(float a)
{
    while (a > PI)  a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

static inline float AngleLerp(float from, float to, float t)
{
    return from + WrapAngle(to - from) * t;
}

/* Squared distance helper - avoids sqrtf in the hot collision loops. */
static inline float Dist2(Vector2 a, Vector2 b)
{
    float dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

#endif /* GAME_H */
