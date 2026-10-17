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
 *  short on purpose - they are read at a glance while flying. Sixteen slots
 *  per event per pilot: over five hundred lines in total, so the radio stays
 *  surprising for a long, long time.
 * ----------------------------------------------------------------------- */
#define LINES_PER_EVENT 16

static const char *kLines[ALLY_ROLE_COUNT][CHAT_EVENTS][LINES_PER_EVENT] = {
    /* ---------------------------------------------------------- VEX ----
       Cocky, loud, allergic to humility. Calls the player "SIX". Swears like
       it is a personality. */
    [ALLY_VEX] = {
        [CHAT_KILL]    = { "SCRATCH ONE, ASSHOLE!", "TOO FUCKIN' EASY.", "WHO'S NEXT?! COME ON!",
                           "LIGHTS OUT, DIPSHIT.", "GOT SOME! EAT SHIT!", "DUMBASS FLEW RIGHT INTO ME.",
                           "ONE MORE FOR THE RECYCLER.", "SAY GOODNIGHT, ASSHOLE.", "SPLAT. HE EXPLODED. NICE.",
                           "THAT'S HOW IT'S DONE, SIX.", "ANOTHER ONE BITES IT.", "BOOM. FREE VENTILATION.",
                           "EAT LEAD AND DIE, IDIOT.", "HE DIDN'T SEE ME. TOO BAD.", "THEY DIED UGLY. GOOD.",
                           "LESS ASSHOLES EVERY SECOND." },
        [CHAT_BUDDY]   = { "NO - FUCK, I'VE GOT YOU!", "THEY'LL PAY FOR THAT. SWORN.", "GODDAMMIT. NOT HIM.",
                           "HE WAS RIGHT THERE. FUCK.", "FUCK! WHO SHOT HIM?!", "HOLD ON, BUDDY! HOLD ON!",
                           "DAMMIT, ANSWER ME!", "I'M COMING - STAY WITH ME!", "THAT'S BULLSHIT. ALL OF IT.",
                           "WAS IT MY FAULT? ...NO. NO.", "THEY CHEATED A WING DOWN.", "HE FOUGHT CLEAN. THEY DIDN'T.",
                           "WHICH ONE? WHICH GUN DID IT?!", "TELL ME IT'S JUST THE RADIO.", "FUCK. FUCK. OKAY. LATER.",
                           "GRIEF LATER. SHOOTING NOW." },
        [CHAT_HURT]    = { "I'M HIT - HULL'S GOIN'!", "CAN'T SHAKE THE BASTARD!", "SIX, I NEED COVER NOW!",
                           "FUCK FUCK FUCK I'M LEAKING!", "MY WING'S GONE. STILL FLYING.", "I'M FINE! I'M NOT FINE!",
                           "HE'S ON MY TAIL - GET HIM OFF!", "QUIT SHOOTING ME, DUMBASS!", "PEEL HIM OFF ME! NOW!",
                           "WHERE'S MY REPAIR? NOBODY'S SWEATING?", "I'VE BEEN HIT FOUR TIMES TODAY. FOUR!",
                           "FUCK IT, I'M STILL DANCING.", "THEY GOT ME. NICE SHOT, ASSHOLE.", "HULL AT THIRTY AND FALLING. HELP.",
                           "EVERYTHING HURTS AND I'M ANGRY.", "MY PLATE'S GONE. EVERYTHING'S GONE." },
        [CHAT_SWARMED] = { "THEY'RE ALL OVER ME!", "TOO MANY - HELP, DAMMIT!", "GET THESE CLOWNS OFF ME!",
                           "OKAY. THIS IS ACTUALLY BAD.", "ON ME! ON ME! EVERYWHERE ON ME!", "EIGHT OF THEM. EIGHT. FUCK.",
                           "CAN SOMEBODY KILL SOME THINGS HERE?!", "I'M THE PINATA! SHOOT THEM, NOT ME!",
                           "HELP ME MAN, I'M DROWNING HERE.", "THEY WON'T STOP COMING - SHOOT!", "BACKING OFF - BUY ME A SECOND!",
                           "I HATE SWARMS. I HATE ALL OF THIS.", "WHERE'S A DOME WHEN YOU NEED ONE?!", "COME ON, SIX, SPLIT THEM UP!",
                           "SO MANY IDIOTS. SO LITTLE ME.", "THIS IS HOW I DIE: DUMB PATTERNS." },
        [CHAT_IDLE]    = { "THAT WAS MY KILL, JUNO.", "KEEP UP, KIDS.", "I COULD DO THIS ALONE.", "SLOW TODAY, SIX.",
                           "ANYONE ELSE HUNGRY?", "WHY AM I EVEN HERE?", "BORING. THAT WAS BORING.", "THEY'RE CRYING RIGHT NOW. HEARD IT.",
                           "IF I HAD TWO GUNS I'D BE LEGENDARY.", "QUIET ONES ARE PLOTTING. SO AM I.", "WHO NUMBERED THE ENEMIES? I DID.",
                           "SOMEONE SCRUB MY NOSE ART LATER.", "I BET YOU TEN KILLS I'M TOP TODAY.", "DO WE GET PAID DOUBLE FOR UGLY ONES?",
                           "SIX, REMIND ME WHO SPOTTED THAT PACK.", "ONE DAY: FAT GUNSHIP. ONE DAY." },
        [CHAT_WAVE]    = { "HERE THEY COME. FINALLY.", "MORE TARGETS. GOOD.", "OH GOOD, MORE OF THEM.", "FRESH MEAT INBOUND.",
                           "WAVE'S UP. STRETCH YOUR GUNS.", "MORONS, AS ADVERTISED.", "ALL RIGHT. LET'S RUIN THEIR DAY.",
                           "CONTACTS! GODDAMMIT, CONTACTS!", "SMELLS LIKE TARGET PRACTICE.", "THEY BROUGHT NUMBERS. WE'VE GOT ANGER.",
                           "EVERY TIME. EVERY SINGLE TIME.", "MORE? HOW MANY ARE LEFT?!", "RIGHT. SAME PLAN. SHOOT EVERYTHING.",
                           "I SMELL SCARED PILOTS.", "WAVE'S HERE. DINNER'S HERE.", "TRASH WAGON ROLLS IN ON TIME." },
        [CHAT_CLEAR]   = { "THAT ALL YOU GOT?", "SKY'S EMPTY. BORING.", "IS THAT IT? WEAK.", "I'M ALMOST DISAPPOINTED.",
                           "TOLD YOU I'M GOOD.", "AND WE'RE STILL BREATHING. HUH.", "CLEAN SWEEP. CHALK IT UP.",
                           "NOTHING LEFT. I BLAME LUCK.", "THAT WAS A WARM-UP, RIGHT?", "EASY MONEY. DISGUSTINGLY EASY.",
                           "PADDING SCORES, EVERYONE? ADMIT IT.", "LESS ASSHOLES THAN BEFORE. GOOD.", "PHEW. STUPID. AGAIN?",
                           "I WANT MORE. SEND MORE.", "NEXT PLEASE! I'M WARM NOW.", "ALL DEAD. I CHECKED. PERSONALLY." },
        [CHAT_REPLY]   = { "YEAH, YEAH.", "SAYS THE ONE WHO MISSED.", "NOTED, JUNO.", "SURE, WHATEVER YOU SAY.",
                           "COOL STORY, SIX.", "BLAME THE WIND, HUH?", "I'M RIGHT. YOU'RE JUST NOISY.", "UH-HUH. WRITE IT DOWN.",
                           "HAHA. NO.", "THANKS FOR THE ADVICE, MOM.", "GREAT PLAN. IT'S MINE, BUT GREAT.", "LOUDER FOR THE DEARLY DEPARTED.",
                           "WHATEVER FLOATS YOUR HULL.", "OKAY. THAT WAS ACTUALLY RIGHT.", "I HEARD YOU. I IGNORED YOU. SAME.",
                           "TALK SOON. OR NEVER." },
        [CHAT_RALLY]   = { "ON IT. MOVE IT!", "YEAH YEAH, I HEARD YOU.", "HOLD ON TO YOUR ASS!", "COMING THROUGH!",
                           "SAVE SOME FOR ME!", "ALREADY THERE, SIX.", "INBOUND, FAST AND RUDE.", "RIGHT. GROUP GUNS, EVERYONE.",
                           "RECALLING MY ANGRY SELF.", "FOR THE SQUAD. AND THE KILLS.", "MAKING MY WAY TO YOU.", "YOU NEED ME. GO FIGURE.",
                           "DON'T DIE, IDIOT, ALMOST THERE.", "HERE COMES THE CAVALRY. ALL ONE OF ME.", "WHEELS UP! SHOW ME THE BODY!",
                           "FINISH EM BEFORE I STEAL THEM." },
        [CHAT_LOOT]    = { "MINE. BACK OFF.", "OH HELL YES.", "SHINY.", "THIS IS COMING WITH ME.",
                           "I CALL DIBS.", "DON'T TOUCH MY CRATE.", "WHO LEFT CRATES OUT? ME. THAT'S WHO.",
                           "THANKS MAN, I NEEDED THAT.", "WOO! PRESENT FOR ME!", "IT'S MINE AND YOU ALL KNOW IT.",
                           "A GIFT. FROM A CORPSE. HA.", "FINDERS KEEPERS, SUCKERS.", "ACQUIRED. FORCEFULLY.",
                           "SCORE. LITERAL SCORE.", "CRATE'S MINE. END OF MEETING.", "STUFFING IT IN THE HULL - WHOOPS." },
        [CHAT_ELITE]   = { "THEY'VE GOT ARMOUR PLATING. FUCK.", "PLATED BASTARDS INBOUND.", "OH GOOD, THE TOUGH ONES.",
                           "THEY BROUGHT THE HARDWARE.", "ARMOUR UP. BIG DEAL.", "GREAT. THE EXPENSIVE ONES.",
                           "THOSE ONES DON'T DIE. ANNOYING.", "FAT ONES. AIM FOR THE SEAMS.", "ELITES. GRAB A CRATE FIRST.",
                           "FINALLY A CHALLENGE - GODDAMMIT.", "BIG GUNS, SMALL BRAINS. FINE BY ME.", "HEAVY TARGETS. DOUBLE-TAP THE FACES.",
                           "THEY SPENT MONEY. RUDE.", "ELITE PACK INBOUND. FOCUS.", "PLATED CLOWNS, SAME DUMB PLAN.",
                           "STEEL SKINS. HATE THOSE." },
    },

    /* --------------------------------------------------------- JUNO ----
       The steady one. Protective, unimpressed, precise - she swears once per
       crisis, exactly where it hurts, and mothers everyone including herself. */
    [ALLY_JUNO] = {
        [CHAT_KILL]    = { "TARGET DOWN.", "CLEAN KILL.", "THREAT REMOVED.", "SPLASH CONFIRMED.",
                           "ONE LESS ASSHOLE.", "DOWN AND STAYING DOWN.", "HE WON'T DO THAT TWICE.",
                           "KILL STOLEN. VERY WELL.", "SCRUBBED OFF THE MAP.", "NEAT, TIDY, DEAD.",
                           "RIGHT THERE. GOT HIM.", "THE SKY TAKES OUT THE TRASH.", "SWEEPER'S ON THE JOB.",
                           "PROBLEM SOLVED. NEXT PROBLEM.", "PUT HIM DOWN. POLITELY.", "GOTCHA." },
        [CHAT_BUDDY]   = { "WE LOST A WINGMAN.", "COVER ME. I'M ALONE BACK HERE.", "DAMN. HE'S GONE.",
                           "THAT ONE'S ON ALL OF US.", "NO! WE WERE RIGHT THERE!", "WHO DROPPED HIM? TALK.",
                           "FUCK. DAMN IT ALL.", "HE FOUGHT FOR US TILL THE END.", "I'LL WRITE THE LINE. LATER.",
                           "SAY THE NAME. OUT LOUD.", "GODDAMMIT, WE WERE CLOSE ENOUGH!", "HE'S ON THE LOG. FINAL ENTRY.",
                           "SWALLOW IT. WE KEEP FLYING.", "DEBTS ARE LOGGED.", "FOR HIM, THEN. DOUBLE TIME.",
                           "NOT LIKE THIS. NOT TO THAT THING." },
        [CHAT_HURT]    = { "STRUCTURAL DAMAGE. I'M LEAKING.", "I NEED ASSISTANCE.", "MY HULL IS THIN, SIX.",
                           "I CAN'T TAKE ANOTHER ONE.", "TAKING FIRE. STILL HERE.", "THIS IS GETTING OLD.",
                           "ONE MORE HIT AND I'M DEBRIS.", "SIX! COVER FIRE! NOW!", "PLEASE, SOMEONE, PEEL HIM OFF!",
                           "FUCKSAKE, THAT HURT.", "STILL FLYING. BARELY. STUBBORNLY.", "PATCHED WITH TAPE AND SWEARING.",
                           "THANKS - WHOEVER THAT WAS. NEEDED IT.", "HULL'S COMPLAINING. LOUDLY.", "NOT DYING TODAY. NOT PLANNING TO.",
                           "EVERY HIT'S A LIE. I REFUSE." },
        [CHAT_SWARMED] = { "I'M SURROUNDED. SUPPORT?", "THEY'VE BOXED ME IN.", "THIS IS GETTING RIDICULOUS.",
                           "SIX. NOW WOULD BE GOOD.", "TOO MANY AT ONCE. SPREAD THEM!", "ON ME FOUR WAYS - HELP!",
                           "HELP ME MAN, THIS ISN'T FAIR.", "BACKING OFF. STILL FIRING.", "FUCK, THEY'RE PACING ME.",
                           "KITE THEM. KITE. KITE.", "ANYONE SPARE A GRUDGE? I NEED ONE.", "THEY WORRY ME. GET THEM OFF.",
                           "RING OF IDIOTS. FINE. FINE!", "WORKING FREE - KEEP THE PRESSURE!", "I HATE CROWDS MORE THAN EVER.",
                           "SWARM CLOSING. BUY ME ROOM!" },
        [CHAT_IDLE]    = { "STAY IN FORMATION.", "CHECK YOUR SIX, BIT.", "SIX, WATCH YOUR HULL.",
                           "FUEL'S FINE. NERVES AREN'T.", "WHO KEEPS HUMMING?", "I HATE THIS PLANET.",
                           "FORMATIONS AREN'T DECORATION, PEOPLE.", "IF I DIE, DIVIDE MY CRATES. FAIRLY.",
                           "QUIET SKIES MAKE ME SUSPICIOUS.", "IS ANYONE ACTUALLY RESTING?", "DID EVERYONE EAT? GOOD.",
                           "YOU TWO ARE INSANE. WELCOME ABOARD.", "I COUNT BULLETS LIKE LOUD NEIGHBOURS.", "SOMEONE SCRAPE THE GUNSMOKE OFF ME.",
                           "THE CALM IS ALWAYS A TRAP SO FAR.", "I MISS SLEEP. AND CIVIL PILOTS." },
        [CHAT_WAVE]    = { "NEW CONTACTS INBOUND.", "BRACE.", "HERE WE GO AGAIN.", "THEY BROUGHT FRIENDS.",
                           "WAVE SIGNS ON THE SCOPE.", "MOVING IN. STEADY NOW.", "COUNT'S HIGH. SAME ANSWERS.",
                           "EYES UP. PLAN SHORT.", "INCOMING. FORMATION HOLDS.", "MORE OF THEM. STILL JUST TARGETS.",
                           "WATCH FOR ELITES IN THE PACK.", "FRESH WAVE. FRESH MISTAKES.", "DON'T CHARGE IN. COVER FIRST.",
                           "FORMING UP - THEN WE FEAST.", "SCOPES ARE NOISY. READY?", "REMEMBER: TOGETHER OR NOT AT ALL." },
        [CHAT_CLEAR]   = { "AREA CLEAR. REGROUP.", "GOOD WORK, TEAM.", "BREATHE, PEOPLE.", "THAT WENT WELL. SUSPICIOUS.",
                           "SKY'S QUIET. RELOAD, REST.", "EVERYONE COUNT THEIR WINGS.", "THANKS ALL. THAT WAS ROUGH.",
                           "CLEAN. POLITE. HANDSOME. US.", "NO MORE TARGETS. I'LL TAKE IT.", "PATCHING UP. DON'T SPEND IT ALL.",
                           "WE SURVIVED. AN IMPROVABLE GROUP.", "TOO CLOSE FOR COMFORT.", "CRATE COUNTS FIRST. YES?",
                           "SWEEP THE EDGES. THEN WE TALK.", "CLEAR SKY. ENJOY IT. IT'S BRIEF.", "STILL STANDING. ALL OF US." },
        [CHAT_REPLY]   = { "UNDERSTOOD.", "AS YOU SAY.", "COPY THAT, VEX.", "YOU'RE NOT THE BOSS.",
                           "IF IT MAKES YOU HAPPY.", "WHATEVER HELPS YOU SLEEP.", "NOTED. WITH AFFECTION.",
                           "THANKS FOR CONFIRMING THE OBVIOUS.", "I HEARD IT THE FIRST TIME.", "SMART. ONCE IN A WHILE.",
                           "KEEP THE NOISE. I KEEP THE PLAN.", "GOT IT.", "STOP TALKING. FLY.",
                           "THE LOG AGREES WITH YOU. NOT ME.", "COPY. MOSTLY.", "YEAH. NO." },
        [CHAT_RALLY]   = { "ON YOUR WING.", "CLOSING. HOLD ON.", "FORMING UP.", "MOVING TO YOU NOW.",
                           "RIGHT BEHIND YOU.", "COVERING YOUR SIX.", "ON MY WAY. DON'T SPEND AMMO.",
                           "INBOUND. SHOULDER TO SHOULDER.", "RECALL RECEIVED. LEAVING.", "WING TOGETHER, WINS TOGETHER.",
                           "GOT YOUR CALL. FLYING IT.", "HOLD FIRE TILL I'M INSIDE.", "TWO SECONDS. DON'T SHOW OFF.",
                           "SQUAD'S REBUILDING ON YOU.", "HERE. NOW WE'RE US AGAIN.", "BACK TOGETHER. BETTER TOGETHER." },
        [CHAT_LOOT]    = { "TOOK IT.", "GRABBED IT.", "NEEDED THAT, THANKS.", "PERFECT TIMING.",
                           "I'LL TAKE IT.", "USEFUL.", "CRATE'S LOGGED IN MY NAME.", "THANK YOU, DEAD GUNNER.",
                           "SHARING IT AROUND AFTER.", "MINE. SORRY. NOT SORRY.", "SAVED A PRESENT FOR SOMEBODY.",
                           "SWIPED IT BEFORE SHRAPNEL COULD.", "CRATES DON'T NEGOTIATE. I DO.", "SPEND IT WISELY. OR LOUDLY.",
                           "MAYBE I'LL SHARE. MAYBE NOT.", "MOTHER'S PRIVILEGE: FIRST CRATE." },
        [CHAT_ELITE]   = { "PLATED CONTACTS. WATCH YOUR FIRE.", "ARMOUR. FOCUS THE LEADERS.", "ELITE ESCORT. PRIORITY TARGET.",
                           "THEY'VE UPGRADED. SO HAVE WE.", "HEAVIER HULLS. ADJUST.", "THEY BROUGHT THE GOOD STUFF.",
                           "ELITES IN THE PACK. CONSERVE GUNS.", "THICK SKINS. COORDINATE, PEOPLE.", "SHIELDS FIRST, THEN HULLS.",
                           "ALL EYES ON THE BIG ONE.", "FAIR WARNING: THOSE BITE BACK.", "GUNS INTO THE FAT GUY. TOGETHER.",
                           "THOSE COST REAL MONEY. RUIN IT.", "PLATED FOLKS. PERSUASION TIME.",
                           "PACK LEADERS FIRST.", "NO SOLO RUNS AT THE ARMOUR." },
    },

    /* ---------------------------------------------------------- BIT ----
       The rookie. Brilliant with the seeker rack, terrified of everything,
       and honest to a fault - panic, gratitude and all of it live. */
    [ALLY_BIT] = {
        [CHAT_KILL]    = { "OH! GOT HIM! GOT HIM!", "DID YOU SEE THAT?!", "AWAY- AND IT HIT! WOW.",
                           "I DID A KILL! I MEAN- A KILL!", "HAHAHA HE EXPLODED!", "AM I GOOD AT THIS?!",
                           "DEAD! I MADE HIM DEAD!", "SIX! SAW THAT, RIGHT?!", "WOO! THIS GUN LOVES ME!",
                           "GOTCHA GOTCHA GOTCHA!", "EAT ROCKS, IDIOT!", "FUCKSAKE, DUMBASS BLEW UP.",
                           "THAT WAS LOUD AND I LOVED IT.", "PEELED! I MEAN- KILLED!", "SORRY. NOT SORRY, ACTUALLY.",
                           "MY HANDS ARE SHAKING! GOOD SHAKES!" },
        [CHAT_BUDDY]   = { "OH NO. OH NO OH NO.", "ANSWER ME! PLEASE ANSWER!", "HE'S GONE HE'S GONE HE'S GONE.",
                           "IT WASN'T SUPPOSED TO DO THAT.", "NO NO NO HE WAS JUST HERE!", "FUCK! WHY HIM?!",
                           "WAIT- HE'S OFF THE LOG-", "TELL ME IT'S JUST THE RADIO.", "I- I CAN'T SEE HIM ANYMORE.",
                           "STOP DYING! NOBODY AGREED TO THAT!", "THAT'S NOT FAIR! THAT'S NOT FAIR!",
                           "HE TAUGHT ME THE LOOP TRICK. BASTARDS.", "I WAS LAUGHING A MINUTE AGO. GODDAMMIT.",
                           "SOMEBODY TELL ME HE PINGED OUT.", "WHY DO YOU GUYS GET SHOT, HUH?!", "PAY THEM BACK. ALL OF THEM. NOW." },
        [CHAT_HURT]    = { "AAAH MY HULL!", "I'M HIT I'M HIT I'M HIT!", "SOMEONE HELP!",
                           "MY LEG! ...I DON'T HAVE LEGS.", "FFFF- I'M OKAY. SORT OF.", "THIS HURTS SO MUCH!",
                           "HELP ME MAN, I'M SCREAMING!", "IT BURNS! WHY DOES IT BURN?!", "FUCK! FUCK! COVER ME!",
                           "MY HULL'S SHEDDING! IT'S NOT SUPPOSED TO DO THAT.", "I NEED A MOM AND A MECHANIC!",
                           "ON ME! REPAIR SEAT! PLEASE!", "I'M SPARKING. IS SPARKING BAD? IT'S BAD.",
                           "THANKS! THANKS! NEEDED THAT BADLY!", "DON'T LET ME DIE DON'T LET ME DIE.", "STILL FLYING! KINDA! TECHNICALLY!" },
        [CHAT_SWARMED] = { "THEY'RE EVERYWHERE!", "SIX! VEX! ANYONE!", "TOO MANY TOO MANY TOO MANY!",
                           "I HATE THIS I HATE THIS!", "ON ME! ON ME! ALL OF THEM!", "HELP! HELP! HELP!",
                           "WHY ARE THERE SO MANY?!", "I'M FLYING IN A WASP NEST!", "GET THEM OFF ME OFF ME OFF ME!",
                           "HELP ME MAN, THEY'RE HUGE AND MANY!", "I'M CRYING AND STILL SHOOTING!",
                           "BACKING UP! SCREAMING! TALKING!", "THIS IS MY NON-BUDDY. TWO SEATS LEFT!",
                           "I MISS THE FENCE. ANY FENCE.", "THEY SECRETLY AGREED TO RUIN ME.", "SO MANY. I CAN'T EVEN COUNT ANGRY." },
        [CHAT_IDLE]    = { "UH. IS THE LEAD SUPPOSED TO DO THAT?", "MY SENSORS ARE SCREAMING.",
                           "I'M FINE. TOTALLY FINE.", "DO WE GET PAID FOR THIS?",
                           "I THINK I LEFT THE STOVE ON.", "MY HANDS ARE SHAKING. COOL.",
                           "I FLY BETTER WHEN SCREAMING. FACT.", "NIGHT, EVERYONE! ...IS IT NIGHT?",
                           "I NAMED A BULLET. IT KEEPS MISSING.", "SO THIS IS MY FAMILY NOW. GUYS.",
                           "I COLLECTED SCRAPS. SENTIMENTAL ONES.", "VEX, CAN I TOUCH YOUR GUN ONE TIME?",
                           "MY SUIT SMELLS LIKE BURNED IDIOT.", "IF I STOPPED BEING SCARED, WORRIED.", "LOG SAYS EVERYONE SURVIVED. GOOD ROUND.",
                           "THE QUIET KINDA TERRIFIES ME, YEAH." },
        [CHAT_WAVE]    = { "OH NO. HERE THEY COME.", "I DON'T LIKE THIS.", "NOT AGAIN NOT AGAIN.",
                           "WHY ARE THERE SO MANY?!", "NEW WAVE! I'M NOT READY!", "SEEING RED EVERYWHERE!",
                           "SCOPES SAY NO. MY STOMACH SAYS NO.", "THAT'S A LOT OF SHAPES.", "INCOMING! HIDING! KIDDING! NOT HIDING!",
                           "THEY'RE BACK! GREAT! TERRIBLE!", "STRETCHING MY ARMS- WINGS!", "SAME SONG, SCARIER VERSE.",
                           "EVERY ONE OF THOSE IS EXCITED TO EAT ME.", "I'LL BE FINE. PROBABLY. ROUGHLY.", "WAVE'S UP! PANIC'S UP!",
                           "BLIP BLIP BLIP. TOO MANY BLIPS!" },
        [CHAT_CLEAR]   = { "WE LIVED! WE LIVED!", "IS IT OVER? IS IT OVER?", "I NEED A MINUTE.",
                           "HAHAHA WE'RE ALIVE?!", "THANK YOU THANK YOU THANK YOU.", "THAT WAS THE WORST BEST ROUND!",
                           "ALL QUIET. I DON'T TRUST IT.", "I HAVEN'T SCREAMED IN SECONDS. RECORD!", "IS THAT ALL OF THEM? COUNT AGAIN.",
                           "I'M GOING TO SIT DOWN FOREVER.", "WE DID IT! WHO DID MOST OF IT? VEX? OKAY.",
                           "MY WINGS SURVIVED. ALL FOUR EXITS.", "I'M SO RELIEVED I'M LAUGHING WRONG.", "I PROFESSED LOVE TO MY GUN MID-FIGHT.",
                           "NOT DEAD. CERTIFIED NOT DEAD.", "PHEW. PHEW. PHEW." },
        [CHAT_REPLY]   = { "OKAY OKAY OKAY!", "R-RIGHT!", "COPY! I THINK! YES! COPY!", "SORRY! SORRY!",
                           "YES SIR! I MEAN- YES!", "THAT'S NOT HELPING!", "I'LL DO BETTER I'LL DO BETTER!",
                           "UNDERSTOOD! MOSTLY! PARTLY!", "THANKS, I GUESS?", "WAIT- WAS THAT FOR ME?",
                           "MY LOG SAYS YOU'RE WRONG. I WISH IT DIDN'T.", "TAKING NOTES. SWEATY NOTES.", "WHY IS EVERYONE YELLING- OH. RIGHT. FIGHTING.",
                           "GOTCHA! GOT IT! GOTCHA?!", "DI-DID EVERYONE SEE MY KILL? NOBODY?", "OKAY! NEW SCARED ME LOADED!" },
        [CHAT_RALLY]   = { "ON MY WAY! ON MY WAY!", "COMING! I'M COMING!", "WAIT FOR ME! I'M LATE!",
                           "DON'T LEAVE ME!", "OKAY OKAY I'M COMING!", "COMING COMING COMING!",
                           "FULL BURN! MOSTLY CONTROLLED!", "TO THE PLAYER! FAST AS SCARED!", "I'M FLYING SO GOOD RIGHT NOW! AAAH!",
                           "REAR-FORMATION REAR-FORMATION- JOKING! INBOUND!", "SAFETY-FIRST RECALL COMING RIGHT UP!",
                           "I'M ASTERN! ASTERN IS GOOD!", "ON COURSE. SOMEWHAT. KINDA. YES.", "I HEAR YOU! I COME TO YOU!",
                           "WHEELS- WINGS? WINGS RUNNING!", "COMING! DON'T SHOOT ME THOUGH FIGHT SOMEONE ELSE!" },
        [CHAT_LOOT]    = { "OH! SHINY!", "IS THIS MINE? IT'S MINE.", "I FOUND SOMETHING!",
                           "WAIT, WAS THAT YOURS?", "OOPS. MINE NOW.", "HEHEHE. PRESENTS.",
                           "THANKS MAN, I NEEDED THAT SO MUCH!", "CRATE! MINE! DIBS DIBS!", "I GOT PRESENTS FROM A CORPSE!",
                           "I'M A GIFT-COLLECTING PROFESSIONAL NOW.", "MINE! THANK YOU, EXPLOSION MAN!", "OHHHH THAT'S A BIG CRATE.",
                           "SHARING! ...SHARING MOST!", "I STOLE IT. POLITELY. GENTLY. FAST.", "IT'S SNUG IN MY CARGO NOW.",
                           "LOOTING IS MY FAVOURITE BORING SKILL!" },
        [CHAT_ELITE]   = { "THEY'RE ALL SHINY NOW! RUN!", "PLATED. BIG PLATED. HELP.", "THEY BROUGHT THE SCARY ONES.",
                           "WHY ARE THEY BIGGER?!", "I DON'T LIKE THIS UPGRADE.", "ELITE?! SINCE WHEN?!",
                           "THEY GOT A GYM MEMBERSHIP?!", "THICK-ARMOUR! THAT'S CHEATING!", "I'M AIMING AT ONE. I'M DOING IT. AAAH.",
                           "TEAM UP ON THE BIG ONE, RIGHT?!", "MY SEEKERS SAY THAT ONE IS EXPENSIVE.", "ELITE! TIME FOR MY BEST SCREAM!",
                           "THE BIG ONE WINKED AT ME. ASSHOLE.", "IF WE KILL IT, IS DINNER ON IT?", "PLATED AND PROUD OF IT.",
                           "I'M HELPFULLY MAINTAINING DISTANCE!" },
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
    if (g.chatIdle > 5.5f) {
        g.chatIdle = -GetRandomValue(0, 40) * 0.05f;   /* small random gap */

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
    /* H toggles the bubbles overhead, L toggles this log - the squadron can
       be muted one channel at a time */
    if (input.chatLog) DrawLog();

    for (int i = 0; i < MAX_ALLIES; i++) {
        ChatLine *c = LineOf(i);
        /* the radio log on the right stays; only the bubbles overhead can
           be switched off, when the screen starts to matter more than the
           banter */
        if (c && input.chatPopups) DrawBubble(i, c);
    }
}
