/*****************************************************************************
 *  chat.c - the wingman radio.
 *
 *  Three characters ride with the player and they do not stop talking. Each
 *  has their own voice, their own paint job and their own opinions, and the
 *  lines are picked from per-character tables so the same event never sounds
 *  the same twice.
 *
 *  Every line goes into a ring buffer and is then shown twice: once as a
 *  speech bubble floating over the speaker's own ship - so any number of them
 *  can talk at once without the radio flip-flopping between voices - and once
 *  in the message log down the right-hand edge of the screen.
 *****************************************************************************/
#include "game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* --------------------------------------------------------------------------
 *  The script
 *
 *  Indexed [role][event][variation]; a NULL variation ends the list. Lines are
 *  short on purpose - they are read at a glance while flying.
 * ----------------------------------------------------------------------- */
#define LINES_PER_EVENT 4

static const char *kLines[ALLY_ROLE_COUNT][CHAT_EVENTS][LINES_PER_EVENT] = {
    /* ---------------------------------------------------------- VEX ----
       Cocky, loud, allergic to humility. Calls the player "SIX". */
    [ALLY_VEX] = {
        [CHAT_KILL]    = { "SCRATCH ONE!", "TOO EASY.", "WHO'S NEXT?!", "THAT'S HOW IT'S DONE." },
        [CHAT_BUDDY]   = { "NO - I'VE GOT YOU!", "THEY'LL PAY FOR THAT.", NULL, NULL },
        [CHAT_HURT]    = { "I'M HIT - HULL'S GOING!", "CAN'T SHAKE HIM!", "SIX, I NEED COVER!", NULL },
        [CHAT_SWARMED] = { "THEY'RE ALL OVER ME!", "TOO MANY - HELP!", NULL, NULL },
        [CHAT_IDLE]    = { "THAT WAS MY KILL, JUNO.", "KEEP UP, KIDS.", "I COULD DO THIS ALONE.",
                            "SLOW TODAY, SIX." },
        [CHAT_WAVE]    = { "HERE THEY COME. FINALLY.", "MORE TARGETS. GOOD.", NULL, NULL },
        [CHAT_CLEAR]   = { "THAT ALL YOU GOT?", "SKY'S EMPTY. BORING.", NULL, NULL },
        [CHAT_REPLY]   = { "YEAH, YEAH.", "SAYS THE ONE WHO MISSED.", "NOTED, JUNO.", NULL },
    },

    /* --------------------------------------------------------- JUNO ----
       The steady one. Protective, unimpressed, keeps the formation honest. */
    [ALLY_JUNO] = {
        [CHAT_KILL]    = { "TARGET DOWN.", "CLEAN KILL.", "THREAT REMOVED.", "CONFIRMED SPLASH." },
        [CHAT_BUDDY]   = { "WE LOST A WINGMAN.", "COVER ME. I'M ALONE BACK HERE.", NULL, NULL },
        [CHAT_HURT]    = { "STRUCTURAL DAMAGE. I'M LEAKING.", "I NEED ASSISTANCE.",
                            "MY HULL IS THIN, SIX.", NULL },
        [CHAT_SWARMED] = { "I'M SURROUNDED. REQUEST SUPPORT.", "THEY'VE BOXED ME IN.", NULL, NULL },
        [CHAT_IDLE]    = { "STAY IN FORMATION.", "CHECK YOUR SIX, BIT.", "SIX, WATCH YOUR HULL.",
                            "FUEL'S FINE. NERVES AREN'T." },
        [CHAT_WAVE]    = { "NEW CONTACTS INBOUND.", "BRACE.", NULL, NULL },
        [CHAT_CLEAR]   = { "AREA CLEAR. REGROUP.", "GOOD WORK, TEAM.", NULL, NULL },
        [CHAT_REPLY]   = { "UNDERSTOOD.", "AS YOU SAY.", "COPY THAT, VEX.", NULL },
    },

    /* ---------------------------------------------------------- BIT ----
       The rookie. Brilliant with the seeker rack, terrified of everything. */
    [ALLY_BIT] = {
        [CHAT_KILL]    = { "OH! GOT HIM! GOT HIM!", "DID YOU SEE THAT?!",
                            "AWAY- AND IT HIT! WOW.", "I DID A KILL! I MEAN- A KILL!" },
        [CHAT_BUDDY]   = { "OH NO. OH NO OH NO.", "ANSWER ME! PLEASE ANSWER!", NULL, NULL },
        [CHAT_HURT]    = { "AAAH MY HULL!", "I'M HIT I'M HIT I'M HIT!", "SOMEONE HELP!", NULL },
        [CHAT_SWARMED] = { "THEY'RE EVERYWHERE!", "SIX! VEX! ANYONE!", NULL, NULL },
        [CHAT_IDLE]    = { "UH. IS THE LEAD SUPPOSED TO DO THAT?",
                            "MY SENSORS ARE SCREAMING. JUST SAYING.",
                            "I'M FINE. TOTALLY FINE.", "DO WE GET PAID FOR THIS?" },
        [CHAT_WAVE]    = { "OH NO. HERE THEY COME.", "I DON'T LIKE THIS.", NULL, NULL },
        [CHAT_CLEAR]   = { "WE LIVED! WE LIVED!", "IS IT OVER? IS IT OVER?", NULL, NULL },
        [CHAT_REPLY]   = { "OKAY OKAY OKAY!", "R-RIGHT!", "COPY! I THINK! YES! COPY!" },
    },
};

/* --------------------------------------------------------------------------
 *  Internals
 * ----------------------------------------------------------------------- */
static void Push(int slot, const char *who, Color color, const char *text)
{
    ChatLine *c = &g.chat[g.chatNext];
    g.chatNext  = (g.chatNext + 1) % CHAT_SLOTS;

    c->slot = (slot >= 0 && slot < MAX_ALLIES) ? slot : -1;
    snprintf(c->who, sizeof c->who, "%s", who);
    snprintf(c->text, sizeof c->text, "%s", text);
    c->color = color;
    c->t     = 0.0f;
    c->life  = 2.2f + (float)strlen(c->text) * 0.045f;   /* longer lines linger */
    c->used  = true;
}

static const char *PickLine(int role, ChatEvent ev)
{
    if (role < 0 || role >= ALLY_ROLE_COUNT) return NULL;
    if (ev < 0 || ev >= CHAT_EVENTS) return NULL;

    const char *const *list = kLines[role][ev];

    int count = 0;
    while (count < LINES_PER_EVENT && list[count]) count++;
    if (count == 0) return NULL;

    return list[GetRandomValue(0, count - 1)];
}

/* The most recent live line from one speaker, or NULL. Scanning backwards from
   the write cursor means the newest line wins, which is the one whose bubble
   should be up. */
static ChatLine *LineOf(int slot)
{
    for (int i = 0; i < CHAT_SLOTS; i++) {
        int idx = (g.chatNext - 1 - i + CHAT_SLOTS) % CHAT_SLOTS;
        if (g.chat[idx].used && g.chat[idx].slot == slot) return &g.chat[idx];
    }
    return NULL;
}

static bool AnyLive(void)
{
    for (int i = 0; i < CHAT_SLOTS; i++)
        if (g.chat[i].used) return true;
    return false;
}

/* --------------------------------------------------------------------------
 *  Public
 * ----------------------------------------------------------------------- */
void ChatReset(void)
{
    for (int i = 0; i < CHAT_SLOTS; i++) g.chat[i].used = false;
    g.chatNext = 0;
    g.chatIdle = 0.0f;
}

void ChatSay(int slot, ChatEvent ev)
{
    if (slot < 0 || slot >= MAX_ALLIES) return;
    if (!g.allies[slot].alive) return;      /* the dead do not talk */

    int role = g.allies[slot].role;
    const char *line = PickLine(role, ev);
    if (!line) return;

    Push(slot, AllyProfileOf(role)->name, AllyProfileOf(role)->text, line);
}

void ChatSayText(int slot, const char *text)
{
    if (slot < 0 || slot >= MAX_ALLIES || !text) return;

    int role = g.allies[slot].role;
    Push(slot, AllyProfileOf(role)->name, AllyProfileOf(role)->text, text);
}

void ChatSystem(const char *who, Color color, const char *text)
{
    if (!text) return;
    Push(-1, who ? who : "RADIO", color, text);
}

void ChatUpdate(float dt)
{
    for (int i = 0; i < CHAT_SLOTS; i++) {
        ChatLine *c = &g.chat[i];
        if (!c->used) continue;
        c->t += dt;
        if (c->t >= c->life) c->used = false;
    }

    if (AnyLive()) {
        g.chatIdle = 0.0f;
        return;
    }

    /* nobody is talking: eventually somebody gets bored */
    g.chatIdle += dt;
    if (g.chatIdle > 7.5f) {
        g.chatIdle = -GetRandomValue(0, 60) * 0.05f;   /* small random gap */

        int alive[MAX_ALLIES];
        int n = 0;
        for (int i = 0; i < MAX_ALLIES; i++) if (g.allies[i].alive) alive[n++] = i;
        if (n > 0) {
            int a = alive[GetRandomValue(0, n - 1)];
            ChatSay(a, CHAT_IDLE);

            /* half the time somebody answers back */
            if (n > 1 && GetRandomValue(0, 1) == 1) {
                int b = alive[GetRandomValue(0, n - 1)];
                if (b == a) b = alive[(a + 1) % n];
                ChatSay(b, CHAT_REPLY);
            }
        }
    }
}

/* --------------------------------------------------------------------------
 *  Drawing
 * ----------------------------------------------------------------------- */
#define CHAT_FADE_IN  0.16f
#define CHAT_FADE_OUT 0.34f

/* Word-wrap `text` into rows no wider than `maxW`. Returns the row count and
   records, per row, where in the original string that row began - row 0
   starting at 0 means the speaker's name leads the line. */
static int WrapText(const char *text, float maxW, char rows[][CHAT_TEXT + 1],
                    int rowStart[], int maxRows)
{
    int n = 0;
    const char *p = text;

    while (*p != '\0' && n < maxRows) {
        const char *rowBegin = p;
        char row[CHAT_TEXT + 1];
        row[0] = '\0';
        int rowLen = 0;

        while (*p != '\0') {
            const char *w = p;
            while (*w != '\0' && *w != ' ') w++;
            int wlen = (int)(w - p);

            char cand[CHAT_TEXT + 1];
            if (rowLen == 0) {
                if (wlen > CHAT_TEXT) wlen = CHAT_TEXT;
                memcpy(cand, p, (size_t)wlen);
                cand[wlen] = '\0';
            } else {
                if (rowLen + 1 + wlen > CHAT_TEXT) wlen = CHAT_TEXT - rowLen - 1;
                memcpy(cand, row, (size_t)rowLen);
                cand[rowLen] = ' ';
                memcpy(cand + rowLen + 1, p, (size_t)wlen);
                cand[rowLen + 1 + wlen] = '\0';
            }

            if (rowLen > 0 && TextWidthPix(assets.font, cand, 1) > maxW) break;

            memcpy(row, cand, strlen(cand) + 1);
            rowLen = (int)strlen(row);
            p = w;
            while (*p == ' ') p++;
            if (rowLen >= CHAT_TEXT) break;
        }

        if (rowLen == 0) break;

        memcpy(rows[n], row, (size_t)rowLen + 1);
        rowStart[n] = (int)(rowBegin - text);
        n++;
    }
    return n;
}

/* A speech bubble over the speaker's own ship. */
static void DrawBubble(int slot, ChatLine *c)
{
    const Ally *a = &g.allies[slot];
    if (!a->alive) return;

    Vector2 sp = WorldToScreen(a->pos);
    if (sp.x < -60.0f || sp.x > RENDER_W + 60.0f) return;   /* off screen */

    char head[24];
    snprintf(head, sizeof head, "%s: ", c->who);
    int headLen = (int)strlen(head);
    float headW = TextWidthPix(assets.font, head, 1);

    char full[CHAT_TEXT + 32];
    snprintf(full, sizeof full, "%s%s", head, c->text);

    char rows[3][CHAT_TEXT + 1];
    int rowStart[3];
    int n = WrapText(full, 176.0f, rows, rowStart, 3);

    float w = 0.0f;
    for (int i = 0; i < n; i++) {
        float rw = TextWidthPix(assets.font, rows[i], 1);
        if (rw > w) w = rw;
    }
    w += 13.0f;
    float h = (float)n * 13.0f + 7.0f;

    float x = floorf(sp.x - w * 0.5f);
    float y = floorf(sp.y - 26.0f - h);

    /* keep the bubble on screen: if it would hang off the top, drop it below */
    if (y < 4.0f) y = floorf(sp.y + 18.0f);
    if (x < 4.0f) x = 4.0f;
    if (x + w > RENDER_W - 4.0f) x = RENDER_W - 4.0f - w;

    float alpha = 1.0f;
    if (c->t < CHAT_FADE_IN) {
        alpha = c->t / CHAT_FADE_IN;
    } else if (c->t > c->life - CHAT_FADE_OUT) {
        alpha = Clamp((c->life - c->t) / CHAT_FADE_OUT, 0.0f, 1.0f);
    }

    unsigned char a8 = (unsigned char)(Clamp(alpha, 0.0f, 1.0f) * 255.0f);
    Color paper = { 12, 20, 28, (unsigned char)((float)a8 * 0.90f) };
    Color edge  = { c->color.r, c->color.g, c->color.b,
                    (unsigned char)((float)a8 * 0.95f) };

    /* the little tail that ties the bubble to the ship */
    float tailX = Clamp(sp.x, x + 5.0f, x + w - 5.0f);
    if (y < sp.y) {
        DrawTriangle(V2(tailX - 2.0f, y + h - 1.0f), V2(tailX + 2.0f, y + h - 1.0f),
                     V2(tailX, y + h + 3.0f), paper);
    } else {
        DrawTriangle(V2(tailX - 2.0f, y + 1.0f), V2(tailX + 2.0f, y + 1.0f),
                     V2(tailX, y - 3.0f), paper);
    }

    DrawRectangle((int)x, (int)y, (int)w, (int)h, paper);
    DrawRectangleLinesEx((Rectangle){ x, y, w, h }, 1.0f, edge);

    for (int i = 0; i < n; i++) {
        float ty = y + 3.0f + (float)i * 13.0f;
        if (i == 0 && rowStart[0] == 0) {
            DrawTextPix(assets.font, head, V2(x + 6.0f, ty), 1,
                        (Color){ c->color.r, c->color.g, c->color.b, a8 });
            if (rows[0][headLen] != '\0')
                DrawTextPix(assets.font, rows[0] + headLen, V2(x + 6.0f + headW, ty), 1,
                            (Color){ 223, 246, 245, a8 });
        } else {
            DrawTextPix(assets.font, rows[i], V2(x + 6.0f, ty), 1,
                        (Color){ 223, 246, 245, a8 });
        }
    }
}

typedef struct {
    char  text[CHAT_TEXT + 1];
    int   head;      /* leading characters that belong to the speaker's name */
    Color color;     /* who is talking                             */
} LogRow;

/* The message log down the right-hand edge. Deliberately see-through: it has
   to sit over the play field without hiding it. */
static void DrawLog(void)
{
    ChatLine *lines[CHAT_LOG];
    int n = 0;
    for (int i = 0; i < CHAT_SLOTS && n < CHAT_LOG; i++) {
        int idx = (g.chatNext - 1 - i + CHAT_SLOTS) % CHAT_SLOTS;
        if (g.chat[idx].used) lines[n++] = &g.chat[idx];
    }
    if (n == 0) return;

    LogRow lr[CHAT_LOG * 2];
    int rowCount = 0;
    float w = 0.0f;

    for (int i = 0; i < n; i++) {
        char full[CHAT_TEXT + 32];
        snprintf(full, sizeof full, "%s: %s", lines[i]->who, lines[i]->text);
        int headLen = (int)strlen(lines[i]->who) + 2;

        char r[2][CHAT_TEXT + 1];
        int rs[2];
        int k = WrapText(full, 148.0f, r, rs, 2);
        for (int j = 0; j < k; j++) {
            int h = (rs[j] == 0) ? headLen : 0;
            if (h > (int)strlen(r[j])) h = (int)strlen(r[j]);

            memcpy(lr[rowCount].text, r[j], strlen(r[j]) + 1);
            lr[rowCount].head = h;
            lr[rowCount].color = lines[i]->color;
            rowCount++;

            float rw = TextWidthPix(assets.font, r[j], 1);
            if (rw > w) w = rw;
        }
    }

    float pw = w + 14.0f;
    float ph = (float)rowCount * CHAT_ROW + 8.0f;
    float px = RENDER_W - pw - 6.0f;
    float py = 50.0f;   /* clear of the score panel above it */

    DrawRectangle((int)px, (int)py, (int)pw, (int)ph, (Color){ 10, 16, 24, 148 });
    DrawRectangleLinesEx((Rectangle){ px, py, pw, ph }, 1.0f,
                         (Color){ 120, 160, 190, 80 });

    for (int i = 0; i < rowCount; i++) {
        /* the newest line sits at the bottom, and the older ones dim out */
        int age = rowCount - 1 - i;
        unsigned char a = (unsigned char)(255 - age * 24);
        if (a < 100) a = 100;

        float ty = py + 4.0f + (float)i * CHAT_ROW;
        if (lr[i].head > 0) {
            char head[24];
            memcpy(head, lr[i].text, (size_t)lr[i].head);
            head[lr[i].head] = '\0';
            float hw = TextWidthPix(assets.font, head, 1);

            DrawTextPix(assets.font, head, V2(px + 7.0f, ty), 1,
                        (Color){ lr[i].color.r, lr[i].color.g, lr[i].color.b, a });
            DrawTextPix(assets.font, lr[i].text + lr[i].head, V2(px + 7.0f + hw, ty), 1,
                        (Color){ 223, 246, 245, a });
        } else {
            DrawTextPix(assets.font, lr[i].text, V2(px + 7.0f, ty), 1,
                        (Color){ 223, 246, 245, a });
        }
    }
}

void ChatDraw(void)
{
    DrawLog();

    for (int i = 0; i < MAX_ALLIES; i++) {
        ChatLine *c = LineOf(i);
        if (c) DrawBubble(i, c);
    }
}
