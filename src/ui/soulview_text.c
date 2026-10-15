/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The Soulview's texts: a tracked SOUL's name (0x47f8fe) and its life story
 * (0x47adea's text, the template expander 0x47af6f and its helpers
 * 0x47aca3-0x47d018). See ui/soulview.h.
 *
 * Both are pure functions of the tracker's seed (record +8), plus its
 * plane, temporary flag and building's zone kind, the game year (for
 * dates) and the selected plane's sim cells. They draw no random numbers.
 *
 * The story is one of 18 templates (k_stories), each a format string with
 * three lists of words picked by seed bits; the result is expanded:
 * "&(XY ...)" inserts an item, mostly a word from a list picked by seed bits
 * (the word lists below, named after what they hold, with the item code in
 * their comment), sometimes a nested template, a name, a number, a date or
 * one of two alternatives "&(OR (a) (b))". After '.', '?' or '!' the next
 * letter is capitalised; "&(CP)" forces it.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/util.h"
#include "game/world.h"
#include "ui/soulview.h"

/* 0x47aca3: n bits of v from bit `shift`, with the original's mask
 * (1 << (shift + n)) - 1 taken mod 32, so a field reaching bit 32 or past
 * it reads (mostly) zeros */
static uint32_t bits(uint32_t v, unsigned shift, unsigned n)
{
    uint32_t m = (1u << ((shift + n) & 31)) - 1u;
    return (v & m) >> (shift & 31);
}

/* 0x47accf: v rotated left by k, as the original does it: k > 31 is taken
 * mod 31 and the right shift is by 31 - k, so bit 31 - k is dropped */
static uint32_t rot(uint32_t v, unsigned k)
{
    k &= 0xff;
    if (k > 31)
        k %= 31;
    if (k != 0)
        v = v << k | v >> ((31 - k) & 31);
    return v;
}

/* ---- the name ------------------------------------------------------------ */

/* 0x66a960: name syllables at even positions */
static const char *const k_name_first[5] = {"kla", "klu", "kli", "tra", "t'kla"};

/* 0x66a998: name syllables at odd positions */
static const char *const k_name_second[4] = {"k'ta", "n'rk", "aktu", "aktr"};

/* 0x47f8fe */
void soulview_soul_name(uint32_t seed, char *out)
{
    if (seed == 0) {
        snprintf(out, SOULVIEW_NAME_LEN, "%s", "No Soul" /* 0x66ac40 */);
        return;
    }
    out[0] = 0;
    size_t len = 0;
    unsigned n = ((seed >> 29) & 1) + 3;
    for (unsigned i = 0; i < n; i++) {
        unsigned nib = (seed >> ((i * 4) & 31)) & 0xf;
        const char *s = (i & 1) == 0 ? k_name_first[nib % 5] : k_name_second[nib & 3];
        size_t l = strlen(s);
        if (len + l >= SOULVIEW_NAME_LEN)
            l = SOULVIEW_NAME_LEN - 1 - len;
        memcpy(out + len, s, l);
        len += l;
        out[len] = 0;
    }
    out[0] = (char)(out[0] - 0x20);
}

/* ---- the word lists ------------------------------------------------------ */

/* A word group (packed {char *fmt; u8 n; char *word[n]} in the original):
 * sprintf(fmt, word[i % n]). */
typedef struct WordGroup {
    const char *fmt;
    uint8_t n;
    const char *const *words;
} WordGroup;

/* A noun (packed {char *word; char *singular; u8 plural}): the word plus
 * the singular ending, or k_plural_endings[plural]. */
typedef struct Noun {
    const char *word, *singular;
    uint8_t plural;
} Noun;

/* A story template (packed {char *fmt; u8 n0, n1, n2; char *word[n0 + n1 +
 * n2]}): fmt with one word from each of its three lists. */
typedef struct StoryTemplate {
    const char *fmt;
    uint8_t n[3];
    const char *const *words[3];
} StoryTemplate;

/* clang-format off */
/* 0x6658d8: &(AC) */
static const char *const k_activities[29] = {
    "fishing", "square dancing", "tricycle drag racing", "slacking off", "beer chugging", "bicycling",
    "playing mahjong", "mud wrestling", "playing golf", "slam dancing", "scuba diving", "hiking", "sun bathing",
    "camping", "mountain biking", "giving dumb looks", "jogging", "playing chess", "arm wrestling",
    "playing checkers", "sun bathing", "surfing", "para-sailing", "bowling", "sumo wrestling", "telling bad jokes",
    "snorkeling", "hunting", "dancing the Mao-Mao in the moonlight by the Oolalei",
};

/* 0x666510: &(AJ) */
static const char *const k_adjectives[21] = {
    "grungy", "cheesy", "dark", "affluent", "stormy", "dorky", "peachy", "geeky", "disturbing", "smelly", "spiffy",
    "yuky", "obnoxious", "pooey", "austentatious", "dopey", "dumb", "lugubrious", "socially challenged", "slimy",
    "overweight",
};

/* 0x6665d8: &(AN) */
static const char *const k_animals[14] = {
    "cat", "arctic turn", "dog", "chicken", "hippopotomous", "peacock", "pig", "horse", "mouse", "rooster", "fish",
    "snake", "shark", "guppy",
};

/* 0x666730: &(BF) */
static const char *const k_body_fluids[8] = {
    "blood", "spinal fluid", "saliva", "urine", "bile", "mucous", "vomit", "spit",
};

/* 0x666c38: &(BN) */
static const char *const k_insults[9] = {
    "poo-bear", "slimeball", "idiot", "geek", "dork", "moron", "dweeb", "bastard", "butthole",
};

/* 0x6668a0: &(BP) */
static const char *const k_body_parts[41] = {
    "head", "torso", "hand", "eyelash", "knee", "vertebrae", "arm", "wrist", "leg", "shoulder", "tooth", "ankle",
    "jaw", "foot", "finger", "nose", "toe", "elbow", "thumb", "pinky", "eyelid", "meta-carpel", "posterier",
    "index finger", "eye", "heiny", "cranium", "butt", "neck", "chest", "pelvis", "calf", "ear", "face", "back",
    "thigh", "but-tocks", "spine", "stomach", "mouth", "big toe",
};

/* 0x665ea0: &(CH) */
static const char *const k_hangouts[12] = {
    "Lazy Horse IV", "Hedonist-R-Us", "Marilyn's House-O-Fun", "Phil's Peepshow and Restaurant", "Garterland",
    "The Apple Ranch", "Hooks' Girl-O-Rama", "The Thoroughbred Ranch", "Babeland", "Madam Svelta's Flesh Farm",
    "Rachel Brother's Funhouse", "Keith's Girls on Stage Review",
};

/* 0x666288: &(CI) */
static const char *const k_cities[11] = {
    "the Balkans", "Baghdad", "Venice", "Madrid", "the Mediterranean", "New San Frisco", "Bangkok", "Brownsville",
    "Mesopotamia", "Ethiopia", "the Aegean Sea",
};

/* 0x666b70: &(CL) */
static const char *const k_clothes[28] = {
    "shirt", "pants", "trousers", "pom-pom", "cowboy hat", "shorts", "suit", "bolero", "stockings", "garter belt",
    "black tie", "suspenders", "Texas Tie", "dinner gown", "tie", "boots", "loafers", "underwear", "lingerie",
    "boxer shorts", "tennis shoes", "hat", "cap", "jeans", "cape", "Sicilian necktie", "tuxedo", "sandals",
};

/* 0x6666a0: &(CO) */
static const char *const k_colors[18] = {
    "aqua", "beige", "black", "blue", "brown", "cyan", "gold", "green", "grey", "maroon", "mauve", "orange", "pink",
    "purple", "red", "tiel", "white", "yellow",
};

/* 0x666148: &(CU) */
static const char *const k_coins[15] = {
    "penny", "nickel", "dime", "quarter", "dollar", "yen", "shekel", "pence", "farthing", "shilling", "Pound",
    "peseta", "centavo", "peso", "franc",
};

/* 0x666418: &(DI) */
static const char *const k_diseases[10] = {
    "salmonella", "diabetes", "pneumonia", "mad bovine disease", "flu", "tuberculosis", "crazy chicken disease",
    "ebola", "the plague", "anthrax",
};

/* 0x6659d8: &(DR) */
static const char *const k_drinks[12] = {
    "beer", "milk", "soda", "orange juice", "hot chocolate", "whiskey", "strawberry shake", "malt", "black and tan",
    "scotch", "orange soda", "orange whip",
};

/* 0x6661d8: &(EM) */
static const char *const k_emotions[10] = {
    "love", "hate", "like", "fear", "desire", "crave", "adore", "want", "need", "despise",
};

/* 0x6662f0: &(FR) (expanded further) */
static const char *const k_acquaintances[5] = {
    "&(RL)", "neighbor", "acquaintance", "co-worker", "friend",
};

/* 0x665f20: &(GO) (expanded further) */
static const char *const k_exclamations[8] = {
    "yikes", "golly", "gee wiz", "jeepers", "holy &(CI)", "jimminy crickets", "shazoom", "mama",
};

/* 0x6651a0: &(IN) */
static const char *const k_insects[8] = {
    "wasps", "locusts", "bees", "mosquitoes", "gnats", "fire ants", "wamp rats", "Ewoks",
};

/* 0x666050: &(LN) */
static const char *const k_last_names[31] = {
    "Jones", "Smith", "Castro", "Brown", "Graham", "White", "Lee", "Martinez", "Gonzalez", "Stemmle", "Pick",
    "Einstein", "Washington", "Remmes", "Gregory", "Hooks", "Leech", "Sulak", "Cafrelli", "Taylor", "Vidos",
    "Guttenburg", "Marti", "Oppenheimer", "Kahn", "Darwin", "Mica", "Simpson", "Armstrong", "Wu", "Wong",
};

/* 0x665a40: &(ME) */
static const char *const k_meals[6] = {
    "breakfast", "brunch", "lunch", "dinner", "supper", "snack",
};

/* 0x666a08: &(OG) */
static const char *const k_organs[19] = {
    "tongue", "kidney", "stomach", "heart", "lung", "throat", "esophagus", "ear drum", "pancreas", "brain",
    "intestine", "lymph node", "lymph gland", "spinal column", "optic nerve", "spleen", "appendix", "liver",
    "tonsils",
};

/* 0x665b88: &(PL) */
static const char *const k_places[26] = {
    "restaurant", "theater", "movies", "bar", "beach", "home", "mall", "opera", "park", "monastery", "golf course",
    "parking lot", "night club", "church", "shopping center", "bowling alley", "temple", "palace", "zoo", "aquarium",
    "theme park", "Timmie's Tacky Toys", "casino", "Commuter's Express Show", "house of ill repute",
    "back of a pickup truck",
};

/* 0x665440: &(PR) */
static const char *const k_professions[55] = {
    "doctor", "lawyer", "arsonist", "police officer", "mayor", "dentist", "construction worker", "blacksmith",
    "minstrel", "wood carver", "computer programmer", "writer", "painter", "sculptor", "fireman", "plumber",
    "scuba diver", "athlete", "amateur boxer", "flutist", "pianist", "carpenter", "news reporter", "butcher",
    "aspiring dictator", "gambler", "priest", "monk", "astronaut", "chemist", "physicist", "biologist",
    "marine biologist", "teacher", "farmer", "rancher", "cleaner", "corporate whipping boy", "bovine scatologist",
    "tailor", "surfer", "tattoo artist", "chef", "shoe salesman", "fry cook", "receptionist", "secretary",
    "seamstress", "politician", "shrimper", "fisherman", "mariachi", "hit man", "merchant", "terrorist",
};

/* 0x665d58: &(RE) */
static const char *const k_restaurants[16] = {
    "Mayberry Brewing Company", "Nose Brother's Burger Shack", "Burger World", "Poodle's", "Wong's Cantonese Deli",
    "Willis' Oil Change and Burger Pit", "Blackie's BBQ Grill", "One Rock Brewery", "The Grease Pit",
    "Cat's Old York Deli", "Los Mariachis Restaurant", "Fuchang's MSG Dragon", "Heart Block Cafe",
    "Wok-n-Talk's Restaurant", "The Cat-N-Possum Grill", "The Roadkill Cafe",
};

/* 0x666368: &(RL) */
static const char *const k_relatives[11] = {
    "father", "mother", "sister", "brother", "son", "daughter", "cousin", "grandfather", "grandmother", "aunt",
    "uncle",
};

/* 0x6656c8: &(SH) */
static const char *const k_superheroes[20] = {
    "Catman", "The Flea", "Infinityman", "American Bam Stan", "Wonder Wimp", "YESman", "Ratman", "H2Oman",
    "Nowhere Man", "Colonel Landers", "Captain Americus", "Aluminum Man", "Superfly", "Captain Jinx", "Inflator Rat",
    "Coward Man", "Whipping Boy", "Three-eyed Theo", "Major Kria", "Knuckles McNally",
};

/* 0x665588: &(ZO) */
static const char *const k_zodiac[12] = {
    "Aries", "Taurus", "Gemini", "Cancer", "Leo", "Virgo", "Libra", "Scorpio", "Sagittarius", "Capricorn", "Aquarius",
    "Pisces",
};

/* 0x665068: &(SYGO) */
static const char *const k_syn_gorging[3] = {
    "gorging", "eating", "feasting",
};

/* 0x6680f0: &(SYFO) */
static const char *const k_syn_formerly[3] = {
    "formerly", "previously", "once",
};

/* 0x665098: &(SYHA) */
static const char *const k_syn_hated[3] = {
    "hated", "despised", "detested",
};

/* 0x6650d0: &(SYNN) */
static const char *const k_syn_nicknamed[4] = {
    "dubbed", "nicknamed", "known as", "called",
};

/* 0x665138: &(SYST) */
static const char *const k_syn_stealing[7] = {
    "stealing", "taking", "pilfering", "embezzling", "'lifting'", "permanently borrowing", "robbing",
};

/* 0x664970: Noun.plural */
static const char *const k_plural_endings[4] = {
    "s", "ies", "es", "",
};

/* 0x664bb8: &(NO) */
static const Noun k_possessions[35] = {
    {"little red wagon", "", 0}, {"car", "", 0}, {"boat", "", 0}, {"domestic partner", "", 0}, {"door knob", "", 0},
    {"house", "", 0}, {"Havana", "", 0}, {"moped", "", 0}, {"unicycle", "", 0}, {"carpet", "", 0}, {"sweater", "", 0},
    {"power tool", "", 0}, {"job", "", 0}, {"piece of land", "", 0}, {"ranch", "", 2}, {"shoe", "", 0},
    {"appliance", "", 0}, {"stereo", "", 0}, {"TV", "", 0}, {"job", "", 0}, {"tricycle", "", 0},
    {"lawn mower", "", 0}, {"abacus", "", 2}, {"social status", "", 3}, {"Wimpos '95", "", 3},
    {"patio furniture", "", 3}, {"spouse", "", 0}, {"Ultium P9000 computer system", "", 0}, {"homework", "", 3},
    {"Amigo 6000 computer system", "", 0}, {"garage", "", 0}, {"sail boat", "", 0}, {"motorcycle", "", 0},
    {"watercraft", "", 0}, {"Pooch and Psycho Bunny poster", "", 0},
};

/* 0x664ef8: &(FO) */
static const Noun k_foods[37] = {
    {"garlic", "", 0}, {"beef ribs", "", 3}, {"steak", "", 0}, {"casserole", "", 0}, {"mashed potatoes", "", 3},
    {"papaya", "", 0}, {"burger", "", 0}, {"pizza", "", 0}, {"pear", "", 0}, {"paella", "", 3}, {"peach", "", 2},
    {"liver", "", 0}, {"braunschweiger", "", 0}, {"french fries", "", 3}, {"apple", "", 0}, {"egg nog", "", 0},
    {"banana", "", 0}, {"flan", "", 0}, {"pate'", "", 0}, {"chili dog", "", 3}, {"apple pie", "", 0},
    {"watermelon", "", 0}, {"sushi", "", 3}, {"pecan pie", "", 0}, {"guava", "", 0}, {"fish and chips", "", 3},
    {"cantaloupe", "", 0}, {"enchilada", "", 0}, {"beer", "", 0}, {"potato", "", 2}, {"grape", "", 0},
    {"strawberr", "y", 1}, {"taco", "", 0}, {"mango", "", 0}, {"patty melt", "", 0}, {"avocado", "", 0},
    {"maganja", "", 0},
};

/* 0x668090: BS/FB/GD, by zone kind */
static const WordGroup k_good_deeds[7] = {
    {"%s", 4, (const char *const[]){
        "being utterly content with &(SXP) &(NOS) in life", "living a simple life",
        "being content with only one &(NOS)", "being happy with &(SXP) allotment of &(NOP)",
    }}, /* 0x666d00 */
    {"%s", 5, (const char *const[]){
        "contributing generously to charities", "giving free &(NOP) to all", "tipping well",
        "being free with &(SXP) money", "magnanimously supporting the arts",
    }}, /* 0x666db8 */
    {"%s", 6, (const char *const[]){
        "dieting", "founding &(RE)", "cutting back on &(FOP) between meals", "never eating all you can eat",
        "resisting the urge to wipe &(SXP) plate clean", "avoiding &(RE) like a bad case of &(DI)",
    }}, /* 0x666e90 */
    {"%s", 5, (const char *const[]){
        "having an \"all work and no play\" attitude", "staying that extra hour after work every day",
        "working hard to afford a new &(NOS)", "adhering vigorously to the Puritan work ethic",
        "crunching through the crunchiest of crunch modes",
    }}, /* 0x666f98 */
    {"%s", 5, (const char *const[]){
        "controlling &(SXP) libido", "being the master of &(SXP) domain", "never renting naughty movies",
        "avoiding &(CH) like nobody's business", "knowing when to say \"No\"",
    }}, /* 0x667060 */
    {"%s", 6, (const char *const[]){
        "graduating from the Mahatma Gandhi School for Pacifism",
        "having angelic understanding and patience for others", "defusing conflicts before they arise",
        "always thinking before reacting", "maintaining a calm composure through the direst of circumstances",
        "keeping the Balistometer below the boiling point",
    }}, /* 0x6671b0 */
    {"%s", 4, (const char *const[]){
        "giving due credit to others", "humbling acknowledging &(SXP) own limitations",
        "never seeking praise for &(SXP) own work", "acknowledging the limitations of &(SXP) &(NOS)",
    }}, /* 0x667280 */
};

/* 0x6680b0: WS/FW/ED, by zone kind */
static const WordGroup k_bad_deeds[7] = {
    {"%s", 5, (const char *const[]){
        "coveting &(SXP) neighbor's &(NOS)", "putting down other people's abilities",
        "taking credit for other people's work", "getting a bigger and better &(NOS) than &(SXP) &(FR)",
        "keeping up with the &(LN)'s",
    }}, /* 0x667368 */
    {"%s", 14, (const char *const[]){
        "being cheaper than &(OR (Congress) (local government)) during budget hearings", "never tipping",
        "&(CU)-pinching to the Nth degree", "&(OR (income) (corporate)) tax evasion", "&(SYST) &(NOP)",
        "underpaying &(SXP) share of the tip", "hording &(OR (&(NOP)) (every last &(CU)))",
        "shaving down every last &(CU)", "never giving a &(CU) (or a darn) to help someone",
        "spending &(NU 5 24) hours a day counting &(SXP) &(CU)s", "shortchanging others",
        "taking tip money from tables", "stopping and picking up every last &(CU) off the street",
        "wanting more and more &(OR (&(CU)) (&(NOP)))",
    }}, /* 0x6675c0 */
    {"%s", 8, (const char *const[]){
        "&(SYGO) on too many &(FOP)", "founding &(RE)", "always being the first to arrive and last to leave buffets",
        "stuffing oneself at every meal",
        "arriving &(NU 1 6) hours early to buffets in order to reap the maximum harvest",
        "hogging all the popcorn at movies", "ordering a mountain of greasy food and then a diet &(DR)",
        "craving &(FOS) for &(ME)",
    }}, /* 0x667758 */
    {"%s", 10, (const char *const[]){
        "leading a life of total slack", "letting other people do all the work", "not washing &(SXP) &(NOS)",
        "letting the dishes stockpile", "procrastinating", "freeloading", "refusing to do the laundry",
        "leaving empty &(DR) cans all over the floor", "never cutting the grass",
        "always trying to catch just one more hour of sleep",
    }}, /* 0x6678b8 */
    {"%s", 12, (const char *const[]){
        "not knowing when to say \"No\"", "renting &(NU 1 6) naughty movies each night",
        "pasting &(NU 10 30) centerfolds on EVERY wall", "having an overly active libido",
        "surfing the Net endlessly in search of cyberporn",
        "staying up past &(NU 1 6) every night thumbing through naughty magazines", "having a one track mind",
        "downloading &(NU 80 250) googabytes of smut every day (that's alot of smut)",
        "chasing thy neighbor's &(RE)",
        "spending &(NU 2 10) hours a day watching every scrambled adult channel &(SXS) could find",
        "failing to master one's domain", "having abnormally high teenage hormone levels (if there is such a thing)",
    }}, /* 0x667b70 */
    {"%s", 9, (const char *const[]){
        "starting fights in &(PL)s", "brawling over someone's &(NOS)",
        "having the temperament of a swarm of &(IN) on a hot summer day", "ranting and raving for the Hell of it",
        "pouting to get &(SXP) way", "throwing temper tantrums", "kicking furniture around",
        "going Postal Wookie style", "graduating from Vlad the Impaler's School for Social Temperance",
    }}, /* 0x667d08 */
    {"%s", 6, (const char *const[]){
        "unduly praising oneself", "overly exalting oneself", "belittling others",
        "looking at oneself in the mirror &(NU 2 20) hours a day ", "putting on makeup while driving",
        "never acknowledging the contributions of others",
    }}, /* 0x667e08 */
};

/* &(HO) */
static const WordGroup k_hobbies = {"%s", 1, (const char *const[]){"&(AC)"}}; /* 0x667e38 */

/* &(QO) */
static const WordGroup k_quotes = {"%s", 12, (const char *const[]){
    "life is too short to drink cheap &(DR)", "&(NOS) will get you more chicks", "&(GO)!  Did I do that?",
    "holy skintight outfits, &(SH), I think I'm in love", "I'm a &(NOS), yes I am",
    "Will you be my &(OR (daddy) (mommy))?", "&(AC), it's the new sport I tell you",
    "&(FOS), it's not just for &(ME) anymore", "no &(NOS)? Now you must fight the bear",
    "I don't know what hit me, if it was an uppercut or a downward punch.  That was longest 98 seconds of my life!",
    "I'm a &(ZO), what are you?", "if ya ain't winning, ya ain't cheatin' hard enough",
}}; /* 0x668058 */

/* &(VI) */
static const char *const k_virtues[7] = {
    "contented" /* 0x66aa0c */, "charitable" /* 0x66aa24 */, "temperate" /* 0x66aa3c */, "diligent" /* 0x66aa54 */,
    "chaste" /* 0x66aa68 */, "peaceful" /* 0x66aa7c */, "humble" /* 0x66aa90 */,
};

/* 0x66a7f0 */
static const StoryTemplate k_stories[18] = {
    {/* 0x6681c8: the third word is read from past the record, which gives none */
     "&(SYHA) for &(WS), &(NA)'s most memorable deed was &(BS).\n"
     "\n"
     "Regrettably, &(NA)'s habit of &(FB) did not make up for &(SXP) &(FW).\n"
     "\n"
     "&(SXP) favorite &(OR (pastime is &(HO)) (quote is: \"&(QO)\")).",
     {1, 1, 1},
     {(const char *const[]){""},
      (const char *const[]){""},
      (const char *const[]){""}}},
    {/* 0x668360 */
     "         WANTED!\n"
     "\n"
     "Name:   &(NA)\n"
     "Height: &(NU 122 152) cm\n"
     "Weight: &(NU 68 95) kg\n"
     "DOB:    &(DA)\n"
     "\n"
     "Wanted for:\n"
     "&(FW)\n"
     "\n"
     "Frequently seen &(HO)\n"
     "\n"
     "Considered extremely dangerous for &(WS).\n"
     "If you see this %s SOUL, do not approach!  Contact your nearest divine administrator immediately!\n",
     {9, 1, 1},
     {(const char *const[]){
          "poor", "wretched", "miserable", "decrepit", "shameful", "despicable", "honorable", "fascinating", "bogus",
      },
      (const char *const[]){""},
      (const char *const[]){""}}},
    {/* 0x668688 */
     "&(CI) - Memorial services were held today for &(NA), considered by many to be one of the top &(NU 3 1000) "
     "&(PR)s in the world.\n"
     "\n"
     "Many &(AJ) luminaries spoke before the crowd of &(NU 100 10000000) including &(NA 1), who recalled &(NA) as "
     "a \"&(VI) EMBO who always &(GD) at least &(NU 3, 20) times a day.  Whether &(SXS) wanted to or not.\"\n"
     "Other speakers, especially &(NA 2), make it clear that they were there to bury &(NA), not praise &(SXO).  "
     "\"Let's face it, \" said the noted &(PR), \"&(NA) could be a real &(BN).  When I think of how &(SXS) nearly "
     "blew all &(SXP) &(CU) on &(ED) it makes me wanna scream \"&(QO)\" to the unfeeling universe.\"\n"
     "\n"
     "&(NA) died &(NU 1 10) weeks ago in an as-yet unexplained accident involving a &(NOS) and a &(NOS 1).",
     {1, 1, 1},
     {(const char *const[]){""},
      (const char *const[]){""},
      (const char *const[]){""}}},
    {/* 0x668750: the third word is read from past the record, which gives none */
     "&(SYFO) a &(PR) by trade, &(NA) joins us in the afterlife for &(BS) and constantly &(FB).\n"
     "\n"
     "Of course you'd never know that by &(SXP) &(WS), but, hey, \"&(QO),\" &(SXS) says.",
     {1, 1, 1},
     {(const char *const[]){""},
      (const char *const[]){""},
      (const char *const[]){""}}},
    {/* 0x668890 */
     "&(SYNN) %s for &(FW), &(NA)'s worst deed was &(WS).\n"
     "\n"
     "\"&(QO)\", &(SXS) says.\n"
     "\n"
     "&(OR (Friends) (Family)) and &(OR (neighbors) (friends)) remember &(NA) for &(FB).  \"I'll never forget "
     "&(SXP) &(BS)\", &(SXP) &(FR) said.",
     {4, 1, 1},
     {(const char *const[]){"the Scourge of &(CI)", "the Plague of &(CI)", "\"El Butthead\"", "Painus Rectus"},
      (const char *const[]){""},
      (const char *const[]){""}}},
    {/* 0x668cf8 */
     "         EXTRA!!\n"
     "\n"
     "One of afterlife's newest SOULS is &(NA).  &(SXS) joins us from &(CI) where &(SXS) worked as a &(PR) for "
     "&(NU 5 30) years.\n"
     "\n"
     "%s recently caught up with &(NA) at a local &(PL) and asked &(SXO) about &(SXP) &(FW).  \"&(QO)\" &(SXS) "
     "said,  \"and those rumors about me &(WS) are just that, rumors.  I swear it on my grave!\"  Later &(SXS) "
     "said, \"umm, please don't quote me that.\"  Sorry &(NA), we've heard that one before.\n"
     "\n"
     "On the warmer side of the news, Hell hit a %s &(NU 140 350) degrees today, &(OR (nearly) ()) %s the previous "
     "record set back on &(DA).",
     {17, 9, 5},
     {(const char *const[]){
          "Afterlife Times", "The Dead Man's Journal", "New &(OR (Angel) (Demon))",
          "The &(OR (Heavenly) (Hellish)) Tribune", "New SOUL", "Stone Cold Magazine", "The Mortem Post",
          "Modern &(OR (Angel) (Demon))", "The Soulview Observer", "New Life Gazette", "The Witching Hour",
          "New &(OR (Heaven) (Hell)) Times", "Channel 666 News", "Sly Devil Digest", "The 13 O'clock News",
          "Postmortem Daily", "The Graveyard Shift",
      },
      (const char *const[]){
          "steamy", "warm", "hot", "blistering", "scorching", "simmering", "bloody hot", "sweltering", "maddening",
      },
      (const char *const[]){"tying", "breaking", "passing", "exceeding", "approaching"}}},
    {/* 0x668f60 */
     "&(NA)\n"
     "&(CI) resident\n"
     "\n"
     "&(NA) &(EM) &(OR (&(HO)) (dining at &(RE))).\n"
     "&(SXS) died &(DA) at a &(PL) at age &(NU 4 120).\n"
     "A lifelong resident of &(CI), &(NA) was a%s &(PR) for &(NU 3 65) years.\n"
     "\n"
     "Long time &(FR)s remember &(SXO) &(BS), but most of all remember &(SXP) &(FB).  Others will never forget, "
     "nor forgive, &(NA)'s &(FW).\n"
     "\n"
     "&(NA) is survived by &(NU 1 5) &(RL)s.  Memorial services will be held tomorrow at &(NU 1 12) PM.",
     {4, 1, 1},
     {(const char *const[]){" professional", "n amateur", "n assistant", "n aspiring"},
      (const char *const[]){""},
      (const char *const[]){""}}},
    {/* 0x669248 */
     "Welcome to %s, the &(OR (semi-) ())%s show where our lucky guest will have a chance to win a weekend stay in "
     "the realm of his or her choice.\n"
     "\n"
     "Our guest this evening comes to us from &(CI) where &(SXS) was a practicing &(PR) for over &(NU 2 40) years. "
     " &(SXS)'s sure to be popular in Heaven for &(FB).  Hell, &(SXS)'d feel just as at home Down Under for &(FW) "
     "and, oh yes, &(WS).\n"
     "\n"
     "Let's give a big &(OR (heavenly) (hellish)) welcome to &(NA)!  So, &(NA), how does it feel being a guest?\n"
     "\n"
     "\"&(QO).\"",
     {12, 4, 1},
     {(const char *const[]){
          "SOULmates", "Demon Rant", "Inner SOUL", "Angel Chat", "Hell Fire", "Divine Inspiration", "Raining Hell",
          "Viewpoint", "Bare Your SOUL", "Heavenly Light", "Fire and Brimstone", "Pitchfork and Halo",
      },
      (const char *const[]){"weekly", "monthly", "yearly", "hourly"},
      (const char *const[]){""}}},
    {/* 0x669378 */
     "Name:   &(NA)\n"
     "Height: &(NU 122 152) cm\n"
     "Weight: &(NU 68 95) kg\n"
     "DOB.:   &(DA)\n"
     "Home:   &(CI)\n"
     "\n"
     "Worst Deeds:\n"
     "&(FW)\n"
     "&(WS)\n"
     "\n"
     "Best Deeds:\n"
     "&(FB)\n"
     "&(BS)\n"
     "\n"
     "&(OR (Favorite pastime) (Hobby)):\n"
     "&(HO)\n"
     "\n"
     "Famous last words:\n"
     "\"&(QO)\"",
     {1, 1, 1},
     {(const char *const[]){""},
      (const char *const[]){""},
      (const char *const[]){""}}},
    {/* 0x6694b8 */
     "%s, plagued by &(FW), seeks balancing %s, troubled by &(WS), for %s and possible &(OR (romance) "
     "(friendship)).\n"
     "\n"
     "Must &(EM) &(HO) and &(ED).\n"
     "\n"
     "&(BS) a definite plus.\n",
     {4, 3, 3},
     {(const char *const[]){"inner child", "free spirit", "cosmic child", "lost soul"},
      (const char *const[]){"outer soul", "soulmate", "cosmic twin"},
      (const char *const[]){"get together", "romp", "acquaintance"}}},
    {/* 0x6697e8 */
     "%s that people with an equal mixture of good and evil in their lives make even more interesting SOULs in the "
     "afterlife.\n"
     "\n"
     "Take &(NA) for instance, &(SXS) typifies the classic %s SOUL, &(FW) on more than &(NU 1 10) occasions, yet "
     "also &(FB).  And, occasionally &(WS), but also &(BS).\n"
     "\n"
     "To make matters even more interesting, &(SXS) loved &(HO) often spending &(NU 1 24) or more hours a day "
     "doing so, sometimes at the expense of ignoring &(SXP) &(FR).",
     {9, 5, 1},
     {(const char *const[]){
          "It's common knowledge and just plain common sense", "The Hegelian dialectic suggests",
          "Anyone with half a brain knows", "Every SOUL knows", "All SOULs know", "All &(OR (demons) (angels)) know",
          "Common sense dictates", "Logic dictates", "Conventional wisdom demands",
      },
      (const char *const[]){"Chondracian", "Skinnerian", "Cosbian", "Floydian", "Snerdian"},
      (const char *const[]){""}}},
    {/* 0x669a88 */
     "&(OR (   Soulview Spotlight\n"
     "\n"
     ") ())%s &(CI), where &(SXS) worked as a &(PR) for the past &(NU 1 35) years, is &(NA), one of the "
     "afterlife's %s SOULs.\n"
     "\n"
     "One day while &(WS), &(NA) discovered &(HO), a passion that would shape the rest of &(SXP) life.  &(SXS) was "
     "&(NU 10 45) years old at the time.  As &(SXP) interest in &(HO) grew, &(NA) stopped &(FW) and began &(FB).  "
     "Abruptly one day, &(SXS) even started &(BS).\n"
     "\n"
     "\"&(QO), \" &(NA) recalls.",
     {5, 7, 1},
     {(const char *const[]){"Hailing from", "A native of", "Originating from", "Coming from", "Born in"},
      (const char *const[]){
          "most veteran", "most bizarre", "newest", "most eccentric", "most recent", "strangest", "oldest",
      },
      (const char *const[]){""}}},
    {/* 0x669cf0 */
     "   Soulview &(OR (Close-up) (Spotlight))\n"
     "\n"
     "&(NA) was born in a%s %s &(OR (near) (outside of)) &(CI) where &(SXS) worked as a practicing &(PR) for &(NU "
     "2 40) years.  After catching a quick &(ME) at &(RE), &(SXP) favorite %s, &(SXS) would enjoy &(HO) at a &(PL) "
     "for countless hours.\n"
     "\n"
     "Often &(FB) and &(FW), &(SXP) most notable deeds were &(BS) and &(WS).",
     {8, 7, 7},
     {(const char *const[]){
          " small", "n obscure", " medium", " remote", " secluded", " large", " popular", " beautiful",
      },
      (const char *const[]){"town", "village", "city", "villa", "hamlet", "chateau", "shack"},
      (const char *const[]){"restaurant", "cabaret", "hang-out", "pad", "grease pit", "bistro", "cafe"}}},
    {/* 0x66a0b8 */
     "To whom it may concern:\n"
     "By the time you read this, my &(AJ), mortal shell will already be dead, crushed beneath the &(NU 2 20) tons "
     "of &(NOP) I've jury-rigged to fall on my head while I was sleeping.\n"
     "\n"
     "\"&(GO)\" you're probably saying, \"What could've possessed a &(AJ), young EMBO like &(NA) to take &(SXP) "
     "life in such a %s manner?\n"
     "\n"
     "Why?  Maybe because I'm tired of being the goody-&(NU 1 7)-shoes who always &(GD) when &(SXS)'s asked, and "
     "never &(ED) even when offered &(NU 20 1000000) &(CU)s.  The truth is, I'm a &(AJ), &(AJ) &(BN), who loves "
     "turning off the lights and &(ED 1) until the &(AN)s come home.  I die content knowing that none of you ever "
     "knew the real &(AJ) me.\n"
     "\n"
     "    Buried beneath &(NU 2 20) tons of &(NOP),\n"
     "        &(NA)",
     {12, 1, 1},
     {(const char *const[]){
          "gruesome", "grisly", "unique", "off-beat", "light-hearted", "ghastly", "mundane", "humorous", "bizarre",
          "gorey", "dull", "eclectic",
      },
      (const char *const[]){""},
      (const char *const[]){""}}},
    {/* 0x66a248 */
     "It was a &(AJ) and &(AJ) night.  Rain fell from the sky like &(AJ) wet &(AN)s.  Suddenly, a shot rang out, "
     "shattering the silence like a &(NOS) flying through a church window.  Somewhere, a &(PR) screamed \"&(GO)!\" "
     " But it was too late; &(NA) was dead, dead as a &(NOS 1).\n"
     "\n"
     "Too bad; the &(BN) owed me &(NU 10 100000) &(CU)s.",
     {1, 1, 1},
     {(const char *const[]){""},
      (const char *const[]){""},
      (const char *const[]){""}}},
    {/* 0x66a4a8 */
     "As &(NA) prepared to take a bite out of &(NA 1)'s &(BP), &(SXS) couldn't help but wonder how &(SXS) arrived "
     "at this &(AJ) state.\n"
     "\n"
     "Maybe it was some sort of cosmic punishment for all the times &(SXS) &(ED) while listening to &(NA 2)'s "
     "records in the &(CO) moonlight wearing nothing but a &(AJ) &(CL).\n"
     "Or maybe it was a vast karmic joke, someone's way of saying \"&(GO), &(NA), all those times you &(GD) just "
     "don't add up to &(NA 3)-squat!\n"
     "\n"
     "Oh well, the die was cast, and it was no use crying over spilt &(DR).  &(NA) grabbed a juicy slab of &(NA "
     "1)'s arm and bit down.  Hard.",
     {1, 1, 1},
     {(const char *const[]){""},
      (const char *const[]){""},
      (const char *const[]){""}}},
    {/* 0x66a6d8 */
     "   Coroner's Report  #&(NU 1000 23000000)\n"
     "\n"
     "Subject:  &(NA)\n"
     "Height:   &(NU 30 200) cm\n"
     "Weight:   &(NU 20 500) kg\n"
     "\n"
     "Distinguishing Marks:  &(CO) tattoo of &(NOS) &(NU 1 20) cm from &(BP).  &(AJ) mole &(NU 1 200) cm from &(BP "
     "1).\n"
     "\n"
     "Organs:  Mostly intact, with exception of &(OG), which was fill with &(AJ) &(NOS 1)-like substance, and the "
     "&(OG 1) which appears to have been punctured by a &(AJ) &(NOS 2).\n"
     "\n"
     "Probable Cause of Death:  Subject either drowned in a pool of &(SXP) own &(BF) or was attacked by a pack of "
     "&(AJ) &(AN)s.",
     {1, 1, 1},
     {(const char *const[]){""},
      (const char *const[]){""},
      (const char *const[]){""}}},
    {/* 0x66a7d8 */
     "&(NA) was not a &(AJ) EMBO.  &(CP)&(SXS) wasn't a &(AJ) EMBO, nor a &(AJ) EMBO, nor a &(AJ) EMBO, nor a "
     "&(AJ) EMBO.  &(NA) wasn't a lot of things, but &(SXS) was a &(WS) son-of-a-&(NOS) and that's why &(SXS)'s in "
     "&(RA).",
     {1, 1, 1},
     {(const char *const[]){""},
      (const char *const[]){""},
      (const char *const[]){""}}},
};
/* clang-format on */

/* ---- the expander -------------------------------------------------------- */

typedef struct Ctx {
    const Rec26 *t;
    uint32_t seed;
    char *end; /* the output buffer's last byte (kept for the NUL) */
} Ctx;

static uint8_t g_off; /* 0x66482c: added to most list picks, set by BS/WS/FB/FW */
static uint8_t g_cap; /* 0x664830: capitalise the next letter */
static uint8_t g_adj; /* 0x66a838: adjectives inserted so far */

static char *put(Ctx *c, char *d, const char *s)
{
    while (*s && d < c->end)
        *d++ = *s++;
    *d = 0;
    return d;
}

/* sprintf(out, fmt, a[0], a[1], a[2]) for the templates' "%s"s */
static void fmt_s(char *out, size_t size, const char *fmt, const char *const *a, int na)
{
    size_t o = 0;
    int k = 0;
    for (const char *p = fmt; *p && o + 1 < size; p++) {
        if (p[0] == '%' && p[1] == 's') {
            const char *s = k < na ? a[k] : "";
            k++;
            p++;
            while (*s && o + 1 < size)
                out[o++] = *s++;
        } else if (p[0] == '%' && p[1] == '%') {
            out[o++] = '%';
            p++;
        } else {
            out[o++] = *p;
        }
    }
    out[o] = 0;
}

/* sprintf(out, g->fmt, word[i % n]); no group gives "" */
static void group_pick(const WordGroup *g, unsigned i, char *out, size_t size)
{
    unsigned n = g ? g->n : 0;
    const char *w = n ? g->words[i % n] : "";
    fmt_s(out, size, g ? g->fmt : "", &w, 1);
}

static char *expand(Ctx *c, const char *src, char *dst);

static char *expand_group(Ctx *c, const WordGroup *g, unsigned i, char *dst)
{
    char tmp[1024];
    group_pick(g, i, tmp, sizeof tmp);
    return expand(c, tmp, dst);
}

/* 0x47cd0f: the zone kind a story is about */
static unsigned story_kind(const Rec26 *t)
{
    if (t->unk_00[0] == 0)
        return 0;
    if (t->unk_0c == 0)
        return bits(t->unk_08, 26, 4) % 7;
    return sim_zone_kind(t->unk_00[2] % GS_MAP_W, t->unk_00[3] % GS_MAP_H);
}

/* 0x47cd8a (BS): a good deed of the story's kind. The original indexes
 * k_good_deeds with any zone kind: its slot 7 is NULL (no deed) and kinds
 * 8-14 run on into k_bad_deeds. */
static char *best_deed(Ctx *c, char *dst)
{
    if (c->t->unk_00[0] == 0)
        return dst;
    unsigned k = story_kind(c->t);
    const WordGroup *g = k < 7 ? &k_good_deeds[k] : k >= 8 && k < 15 ? &k_bad_deeds[k - 8] : NULL;
    return expand_group(c, g, bits(c->seed, 9, 4), dst);
}

/* 0x47ce2d (WS): a bad deed of another kind */
static char *worst_sin(Ctx *c, char *dst)
{
    if (c->t->unk_00[0] == 0)
        return dst;
    unsigned k = (c->t->unk_0c == 0 ? bits(c->seed, 27, 4) : bits(c->seed, 13, 4)) % 7;
    if (story_kind(c->t) == k)
        k = (k + 1) % 7;
    return expand_group(c, &k_bad_deeds[k], bits(c->seed, 7, 4), dst);
}

/* 0x47cf66 (FB) */
static char *good_habit(Ctx *c, char *dst)
{
    if (c->t->unk_00[0] == 0)
        return dst;
    unsigned k = bits(c->seed, 20, 2) % 7;
    return expand_group(c, &k_good_deeds[k], bits(c->seed, 1, 4), dst);
}

/* 0x47d018 (FW) */
static char *bad_habit(Ctx *c, char *dst)
{
    if (c->t->unk_00[0] == 0)
        return dst;
    unsigned a = bits(c->seed, 20, 2) % 7, b = bits(c->seed, 23, 2) % 7;
    if (a == b)
        b = (b + 1) % 7;
    return expand_group(c, &k_bad_deeds[b], bits(c->seed, 4, 4), dst);
}

/* 0x47cc60: a noun: the word, then the singular ending for 'S', else the
 * plural one */
static char *noun(Ctx *c, const Noun *n, char *dst, char form)
{
    dst = put(c, dst, n->word);
    if (form == 'S')
        return put(c, dst, n->singular);
    return put(c, dst, k_plural_endings[n->plural]);
}

/* 0x47cc04: the last character inside the parentheses opened just before p */
static const char *paren_end(const char *p)
{
    int depth = 1;
    const char *q = p;
    for (;;) {
        if (*q == 0)
            return q - 1;
        if (*q == '(')
            depth++;
        else if (*q == ')')
            depth--;
        if (depth == 0)
            return q - 1;
        q++;
    }
}

static const char *after(const char *s, int ch)
{
    const char *p = strchr(s, ch);
    return p ? p + 1 : s + strlen(s);
}

/* a list word: index (bits + offset) mod n */
static unsigned pick(Ctx *c, unsigned shift, unsigned n_bits, unsigned n)
{
    return ((bits(c->seed, shift, n_bits) + g_off) & 0xffff) % n;
}

/* the seed, or for "&(XY k)" the seed rotated by k */
static uint32_t arg_seed(Ctx *c, const char *s) { return *s == ')' ? c->seed : rot(c->seed, (unsigned)atoi(s)); }

#define CODE(a, b) ((uint32_t)(a) << 16 | (uint32_t)(b))

/* 0x47af6f */
static char *expand(Ctx *c, const char *src, char *dst)
{
    for (;;) {
        while (*src != '&') {
            unsigned char ch = (unsigned char)*src;
            if (ch == 0)
                return dst;
            if (dst < c->end)
                *dst++ = g_cap && ch < 0x80 ? (char)toupper(ch) : (char)ch;
            *dst = 0;
            if (ch == '.' || ch == '?' || ch == '!')
                g_cap = 1;
            else if (ch < 0x80 && isalnum(ch))
                g_cap = 0;
            src++;
        }
        const char *op = strchr(src, '(');
        if (!op || !op[1] || !op[2])
            return dst;
        uint32_t code = CODE((unsigned char)op[1], (unsigned char)op[2]);
        src = op + 3;
        char *start = dst;
        char tmp[2048];
        switch (code) {
        case CODE('A', 'J'): { /* adjective; each one rotates the seed further */
            uint32_t v = rot(c->seed, g_adj);
            dst = put(c, dst, k_adjectives[bits(v, 24, 5) % 21]);
            g_adj++;
            break;
        }
        case CODE('A', 'C'):
            dst = put(c, dst, k_activities[pick(c, 6, 6, 29)]);
            break;
        case CODE('B', 'F'):
            dst = put(c, dst, k_body_fluids[bits(arg_seed(c, src), 13, 3) & 7]);
            break;
        case CODE('A', 'N'):
            dst = put(c, dst, k_animals[bits(c->seed, 18, 4) % 14]);
            break;
        case CODE('C', 'H'):
            dst = put(c, dst, k_hangouts[pick(c, 21, 5, 12)]);
            break;
        case CODE('B', 'N'):
            dst = put(c, dst, k_insults[bits(c->seed, 27, 4) % 9]);
            break;
        case CODE('B', 'P'):
            dst = put(c, dst, k_body_parts[bits(arg_seed(c, src), 5, 6) % 41]);
            break;
        case CODE('B', 'S'):
            g_off = 0;
            dst = best_deed(c, dst);
            break;
        case CODE('C', 'L'):
            dst = put(c, dst, k_clothes[bits(arg_seed(c, src), 24, 5) % 28]);
            break;
        case CODE('C', 'I'):
            dst = put(c, dst, k_cities[pick(c, 21, 5, 11)]);
            break;
        case CODE('D', 'I'):
            dst = put(c, dst, k_diseases[pick(c, 28, 7, 10)]);
            break;
        case CODE('D', 'A'): { /* a date */
            static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            unsigned nb = 0;
            uint32_t y = g_state.year;
            uint32_t ay = (int32_t)y < 0 ? (uint32_t)-(int32_t)y : y;
            for (nb = 32; nb > 0 && !(ay & (1u << (nb - 1))); nb--)
                ;
            unsigned m = bits(c->seed, 14, 4) % 11 + 1;
            /* (the days of the month after m, as in the original) */
            unsigned d = bits(c->seed, 18, 5) % (unsigned)(days[m] - 1) + 1;
            uint32_t yy = bits(c->seed, 16, nb);
            yy = mul_div_clamp(yy, (1u << (nb & 31)) - 1u, y);
            snprintf(tmp, sizeof tmp, "%d-%d-%d" /* 0x66a9d8 */, (int)m, (int)d, (int)yy);
            dst = put(c, dst, tmp);
            break;
        }
        case CODE('E', 'D'):
            dst = expand_group(c, &k_bad_deeds[bits(c->seed, 16, 3) % 7], bits(c->seed, 2, 4), dst);
            break;
        case CODE('D', 'R'):
            dst = put(c, dst, k_drinks[pick(c, 12, 4, 12)]);
            break;
        case CODE('F', 'B'):
            g_off = 2;
            dst = good_habit(c, dst);
            break;
        case CODE('E', 'M'):
            dst = put(c, dst, k_emotions[pick(c, 29, 5, 10)]);
            break;
        case CODE('F', 'R'):
            snprintf(tmp, sizeof tmp, "%s", k_acquaintances[pick(c, 26, 5, 5)]);
            dst = expand(c, tmp, dst);
            break;
        case CODE('F', 'O'): {
            unsigned i = pick(c, 7, 6, 37);
            char form = *src;
            if (*src)
                src++;
            dst = noun(c, &k_foods[i], dst, form);
            break;
        }
        case CODE('G', 'D'):
            dst = expand_group(c, &k_good_deeds[bits(c->seed, 22, 3) % 7], bits(c->seed, 4, 4), dst);
            break;
        case CODE('F', 'W'):
            g_off = 3;
            dst = bad_habit(c, dst);
            break;
        case CODE('H', 'O'):
            dst = expand_group(c, &k_hobbies, bits(c->seed, 15, 5), dst);
            break;
        case CODE('G', 'O'):
            snprintf(tmp, sizeof tmp, "%s", k_exclamations[(bits(c->seed, 13, 4) + g_off) & 7]);
            dst = expand(c, tmp, dst);
            break;
        case CODE('L', 'N'):
            dst = put(c, dst, k_last_names[pick(c, 22, 6, 31)]);
            break;
        case CODE('I', 'N'):
            dst = put(c, dst, k_insects[(bits(c->seed, 29, 7) + g_off) & 7]);
            break;
        case CODE('N', 'A'): { /* a name: this SOUL's, or another one's */
            uint32_t v = c->seed;
            if (*src != ')') {
                src = op + 4;
                v = rot(v, (unsigned)atoi(src));
            }
            char name[SOULVIEW_NAME_LEN];
            soulview_soul_name(v, name);
            dst = put(c, dst, name);
            break;
        }
        case CODE('M', 'E'):
            dst = put(c, dst, k_meals[pick(c, 5, 3, 6)]);
            break;
        case CODE('N', 'U'): { /* a number lo..hi */
            src = op + 4;
            int lo = atoi(src);
            src = after(src, ' ');
            int hi = atoi(src);
            uint32_t r = mul_div_clamp(bits(c->seed, 0, 10), 0x3ff, (uint32_t)(hi - lo));
            snprintf(tmp, sizeof tmp, "%d", (int)(r + (uint32_t)lo));
            dst = put(c, dst, tmp);
            break;
        }
        case CODE('N', 'O'): {
            unsigned i;
            if (op[3] && op[4] == ')')
                i = pick(c, 10, 6, 35);
            else
                i = rot(c->seed, (unsigned)atoi(op[3] ? op + 5 : op + 3)) % 35;
            char form = *src;
            if (*src)
                src++;
            dst = noun(c, &k_possessions[i], dst, form);
            break;
        }
        case CODE('O', 'R'): { /* one of two parenthesised alternatives */
            const char *a = after(src, '(');
            const char *ae = paren_end(a);
            if (bits(c->seed, 1, 1) == 0) {
                src = paren_end(after(ae, '('));
            } else {
                a = after(ae, '(');
                ae = paren_end(a);
                src = ae;
            }
            if (*src == ')')
                src++;
            src = after(src, ')');
            size_t len = ae >= a ? (size_t)(ae - a + 1) : 0;
            if (len >= sizeof tmp)
                len = sizeof tmp - 1;
            memcpy(tmp, a, len);
            tmp[len] = 0;
            dst = expand(c, tmp, dst);
            break;
        }
        case CODE('O', 'G'):
            dst = put(c, dst, k_organs[bits(arg_seed(c, src), 22, 5) % 19]);
            break;
        case CODE('W', 'S'):
            g_off = 1;
            dst = worst_sin(c, dst);
            break;
        case CODE('Z', 'O'):
            dst = put(c, dst, k_zodiac[pick(c, 2, 4, 12)]);
            break;
        case CODE('V', 'I'):
            dst = put(c, dst, k_virtues[bits(c->seed, 12, 3) % 7]);
            break;
        case CODE('S', 'Y'): { /* a synonym: &(SYxx) */
            uint32_t sub = CODE((unsigned char)op[3], op[3] ? (unsigned char)op[4] : 0);
            const char *const *list = NULL;
            unsigned i = 0;
            if (sub == CODE('G', 'O'))
                list = k_syn_gorging, i = pick(c, 6, 4, 3);
            else if (sub == CODE('F', 'O'))
                list = k_syn_formerly, i = pick(c, 6, 4, 3);
            else if (sub == CODE('H', 'A'))
                list = k_syn_hated, i = pick(c, 6, 4, 3);
            else if (sub == CODE('N', 'N'))
                list = k_syn_nicknamed, i = (bits(c->seed, 6, 4) + g_off) & 3;
            else if (sub == CODE('S', 'T'))
                list = k_syn_stealing, i = pick(c, 6, 4, 7);
            if (list)
                dst = put(c, dst, list[i]);
            break;
        }
        case CODE('S', 'X'): { /* he / him / his or she / her / her */
            char form = *src;
            if (*src)
                src++;
            const char *word;
            if (bits(c->seed, 16, 1) == 0)
                word = form == 'N' || form == 'P' ? "his" /* 0x66a9f4 */
                       : form == 'O'              ? "him" /* 0x66a9f8 */
                                                  : "he" /* 0x66a9fc */;
            else
                word = form == 'N'                  ? "hers" /* 0x66a9e8 */
                       : form == 'O' || form == 'P' ? "her"  /* 0x66a9e4 */
                                                    : "she" /* 0x66a9f0 */;
            dst = put(c, dst, word);
            break;
        }
        case CODE('S', 'H'):
            dst = put(c, dst, k_superheroes[pick(c, 27, 4, 20)]);
            break;
        case CODE('R', 'L'):
            dst = put(c, dst, k_relatives[pick(c, 27, 5, 11)]);
            break;
        case CODE('R', 'E'):
            dst = put(c, dst, k_restaurants[(bits(c->seed, 25, 5) + g_off) & 15]);
            break;
        case CODE('R', 'A'):
            dst = put(
                c, dst,
                (c->t->unk_10 == GS_PLANE_HELL ? "Hell" /* 0x66aa98 */ : "Heaven" /* 0x66aaa0 */)); /* Hell / Heaven */
            break;
        case CODE('Q', 'O'):
            dst = expand_group(c, &k_quotes, bits(c->seed, 15, 4), dst);
            break;
        case CODE('P', 'R'):
            dst = put(c, dst, k_professions[((bits(arg_seed(c, src), 8, 7) + g_off) & 0xffff) % 55]);
            break;
        case CODE('P', 'L'):
            dst = put(c, dst, k_places[pick(c, 11, 6, 26)]);
            break;
        case CODE('C', 'U'):
            dst = put(c, dst, k_coins[pick(c, 3, 4, 15)]);
            break;
        case CODE('C', 'O'):
            dst = put(c, dst, k_colors[bits(arg_seed(c, src), 1, 5) % 18]);
            break;
        case CODE('C', 'P'):
            g_cap = 1;
            src = after(src, ')');
            continue;
        default:
            break;
        }
        if (g_cap && dst > start && (unsigned char)*start < 0x80)
            *start = (char)toupper((unsigned char)*start);
        src = after(src, ')');
        g_cap = 0;
    }
}

/* 0x47ad32: the story template: fmt with a word from each of its three
 * lists */
static void story_template(const StoryTemplate *t, uint32_t seed, char *out, size_t size)
{
    const char *w[3] = {"", "", ""};
    if (t->n[0])
        w[0] = t->words[0][bits(seed, 10, 4) % t->n[0]];
    if (t->n[1])
        w[1] = t->words[1][bits(seed, 11, 4) % t->n[1]];
    if (t->n[2])
        w[2] = t->words[2][bits(seed, 12, 4) % t->n[2]];
    fmt_s(out, size, t->fmt, w, 3);
}

/* 0x47adea, the text */
size_t soulview_describe(const Rec26 *t, char *out, size_t size)
{
    if (size == 0)
        return 0;
    out[0] = 0;
    if (t->unk_00[0] == 0 || size < 2)
        return 0;
    char tmpl[2048];
    story_template(&k_stories[bits(t->unk_08, 0, 5) % 18], t->unk_08, tmpl, sizeof tmpl);
    Ctx c = {t, t->unk_08, out + size - 1};
    g_off = 0;
    g_cap = 1;
    g_adj = 0;
    char *end = expand(&c, tmpl, out);
    *end = 0;
    return (size_t)(end - out);
}
