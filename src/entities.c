/*****************************************************************************
 *  entities.c - the player, the enemy swarm, bullets, particles and drops.
 *
 *  Everything lives in flat fixed-size pools inside the Game struct. A slot
 *  is either `alive` or free; spawning walks the pool from a rolling cursor
 *  so the oldest entries get recycled first.
 *****************************************************************************/
#include "game.h"

#include <math.h>
#include <stdio.h>

/* --------------------------------------------------------------------------
 *  Tuning
 * ----------------------------------------------------------------------- */
#define PLAYER_ACCEL      1000.0f
#define PLAYER_MAX_SPEED  132.0f
#define PLAYER_FRICTION   7.5f
#define PLAYER_RADIUS     10.0f
#define PLAYER_FIRE_TIME  0.145f
#define PLAYER_BULLET_SPD 320.0f
#define PLAYER_BULLET_DMG 1.0f

#define POWER_DURATION    10.0f   /* seconds of the rapid-fire buff  */
#define HIT_INVULN        0.80f   /* i-frames granted after a hit    */
#define SHIELD_DURATION   6.0f

/* Wingmen */
#define ALLY_MAX_HP       70.0f
#define ALLY_RADIUS       9.0f
#define ALLY_MAX_SPEED    200.0f  /* above the player's 132 (231 while
                                     boosting) so they can catch back up */
#define ALLY_RALLY_SPEED  280.0f  /* sprint while answering a recall       */
#define ALLY_FIRE_TIME    0.28f
#define ALLY_BULLET_SPD   340.0f
#define ALLY_BULLET_DMG   1.0f
#define ALLY_ENGAGE_RANGE 320.0f  /* how far a wingman will break off    */
#define ALLY_LEASH        150.0f  /* ...and how far it may stray doing so */
#define ALLY_STANDOFF     64.0f   /* preferred distance from a target    */
#define ALLY_SPAWN_FADE   0.5f    /* short materialise-in after a respawn */
#define ALLY_HOME_RANGE   1500.0f /* teleports back if it falls this far behind */
#define ALLY_SWARM_RADIUS 150.0f  /* contacts this close count as being swarmed  */
#define ALLY_SWARM_COUNT  4       /* ...and this many of them is a call for help */
#define WINGMAN_RAM_DMG   14.0f

/* Every wave the wing gets rearmed, so they keep pace with a swarm that is
   also getting nastier. The numbers are deliberately close to the enemy
   scaling curve: it should stay a fight. */
#define ALLY_RANK_HP      6.0f    /* extra max hull per wave            */
#define ALLY_RANK_DMG     0.10f   /* extra bullet damage per wave       */
#define ALLY_RANK_FIRE    0.030f  /* faster reload per wave             */
#define ALLY_RANK_COOL    0.035f  /* faster specials per wave           */

#define ENEMY_BULLET_SPD  175.0f
#define ENEMY_BULLET_DMG  9.0f
#define ENEMY_AGGRO_RANGE 380.0f
#define ENEMY_SPAWN_FADE  0.45f

#define BULLET_RADIUS     4.0f

static const int kBurstFrames[] = {
    TILE_FX_BURST1, TILE_FX_BURST2, TILE_FX_BURST3, TILE_FX_SPARKS, TILE_FX_SMOKE
};
#define BURST_FRAMES (int)(sizeof kBurstFrames / sizeof kBurstFrames[0])
#define BURST_FRAME_TIME 0.045f

/* --------------------------------------------------------------------------
 *  Helpers
 * ----------------------------------------------------------------------- */

/* Tint used for the brief white-hot flicker when something takes a hit. */
static Color HitTint(float k)
{
    k = Clamp(k, 0.0f, 1.0f);
    return (Color){
        255,
        (unsigned char)(255.0f - 140.0f * k),
        (unsigned char)(255.0f - 150.0f * k),
        255
    };
}

/* Grab the next free particle and return NULL when the pool is saturated. */
static Particle *NextParticle(void){
    static int cursor = 0;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g.particles[cursor % MAX_PARTICLES];
        cursor++;
        if (!p->alive) return p;
    }
    return NULL;
}

static void EmitParticle(Vector2 pos, Vector2 vel, float life, float size,
                         float spin, Color color)
{
    Particle *p = NextParticle();
    if (!p) return;
    *p = (Particle){
        .alive   = true,
        .pos     = pos,
        .vel     = vel,
        .life    = life,
        .maxLife = life,
        .size    = size,
        .angle   = (float)GetRandomValue(0, 359) * DEG2RAD,
        .spin    = spin,
        .color   = color,
    };
}

/* Wingmen live further down this file, but the enemy AI needs to know who to
   shoot at, so the "nearest friendly" lookup is declared up front. */
static Vector2 NearestFriendlyTo(Vector2 from);

/* --------------------------------------------------------------------------
 *  Pool spawners
 * ----------------------------------------------------------------------- */
Bullet *SpawnBullet(Vector2 pos, Vector2 vel, int sprite, float damage, float life,
                    float scale, int owner, int slot)
{
    static int cursor = 0;

    for (int i = 0; i < MAX_BULLETS; i++) {
        int idx = (cursor + i) % MAX_BULLETS;
        Bullet *b = &g.bullets[idx];
        if (b->alive) continue;

        cursor = idx + 1;
        *b = (Bullet){
            .alive      = true,
            .pos        = pos,
            .vel        = vel,
            .angle      = atan2f(vel.y, vel.x),
            .life       = life,
            .damage     = damage,
            .sprite     = sprite,
            .scale      = scale,
            .owner      = owner,
            .slot       = slot,
            .homing     = 0.0f,
        };
        return b;
    }
    return NULL;
}

void SpawnBurst(Vector2 pos, float scale)
{
    static int cursor = 0;
    for (int i = 0; i < MAX_BURSTS; i++) {
        int idx = (cursor + i) % MAX_BURSTS;
        Burst *b = &g.bursts[idx];
        if (b->alive) continue;

        cursor = idx + 1;
        *b = (Burst){ .alive = true, .pos = pos, .t = 0.0f, .scale = scale };
        return;
    }
}

void SpawnBurstParticles(Vector2 pos, float power, Color tint)
{
    int count = (int)(6.0f + power * 10.0f);

    for (int i = 0; i < count; i++) {
        float ang = (float)GetRandomValue(0, 359) * DEG2RAD;
        float spd = (20.0f + (float)GetRandomValue(0, 100) * 0.9f) * power;

        int shade = GetRandomValue(0, 99);
        Color c = (shade < 55) ? tint
                : (shade < 80) ? (Color){ 255, 236, 159, 255 }
                               : (Color){ 222,  75,  30, 255 };

        EmitParticle(pos,
                     V2(cosf(ang) * spd, sinf(ang) * spd),
                     0.25f + (float)GetRandomValue(0, 45) / 100.0f,
                     (float)GetRandomValue(1, 3) * (0.7f + power * 0.35f),
                     (float)GetRandomValue(-360, 360),
                     c);
    }
}

void SpawnPickup(PickupKind kind, Vector2 pos)
{
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *p = &g.pickups[i];
        if (p->alive) continue;

        *p = (Pickup){
            .alive = true,
            .kind  = kind,
            .pos   = pos,
            .vel   = V2((float)GetRandomValue(-20, 20), (float)GetRandomValue(-20, 20)),
            .t     = 0.0f,
            .life  = 20.0f,
        };
        return;
    }
}

/* Floating combat text. Only crits use it, so it stays rare enough to read. */
void SpawnPopup(Vector2 pos, const char *text, Color color)
{
    static int cursor = 0;

    for (int i = 0; i < MAX_POPUPS; i++) {
        int idx = (cursor + i) % MAX_POPUPS;
        Popup *p = &g.popups[idx];
        if (p->alive) continue;

        cursor = idx + 1;
        *p = (Popup){ .alive = true, .pos = pos, .t = 0.0f, .life = 0.75f,
                      .color = color };
        snprintf(p->text, sizeof p->text, "%s", text ? text : "");
        return;
    }
}

/* What each kind looks like. There are only three enemy hulls on the sheet,
   so the paint job and the scale carry the identity: a yellow speck is a dart,
   a violet dart-plane is a sniper, green is a splitter, gold is the tender. */
typedef struct {
    int    sprite;
    float  scale;
    Color  tint;
} EnemyLook;

static EnemyLook EnemyLookOf(EnemyKind k)
{
    switch (k) {
        case ENEMY_RAIDER:   return (EnemyLook){ SHIP_ENEMY_RAIDER, 1.00f, (Color){ 255, 255, 255, 255 } };
        case ENEMY_HEAVY:    return (EnemyLook){ SHIP_ENEMY_HEAVY,  1.00f, (Color){ 255, 255, 255, 255 } };
        case ENEMY_DART:     return (EnemyLook){ SHIP_ENEMY_SCOUT,  0.70f, (Color){ 255, 214, 110, 255 } };
        case ENEMY_LANCER:   return (EnemyLook){ SHIP_ENEMY_RAIDER, 1.10f, (Color){ 198, 150, 255, 255 } };
        case ENEMY_SPLITTER: return (EnemyLook){ SHIP_ENEMY_HEAVY,  0.85f, (Color){ 140, 235, 150, 255 } };
        case ENEMY_TENDER:   return (EnemyLook){ SHIP_ENEMY_RAIDER, 1.15f, (Color){ 255, 240, 150, 255 } };
        default:             return (EnemyLook){ SHIP_ENEMY_SCOUT,  1.00f, (Color){ 255, 255, 255, 255 } };
    }
}

void SpawnEnemy(EnemyKind kind, Vector2 pos)
{
    static int cursor = 0;

    Enemy *e = NULL;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        int idx = (cursor + i) % MAX_ENEMIES;
        if (!g.enemies[idx].alive) { e = &g.enemies[idx]; cursor = idx + 1; break; }
    }
    if (!e) return;

    float d = g.difficulty;

    switch (kind) {
        case ENEMY_RAIDER:
            e->hp    = 2.0f + d * 0.85f;
            e->radius = 9.0f;
            e->orbit  = 58.0f + (float)GetRandomValue(0, 34);
            e->fireTimer = 0.8f;
            e->score  = 15;
            break;
        case ENEMY_HEAVY:
            e->hp    = 9.0f + d * 2.8f;
            e->radius = 14.0f;
            e->orbit  = 112.0f + (float)GetRandomValue(0, 46);
            e->fireTimer = 1.8f;
            e->score  = 40;
            break;

        case ENEMY_DART:
            /* one good shot kills it; the danger is the swarm and the ram */
            e->hp    = 1.2f + d * 0.45f;
            e->radius = 6.5f;
            e->orbit  = 40.0f + (float)GetRandomValue(0, 26);
            e->fireTimer = 2.6f;
            e->score  = 8;
            break;
        case ENEMY_LANCER:
            e->hp    = 4.5f + d * 1.5f;
            e->radius = 11.0f;
            e->orbit  = 235.0f + (float)GetRandomValue(0, 55);
            e->fireTimer = 1.6f;
            e->score  = 35;
            break;
        case ENEMY_SPLITTER:
            e->hp    = 5.0f + d * 1.7f;
            e->radius = 13.0f;
            e->orbit  = 92.0f + (float)GetRandomValue(0, 30);
            e->fireTimer = 1.5f;
            e->score  = 25;
            break;
        case ENEMY_TENDER:
            /* barely armed and not that tough: the threat is what it keeps
               alive, so the player has a reason to go hunting in the back */
            e->hp    = 5.5f + d * 1.4f;
            e->radius = 12.0f;
            e->orbit  = 255.0f + (float)GetRandomValue(0, 40);
            e->fireTimer = 2.4f;
            e->score  = 45;
            break;

        case ENEMY_SCOUT:
        default:
            kind     = ENEMY_SCOUT;
            e->hp    = 3.0f + d * 1.1f;
            e->radius = 10.0f;
            e->orbit  = 72.0f + (float)GetRandomValue(0, 42);
            e->fireTimer = 1.3f;
            e->score  = 10;
            break;
    }

    e->kind      = kind;
    e->alive     = true;
    e->maxHp     = e->hp;
    e->pos       = pos;
    e->vel       = V2(0, 0);
    e->angle     = 0.0f;
    e->wobble    = (float)GetRandomValue(0, 359) * DEG2RAD;
    e->spawnFade = ENEMY_SPAWN_FADE;
    e->hitFlash  = 0.0f;
    e->charge    = 0.0f;
}

void EnemiesClear(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++) g.enemies[i].alive = false;
}

int EnemiesAliveCount(void)
{
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) if (g.enemies[i].alive) n++;
    return n;
}

void EntitiesReset(void)
{
    for (int i = 0; i < MAX_BULLETS; i++)   g.bullets[i].alive   = false;
    for (int i = 0; i < MAX_ENEMIES; i++)   g.enemies[i].alive   = false;
    for (int i = 0; i < MAX_PARTICLES; i++) g.particles[i].alive = false;
    for (int i = 0; i < MAX_PICKUPS; i++)   g.pickups[i].alive   = false;
    for (int i = 0; i < MAX_BURSTS; i++)    g.bursts[i].alive    = false;
    for (int i = 0; i < MAX_POPUPS; i++)    g.popups[i].alive    = false;

    g.rallyCool  = 0.0f;
    g.rallyTimer = 0.0f;
    g.crits      = 0;
    for (int i = 0; i < MAX_ALLIES; i++) g.allyKills[i] = 0;

    /* Start over the most land-rich part of whatever the arena generator just
       produced, so the first wave does not have to be found across an ocean. */
    Vector2 start = ArenaScenicPos();

    g.player = (Player){
        .pos         = start,
        .vel         = V2(0, 0),
        .angle       = -PI * 0.5f,
        .hp          = PLAYER_MAX_HP,
        .weaponLevel = 1,
        .boostFuel   = PLAYER_BOOST_FUEL,
        .alive       = true,
    };

    for (int i = 0; i < MAX_ALLIES; i++) {
        g.allies[i] = (Ally){
            .alive        = true,
            .pos          = AllyFormationPos(i),
            .vel          = V2(0, 0),
            .angle        = g.player.angle,
            .hp           = ALLY_MAX_HP,
            .maxHp        = ALLY_MAX_HP,
            .fireTimer    = 0.6f + (float)i * 0.2f,
            .specialTimer = 2.5f + (float)i * 0.9f,
            .slot         = i,
            .role         = i,     /* the roster is VEX, JUNO, BIT */
        };
    }
}

/* --------------------------------------------------------------------------
 *  Player
 * ----------------------------------------------------------------------- */
void PlayerDamage(float amount)
{
    Player *p = &g.player;
    if (!p->alive || p->shieldTimer > 0.0f) return;

    p->hp          -= amount;
    p->shieldTimer  = HIT_INVULN;
    g.shake         = fmaxf(g.shake, 7.0f);
    g.hitStop       = fmaxf(g.hitStop, 0.05f);
    g.damageFlash   = 1.0f;

    SpawnBurstParticles(p->pos, 0.7f, (Color){ 255, 236, 159, 255 });
    PlaySfx(assets.hit, 0.55f, 1.0f);

    if (p->hp > 0.0f) return;

    p->hp    = 0.0f;
    p->alive = false;
    SpawnBurst(p->pos, 2.2f);
    SpawnBurstParticles(p->pos, 2.4f, COL_HP);
    g.shake = 18.0f;
    PlaySfx(assets.explosionBig, 0.9f, 1.0f);
}

static void PlayerShoot(void)
{
    Player *p = &g.player;
    bool boosted = p->powerTimer > 0.0f;
    float damage = PLAYER_BULLET_DMG * (boosted ? 1.5f : 1.0f);

    Vector2 dir    = V2(cosf(p->angle), sinf(p->angle));
    Vector2 perp   = V2(-dir.y, dir.x);
    Vector2 muzzle = V2Add(p->pos, V2Scale(dir, 12.0f));

    switch (p->weaponLevel) {
        case 1:
            SpawnBullet(muzzle, V2Scale(dir, PLAYER_BULLET_SPD),
                        TILE_BOLT, damage, 1.3f, 1.0f, BULLET_PLAYER, -1);
            break;

        case 2:
            SpawnBullet(V2Add(muzzle, V2Scale(perp,  5.0f)),
                        V2Scale(dir, PLAYER_BULLET_SPD), TILE_BOLT, damage, 1.3f, 1.0f, BULLET_PLAYER, -1);
            SpawnBullet(V2Add(muzzle, V2Scale(perp, -5.0f)),
                        V2Scale(dir, PLAYER_BULLET_SPD), TILE_BOLT, damage, 1.3f, 1.0f, BULLET_PLAYER, -1);
            break;

        default:
            for (int i = -1; i <= 1; i++) {
                float a = p->angle + 11.0f * DEG2RAD * (float)i;
                Vector2 d = V2(cosf(a), sinf(a));
                SpawnBullet(V2Add(muzzle, V2Scale(d, 4.0f)), V2Scale(d, PLAYER_BULLET_SPD),
                            TILE_BOLT, damage, 1.3f, 1.0f, BULLET_PLAYER, -1);
            }
            break;
    }

    /* muzzle flash */
    for (int i = 0; i < 3; i++) {
        float a = p->angle + (float)GetRandomValue(-25, 25) * DEG2RAD;
        EmitParticle(muzzle, V2(cosf(a) * 90.0f, sinf(a) * 90.0f),
                     0.10f, 2.0f, 0.0f, (Color){ 255, 236, 159, 255 });
    }

    PlaySfx(boosted ? assets.shootHeavy : assets.shoot, 0.30f, boosted ? 1.15f : 1.0f);
}

void PlayerUpdate(float dt)
{
    Player *p = &g.player;
    if (!p->alive) return;

    /* --- movement: accelerate toward the stick, then bleed off speed --- */
    Vector2 wish = V2ClampLen(input.move, 1.0f);

    /* Shift burns boost fuel for a short burst of extra speed. Cruise speed is
       set by friction (accel / friction ≈ 133), so simply raising the clamp
       does nothing - boost has to add real thrust and cut drag as well. */
    bool boosting = input.boost && V2Len(wish) > 0.1f && p->boostFuel > 0.0f;
    if (boosting) {
        p->boostFuel -= dt;
        if (p->boostFuel < 0.0f) p->boostFuel = 0.0f;
    } else {
        p->boostFuel += dt * 0.55f;                    /* refills in ~3s */
        if (p->boostFuel > PLAYER_BOOST_FUEL) p->boostFuel = PLAYER_BOOST_FUEL;
    }

    float accel    = boosting ? PLAYER_ACCEL * 3.0f : PLAYER_ACCEL;
    float friction = boosting ? PLAYER_FRICTION * 0.35f : PLAYER_FRICTION;
    float topSpeed = boosting ? PLAYER_MAX_SPEED * PLAYER_BOOST_MULT : PLAYER_MAX_SPEED;

    p->vel = V2Add(p->vel, V2Scale(wish, accel * dt));
    p->vel = V2Scale(p->vel, 1.0f - fminf(1.0f, friction * dt));
    p->vel = V2ClampLen(p->vel, topSpeed);

    p->pos = V2Add(p->pos, V2Scale(p->vel, dt));

    /* The world has no edge, so the only limit on where the player can go is
       how far the float grid still counts pixels - which is thousands of
       screens away. */
    const float margin = 18.0f;
    p->pos.x = Clamp(p->pos.x, -1.0e6f + margin, 1.0e6f - margin);
    p->pos.y = Clamp(p->pos.y, -1.0e6f + margin, 1.0e6f - margin);

    /* --- aim ---------------------------------------------------------- */
    if (V2Len(input.aim) > 0.01f) {
        p->angle = atan2f(input.aim.y, input.aim.x);
    }

    /* --- thruster trail ---------------------------------------------- */
    float speed = V2Len(p->vel);
    p->engineTimer -= dt;

    if (speed > 25.0f && p->engineTimer <= 0.0f) {
        p->engineTimer = boosting ? 0.008f : 0.018f;

        Vector2 back = V2(-cosf(p->angle), -sinf(p->angle));
        Vector2 side = V2(-back.y, back.x);

        if (boosting) {
            /* a hot rocket plume: warm particles kicked hard out the back so
               the extra speed is obvious on screen */
            for (int k = 0; k < 3; k++) {
                float j   = (float)GetRandomValue(-40, 40) / 40.0f;
                Vector2 fa = V2Add(V2Add(p->pos, V2Scale(back, 12.0f)),
                                   V2Scale(side, j * 5.0f));
                Color flame = (GetRandomValue(0, 2) == 0)
                            ? (Color){ 255, 150, 60, 255 }    /* orange core */
                            : (Color){ 255, 236, 159, 255 };  /* pale flame  */
                EmitParticle(fa,
                             V2Add(V2Scale(back, 120.0f + speed * 0.6f),
                                   V2Scale(side, j * 55.0f)),
                             0.26f + (float)GetRandomValue(0, 14) / 100.0f,
                             1.6f + (float)GetRandomValue(0, 2),
                             0.0f, flame);
            }
        } else {
            float jitter = (float)GetRandomValue(-30, 30) / 30.0f;
            Vector2 at   = V2Add(V2Add(p->pos, V2Scale(back, 10.0f)),
                                 V2Scale(side, jitter * 3.0f));
            EmitParticle(at,
                         V2Add(V2Scale(back, 40.0f + speed * 0.25f),
                               V2Scale(side, jitter * 22.0f)),
                         0.22f,
                         1.0f + (float)GetRandomValue(0, 1),
                         0.0f,
                         (speed > 110.0f) ? COL_SHALLOW : COL_WATER);
        }
    }

    /* --- timers and weapons ------------------------------------------ */
    if (p->powerTimer  > 0.0f) p->powerTimer  -= dt;
    if (p->shieldTimer > 0.0f) p->shieldTimer -= dt;
    if (p->hitFlash    > 0.0f) p->hitFlash    -= dt * 4.0f;

    p->fireTimer -= dt;
    if (input.firing && p->fireTimer <= 0.0f) {
        p->fireTimer = PLAYER_FIRE_TIME * ((p->powerTimer > 0.0f) ? 0.62f : 1.0f);
        PlayerShoot();
    }
}

void PlayerDraw(void)
{
    Player *p = &g.player;
    if (!p->alive) return;

    /* blink while invulnerable */
    if (p->shieldTimer > 0.0f && ((int)(g.timer * 18.0f) & 1)) return;

    DrawShip(SHIP_PLAYER, p->pos, p->angle * RAD2DEG + ART_FORWARD,
             1.0f, HitTint(p->hitFlash));

    if (p->shieldTimer > 0.0f) {
        float pulse = 0.55f + 0.25f * sinf(g.timer * 12.0f);
        DrawCircleLines((int)p->pos.x, (int)p->pos.y, 19.0f,
                        (Color){ 126, 226, 250, (unsigned char)(140.0f * pulse + 60.0f) });
        DrawCircleLines((int)p->pos.x, (int)p->pos.y, 20.0f,
                        (Color){ 223, 246, 245, (unsigned char)(90.0f * pulse + 30.0f) });
    }
}

/* --------------------------------------------------------------------------
 *  Enemies
 * ----------------------------------------------------------------------- */
void DamageEnemy(Enemy *e, float amount)
{
    e->hp      -= amount;
    e->hitFlash = 1.0f;

    if (e->hp > 0.0f) return;

    e->alive = false;
    g.score += e->score;
    g.kills++;

    bool big = (e->kind == ENEMY_HEAVY || e->kind == ENEMY_SPLITTER);
    SpawnBurst(e->pos, big ? 1.6f : 1.0f);
    SpawnBurstParticles(e->pos, big ? 1.9f : 1.0f, (Color){ 255, 236, 159, 255 });

    if (big) {
        g.shake   = fmaxf(g.shake, 9.0f);
        g.hitStop = fmaxf(g.hitStop, 0.06f);
        PlaySfx(assets.explosionBig, 0.5f, 1.0f);
    } else {
        g.shake = fmaxf(g.shake, 3.0f);
        PlaySfx(assets.explosion, 0.30f, 1.0f);
    }

    /* The splitter comes apart into two darts. Kill it at range. */
    if (e->kind == ENEMY_SPLITTER) {
        for (int i = 0; i < 2; i++) {
            Vector2 off = V2((float)GetRandomValue(-14, 14), (float)GetRandomValue(-14, 14));
            SpawnEnemy(ENEMY_DART, V2Add(e->pos, off));
        }
        SpawnBurstParticles(e->pos, 0.9f, (Color){ 140, 235, 150, 255 });
    }

    /* Loot table. */
    int  roll   = GetRandomValue(0, 99);
    int  chance = (e->kind == ENEMY_HEAVY)    ? 70
                : (e->kind == ENEMY_TENDER)   ? 60
                : (e->kind == ENEMY_SPLITTER) ? 40
                : (e->kind == ENEMY_LANCER)   ? 28
                : (e->kind == ENEMY_RAIDER)   ? 12
                                              : 10;
    PickupKind kind = (e->kind == ENEMY_HEAVY)  ? PICK_POWER
                    : (e->kind == ENEMY_TENDER) ? PICK_SHIELD
                    : (e->kind == ENEMY_RAIDER ||
                       e->kind == ENEMY_LANCER) ? PICK_SHIELD
                                                : PICK_HEALTH;

    if (roll < chance) SpawnPickup(kind, e->pos);
}

static void EnemyShoot(Enemy *e)
{
    Vector2 focus = NearestFriendlyTo(e->pos);
    float base = atan2f(focus.y - e->pos.y, focus.x - e->pos.x);

    switch (e->kind) {
        case ENEMY_HEAVY:
            for (int i = -1; i <= 1; i++) {
                Vector2 d = V2(cosf(base + 0.18f * (float)i), sinf(base + 0.18f * (float)i));
                SpawnBullet(V2Add(e->pos, V2Scale(d, 14.0f)),
                            V2Scale(d, ENEMY_BULLET_SPD * 0.85f),
                            TILE_BOLT_WHITE, ENEMY_BULLET_DMG, 2.4f, 1.0f, BULLET_ENEMY, -1);
            }
            PlaySfx(assets.enemyShoot, 0.28f, 0.8f);
            break;

        case ENEMY_RAIDER:
            for (int i = 0; i < 2; i++) {
                float a = base + (float)GetRandomValue(-6, 6) * DEG2RAD;
                Vector2 d = V2(cosf(a), sinf(a));
                SpawnBullet(V2Add(e->pos, V2Scale(d, 10.0f)), V2Scale(d, ENEMY_BULLET_SPD),
                            TILE_BOLT_WHITE, ENEMY_BULLET_DMG * 0.8f, 2.0f, 0.85f, BULLET_ENEMY, -1);
            }
            PlaySfx(assets.enemyShoot, 0.20f, 1.25f);
            break;

        /* Squeezed off at knife range, so the dart has to earn it. */
        case ENEMY_DART: {
            float a = base + (float)GetRandomValue(-10, 10) * DEG2RAD;
            Vector2 d = V2(cosf(a), sinf(a));
            SpawnBullet(V2Add(e->pos, V2Scale(d, 8.0f)), V2Scale(d, ENEMY_BULLET_SPD * 1.05f),
                        TILE_BOLT_WHITE, ENEMY_BULLET_DMG * 0.7f, 1.8f, 0.8f, BULLET_ENEMY, -1);
            PlaySfx(assets.enemyShoot, 0.14f, 1.5f);
            break;
        }

        /* The payoff for standing off: one fast, heavy, dead-straight bolt. */
        case ENEMY_LANCER: {
            Vector2 d = V2(cosf(base), sinf(base));
            SpawnBullet(V2Add(e->pos, V2Scale(d, 16.0f)), V2Scale(d, ENEMY_BULLET_SPD * 2.1f),
                        TILE_BOLT_WHITE, ENEMY_BULLET_DMG * 2.4f, 3.2f, 1.5f, BULLET_ENEMY, -1);
            SpawnBurstParticles(V2Add(e->pos, V2Scale(d, 14.0f)), 0.35f,
                                (Color){ 198, 150, 255, 255 });
            PlaySfx(assets.enemyShoot, 0.26f, 0.7f);
            break;
        }

        /* A wide, weak spray: it is trying to keep you off, not kill you. */
        case ENEMY_SPLITTER:
            for (int i = -2; i <= 2; i++) {
                Vector2 d = V2(cosf(base + 0.22f * (float)i), sinf(base + 0.22f * (float)i));
                SpawnBullet(V2Add(e->pos, V2Scale(d, 12.0f)),
                            V2Scale(d, ENEMY_BULLET_SPD * 0.75f),
                            TILE_BOLT_WHITE, ENEMY_BULLET_DMG * 0.6f, 1.9f, 0.9f, BULLET_ENEMY, -1);
            }
            PlaySfx(assets.enemyShoot, 0.22f, 0.95f);
            break;

        /* Not a gun at all: the tender's pulse fixes up everything nearby. */
        case ENEMY_TENDER: {
            float reach = 150.0f;
            for (int i = 0; i < MAX_ENEMIES; i++) {
                Enemy *o = &g.enemies[i];
                if (!o->alive || o->spawnFade > 0.0f) continue;
                if (Dist2(o->pos, e->pos) > reach * reach) continue;

                o->hp = fminf(o->maxHp, o->hp + 1.6f + g.difficulty * 0.25f);
                SpawnBurstParticles(o->pos, 0.25f, (Color){ 150, 240, 170, 255 });
            }
            /* and a little for itself, so a lone tender is not free score */
            e->hp = fminf(e->maxHp, e->hp + 0.8f);
            PlaySfx(assets.pickup, 0.16f, 1.6f);
            break;
        }

        default: {
            float a = base + (float)GetRandomValue(-4, 4) * DEG2RAD;
            Vector2 d = V2(cosf(a), sinf(a));
            SpawnBullet(V2Add(e->pos, V2Scale(d, 11.0f)), V2Scale(d, ENEMY_BULLET_SPD),
                        TILE_BOLT_WHITE, ENEMY_BULLET_DMG, 2.2f, 0.9f, BULLET_ENEMY, -1);
            PlaySfx(assets.enemyShoot, 0.18f, 1.0f);
            break;
        }
    }
}

void EnemiesUpdate(float dt)
{
    float speedScale = 1.0f + g.difficulty * 0.030f;
    float fireScale  = fmaxf(0.45f, 1.0f - g.difficulty * 0.030f);

    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g.enemies[i];
        if (!e->alive) continue;

        if (e->hitFlash  > 0.0f) e->hitFlash  -= dt * 5.0f;
        if (e->spawnFade > 0.0f) e->spawnFade -= dt;

        float baseSpeed = (e->kind == ENEMY_RAIDER)   ? 104.0f
                        : (e->kind == ENEMY_HEAVY)    ?  46.0f
                        : (e->kind == ENEMY_DART)     ? 158.0f
                        : (e->kind == ENEMY_LANCER)   ?  72.0f
                        : (e->kind == ENEMY_SPLITTER) ?  60.0f
                        : (e->kind == ENEMY_TENDER)   ?  76.0f
                                                      :  78.0f;
        float speed = baseSpeed * speedScale;

        /* Everything hunts whichever of us is closest - that is what makes the
           wingmen feel like they are actually pulling fire off the player. */
        Vector2 focus    = NearestFriendlyTo(e->pos);
        Vector2 toPlayer = V2Sub(focus, e->pos);
        float   dist     = V2Len(toPlayer);
        Vector2 dir      = (dist > 0.001f) ? V2Scale(toPlayer, 1.0f / dist) : V2(1.0f, 0.0f);

        /* Circle-strafe: steer toward a slowly rotating point on a ring
           around the target, so the swarm orbits instead of piling up. */
        float spin  = (e->kind == ENEMY_HEAVY)    ? 0.35f
                    : (e->kind == ENEMY_DART)     ? 1.65f
                    : (e->kind == ENEMY_LANCER)   ? 0.28f
                    : (e->kind == ENEMY_SPLITTER) ? 0.55f
                    : (e->kind == ENEMY_TENDER)   ? 0.30f
                                                  : 0.85f;
        float angle = atan2f(-toPlayer.y, -toPlayer.x) + e->wobble + g.time * spin;
        Vector2 anchor = V2Add(focus,
                               V2(cosf(angle) * e->orbit, sinf(angle) * e->orbit));

        Vector2 steer = (dist > 460.0f) ? dir : V2Norm(V2Sub(anchor, e->pos));

        e->vel = V2Add(e->vel, V2Scale(steer, speed * 6.0f * dt));
        e->vel = V2Scale(e->vel, 1.0f - fminf(1.0f, 4.0f * dt));
        /* The world is huge now, so a long-range chase gets a decent sprint -
           otherwise the player can simply outrun every wave forever. */
        e->vel = V2ClampLen(e->vel, speed * ((dist > 460.0f) ? 1.95f : 1.0f));

        e->pos = V2Add(e->pos, V2Scale(e->vel, dt));

        /* light separation so ships do not overlap */
        for (int j = i + 1; j < MAX_ENEMIES; j++) {
            Enemy *o = &g.enemies[j];
            if (!o->alive) continue;

            Vector2 d  = V2Sub(e->pos, o->pos);
            float   dd = V2Len(d);
            float   minD = e->radius + o->radius + 6.0f;
            if (dd >= minD || dd <= 0.001f) continue;

            Vector2 push = V2Scale(d, (minD - dd) / dd * 0.5f);
            e->pos = V2Add(e->pos, push);
            o->pos = V2Sub(o->pos, push);
        }

        /* face the player */
        e->angle = AngleLerp(e->angle, atan2f(toPlayer.y, toPlayer.x), fminf(1.0f, 6.0f * dt));

        /* shooting */
        e->fireTimer -= dt;

        bool mayFire = (e->fireTimer <= 0.0f && e->spawnFade <= 0.0f &&
                        dist < ENEMY_AGGRO_RANGE);

        if (e->kind == ENEMY_LANCER) {
            /* The lancer lines up in plain sight: a long charge, then one very
               fast bolt. You can see it coming and break the line, or kill it
               mid-charge - which is the whole point of the tell. */
            if (e->charge > 0.0f) {
                e->charge -= dt;
                if (e->charge <= 0.0f) {
                    EnemyShoot(e);
                    e->fireTimer = 2.30f * fireScale * (0.9f + (float)GetRandomValue(0, 25) / 100.0f);
                }
            } else if (mayFire && dist < 460.0f) {
                e->charge = 1.05f;
            }
        } else if (mayFire) {
            EnemyShoot(e);

            float base = (e->kind == ENEMY_RAIDER)   ? 1.05f
                       : (e->kind == ENEMY_HEAVY)    ? 2.10f
                       : (e->kind == ENEMY_DART)     ? 2.60f
                       : (e->kind == ENEMY_SPLITTER) ? 1.65f
                       : (e->kind == ENEMY_TENDER)   ? 2.75f
                                                     : 1.55f;
            e->fireTimer = base * fireScale * (0.85f + (float)GetRandomValue(0, 30) / 100.0f);
        }

        /* ramming hurts whoever gets hit - and for a dart that is the plan */
        if (g.player.alive && Dist2(e->pos, g.player.pos) <
            (e->radius + PLAYER_RADIUS) * (e->radius + PLAYER_RADIUS)) {
            PlayerDamage((e->kind == ENEMY_DART) ? 9.0f : 14.0f);
            e->vel = V2Scale(dir, -1.0f * 150.0f);
        }

        for (int k = 0; k < MAX_ALLIES; k++) {
            Ally *al = &g.allies[k];
            if (!al->alive) continue;

            float rr = e->radius + ALLY_RADIUS;
            if (Dist2(e->pos, al->pos) >= rr * rr) continue;

            AllyDamage(al, WINGMAN_RAM_DMG);
            e->vel = V2Scale(dir, -1.0f * 150.0f);
        }
    }
}

void EnemiesDraw(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g.enemies[i];
        if (!e->alive) continue;

        EnemyLook look = EnemyLookOf(e->kind);

        Color tint = HitTint(e->hitFlash);
        tint = (Color){ (unsigned char)((unsigned int)tint.r * look.tint.r / 255u),
                        (unsigned char)((unsigned int)tint.g * look.tint.g / 255u),
                        (unsigned char)((unsigned int)tint.b * look.tint.b / 255u),
                        tint.a };
        if (e->spawnFade > 0.0f) {
            tint.a = (unsigned char)(255.0f * (1.0f - e->spawnFade / ENEMY_SPAWN_FADE));
        }

        /* The tender broadcasts: a slow pulse so it is obvious who is keeping
           the swarm alive, and obviously worth killing first. */
        if (e->kind == ENEMY_TENDER && e->spawnFade <= 0.0f) {
            float pulse = 0.5f + 0.5f * sinf(g.time * 3.4f + e->wobble);
            DrawCircleLines((int)floorf(e->pos.x), (int)floorf(e->pos.y),
                            24.0f + pulse * 6.0f,
                            (Color){ 255, 240, 150, (unsigned char)(60.0f + 70.0f * pulse) });
        }

        /* The lancer's charge-up, drawn as a growing bead of light down the
           barrel: the longer it has been charging, the brighter it gets. */
        if (e->kind == ENEMY_LANCER && e->charge > 0.0f) {
            float k = Clamp(1.0f - e->charge / 1.05f, 0.0f, 1.0f);
            Vector2 dir = V2(cosf(e->angle), sinf(e->angle));
            Vector2 tip = V2Add(e->pos, V2Scale(dir, 15.0f));
            float   len = 4.0f + k * 12.0f;

            DrawLineEx(e->pos, V2Add(e->pos, V2Scale(dir, 15.0f + len)),
                       1.0f, (Color){ 198, 150, 255, (unsigned char)(90.0f + 140.0f * k) });
            DrawCircleV(tip, 1.5f + k * 2.0f,
                        (Color){ 235, 215, 255, (unsigned char)(150.0f + 105.0f * k) });
        }

        DrawShip(look.sprite, e->pos, e->angle * RAD2DEG + ART_FORWARD,
                 look.scale, tint);

        /* health bar for the ships that can take a beating */
        if (e->kind != ENEMY_SCOUT && e->kind != ENEMY_DART && e->hp < e->maxHp) {
            float ratio = Clamp(e->hp / e->maxHp, 0.0f, 1.0f);
            int   x = (int)(e->pos.x - 9.0f);
            int   y = (int)(e->pos.y - 20.0f);
            DrawRectangle(x, y, 18, 2, COL_INK);
            DrawRectangle(x, y, (int)(18.0f * ratio), 2, COL_HP);
        }
    }
}

/* --------------------------------------------------------------------------
 *  Wingmen
 *
 *  Three friendly fighters that fly formation off the player's wings. They
 *  are not clones: each one has a name, a paint job, a special weapon and a
 *  personality that comes out on the radio. They break off to engage anything
 *  that comes near, fall back into formation, and if one of them starts losing
 *  they will shout for help - the beacon goes up on the map and the rest of
 *  the wing turns to pull them out.
 * ----------------------------------------------------------------------- */

/* The squadron. Hull tints are picked to read against water, sand and grass
   alike, and to stay distinct from the orange enemy fighters. */
static const AllyProfile kProfiles[ALLY_ROLE_COUNT] = {
    [ALLY_VEX] = {
        .name    = "VEX",
        .hull    = { 255, 205,  80, 255 },
        .text    = { 255, 220, 120, 255 },
        .ship    = SHIP_WINGMAN,
        .cooldown= 3.0f,
        .special = "SCATTER",
    },
    [ALLY_JUNO] = {
        .name    = "JUNO",
        .hull    = { 120, 240, 150, 255 },
        .text    = { 150, 250, 185, 255 },
        .ship    = SHIP_WINGMAN,
        .cooldown= 6.5f,
        .special = "AEGIS",
    },
    [ALLY_BIT] = {
        .name    = "BIT",
        .hull    = { 235, 140, 255, 255 },
        .text    = { 245, 175, 255, 255 },
        .ship    = SHIP_WINGMAN,
        .cooldown= 5.0f,
        .special = "WASP",
    },
};

const AllyProfile *AllyProfileOf(int role)
{
    if (role < 0 || role >= ALLY_ROLE_COUNT) role = 0;
    return &kProfiles[role];
}

/* Slot offsets are measured from the player's heading: one on each wing and
   one tucked in behind. */
Vector2 AllyFormationPos(int slot)
{
    static const float kAngle[MAX_ALLIES] = { 143.0f, 217.0f, 180.0f };
    static const float kDist [MAX_ALLIES] = {  74.0f,  74.0f, 112.0f };

    int   i = (slot >= 0 && slot < MAX_ALLIES) ? slot : 0;
    float a = g.player.angle + kAngle[i] * DEG2RAD;

    return V2(g.player.pos.x + cosf(a) * kDist[i],
              g.player.pos.y + sinf(a) * kDist[i]);
}

/* Nearest friendly to `from`: the player, or whichever wingman is closer.
   Falls back on the player's position when nobody is alive. */
static Vector2 NearestFriendlyTo(Vector2 from)
{
    Vector2 best  = g.player.pos;
    float   bestD = g.player.alive ? Dist2(g.player.pos, from) : 1e18f;

    for (int i = 0; i < MAX_ALLIES; i++) {
        Ally *a = &g.allies[i];
        if (!a->alive) continue;

        float d = Dist2(a->pos, from);
        if (d < bestD) { bestD = d; best = a->pos; }
    }
    return best;
}

int AlliesAliveCount(void)
{
    int n = 0;
    for (int i = 0; i < MAX_ALLIES; i++) if (g.allies[i].alive) n++;
    return n;
}

/* How many live enemies are sitting on top of `from`? Used to decide that a
   wingman is in over their head. */
static int EnemiesNear(Vector2 from, float radius)
{
    float r2 = radius * radius;
    int   n  = 0;

    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g.enemies[i];
        if (!e->alive || e->spawnFade > 0.0f) continue;
        if (Dist2(e->pos, from) < r2) n++;
    }
    return n;
}

/* Does this crate offer something the wingman actually needs? A hurt pilot
   wants the repair; one flying with nothing running will still take a weapon
   or a shield, because that is what keeps them alive out here. */
static bool AllyWantsPickup(const Ally *a, const Pickup *p)
{
    switch (p->kind) {
        case PICK_HEALTH: return a->hp < a->maxHp * 0.62f;
        case PICK_POWER:  return a->powerTimer  <= 0.0f;
        case PICK_SHIELD: return a->shieldTimer <= 0.0f;
    }
    return false;
}

/* The nearest crate worth a detour, or NULL. */
static Pickup *AllyFindLoot(const Ally *a)
{
    Pickup *best  = NULL;
    float   bestD = ALLY_CRATE_RANGE * ALLY_CRATE_RANGE;

    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *p = &g.pickups[i];
        if (!p->alive || !AllyWantsPickup(a, p)) continue;

        float d = Dist2(p->pos, a->pos);
        if (d < bestD) { bestD = d; best = p; }
    }
    return best;
}

/* The wingman raises the alarm: the others break off and come running, and
   the HUD puts a red marker on the map until they are out of trouble. */
void AllyCallForHelp(Ally *a)
{
    if (!a->alive || a->distress > 0.0f || a->helpCool > 0.0f) return;

    a->distress = 6.5f;
    a->helpCool = 10.0f;

    bool badlyHurt = (a->hp < a->maxHp * 0.45f);
    ChatSay(a->slot, badlyHurt ? CHAT_HURT : CHAT_SWARMED);
    PlaySfx(assets.hit, 0.20f, 1.6f);

    for (int i = 0; i < MAX_ALLIES; i++) {
        Ally *o = &g.allies[i];
        if (o == a || !o->alive) continue;

        o->assistTimer = 6.0f;
        o->assistAt    = a->pos;
    }
}

void AllyDamage(Ally *a, float amount)
{
    if (!a->alive || a->spawnFade > 0.0f) return;   /* still materialising */

    /* A shield crate soaks most of the hit - that is what they grabbed it for */
    if (a->shieldTimer > 0.0f) amount *= 0.30f;

    a->hp      -= amount;
    a->hitFlash = 1.0f;

    if (a->hp > 0.0f) {
        /* Hurt enough and they start shouting before they die, not after. */
        if (a->hp < a->maxHp * 0.35f) AllyCallForHelp(a);
        return;
    }

    const AllyProfile *pr = AllyProfileOf(a->role);

    a->alive        = false;
    a->respawnTimer = ALLY_RESPAWN;
    a->distress     = 0.0f;

    SpawnBurst(a->pos, 1.0f);
    SpawnBurstParticles(a->pos, 1.1f, pr->hull);
    PlaySfx(assets.explosion, 0.26f, 1.2f);

    /* the survivors react - except the one who just went down, obviously */
    for (int i = 0; i < MAX_ALLIES; i++) {
        if (i == a->slot) continue;
        if (g.allies[i].alive) ChatSay(i, CHAT_BUDDY);
    }

    snprintf(g.message, sizeof g.message, "%s DOWN", pr->name);
    g.messageTimer = 1.6f;
}

/* --------------------------------------------------------------------------
 *  Wing rank
 *
 *  A wingman is only as good as the kit they are flying, and the swarm gets
 *  meaner every wave. Rather than making the player carry the whole squadron
 *  forever, the wing gets better in two ways at once:
 *
 *    - every kill the wing scores together is a small, permanent trickle of
 *      skill: quicker on the stick, quicker on the trigger, hitting harder;
 *    - and every fifth wave they are formally rearmed - a flat few percent on
 *      everything, plus a new hull ceiling.
 *
 *  Both of those ride on top of the per-wave rank, so a long run keeps the
 *  wing in the fight instead of watching them evaporate in wave six.
 * ----------------------------------------------------------------------- */
#define ALLY_KILL_STEP 0.010f   /* +1% (of the wing's total) per kill     */
#define ALLY_KILL_CAP  0.75f    /* ...up to +75% from kills alone         */
#define ALLY_VET_STEP  0.06f    /* +6% every five waves                   */
#define ALLY_VET_CAP   2.20f    /* however long you last, this is the end */
#define ALLY_SPEED_CAP 1.30f    /* they still have to fly with the player */

static int AllyKills(void)
{
    int n = 0;
    for (int i = 0; i < MAX_ALLIES; i++) n += g.allyKills[i];
    return n;
}

/* One number for "how seasoned is this wing". */
static float AllyVet(void)
{
    float kills = fminf((float)AllyKills() * ALLY_KILL_STEP, ALLY_KILL_CAP);
    float waves = (float)(g.wave / 5) * ALLY_VET_STEP;

    return fminf(1.0f + kills + waves, ALLY_VET_CAP);
}

static float AllyRank(void)       { return (float)(g.wave > 0 ? g.wave - 1 : 0); }
static float AllyMaxHpForRank(void){ return (ALLY_MAX_HP + AllyRank() * ALLY_RANK_HP) * AllyVet(); }
static float AllyDamageMul(void)  { return (1.0f + AllyRank() * ALLY_RANK_DMG) * AllyVet(); }
static float AllyFireMul(void)    { return fmaxf(0.38f, (1.0f - AllyRank() * ALLY_RANK_FIRE) / AllyVet()); }
static float AllyCoolMul(void)    { return fmaxf(0.38f, (1.0f - AllyRank() * ALLY_RANK_COOL) / AllyVet()); }
static float AllySpeedMul(void)   { return fminf(AllyVet(), ALLY_SPEED_CAP); }

/* Bring one wingman's hull up to what the wing has earned. Called every frame,
   because the trickle from kills means the ceiling is always creeping up. */
static void AllyRefreshRank(Ally *a)
{
    float maxHp = AllyMaxHpForRank();
    if (maxHp <= a->maxHp) return;

    if (a->alive) a->hp += (maxHp - a->maxHp);   /* the new plating comes fitted */
    a->maxHp = maxHp;
}

/* Called at the top of every wave: everyone is patched up to the new ceiling,
   so being rearmed reads as a reward rather than a free full heal. */
void AlliesUpgrade(void)
{
    float maxHp = AllyMaxHpForRank();

    for (int i = 0; i < MAX_ALLIES; i++) {
        Ally *a = &g.allies[i];
        float gain = maxHp - a->maxHp;
        if (gain < 0.0f) gain = 0.0f;

        a->maxHp = maxHp;
        if (a->alive) a->hp = fminf(maxHp, a->hp + gain + 8.0f);
    }
}

/* The second voice on the radio when the wing is recalled: each pilot's own
   idea of "on my way". */
static const char *const kRallyEcho[ALLY_ROLE_COUNT] = {
    "YEAH YEAH, I'M COMING.",        /* VEX  */
    "ON YOUR WING. HOLD FORMATION.", /* JUNO */
    "ON MY WAY! ON MY WAY! MOVE!",   /* BIT  */
};

/* The player's recall. Everyone breaks off, sprints back, then holds station
   around the player with their guns live - it is a call for backup, not a
   call for a formation flight. */
void AlliesRally(void)
{
    if (g.rallyCool > 0.0f) return;

    int  alive[MAX_ALLIES], n = 0;
    for (int i = 0; i < MAX_ALLIES; i++) {
        Ally *a = &g.allies[i];
        if (!a->alive || a->spawnFade > 0.0f) continue;
        a->rallyTimer  = RALLY_TIME;
        a->assistTimer = 0.0f;
        alive[n++]     = i;
    }
    if (n == 0) return;

    g.rallyCool  = RALLY_COOLDOWN;
    g.rallyTimer = RALLY_TIME;

    TextCopy(g.message, "RALLY - ON ME");
    g.messageTimer = 1.4f;
    PlaySfx(assets.pickup, 0.30f, 1.45f);

    /* one of them answers - every pilot in the wing has their own idea of
       what "get here now" sounds like */
    int who = alive[GetRandomValue(0, n - 1)];
    ChatSay(who, CHAT_RALLY);

    /* ...and a second voice piles on, because nobody shuts up on this radio */
    if (n > 1 && GetRandomValue(0, 1) == 0) {
        int other = alive[GetRandomValue(0, n - 1)];
        if (other == who) other = alive[(who + 1) % n];
        ChatSayText(other, kRallyEcho[g.allies[other].role]);
    }
}

/* Everything a wingman's gun does, in one number: the wave-scoped rank
   upgrade, plus the extra bite from a weapon crate if they are flying hot. */
static float AllyGunMul(const Ally *a)
{
    return AllyDamageMul() * ((a->powerTimer > 0.0f) ? 1.5f : 1.0f);
}

static void AllyShoot(Ally *a)
{
    const AllyProfile *pr = AllyProfileOf(a->role);
    Vector2 dir    = V2(cosf(a->angle), sinf(a->angle));
    Vector2 muzzle = V2Add(a->pos, V2Scale(dir, 11.0f));

    SpawnBullet(muzzle, V2Scale(dir, ALLY_BULLET_SPD), TILE_BOLT,
                ALLY_BULLET_DMG * AllyGunMul(a), 1.3f, 1.0f, BULLET_ALLY, a->slot);
    SpawnBurstParticles(muzzle, 0.18f, pr->hull);

    PlaySfx(assets.shoot, 0.10f, 1.35f);
}

/* --------------------------------------------------------------------------
 *  Special weapons - one per pilot, fired on their own cadence whenever they
 *  have a target lined up.
 * ----------------------------------------------------------------------- */
void AllySpecial(Ally *a)
{
    const AllyProfile *pr = AllyProfileOf(a->role);
    Vector2 dir = V2(cosf(a->angle), sinf(a->angle));
    Vector2 side = V2(-dir.y, dir.x);

    switch (a->role) {
        /* ---------------------------------------------------------- VEX ----
           A five-round spread. Rude, loud, effective at knife range. */
        case ALLY_VEX:
        default: {
            for (int i = -2; i <= 2; i++) {
                float ang = a->angle + (float)i * 12.0f * DEG2RAD;
                Vector2 d = V2(cosf(ang), sinf(ang));
                SpawnBullet(V2Add(a->pos, V2Scale(d, 11.0f)),
                            V2Scale(d, ALLY_BULLET_SPD * 0.94f),
                            TILE_BOLT, ALLY_BULLET_DMG * 0.7f * AllyGunMul(a),
                            0.85f, 0.9f, BULLET_ALLY, a->slot);
            }
            SpawnBurstParticles(V2Add(a->pos, V2Scale(dir, 12.0f)), 0.4f, pr->hull);
            PlaySfx(assets.enemyShoot, 0.22f, 1.55f);
            break;
        }

        /* --------------------------------------------------------- JUNO ----
           A repair field for everyone nearby, plus a heavy twin shot. */
        case ALLY_JUNO: {
            float r2 = 165.0f * 165.0f;
            bool  healed = false;

            if (g.player.alive && g.player.hp < PLAYER_MAX_HP &&
                Dist2(g.player.pos, a->pos) < r2) {
                g.player.hp = fminf(PLAYER_MAX_HP, g.player.hp + 24.0f);
                SpawnBurstParticles(g.player.pos, 0.55f, COL_HEAL);
                healed = true;
            }

            for (int i = 0; i < MAX_ALLIES; i++) {
                Ally *o = &g.allies[i];
                if (!o->alive || o == a) continue;
                if (o->hp >= o->maxHp || Dist2(o->pos, a->pos) > r2) continue;

                o->hp = fminf(o->maxHp, o->hp + 24.0f);
                SpawnBurstParticles(o->pos, 0.55f, COL_HEAL);
                healed = true;
            }

            if (healed) {
                TextCopy(g.message, "AEGIS FIELD");
                g.messageTimer = 1.0f;
            }

            for (int i = -1; i <= 1; i += 2) {
                Vector2 from = V2Add(V2Add(a->pos, V2Scale(dir, 9.0f)),
                                     V2Scale(side, 4.0f * (float)i));
                SpawnBullet(from, V2Scale(dir, ALLY_BULLET_SPD * 1.15f), TILE_BOLT,
                            ALLY_BULLET_DMG * 1.7f * AllyGunMul(a), 1.4f, 1.15f,
                            BULLET_ALLY, a->slot);
            }
            PlaySfx(assets.pickup, 0.28f, 1.25f);
            break;
        }

        /* ---------------------------------------------------------- BIT ----
           Two seekers that hunt by themselves. Terrible for the nerves. */
        case ALLY_BIT: {
            for (int i = -1; i <= 1; i += 2) {
                Vector2 from = V2Add(V2Add(a->pos, V2Scale(dir, 8.0f)),
                                     V2Scale(side, 5.0f * (float)i));
                float ang = a->angle + (float)i * 18.0f * DEG2RAD;
                Vector2 d = V2(cosf(ang), sinf(ang));

                Bullet *b = SpawnBullet(from, V2Scale(d, 210.0f), TILE_BOLT,
                                        ALLY_BULLET_DMG * 1.6f * AllyGunMul(a),
                                        2.6f, 1.15f, BULLET_ALLY, a->slot);
                if (b) b->homing = 5.4f;

                SpawnBurstParticles(from, 0.25f, pr->hull);
            }
            PlaySfx(assets.enemyShoot, 0.20f, 0.9f);
            break;
        }
    }
}

void AlliesUpdate(float dt)
{
    for (int i = 0; i < MAX_ALLIES; i++) {
        Ally *a = &g.allies[i];
        a->slot = i;

        /* The wing is always getting a little better: this is where the trickle
           of shared kill experience lands. */
        AllyRefreshRank(a);

        /* --- waiting to be rebuilt ------------------------------------- */
        if (!a->alive) {
            a->respawnTimer -= dt;
            /* Respawn after 15s if the player is still alive.
               "As long as someone is alive everyone can come back after 15s." */
            if (a->respawnTimer <= 0.0f && g.player.alive) {
                a->alive        = true;
                a->hp           = a->maxHp;
                a->pos          = AllyFormationPos(i);
                a->vel          = g.player.vel;
                a->angle        = g.player.angle;
                a->hitFlash     = 0.0f;
                a->spawnFade    = ALLY_SPAWN_FADE;
                a->fireTimer    = 0.5f;
                a->specialTimer = 2.0f;
                a->distress     = 0.0f;
                a->helpCool     = 3.0f;
                a->assistTimer  = 0.0f;
                a->rallyTimer   = 0.0f;
                a->powerTimer   = 0.0f;
                a->shieldTimer  = 0.0f;
                SpawnBurst(a->pos, 0.7f);

                static const char *const kBack[ALLY_ROLE_COUNT][3] = {
                    { "MISS ME, ASSHOLES?", "BACK. WHO DIED?", "STILL THE BEST ONE HERE." },
                    { "BACK ON YOUR WING.", "I'M UP. FORMATION.", "RESPONDING." },
                    { "I'M ALIVE! I'M ALIVE!", "DID YOU MISS ME?!", "I'M BACK! SORRY!" },
                };
                ChatSayText(i, kBack[a->role][GetRandomValue(0, 2)]);
            }
            continue;
        }

        if (a->hitFlash > 0.0f) a->hitFlash -= dt * 5.0f;
        if (a->spawnFade > 0.0f) a->spawnFade -= dt;

        /* --- beacon bookkeeping ---------------------------------------- */
        if (a->helpCool > 0.0f) a->helpCool -= dt;
        if (a->rallyTimer > 0.0f) a->rallyTimer -= dt;
        if (a->powerTimer  > 0.0f) a->powerTimer  -= dt;
        if (a->shieldTimer > 0.0f) a->shieldTimer -= dt;

        if (a->distress > 0.0f) {
            a->distress -= dt;

            /* once they are patched up and the sky is clear the marker drops */
            bool safe = (a->hp > a->maxHp * 0.5f) &&
                        (EnemiesNear(a->pos, ALLY_SWARM_RADIUS) == 0);
            if (safe && a->distress > 0.5f) a->distress = 0.5f;
        }

        /* --- pick something to shoot at -------------------------------- */
        /* Prioritize: enemies firing at the player, then closest to player,
           then any enemy within engage range.  This keeps the wingmen
           focused on the biggest threats rather than just whoever is
           nearest in the general direction. */
        Enemy *target = NULL;
        float  bestD  = ALLY_ENGAGE_RANGE * ALLY_ENGAGE_RANGE;

        /* 1. enemies that are shooting at the player */
        for (int k = 0; k < MAX_ENEMIES; k++) {
            Enemy *e = &g.enemies[k];
            if (!e->alive || e->spawnFade > 0.0f) continue;
            /* check if this enemy is firing towards the player */
            Vector2 toPlayer = V2Sub(g.player.pos, e->pos);
            float dp = (toPlayer.x * cosf(e->angle) + toPlayer.y * sinf(e->angle)) /
                       (V2Len(toPlayer) * 1.0f + 0.001f);
            /* if enemy's forward dot product > 0.5 towards player, consider it "firing at player" */
            if (dp > 0.5f) {
                float d = Dist2(e->pos, a->pos);
                if (d < bestD) { bestD = d; target = e; }
            }
        }

        /* 2. closest enemy to player as fallback */
        if (!target) {
            for (int k = 0; k < MAX_ENEMIES; k++) {
                Enemy *e = &g.enemies[k];
                if (!e->alive || e->spawnFade > 0.0f) continue;
                float d = Dist2(e->pos, g.player.pos);
                if (d < bestD) { bestD = d; target = e; }
            }
        }

        /* 3. any enemy within engage range */
        if (!target) {
            for (int k = 0; k < MAX_ENEMIES; k++) {
                Enemy *e = &g.enemies[k];
                if (!e->alive || e->spawnFade > 0.0f) continue;
                float d = Dist2(e->pos, a->pos);
                if (d < bestD) { bestD = d; target = e; }
            }
        }

        /* --- answering the player's recall ----------------------------- */
        /* A recall is a call for backup. They break off whatever they were
           chasing, sprint back to the player, and then fight from there:
           once they are on station they keep picking targets and pulling the
           trigger, which is the entire reason to press the button. */
        bool  rallying = (a->rallyTimer > 0.0f);
        float maxSpeed = ALLY_MAX_SPEED * AllySpeedMul();
        bool  inbound  = false;

        if (rallying) {
            /* an enemy already shooting the player outranks one we merely
               happen to be closest to - that is what "backup" means */
            Enemy *threat = NULL;
            float  threatD = ALLY_ENGAGE_RANGE * ALLY_ENGAGE_RANGE;
            for (int k = 0; k < MAX_ENEMIES; k++) {
                Enemy *e = &g.enemies[k];
                if (!e->alive || e->spawnFade > 0.0f) continue;
                float d = Dist2(e->pos, g.player.pos);
                if (d < threatD) { threatD = d; threat = e; }
            }
            if (threat) target = threat;

            float dHome = V2Len(V2Sub(AllyFormationPos(i), a->pos));
            inbound     = (dHome > ALLY_RALLY_ARRIVE);
            if (inbound) maxSpeed = ALLY_RALLY_SPEED * AllySpeedMul();
        }

        /* --- raise the alarm ------------------------------------------- */
        if (a->spawnFade <= 0.0f && a->distress <= 0.0f &&
            (a->hp < a->maxHp * 0.35f ||
             EnemiesNear(a->pos, ALLY_SWARM_RADIUS) >= ALLY_SWARM_COUNT)) {
            AllyCallForHelp(a);
        }

        /* --- work out the velocity we want ----------------------------- */
        Vector2 home = AllyFormationPos(i);

        /* Running to a buddy's rescue overrides the leash - getting there
           before the enemy finishes the job is the whole point. */
        if (a->assistTimer > 0.0f && V2Len(V2Sub(a->assistAt, a->pos)) > 90.0f)
            home = a->assistAt;

        /* Supply run. A hurt pilot, or one flying with nothing running, will
           break off for a crate close enough to be worth the trip - and they
           fight the whole way there, because the leash pulls them back toward
           the crate rather than off the map. A recall outranks shopping. */
        Pickup *loot = NULL;
        if (!rallying && !a->distress) {
            loot = AllyFindLoot(a);
            if (loot) home = loot->pos;
        }

        Vector2 desiredVel;

        if (inbound) {
            /* Called home: fly the line and shoot the whole way. The nose
               points at the player while the guns stay on the target, which
               is what "backup" looks like from the cockpit. */
            Vector2 toSlot = V2Sub(home, a->pos);
            float   d      = V2Len(toSlot);
            desiredVel = V2Scale(V2Norm(toSlot), fminf(d * 3.2f, maxSpeed));
        } else if (target) {
            Vector2 toTarget = V2Sub(target->pos, a->pos);
            float   d        = V2Len(toTarget);
            Vector2 dir      = (d > 0.001f) ? V2Scale(toTarget, 1.0f / d) : V2(1.0f, 0.0f);

            /* Lead the target: enemy velocity approx from its motion */
            Vector2  vel     = V2Sub(target->pos, (Vector2){ target->pos.x - target->vel.x * 0.1f,
                                                              target->pos.y - target->vel.y * 0.1f });
            Vector2 toLead   = V2Sub(vel, a->pos);
            float   dLead    = V2Len(toLead);
            Vector2 dirLead  = (dLead > 0.001f) ? V2Scale(toLead, 1.0f / dLead) : dir;

            /* close in when far away, back off when we are in its face */
            float stand  = target->radius + ALLY_STANDOFF + (float)i * 12.0f;
            float radial = Clamp((d - stand) * 2.4f, -maxSpeed, maxSpeed);

            /* a slow strafe keeps the three of them from forming a queue */
            Vector2 side   = V2(-dirLead.y, dirLead.x);
            float   strafe = sinf(g.time * 1.25f + (float)i * 2.1f) * maxSpeed * 0.38f;

            desiredVel = V2Add(V2Scale(dirLead, radial), V2Scale(side, strafe));

            /* Leash: however good the target looks, never chase so far that we
               lose the player. Without this the whole wing ends up lured away
               across the map and the player flies alone. Looting gets a longer
               rope - they still have to actually reach the crate. */
            float   leash    = loot ? 420.0f : ALLY_LEASH;
            float   homeDist = V2Len(V2Sub(home, a->pos));
            if (homeDist > leash) {
                float t = Clamp((homeDist - leash) / 140.0f, 0.0f, 1.0f);
                desiredVel = V2Add(V2Scale(desiredVel, 1.0f - t),
                                   V2Scale(V2Norm(V2Sub(home, a->pos)),
                                           maxSpeed * t));
            }
        } else {
            Vector2 toSlot = V2Sub(home, a->pos);
            float   d      = V2Len(toSlot);
            desiredVel = V2Scale(V2Norm(toSlot), fminf(d * 3.2f, maxSpeed));
        }

        /* exponential approach to the desired velocity: snappy, no overshoot */
        a->vel = V2Add(a->vel,
                       V2Scale(V2Sub(desiredVel, a->vel), 1.0f - expf(-6.5f * dt)));
        a->vel = V2ClampLen(a->vel, maxSpeed);
        a->pos = V2Add(a->pos, V2Scale(a->vel, dt));

        a->pos.x = Clamp(a->pos.x, -1.0e6f + 16.0f, 1.0e6f - 16.0f);
        a->pos.y = Clamp(a->pos.y, -1.0e6f + 16.0f, 1.0e6f - 16.0f);

        /* --- teleport home if we get left far behind ------------------- */
        if (g.player.alive &&
            Dist2(a->pos, g.player.pos) > ALLY_HOME_RANGE * ALLY_HOME_RANGE) {
            a->pos = AllyFormationPos(i);
            a->vel = g.player.vel;
        }

        /* --- do not fly inside each other ------------------------------ */
        for (int k = i + 1; k < MAX_ALLIES; k++) {
            Ally   *o   = &g.allies[k];
            if (!o->alive) continue;

            Vector2 d   = V2Sub(a->pos, o->pos);
            float   dd  = V2Len(d);
            float   minD = ALLY_RADIUS * 2.0f + 10.0f;
            if (dd >= minD || dd <= 0.001f) continue;

            Vector2 push = V2Scale(d, (minD - dd) / dd * 0.5f);
            a->pos = V2Add(a->pos, push);
            o->pos = V2Sub(o->pos, push);
        }

        /* --- face the target (or where the player is heading) ---------- */
        Vector2 aimAt = target ? target->pos
                               : V2Add(g.player.pos, V2Scale(g.player.vel, 0.35f));
        Vector2 toAim = V2Sub(aimAt, a->pos);
        float   aimL  = V2Len(toAim);

        if (aimL > 0.001f) {
            a->angle = AngleLerp(a->angle, atan2f(toAim.y, toAim.x),
                                 fminf(1.0f, 8.0f * dt));
        }

        /* --- fire once roughly on target ------------------------------- */
        a->fireTimer    -= dt;
        a->specialTimer -= dt;

        if (target && a->fireTimer <= 0.0f && a->spawnFade <= 0.0f && aimL > 4.0f) {
            /* A recall puts every gun on the threat, so the wing shoots more
               loosely on the way in - this is backup, not target practice. */
            float aimError  = fabsf(WrapAngle(atan2f(toAim.y, toAim.x) - a->angle));
            float tolerance = (rallying || inbound) ? 0.60f : 0.30f;

            if (aimError < tolerance) {
                const AllyProfile *pr = AllyProfileOf(a->role);
                float range = V2Len(V2Sub(target->pos, a->pos));

                /* a weapon crate makes them shoot faster and hit harder */
                float hot = (a->powerTimer > 0.0f) ? 0.66f : 1.0f;

                if (a->specialTimer <= 0.0f && range < 300.0f) {
                    /* the special is what makes each pilot fly differently */
                    static const char *const kCry[ALLY_ROLE_COUNT] = {
                        "SCATTER, ASSHOLES!", "AEGIS UP.", "WASPS AWAY! RUN! ...THEY RUN!"
                    };

                    AllySpecial(a);
                    if (GetRandomValue(0, 4) == 0) ChatSayText(i, kCry[a->role]);

                    a->specialTimer = pr->cooldown * AllyCoolMul() *
                                      (0.85f + (float)GetRandomValue(0, 30) / 100.0f);
                    a->fireTimer    = ALLY_FIRE_TIME * AllyFireMul() * hot * 1.7f;
                } else {
                    AllyShoot(a);
                    a->fireTimer = ALLY_FIRE_TIME * AllyFireMul() * hot *
                                   (0.85f + (float)GetRandomValue(0, 40) / 100.0f);
                }
            }
        }
    }
}

void AlliesDraw(void)
{
    for (int i = 0; i < MAX_ALLIES; i++) {
        Ally *a = &g.allies[i];
        if (!a->alive) continue;

        const AllyProfile *pr = AllyProfileOf(a->role);

        /* Squadron paint, flashed toward white when they take a hit. */
        Color tint = pr->hull;
        if (a->hitFlash > 0.0f) {
            float k = Clamp(a->hitFlash, 0.0f, 1.0f);
            tint.r = (unsigned char)((float)tint.r + (255.0f - (float)tint.r) * k);
            tint.g = (unsigned char)((float)tint.g + (255.0f - (float)tint.g) * k);
            tint.b = (unsigned char)((float)tint.b + (255.0f - (float)tint.b) * k);
        }

        if (a->spawnFade > 0.0f)
            tint.a = (unsigned char)(255.0f * (1.0f - a->spawnFade / ALLY_SPAWN_FADE));

        DrawShip(pr->ship, a->pos, a->angle * RAD2DEG + ART_FORWARD, 1.0f, tint);

        if (a->spawnFade > 0.0f) continue;

        /* --- crate buffs, so you can see who is armed and who is armoured --- */
        if (a->shieldTimer > 0.0f) {
            float pulse = 0.5f + 0.5f * sinf(g.time * 9.0f);
            DrawCircleLines((int)floorf(a->pos.x), (int)floorf(a->pos.y),
                            13.0f + pulse * 3.0f,
                            (Color){ 126, 196, 250,
                                     (unsigned char)(70.0f + 90.0f * pulse) });
        }
        if (a->powerTimer > 0.0f) {
            /* hot guns: a warm glow on the nose, honestly earned */
            float pulse = 0.5f + 0.5f * sinf(g.time * 16.0f);
            DrawCircleV(a->pos, 3.0f + pulse * 2.0f,
                        (Color){ 255, 196, 96, (unsigned char)(60.0f + 60.0f * pulse) });
        }

        /* --- distress beacon ------------------------------------------- */
        if (a->distress > 0.0f) {
            float pulse = 0.5f + 0.5f * sinf(g.time * 10.0f);

            DrawCircleLines((int)floorf(a->pos.x), (int)floorf(a->pos.y),
                            15.0f + pulse * 7.0f,
                            (Color){ 255, 74, 74, (unsigned char)(80.0f + 110.0f * pulse) });

            float bx = SnapPix(a->pos.x) - 1.0f;
            float by = SnapPix(a->pos.y) - 26.0f;

            DrawRectangle((int)bx, (int)by, 3, 8, (Color){ 255, 74, 74, 255 });
            DrawRectangle((int)bx, (int)by + 10, 3, 3, (Color){ 255, 74, 74, 255 });
        }
    }
}

/* The recall beacon.
 *
 * The ring goes on the *player*, not on each wingman: it marks the spot the
 * wing was ordered to, so "come here" is legible at a glance. Everyone still
 * on their way home is tied to it with a dashed line in their own paint, and
 * the moment they arrive the line is gone - they are on station and shooting. */
void RallyDraw(void)
{
    if (g.rallyTimer <= 0.0f || !g.player.alive) return;

    float pulse = 0.5f + 0.5f * sinf(g.time * 9.0f);

    /* the ring itself, two passes so it reads over any terrain */
    DrawCircleLines((int)floorf(g.player.pos.x), (int)floorf(g.player.pos.y),
                    30.0f + pulse * 5.0f,
                    (Color){ 126, 226, 250, (unsigned char)(120.0f + 120.0f * pulse) });
    DrawCircleLines((int)floorf(g.player.pos.x), (int)floorf(g.player.pos.y),
                    38.0f - pulse * 4.0f,
                    (Color){ 126, 226, 250, 70 });

    for (int i = 0; i < MAX_ALLIES; i++) {
        Ally *a = &g.allies[i];
        if (!a->alive || a->spawnFade > 0.0f || a->rallyTimer <= 0.0f) continue;

        float d = V2Len(V2Sub(a->pos, g.player.pos));
        if (d <= ALLY_RALLY_ARRIVE) continue;   /* already here, just fighting */

        const AllyProfile *pr = AllyProfileOf(a->role);

        /* dashed tether: the wingman is inbound */
        Vector2 from = a->pos;
        Vector2 to   = g.player.pos;
        float   len  = d;
        Vector2 dir  = V2Scale(V2Sub(to, from), 1.0f / fmaxf(len, 0.001f));

        for (float t = 18.0f; t < len - 26.0f; t += 9.0f) {
            Vector2 p = V2Add(from, V2Scale(dir, t));
            DrawRectangle((int)floorf(p.x) - 1, (int)floorf(p.y) - 1, 2, 2, pr->hull);
        }
    }
}

/* --------------------------------------------------------------------------
 *  Bullets
 * ----------------------------------------------------------------------- */
void BulletsUpdate(float dt)
{
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &g.bullets[i];
        if (!b->alive) continue;

        /* Seekers steer toward the closest thing still in the air. */
        if (b->homing > 0.0f) {
            Vector2 tgt   = b->pos;
            float   bestD = 300.0f * 300.0f;
            bool    found = false;

            for (int k = 0; k < MAX_ENEMIES; k++) {
                Enemy *e = &g.enemies[k];
                if (!e->alive || e->spawnFade > 0.0f) continue;

                float d = Dist2(e->pos, b->pos);
                if (d < bestD) { bestD = d; tgt = e->pos; found = true; }
            }

            float have = atan2f(b->vel.y, b->vel.x);

            if (found) {
                float want = atan2f(tgt.y - b->pos.y, tgt.x - b->pos.x);
                have += Clamp(WrapAngle(want - have), -b->homing * dt, b->homing * dt);

                float spd = V2Len(b->vel);
                b->vel    = V2(cosf(have) * spd, sinf(have) * spd);
            }
            b->angle = have;

            /* exhaust trail, so a seeker reads as a missile and not a bullet */
            if (GetRandomValue(0, 2) == 0) {
                EmitParticle(b->pos, V2Scale(b->vel, -0.06f), 0.30f, 2.0f, 0.0f,
                             (Color){ 255, 216, 170, 170 });
            }
        }

        b->life -= dt;
        b->pos   = V2Add(b->pos, V2Scale(b->vel, dt));

        if (b->life <= 0.0f) {
            b->alive = false;
            continue;
        }

        /* terrain cover soaks up shots */
        if (ArenaBlocksAt(b->pos)) {
            b->alive = false;
            SpawnBurstParticles(b->pos, 0.35f, (Color){ 146, 177, 21, 255 });
            continue;
        }

        float radius = BULLET_RADIUS * b->scale;

        if (b->owner != BULLET_ENEMY) {
            for (int k = 0; k < MAX_ENEMIES; k++) {
                Enemy *e = &g.enemies[k];
                if (!e->alive || e->spawnFade > 0.0f) continue;

                float rr = e->radius + radius;
                if (Dist2(b->pos, e->pos) > rr * rr) continue;

                bool wasHeavy = (e->kind == ENEMY_HEAVY);

                /* Every so often a shot lands somewhere that really matters:
                   it bites a lot harder and spits out a "CRIT" over the
                   target, which is the difference between a stream of taps
                   and a killing blow. */
                bool  crit = (GetRandomValue(0, 99) < CRIT_CHANCE);
                float dmg  = b->damage * (crit ? CRIT_MULT : 1.0f);

                DamageEnemy(e, dmg);

                if (crit) {
                    g.crits++;
                    SpawnBurstParticles(b->pos, 0.85f, (Color){ 255, 236, 159, 255 });
                    SpawnPopup(e->pos, "CRIT", (Color){ 255, 236, 159, 255 });
                    PlaySfx(assets.hit, 0.42f, 1.85f);
                } else {
                    SpawnBurstParticles(b->pos, 0.28f, COL_FOAM);
                }
                b->alive = false;

                /* Kill chatter: the wingman who fired gets the credit, and the
                   player's bigger kills get a comment from the cheap seats. */
                if (!e->alive) {
                    if (b->owner == BULLET_ALLY && b->slot >= 0) {
                        /* ...and the whole wing learns something from it */
                        if (b->slot < MAX_ALLIES) g.allyKills[b->slot]++;
                        if (GetRandomValue(0, 9) < 7) ChatSay(b->slot, CHAT_KILL);
                    } else if (wasHeavy) {
                        int alive[MAX_ALLIES], n = 0;
                        for (int q = 0; q < MAX_ALLIES; q++)
                            if (g.allies[q].alive) alive[n++] = q;
                        if (n > 0) ChatSay(alive[GetRandomValue(0, n - 1)], CHAT_KILL);
                    }
                }
                break;
            }
        } else {
            /* enemy fire hurts whoever it runs into first */
            if (g.player.alive && g.player.shieldTimer <= 0.0f) {
                float rr = PLAYER_RADIUS + radius;
                if (Dist2(b->pos, g.player.pos) <= rr * rr) {
                    PlayerDamage(b->damage);
                    b->alive = false;
                    continue;
                }
            }

            for (int k = 0; k < MAX_ALLIES; k++) {
                Ally *a = &g.allies[k];
                if (!a->alive) continue;

                float rr = ALLY_RADIUS + radius;
                if (Dist2(b->pos, a->pos) > rr * rr) continue;

                AllyDamage(a, b->damage);
                b->alive = false;
                break;
            }
        }
    }
}

void BulletsDraw(void)
{
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &g.bullets[i];
        if (!b->alive) continue;

        Color tint;
        if (b->owner == BULLET_ENEMY) {
            tint = (Color){ 255, 130, 140, 255 };
        } else if (b->owner == BULLET_ALLY) {
            /* tracers wear the pilot's colours, brightened so they read as
               shots rather than as another ship */
            const AllyProfile *pr = (b->slot >= 0 && b->slot < MAX_ALLIES)
                                  ? AllyProfileOf(g.allies[b->slot].role) : NULL;
            tint = pr ? pr->hull : (Color){ 160, 226, 255, 255 };
            tint.r = (unsigned char)fminf(255.0f, (float)tint.r + 45.0f);
            tint.g = (unsigned char)fminf(255.0f, (float)tint.g + 45.0f);
            tint.b = (unsigned char)fminf(255.0f, (float)tint.b + 45.0f);
        } else {
            tint = (Color){ 255, 236, 159, 255 };
        }

        DrawTileSprite(b->sprite, b->pos, b->angle * RAD2DEG + ART_FORWARD, b->scale, tint);
    }
}

/* --------------------------------------------------------------------------
 *  Pickups
 * ----------------------------------------------------------------------- */
void PickupsUpdate(float dt)
{
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *p = &g.pickups[i];
        if (!p->alive) continue;

        p->t    += dt;
        p->life -= dt;

        if (p->life <= 0.0f) { p->alive = false; continue; }

        p->vel = V2Scale(p->vel, 1.0f - fminf(1.0f, 2.5f * dt));
        p->pos = V2Add(p->pos, V2Scale(p->vel, dt));

        if (!g.player.alive) continue;

        /* --- who is actually reaching for it? --------------------------- */
        /* The player has first call; failing that a wingman flying over it
           takes it for themselves. A crate nobody is touching is left alone
           on the map - that is the whole point of a supply drop. */
        bool  playerHas = false;
        Ally *allyHas   = NULL;

        float pr = PLAYER_RADIUS + 13.0f;
        if (Dist2(p->pos, g.player.pos) <= pr * pr) {
            playerHas = true;
        } else {
            for (int k = 0; k < MAX_ALLIES; k++) {
                Ally *a = &g.allies[k];
                if (!a->alive || a->spawnFade > 0.0f) continue;

                float ar = ALLY_RADIUS + 34.0f;
                if (Dist2(p->pos, a->pos) <= ar * ar) { allyHas = a; break; }
            }
        }
        if (!playerHas && !allyHas) continue;

        if (allyHas) {
            /* They grabbed it for a reason: it goes where the crate said it
               would, not just into a repair. */
            bool quiet = false;
            switch (p->kind) {
                case PICK_HEALTH:
                    quiet = (allyHas->hp >= allyHas->maxHp - 0.5f);
                    allyHas->hp = fminf(allyHas->maxHp, allyHas->hp + 35.0f);
                    break;
                case PICK_POWER:
                    quiet = (allyHas->powerTimer > 0.0f);
                    allyHas->powerTimer = fmaxf(allyHas->powerTimer, ALLY_POWER_TIME);
                    allyHas->hp = fminf(allyHas->maxHp, allyHas->hp + 10.0f);
                    break;
                case PICK_SHIELD:
                    quiet = (allyHas->shieldTimer > 0.0f);
                    allyHas->shieldTimer = fmaxf(allyHas->shieldTimer, ALLY_SHIELD_TIME);
                    allyHas->hp = fminf(allyHas->maxHp, allyHas->hp + 14.0f);
                    break;
            }
            allyHas->hitFlash = 0.5f;
            p->alive = false;
            SpawnBurstParticles(p->pos, 0.4f, COL_SHALLOW);
            PlaySfx(assets.pickup, 0.22f, 1.35f);

            /* ...and of course they tell everyone about it */
            if (!quiet && GetRandomValue(0, 2) == 0) ChatSay(allyHas->slot, CHAT_LOOT);
            continue;
        }

        switch (p->kind) {
            case PICK_HEALTH:
                g.player.hp = fminf(PLAYER_MAX_HP, g.player.hp + 35.0f);
                TextCopy(g.message, "HULL REPAIRED");
                break;
            case PICK_POWER:
                g.player.powerTimer = POWER_DURATION;
                if (g.player.weaponLevel < 3) g.player.weaponLevel++;
                TextCopy(g.message, "WEAPON UP");
                break;
            case PICK_SHIELD:
                g.player.shieldTimer = fmaxf(g.player.shieldTimer, SHIELD_DURATION);
                TextCopy(g.message, "SHIELD ON");
                break;
        }

        g.messageTimer = 1.2f;
        PlaySfx(assets.pickup, 0.35f, 1.0f);
        SpawnBurstParticles(p->pos, 0.4f, COL_SHALLOW);
        p->alive = false;
    }
}

void PickupsDraw(void)
{
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *p = &g.pickups[i];
        if (!p->alive) continue;

        /* blink out over the final seconds */
        if (p->life < 3.0f && ((int)(p->life * 10.0f) & 1)) continue;

        int sprite = (p->kind == PICK_HEALTH) ? TILE_PICK_HEALTH
                   : (p->kind == PICK_POWER)  ? TILE_PICK_POWER
                                              : TILE_PICK_SHIELD;

        Vector2 at = V2(p->pos.x, p->pos.y + sinf(p->t * 4.0f) * 2.0f);

        DrawCircleV(at, 10.0f, (Color){ 24, 34, 46, 60 });   /* soft halo */
        DrawTileSprite(sprite, at, 0.0f, 1.0f, WHITE);
    }
}

/* --------------------------------------------------------------------------
 *  Particles & explosion sprites
 * ----------------------------------------------------------------------- */
void ParticlesUpdate(float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g.particles[i];
        if (!p->alive) continue;

        p->life -= dt;
        if (p->life <= 0.0f) { p->alive = false; continue; }

        p->pos    = V2Add(p->pos, V2Scale(p->vel, dt));
        p->vel    = V2Scale(p->vel, 1.0f - fminf(1.0f, 3.2f * dt));
        p->angle += p->spin * dt;
    }
}

void ParticlesDraw(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g.particles[i];
        if (!p->alive) continue;

        float k = p->life / p->maxLife;
        Color c = p->color;
        c.a = (unsigned char)(255.0f * Clamp(k * 1.6f, 0.0f, 1.0f));

        /* whole pixels and whole sizes, otherwise the sparkle crawls */
        float size = floorf(p->size * (0.4f + k * 0.6f) + 0.5f);
        if (size < 1.0f) size = 1.0f;

        Rectangle rec = { SnapPix(p->pos.x - size * 0.5f) + size * 0.5f,
                          SnapPix(p->pos.y - size * 0.5f) + size * 0.5f, size, size };
        DrawRectanglePro(rec, V2(size * 0.5f, size * 0.5f), SnapAngle(p->angle), c);
    }
}

void BurstsUpdate(float dt)
{
    for (int i = 0; i < MAX_BURSTS; i++) {
        Burst *b = &g.bursts[i];
        if (!b->alive) continue;

        b->t += dt;
        if (b->t >= BURST_FRAME_TIME * (float)BURST_FRAMES) b->alive = false;
    }
}

void BurstsDraw(void)
{
    for (int i = 0; i < MAX_BURSTS; i++) {
        Burst *b = &g.bursts[i];
        if (!b->alive) continue;

        int frame = (int)(b->t / BURST_FRAME_TIME);
        if (frame >= BURST_FRAMES) frame = BURST_FRAMES - 1;

        float k     = (float)frame / (float)(BURST_FRAMES - 1);
        float scale = b->scale * (0.85f + k * 0.55f);

        DrawTileSprite(kBurstFrames[frame], b->pos,
                       (float)((int)(b->t * 1000.0f) % 4) * 90.0f, scale, WHITE);
    }
}

/* --------------------------------------------------------------------------
 *  Floating combat text
 * ----------------------------------------------------------------------- */
void PopupsUpdate(float dt)
{
    for (int i = 0; i < MAX_POPUPS; i++) {
        Popup *p = &g.popups[i];
        if (!p->alive) continue;

        p->t += dt;
        if (p->t >= p->life) { p->alive = false; continue; }

        p->pos.y -= 22.0f * dt;   /* drifts up off the target */
    }
}

void PopupsDraw(void)
{
    for (int i = 0; i < MAX_POPUPS; i++) {
        Popup *p = &g.popups[i];
        if (!p->alive) continue;

        float k = p->t / p->life;
        Color c = p->color;
        c.a = (unsigned char)(255.0f * Clamp(1.0f - k * k, 0.0f, 1.0f));

        DrawTextPixShadow(assets.font, p->text,
                          V2(SnapPix(p->pos.x), SnapPix(p->pos.y)), 1, c);
    }
}

/* --------------------------------------------------------------------------
 *  Waves
 * ----------------------------------------------------------------------- */
void StartWave(int wave)
{
    g.wave       = wave;
    g.difficulty = (float)(wave - 1);

    /* The swarm grows a little faster than the wing does - a long run is
       meant to end badly eventually - but it is a steady ramp, not a cliff,
       and it keeps growing for as long as the player can stay alive. New
       kinds turn up as the run goes on, so the fight keeps changing shape. */
    int heavy    = (wave >= 3) ? (wave - 1) / 2      : 0;
    int raider   = (wave >= 2) ? wave                : 0;
    int scout    = 4 + wave + wave / 2;
    int dart     = (wave >= 2) ? 2 + wave / 2        : 0;
    int lancer   = (wave >= 4) ? 1 + (wave - 4) / 3  : 0;
    int splitter = (wave >= 5) ? 1 + (wave - 5) / 4  : 0;
    int tender   = (wave >= 6) ? 1 + (wave - 6) / 5  : 0;

    g.waveHeavy    = (heavy    > 14) ? 14 : heavy;
    g.waveRaider   = (raider   > 26) ? 26 : raider;
    g.waveScout    = (scout    > 36) ? 36 : scout;
    g.waveDart     = (dart     > 20) ? 20 : dart;
    g.waveLancer   = (lancer   >  7) ?  7 : lancer;
    g.waveSplitter = (splitter >  6) ?  6 : splitter;
    g.waveTender   = (tender   >  3) ?  3 : tender;

    g.wavePending = g.waveScout + g.waveRaider + g.waveHeavy +
                    g.waveDart + g.waveLancer + g.waveSplitter + g.waveTender;
    g.spawnTimer  = 1.2f;

    /* Rearm the wing before the shooting starts. */
    AlliesUpgrade();

    snprintf(g.message, sizeof g.message, "WAVE %d", wave);
    g.messageTimer = 2.2f;

    /* --- supply drop ------------------------------------------------- */
    /* Crates land on the islands around the squadron at the top of every
       wave, so there is something worth breaking formation for. Weapon and
       hull crates are the common ones. */
    int drops = 2 + wave / 3;
    if (drops > 5) drops = 5;

    for (int i = 0; i < drops; i++) {
        Vector2 at   = ArenaRandomLandNear(g.player.pos, 90.0f, 360.0f);
        int     roll = GetRandomValue(0, 99);

        PickupKind kind = (roll < 45) ? PICK_POWER
                        : (roll < 85) ? PICK_HEALTH
                                      : PICK_SHIELD;
        SpawnPickup(kind, at);
    }

    TraceLog(LOG_DEBUG, "WAVE %d: %d drops placed", wave, drops);

    /* --- radio -------------------------------------------------------- */
    {
        int alive[MAX_ALLIES], n = 0;
        for (int i = 0; i < MAX_ALLIES; i++) if (g.allies[i].alive) alive[n++] = i;
        if (n == 0) return;

        if (wave > 1 && (wave % 5) == 0) {
            static const char *const kVet[ALLY_ROLE_COUNT] = {
                "NEW ENGINES. TRY TO KEEP UP.",
                "SQUADRON UPGRADE COMPLETE.",
                "OOH! EVERYTHING'S SHINY!"
            };
            int who = alive[GetRandomValue(0, n - 1)];
            ChatSayText(who, kVet[g.allies[who].role]);
        } else if (wave > 1 && (wave % 3) == 0) {
            static const char *const kRearm[ALLY_ROLE_COUNT] = {
                "NEW GUNS. NOW IT'S FAIR, PUNKS.",
                "REARMED. PATCHED. PISSED OFF.",
                "NEW PARTS! I FEEL FAST! TOO FAST?!"
            };
            int who = alive[GetRandomValue(0, n - 1)];
            ChatSayText(who, kRearm[g.allies[who].role]);
        } else {
            ChatSay(alive[GetRandomValue(0, n - 1)], CHAT_WAVE);
        }
    }
}

void WaveSpawnStep(float dt)
{
    if (g.wavePending <= 0) return;

    g.spawnTimer -= dt;
    if (g.spawnTimer > 0.0f) return;

    g.spawnTimer = fmaxf(0.14f, 0.70f - g.difficulty * 0.035f);

    /* Whatever is left of the wave, weighted by what is left of each kind -
       so the composition stays the one StartWave decided, in a random order. */
    int remaining[ENEMY_KIND_COUNT] = {
        g.waveScout, g.waveRaider, g.waveHeavy, g.waveDart,
        g.waveLancer, g.waveSplitter, g.waveTender,
    };

    int total = 0;
    for (int i = 0; i < ENEMY_KIND_COUNT; i++) total += remaining[i];
    if (total <= 0) { g.wavePending = 0; return; }

    int roll = GetRandomValue(0, total - 1);
    EnemyKind kind = ENEMY_SCOUT;
    for (int i = 0; i < ENEMY_KIND_COUNT; i++) {
        if (roll < remaining[i]) { kind = (EnemyKind)i; break; }
        roll -= remaining[i];
    }

    switch (kind) {
        case ENEMY_RAIDER:   g.waveRaider--;   break;
        case ENEMY_HEAVY:    g.waveHeavy--;    break;
        case ENEMY_DART:     g.waveDart--;     break;
        case ENEMY_LANCER:   g.waveLancer--;   break;
        case ENEMY_SPLITTER: g.waveSplitter--; break;
        case ENEMY_TENDER:   g.waveTender--;   break;
        default:             g.waveScout--;    break;
    }
    g.wavePending--;

    /* Drop enemies in on a ring around the player, so the fight starts right
       away instead of after a long flight across the island. */
    Vector2 pos = g.player.pos;
    for (int attempt = 0; attempt < 12; attempt++) {
        float angle = (float)GetRandomValue(0, 359) * DEG2RAD;
        float r     = 235.0f + (float)GetRandomValue(0, 130);

        Vector2 candidate = V2(g.player.pos.x + cosf(angle) * r,
                               g.player.pos.y + sinf(angle) * r);

        pos = candidate;
        if (V2Len(V2Sub(candidate, g.player.pos)) > 165.0f) break;
    }

    SpawnEnemy(kind, pos);
}
