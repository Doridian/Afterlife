/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The advisors' speeches (ALIFE.EXE 0x419b65, 0x41b768, 0x41db2b and the
 * quips of 0x4196ae) and the issue labels of 0x41801f.
 *
 * Each of the three original functions is a switch on the issue code
 * whose cases are a fixed sequence of lines, each
 *
 *     if (!silenced && stamp unchanged)       (the first line: !silenced)
 *         snd_talk_start(talker, &text_rect[talker], text, voice, 0x13);
 *         akos_talk_wait(talker);
 *
 * so here they are tables of {talker, text, voice}, with the subtitles'
 * original addresses in comments.
 */
#include <stddef.h>

#include "gfx/akos_talk.h"
#include "ui/advisors_int.h"

typedef struct AdvLine {
    uint8_t talker;   /* 1 Jasper, 2 Aria */
    const char *text; /* the subtitle */
    uint32_t voice;   /* voice resource (sound slot 199) */
} AdvLine;

#define ADV_DEFAULT 0xff /* the switch's default case */

typedef struct AdvCase {
    uint8_t issue;
    uint8_t first, count; /* lines */
} AdvCase;

/* 0x419b65: one issue in Heaven only */
static const AdvLine k_heaven_lines[] = {
    {2, /* 0x654750 */
     "I don't mean to be a noodge, but your Heaven really won't start growing until you build a Gate.",
     0x10125},
    {1, "Of course, you don't HAVE to build a Heaven..." /* 0x6547b0 */, 0x10dda},
    {1, "Now now, mon petite cherub, no one's being ripped off here." /* 0x656494 */, 0x10d9c},
    {1, /* 0x6564d0 */
     "My demonic brethren are simply demanding a fair market value for their <ahem> 'specialized' services.",
     0x10d9d},
    {1, /* 0x654678 */
     "SOULs are leaving your Afterlife because they can't find their expected Fate Structures.",
     0x10ecc},
    {2, /* 0x6546d4 */
     "Jasper's right, Boss.  You'd better look at the Graphview, find out what kinds of Fate Structures you need,",
     0x1021f},
    {2, "And get zoning!" /* 0x654740 */, 0x10220},
    {2, "Heaven's Gates are clogged worse'n Jasper's arteries!" /* 0x6547e0 */, 0x10126},
    {1, "How droll.  The message is accurate, however.  Heaven could use some new Gates." /* 0x654818 */, 0x10ddb},
    {2, "Hey Honcho, stop screwing around, and zone for new Reward Structures!" /* 0x656324 */, 0x1019c},
    {2, "Heaven's losing SOULs faster than...well something really fast!" /* 0x65636c */, 0x1019d},
    {2, "If you don't put a Karma Station in Heaven, SOON, you'll be losing SOULs --" /* 0x654868 */, 0x10127},
    {1, "--and Pennies--" /* 0x6548b4 */, 0x10ddc},
    {2, "faster than you can say 'transubstantiation.'" /* 0x6548c4 */, 0x10128},
    {2, /* 0x6548f4 */
     "Somewhere out in Heaven there's a sad little Karma Station that isn't hooked up to a Karma Portal.",
     0x10129},
    {2, "So put down some Karma Track and hook it up, fer cryin' out loud!" /* 0x654958 */, 0x1012a},
    {2, "I think now would be a fantabulous time to add another Karma Station to Heaven." /* 0x65499c */, 0x1012b},
    {1, /* 0x6549ec */
     "Alternatively, you COULD simply upgrade one of your existing Stations...if you've got the money.",
     0x10ddd},
    {2, "We've taken one of those census-thingies," /* 0x6560b0 */, 0x10185},
    {2, "and decided that you've got waaay more Reward Structures than you need," /* 0x6560dc */, 0x10186},
    {2, "and that it's costing you a whole lotta Pennies." /* 0x656124 */, 0x10187},
    {1, "Does this mean that the Demiurge should, ooh, blow up some of Heaven?" /* 0x656158 */, 0x10e24},
    {2, "Well, yeah, if he's got half a brain." /* 0x6561a0 */, 0x10188},
    {2, "Boss, you know I wuv you, right?  Right.  So, I'm gonna say this as nicely as I can:" /* 0x654a50 */, 0x1012c},
    {2, "If you don't build a Utopia for me and my fellow Angels, we're gonna bleed you dry!" /* 0x654aa8 */, 0x1012d},
    {1, "Brah-vo, dear.  Fight the Power, etc., etc." /* 0x654afc */, 0x10dde},
    {2, "It might be a good idea to build a Utopia for us Angels, Boss." /* 0x654b28 */, 0x1012e},
    {2, "It'll save ya a pretty penny in the future." /* 0x654b68 */, 0x1012f},
    {1, "I'd listen to her, your Dominance; a disgruntled Angel is a VERY ugly thing." /* 0x654b94 */, 0x10ddf},
    {2, /* 0x654be4 */
     "Okay, okay, okay, here's the deal.  We Angels just luuuuv our Utopia Cities, but they're getting too crowded.",
     0x10130},
    {2, /* 0x654c54 */
     "Could you, like, maybe build us a new one, or upgrade an old one?  Whadda you think, Jasper?",
     0x10131},
    {1, "I think you've gravely mistaken me for someone who gives two hoots in Hell.  Hoot." /* 0x654cb4 */, 0x10de0},
    {1, "See?  I can barely get out one hoot." /* 0x654d08 */, 0x10de1},
    {1, /* 0x654d30 */
     "I hate to gossip, oh Omniscient One, but I believe your Angels are taking advantage of your generous nature.",
     0x10de2},
    {2, /* 0x654da0 */
     "Wellll, that's not exactly true (after all, we Angels don't have a greedy bone in our bodies),",
     0x10132},
    {2, /* 0x654e00 */
     "but it IS true that all of our Angels are imported, which is costing you a LOT of Pennies.",
     0x10133},
    {2, /* 0x654e5c */
     "Maybe you should build an Angel Training Center, which'll let us train our own, cheaper, Angels.",
     0x10134},
    {1, "'Less expensive,' you mean, not 'cheaper.'" /* 0x654ec0 */, 0x10de3},
    {2, "Whatever." /* 0x654eec */, 0x10135},
    {2, "Well, the bad news is:  Heaven is losing SOULs.  The good news is:" /* 0x6563ac */, 0x1019e},
    {2, "You've got enough Reward Structures, but your Roads aren't connected well enough." /* 0x6563f0 */, 0x1019f},
    {2, "Remember, the ONLY way a SOUL can walk from one place to another is on a Road." /* 0x656444 */, 0x101a0},
    {2, "Hoo-gosh, Boss, all of Heaven's Angel Training Centers are full!" /* 0x654ef8 */, 0x10136},
    {2, /* 0x654f3c */
     "Maybe you should build a new one, or upgrade an old one, or sing tra-la-la-la-la-la-la until you're happy.",
     0x10137},
    {2, /* 0x654fa8 */
     "Umm, this is kind of embarrassing, Boss.  Well, we've trained so many Angels that, well, we've got more than "
     "Heaven needs!",
     0x10138},
    {2, "You'd better shut off the Angel Training Centers, P.D.Q.!" /* 0x655024 */, 0x10139},
    {1, /* 0x655060 */
     "If I might be so bold as to chime in, sir, the thought of thousands of Angels sitting around, twiddling their "
     "pinfeathers",
     0x10de4},
    {1, /* 0x6550dc */
     "doesn't exactly fill me with girlish delight.  You know what they say about idle hands...",
     0x10de5},
    {2, /* 0x655138 */
     "Heaven's pretty little boats can't keep up with all the pretty little SOULs that wanna cross the pretty little "
     "rivers.",
     0x1013a},
    {2, "Why don't you build them a new pretty little port?" /* 0x6551b0 */, 0x1013b},
    {1, "If I wasn't already dead, I'd get a pretty little gun and shoot myself." /* 0x6551e4 */, 0x10de6},
    {2, "Nerftoggles, Boss, Heaven's SOUL losses are getting a little bit out of control." /* 0x656280 */, 0x1019a},
    {2, "Maybe you'd better take a few minutes to zone for some new Reward Structures." /* 0x6562d4 */, 0x1019b},
    {2, "Okay, the good news is: Heaven's Limbos are working just fine!" /* 0x65522c */, 0x1013c},
    {1, "The bad news is: You're mismanaging Heaven so badly that you're STILL losing SOULs." /* 0x65526c */, 0x10de7},
    {2, /* 0x6552c0 */
     "Soooo, the way I see it, you can either build more Limbos, or work on your management skills.",
     0x1013d},
    {2, /* 0x6553b0 */
     "You know somethin'?  You're doing a great job!  Except for this one itsy-bitsy, teeny-weeny thing.",
     0x10140},
    {2, "All of Heaven's Rewards are unempowered!  It's so sad..." /* 0x655414 */, 0x10141},
    {1, "Tragic, really." /* 0x655450 */, 0x10de8},
    {2, /* 0x655460 */
     "If you want Heaven to be really snazzy, you'll juice it up with an Ad Infinitum Siphon or two!",
     0x10142},
    {2, /* 0x6554c0 */
     "My friends at the Celestial Empowerment Division tell me that at least one of Heaven's Ad Infinitum Siphons",
     0x10143},
    {2, /* 0x65552c */
     "isn't touching ANY structures, roads, or rivers, wasting its empowering charge.  Bummer.",
     0x10144},
    {2, "It looks like Heaven could use some more Ad Infinitum Siphons, Boss-o-mine." /* 0x655588 */, 0x10145},
    {1, "Or you could just let the whole insipid realm rot into mediocrity." /* 0x6555d4 */, 0x10de9},
    {2, /* 0x6561c8 */
     "Well, fiddlesticks, Heaven's losing a few SOULs.  Oh, it's nothing to worry about NOW, ",
     0x10198},
    {2, /* 0x656220 */
     "but you might wanna find out what kinda Rewards don't have enough Structures, and zone for it.",
     0x10199},
    {2, /* 0x655320 */
     "I don't wanna be a worry wart, but maybe you should invest some of your money in a Penny Bank.",
     0x1013e},
    {2, "You never know when you're gonna need a loan." /* 0x655380 */, 0x1013f},
    {2, "Heaven's roads are a tangled mess, Boss!" /* 0x655618 */, 0x10148},
    {2, /* 0x655644 */
     "We'll never be running in tip-top shape until you fix it so that SOULs don't have to walk so much!",
     0x10149},
    {2, /* 0x6556a8 */
     "You know, some of Heaven's Blessed SOULs have complained about how long they had to walk to get to their final "
     "reward.",
     0x1014a},
    {1, "My heart positively bleeds for them." /* 0x655720 */, 0x10dec},
    {2, "Whoa, whole bunches of Heaven's Rewards are just plain whacked out of alignment." /* 0x6558c0 */, 0x1014e},
    {2, /* 0x655914 */
     "If you ever wanna see these puppies evolve any further, you'd better find 'em all and use the Macromanager on "
     "'em.",
     0x1014f},
    {1, /* 0x655988 */
     "Or you could blow them up and try again.  I find a little gratuitous destruction does wonders for the spirits.",
     0x10def},
    {2, "A few of Heaven's Rewards are way out of alignment, workforce-management wise." /* 0x655748 */, 0x1014b},
    {2, "If you can find the time, you might wanna hunt them down and balance 'em." /* 0x655798 */, 0x1014c},
    {1, "Personally, I'd just ignore the situation and hope that it goes away." /* 0x6557e4 */, 0x10ded},
    {2, /* 0x65582c */
     "But that trick never works, Wormsy...after all, I'm still here, and you ignore me all the time.",
     0x1014d},
    {1, "As usual, my dear, your logic is...impenetrable." /* 0x65588c */, 0x10dee},
    {2, /* 0x6559f8 */
     "Some of Heaven's Rewards have fallen under the dark influence of some spooky bad vibes.",
     0x10150},
    {2, "To, umm, what's the word..." /* 0x655a50 */, 0x10151},
    {1, "'Counteract?'" /* 0x655a6c */, 0x10df0},
    {2, /* 0x655a7c */
     "Yeah!  To counteract the bad vibes, you might wanna put down a Utopia or Gift next to those gloomy Gusses.",
     0x10152},
    {2, /* 0x655ae8 */
     "There's a cheap way to get Heaven's Rewards to evolve, y'know.  It's spelled D-I-V-E-R-S-I-T-Y.",
     0x10153},
    {2, /* 0x655b48 */
     "Diversity.  The Blessed SOULs of Heaven just loooove mingling with different kinds of SOULs.",
     0x10154},
    {1, /* 0x655f40 */
     "For some unfathomable reason, the EMBOs on the Planet just don't believe in an Afterlife as much as they "
     "should.",
     0x10df1},
    {2, "And that's a very bad thing." /* 0x655fb4 */, 0x10155},
    {1, /* 0x655fd4 */
     "Halobrain is right.  Without belief, your Afterlife is nothing, and without your Afterlife...we DON'T GET PAID!",
     0x10df2},
    {2, /* 0x656044 */
     "Hey, that's right!  You'd better get those folks believing in an Afterlife, Boss, or we're ALL in trouble!",
     0x10156},
    {2, /* 0x655ba8 */
     "I've had a few Angels run some numbers, El Capitan, and we figure that if Heaven keeps growing like it is,",
     0x10157},
    {2, "well, we'll soon be buried in SOULs." /* 0x655c14 */, 0x10158},
    {1, "Now there's an interesting turn of phrase." /* 0x655c3c */, 0x10df3},
    {2, /* 0x655c68 */
     "If you've got any influence over the Planet, Boss, I'd try to get those cute little EMBOs to believe in "
     "reincarnation!",
     0x10159},
    {2, /* 0x655ce0 */
     "Heaven's population is growing pretty darn slowly, cuz so many EMBOs believe in reincarnation.",
     0x1015a},
    {2, "Well, this isn't a bad thing, but I just thought you should know." /* 0x655d40 */, 0x1015b},
    {2, "Have you looked at the Planet lately?  It's a scary, evil place." /* 0x655d84 */, 0x1015c},
    {1, /* 0x655dc8 */
     "Needless to say, I ADORE it.  Still, I would be remiss in my duties if I failed to warn you",
     0x10df4},
    {1, "that an overly evil Planet can have dangerous consequences." /* 0x655e24 */, 0x10df5},
    {2, "This is just weird, Boss.  The EMBOs on the Planet believe in an Afterlife," /* 0x655e60 */, 0x1018e},
    {2, "but most of 'em DON'T believe in Heaven." /* 0x655eac */, 0x1018f},
    {2, "I've never seen anything like it, and that CAN'T be good." /* 0x655ed8 */, 0x10190},
    {2, "If I were you, I'd try to fix the problem." /* 0x655f14 */, 0x10191},
};
static const AdvCase k_heaven_cases[] = {
    {20, 0, 2}, {25, 4, 3}, {30, 7, 2}, {35, 9, 2}, {40, 11, 3},
    {45, 14, 2}, {50, 16, 2}, {55, 18, 5}, {60, 23, 3}, {65, 26, 3},
    {70, 29, 4}, {75, 33, 6}, {80, 39, 3}, {85, 42, 2}, {90, 44, 4},
    {95, 48, 3}, {100, 51, 2}, {105, 53, 3}, {110, 56, 4}, {115, 60, 2},
    {120, 62, 2}, {125, 64, 2}, {130, 66, 2}, {145, 68, 2}, {155, 70, 2},
    {160, 72, 3}, {165, 75, 5}, {171, 80, 4}, {175, 84, 2}, {185, 86, 4},
    {190, 90, 4}, {195, 94, 2}, {200, 96, 3}, {210, 99, 4}, {ADV_DEFAULT, 2, 2},
};

/* 0x41b768: one issue in Hell only */
static const AdvLine k_hell_lines[] = {
    {1, "Until you construct a Gate, Hell is never going to amount to anything." /* 0x656610 */, 0x10d89},
    {1, "Waiting in line to get in.  In other words, build some more Gates." /* 0x658770 */, 0x10dd9},
    {1, /* 0x656538 */
     "SOULs are leaving your Afterlife because they can't find their expected Fate Structures.",
     0x10ecc},
    {2, /* 0x656594 */
     "Jasper's right, Boss.  You'd better look at the Graphview, find out what kinds of Fate Structures you need,",
     0x1021f},
    {2, "And get zoning!" /* 0x656600 */, 0x10220},
    {1, "There's an old joke in the Afterlife that goes something like this:" /* 0x656658 */, 0x10d8a},
    {1, "What's worse than being sent to Hell?" /* 0x65669c */, 0x10d8b},
    {2, "I don't know, what?" /* 0x6566c4 */, 0x10109},
    {1, "Waiting in line to get in.  In other words, build some more Gates." /* 0x6566d8 */, 0x10dd9},
    {1, "Frankly, Your Scrofulousness, Hell's SOUL losses are completely intolerable." /* 0x658410 */, 0x10e40},
    {1, "If you don't start taking the appropriate corrective steps immediately" /* 0x658460 */, 0x10e41},
    {1, "(looking at the Lost SOUL Spikes, adding the necessary zones, etc.)," /* 0x6584a8 */, 0x10e42},
    {1, "I fear for your economic survival." /* 0x6584f0 */, 0x10e43},
    {1, "Hell is losing SOULs (and, more importantly, Pennies)," /* 0x656938 */, 0x10d91},
    {1, /* 0x656970 */
     "because you haven't built a Karma Station for the lucky stiffs who are going to be reincarnated.",
     0x10d92},
    {2, "Hmmm.  If you ask me, that's not such a bad thing..." /* 0x6569d4 */, 0x1010c},
    {2, "The fewer bad SOULs that get reincarnated, the nicer the Planet'll be!" /* 0x656a0c */, 0x1010d},
    {1, "Well, no one asked YOU, did they?" /* 0x656a54 */, 0x10d93},
    {1, "Our Infernal Engineers inform me that Hell's losing a number of SOULs," /* 0x656a78 */, 0x10d94},
    {1, "simply because one or more of its Karma Stations isn't hooked up to a Karma Portal." /* 0x656ac0 */, 0x10d95},
    {2, "Well, that's just plain silly!" /* 0x656b14 */, 0x1010e},
    {2, "All the Boss needs to do is lay some Karma Track from the Station to the Portal." /* 0x656b34 */, 0x1010f},
    {1, "You're right; it IS silly." /* 0x656b88 */, 0x10d96},
    {1, /* 0x656ba4 */
     "Hell's Karma Stations are models of demonic efficiency, Your Munificence, with one minute exception:",
     0x10d97},
    {1, "There aren't enough of them." /* 0x656c0c */, 0x10d98},
    {1, "For some unknowable reason, you've overbuilt Hell to a ludicrously high degree." /* 0x6580a0 */, 0x10e25},
    {2, "I like to think of it as 'painting with a broad stroke.'" /* 0x6580f0 */, 0x10189},
    {1, /* 0x65812c */
     "Well, unless His Infallibleness can afford to pay for the extra labor that comes with his 'broad strokes,'",
     0x10e26},
    {1, /* 0x658198 */
     "I'd suggest that he spike down with a furious vengeance about half of his Punishments, A.S.A.P.",
     0x10e27},
    {1, /* 0x65683c */
     "I'm a patient Demon, Your Deminipotence, but the logic behind your behavior confounds me.",
     0x10d8e},
    {1, "Hell is losing Pennies faster than a voyeur in a peep show," /* 0x656898 */, 0x10d8f},
    {1, /* 0x6568d4 */
     "mainly due to the fact that you STILL haven't built a Distopia for me and my demonic colleagues.",
     0x10d90},
    {1, "I've just been going over some otherwise dry budget analysis, Your Superfluousness," /* 0x65671c */, 0x10d8c},
    {1, "and I was SHOCKED to discover that Hell doesn't have a single Distopia yet." /* 0x656770 */, 0x10d8d},
    {2, "Oooh, you really should build one, Boss." /* 0x6567bc */, 0x1010a},
    {2, "When Demons have to commute, they really start holding out for the big salaries." /* 0x6567e8 */, 0x1010b},
    {1, "I'm afraid the time has come to build new Distopias, or upgrade old ones." /* 0x656c2c */, 0x10d9a},
    {1, "Oh well, C'est la vie." /* 0x656c78 */, 0x10d9b},
    {2, "Hmmm.  Shouldn't that be 'Ce la aprevie?'" /* 0x656c90 */, 0x10110},
    {2, "Psst...don't look now, Boss, but the Demons are ripping you off." /* 0x656cbc */, 0x10111},
    {1, "Now now, mon petite cherub, no one's being ripped off here." /* 0x656d00 */, 0x10d9c},
    {1, /* 0x656d3c */
     "My demonic brethren are simply demanding a fair market value for their <ahem> 'specialized' services.",
     0x10d9d},
    {2, "Like tormenting and torturing?" /* 0x656da4 */, 0x10112},
    {1, "Precisely.  Since the Demiurge hasn't seen fit to build a demonic Training Center," /* 0x656dc4 */, 0x10d9e},
    {1, "it's obvious that he's decided to stick with imported, expensive Demons." /* 0x656e18 */, 0x10d9f},
    {2, "But that's stupid!" /* 0x656e64 */, 0x10113},
    {1, "From your lips to the Demiurge's ears, my dear." /* 0x656e78 */, 0x10da0},
    {1, "I'm afraid that Hell is suffering from an all-too-common problem." /* 0x658514 */, 0x10e44},
    {1, /* 0x658558 */
     "You've built plenty of Punishments, but your roads just don't connect them to one another.",
     0x10e45},
    {1, "Try to keep this in mind: Roads are the ONLY thing that SOULs can walk on." /* 0x6585b4 */, 0x10e46},
    {1, /* 0x656ea8 */
     "If you wish to continue trimming your payroll budget, I'd suggest building a new demonic Training Center.",
     0x10da1},
    {1, "(or upgrading an old one)." /* 0x656f14 */, 0x10da2},
    {1, /* 0x656f30 */
     "If, on the other hand, you wish to continue throwing caution to the wind, feel free to ignore me.",
     0x10da3},
    {1, "I think you'll be amused by this situation, Your Rapaciousness." /* 0x656f94 */, 0x10da4},
    {1, "Apparently, we've trained many more Demons than we actually need." /* 0x656fd4 */, 0x10da5},
    {1, /* 0x657018 */
     "Even now, these excess, out-of-work Demons with idle hands are girding their ennui-laden loins for battle with "
     "the forces of Heaven.",
     0x10da6},
    {1, "Couldn't you just die?  Or die again, in any case..." /* 0x6570a0 */, 0x10da7},
    {2, "Well, I don't think it's funny, I think it's dangerous!" /* 0x6570d8 */, 0x10114},
    {2, "You'd better turn off Hell's Training Centers, NOW!!!" /* 0x657110 */, 0x10115},
    {1, "<sigh> Unfortunately, the annoying little twit is right." /* 0x657148 */, 0x10da8},
    {1, "Better to shut the Training Centers down now, than to run the risk of a Ragnarok." /* 0x657184 */, 0x10da9},
    {1, /* 0x6571d8 */
     "The ferrymen who shuttle Damned SOULs across the fetid, flaming rivers of Hell are renowned for their stamina.",
     0x10daa},
    {1, "However, the current--" /* 0x657248 */, 0x10dab},
    {2, "Blah blah blah.  What he's trying to say is:" /* 0x657260 */, 0x10116},
    {2, "Hell's Ports are overflowing, so you need to build some more." /* 0x657290 */, 0x10117},
    {1, "I would have gotten to it eventually, you know." /* 0x6572d0 */, 0x10dac},
    {1, /* 0x65830c */
     "Hell's SOUL losses are currently somewhat of a burden, financially and aesthetically.",
     0x10e3d},
    {1, /* 0x658364 */
     "An accomplished Demiurge would probably drop everything, take a good look at the Lost SOULs Spikes",
     0x10e3e},
    {1, "(to find out which Punishments were overflowing), and zone accordingly." /* 0x6583c8 */, 0x10e3f},
    {1, "Personally, I believe that Limbos are a crutch for inadequate Demiurges." /* 0x657300 */, 0x10dad},
    {1, /* 0x65734c */
     "If, however, you're going to insist on using them, I'd suggest building more of them.",
     0x10dae},
    {1, "The one's you've built in Hell are overflowing, amplifying your incompetence." /* 0x6573a4 */, 0x10daf},
    {2, "That wasn't very nice." /* 0x6573f4 */, 0x10118},
    {1, "Oh, bite me." /* 0x65740c */, 0x10db0},
    {1, "Hell is looking pretty awful, My Liege, but it's not looking as awful as it could." /* 0x657500 */, 0x10db3},
    {1, "Our various Punishment Structures have been stymied in their efforts to evolve" /* 0x657554 */, 0x10db4},
    {1, "into larger, crueler torments due to one overriding factor:" /* 0x6575a4 */, 0x10db5},
    {1, "You haven't constructed any Ad Infinitum Siphons." /* 0x6575e0 */, 0x10db6},
    {2, "Gosh, that IS a serious oversight." /* 0x657614 */, 0x1011b},
    {1, "In Hell, a number of Punishments aren't at their worst," /* 0x657638 */, 0x10db7},
    {1, "because there's a Siphon that isn't connected to enough demonic structures." /* 0x657670 */, 0x10db8},
    {1, "Now you've done it..." /* 0x6576bc */, 0x10db9},
    {1, /* 0x6576d4 */
     "Hell has grown so large that you'll need more Ad Infinitum Siphons to keep it empowered.",
     0x10dba},
    {1, "Hell seems to be losing a trifling, nearly insignificant number of SOULs." /* 0x6581f8 */, 0x10e3a},
    {1, /* 0x658244 */
     "While this isn't busting your budget, it'd probably be wise to attend to the problem before it escalates.",
     0x10e3b},
    {1, /* 0x6582b0 */
     "Check the Graphview to find out which Punishments are overflowing, and zone accordingly.",
     0x10e3c},
    {1, "While you've still got the cash, I'd suggest building a Bank or two." /* 0x65741c */, 0x10db1},
    {1, "With a Bank you can get a loan, and loans can be VERY helpful." /* 0x657464 */, 0x10db2},
    {2, "Wormwood is right, but I'd build my Banks in Heaven." /* 0x6574a4 */, 0x10119},
    {2, "Our lending terms are MUCH nicer." /* 0x6574dc */, 0x1011a},
    {1, "Congratulations, Your Pandimensionalness, Hell's Roads are disgustingly efficient," /* 0x6577f4 */, 0x10dbd},
    {1, /* 0x657848 */
     "briskly transporting the Damned from one Punishment to another with a minimum of fuss.",
     0x10dbe},
    {2, "Hey, waitaminute, I thought you said--" /* 0x6578a0 */, 0x1011e},
    {1, /* 0x6578c8 */
     "Wait for it...Unfortunately, an efficient Road system is the LAST thing you want in Hell,",
     0x10dbf},
    {1, "and will only serve to suppress the evolution of your Punishments!" /* 0x657924 */, 0x10dc0},
    {1, "If you ever want to see a highly-evolved Hell," /* 0x657968 */, 0x10dc1},
    {1, "you had better start snarling your traffic, tut suite!" /* 0x657998 */, 0x10dc2},
    {1, /* 0x657730 */
     "I'm a trifle concerned that the Damned aren't being forced to walk long enough to get where they're going.",
     0x10dbb},
    {2, "Do you really spend your time worrying about stuff like that?" /* 0x65779c */, 0x1011c},
    {1, "Of course." /* 0x6577dc */, 0x10dbc},
    {2, "How sad." /* 0x6577e8 */, 0x1011d},
    {1, "With all due respect, Your Immenseness, Hell's Punishments are a downright mess," /* 0x657ae4 */, 0x10dc6},
    {1, "at least, in terms of workforce alignment." /* 0x657b38 */, 0x10dc7},
    {2, "That sounds bad." /* 0x657b64 */, 0x10120},
    {1, "Oh, it is.  If Hell is ever to evolve out of its current quagmire of mediocrity," /* 0x657b78 */, 0x10dc8},
    {1, /* 0x657bcc */
     "you'll probably have to use the Macromanager to make some major, and expensive, adjustments.",
     0x10dc9},
    {1, /* 0x6579d0 */
     "Field Auditors in Hell have concluded that several of your Punishments are considerably misaligned.",
     0x10dc3},
    {2, "And we all know how painful that can be." /* 0x657a34 */, 0x1011f},
    {1, "Yes, well, if you can spare the time, you might want to track down these structures," /* 0x657a60 */, 0x10dc4},
    {1, "and readjust their workforce alignments." /* 0x657ab8 */, 0x10dc5},
    {1, /* 0x657c2c */
     "More than a few of Hell's Punishments have fallen under unfortunate clouds of Good Vibes.",
     0x10dca},
    {2, "Ooh, good Vibes are keen!" /* 0x657c88 */, 0x10121},
    {1, "Yes, but not in Hell." /* 0x657ca4 */, 0x10dcb},
    {1, "If you wish to eliminate these 'keen' patches of Hell," /* 0x657cbc */, 0x10dcc},
    {1, "I'd suggest putting a Distopia or a Gift Structure near the afflicted buildings." /* 0x657cf4 */, 0x10dcd},
    {1, "Far too many Damned SOULs are being comforted by the vagaries of Schadenfreude." /* 0x657d48 */, 0x10dce},
    {2, "Schadenwawa?" /* 0x657d98 */, 0x10122},
    {1, "'Schadenfreude:  to take delight in the suffering of others.'" /* 0x657da8 */, 0x10dcf},
    {1, /* 0x657de8 */
     "In other words, your Hell is too diverse, and should be more compartmentalized.  Remember: misery loves "
     "company.",
     0x10dd0},
    {1, /* 0x658600 */
     "For some unfathomable reason, the EMBOs on the Planet just don't believe in an Afterlife as much as they "
     "should.",
     0x10df1},
    {2, "And that's a very bad thing." /* 0x658674 */, 0x10155},
    {1, /* 0x658694 */
     "Halobrain is right.  Without belief, your Afterlife is nothing, and without your Afterlife...we DON'T GET PAID!",
     0x10df2},
    {2, /* 0x658704 */
     "Hey, that's right!  You'd better get those folks believing in an Afterlife, Boss, or we're ALL in trouble!",
     0x10156},
    {1, "By my calculations, Hell will eventually become overrun with SOULs," /* 0x657e5c */, 0x10dd1},
    {1, "if the belief rates in reincarnation don't start going up." /* 0x657ea0 */, 0x10dd2},
    {1, "This is more of a petulant whine than a problem," /* 0x657edc */, 0x10dd3},
    {1, "but have you noticed how sickeningly sweet the Planet is lately?" /* 0x657f10 */, 0x10dd4},
    {2, "I think it's fabulous, how everyone is happy, 'n' lovin', 'n' peaceful, 'n'..." /* 0x657f54 */, 0x10123},
    {1, "On second thought, forget I said anything." /* 0x657fa4 */, 0x10dd5},
    {1, "For some inexplicable reason, Oh Nearly-Perfect One, the EMBOs on the Planet," /* 0x657fd0 */, 0x10dd6},
    {1, "while having a strong belief in the Afterlife, don't have much of a belief in Hell." /* 0x658020 */, 0x10dd7},
    {1, "If I were you, I'd do something about that." /* 0x658074 */, 0x10dd8},
};
static const AdvCase k_hell_cases[] = {
    {20, 0, 1}, {25, 2, 3}, {30, 5, 4}, {35, 9, 4}, {40, 13, 5},
    {45, 18, 5}, {50, 23, 2}, {55, 25, 4}, {60, 29, 3}, {65, 32, 4},
    {70, 36, 3}, {75, 39, 8}, {80, 47, 3}, {85, 50, 3}, {90, 53, 8},
    {95, 61, 5}, {100, 66, 3}, {105, 69, 5}, {110, 74, 5}, {115, 79, 2},
    {120, 81, 2}, {125, 83, 3}, {130, 86, 4}, {140, 90, 7}, {150, 97, 4},
    {160, 101, 5}, {165, 106, 4}, {170, 110, 5}, {180, 115, 4}, {185, 119, 4},
    {190, 123, 2}, {205, 125, 4}, {215, 129, 3}, {ADV_DEFAULT, 1, 1},
};

/* 0x41db2b: the same issue in both realms */
static const AdvLine k_both_lines[] = {
    {1, /* 0x659bf8 */
     "Sir, the Powers That Be have given us 5 years to pull out of debt, or they're shutting us down.",
     0x10e2b},
    {2, "I'm gonna start working on my resume." /* 0x659c58 */, 0x1018c},
    {2, /* 0x659c80 */
     "Are there one or two Rs in 'cherub'?  Oh, never mind, I'll just use my spell checker.",
     0x1018d},
    {1, "Waiting in line to get in.  In other words, build some more Gates." /* 0x65a0ac */, 0x10dd9},
    {1, "Sir, the Powers That Be have been sniffing around my office," /* 0x659b3c */, 0x10e29},
    {1, "wondering why we haven't been paying our Angels and Demons." /* 0x659b7c */, 0x10e2a},
    {2, "If you don't start paying, you'd better start praying, Boss." /* 0x659bb8 */, 0x1018b},
    {2, "Boss, you've slipped into debt!" /* 0x659aec */, 0x1018a},
    {1, "If you're able to take out a loan, do it NOW." /* 0x659b0c */, 0x10e28},
    {1, /* 0x658994 */
     "Your little slice of Hell isn't going to start growing until you build a gate, your Eminence.",
     0x10df6},
    {2, "Yeah, and neither will Heaven, Boss." /* 0x6589f4 */, 0x1015d},
    {1, /* 0x659cd8 */
     "SOULs are leaving your Afterlife because they can't find their expected Fate Structures.",
     0x10ecc},
    {2, /* 0x659d34 */
     "Jasper's right, Boss.  You'd better look at the Graphview, find out what kinds of Fate Structures you need,",
     0x1021f},
    {2, "And get zoning!" /* 0x659da0 */, 0x10220},
    {2, "All of Heaven's gates are overflowing like a cup of infinite goodness!" /* 0x658a1c */, 0x1015e},
    {1, /* 0x658a64 */
     "Hell's gates are similarly afflicted, aside from that 'cup of infinite goodness' part.  I'd suggest that you "
     "build more gates.",
     0x10df7},
    {2, "Or you could upgrade the ones you've already got, making 'em bigger and spiffier!" /* 0x658ae4 */, 0x1015f},
    {2, /* 0x659f60 */
     "Boss, Boss, Boss! Heaven and Hell are losing SOULs like they were going out of style!",
     0x10221},
    {1, /* 0x659fb8 */
     "The cherub is right; stop whatever you're doing, and start zoning for Fate Structures at once!",
     0x10ed0},
    {1, "Alas, none of your Afterlife's SOULs can reincarnate, your greatness." /* 0x658b38 */, 0x10df8},
    {2, "So, whaddaya waitin' for?  Build some Karma Stations, already." /* 0x658b80 */, 0x10160},
    {2, /* 0x658bc0 */
     "Using my vast cosmic senses, I can tell that you've got unhooked Karma Stations in Heaven.",
     0x10161},
    {1, "Using my eyeballs, I can tell you that the same is true in Hell." /* 0x658c1c */, 0x10df9},
    {1, /* 0x658c60 */
     "I'd suggest running some track from the unhooked stations to a Karma Portal, immediamant.",
     0x10dfa},
    {2, "Heaven and Hell's Karma Stations are all backed up and gaflooey!" /* 0x658cbc */, 0x10162},
    {1, /* 0x658d00 */
     "I wouldn't know about 'gaflooey' per se, but I do know that you should build some more Karma Stations.",
     0x10dfb},
    {2, "Or maybe upgrade the ones you've got!" /* 0x658d68 */, 0x10163},
    {2, "Whooooa, Boss, you've built way more structures in Heaven and Hell than you need." /* 0x659a38 */, 0x10184},
    {1, /* 0x659a8c */
     "Unless the concept of bankruptcy appeals to you, I'd recommend blowing up about half of them.",
     0x10ec9},
    {1, "I'd be remiss in my duties if I didn't inform you that your Demons are a bit peeved." /* 0x658d90 */, 0x10dfc},
    {2, "So are the Angels.  We're tired of commuting from other Afterlifes." /* 0x658de8 */, 0x10164},
    {1, "If you've got the money, you should build Topias for your workforce." /* 0x658e2c */, 0x10dfd},
    {1, /* 0x658e74 */
     "They'll provide housing for your Angels and Demons, and cut your labor costs in half.",
     0x10dfe},
    {2, "Eww, your Topias in Heaven and Hell are overflowing." /* 0x658ecc */, 0x10165},
    {1, /* 0x658f04 */
     "Unless you can afford a rise in labor costs, I'd personally recommend building more Topias.",
     0x10dff},
    {1, "Strictly on the Q.T., your excellency, this imported labor is ripping you off." /* 0x658f60 */, 0x10e00},
    {1, "You'd be better off training your own workforce." /* 0x658fb0 */, 0x10e01},
    {2, /* 0x658fe4 */
     "Jasper's right, Boss.  You really should build some Training Centers in Heaven and Hell.",
     0x10166},
    {1, /* 0x65a018 */
     "According to my reports, Heaven and Hell are both losing SOULs due to incorrectly connected Roads.",
     0x10ed1},
    {2, "Remember, Boss: SOULs can ONLY walk on Roads." /* 0x65a07c */, 0x10222},
    {1, /* 0x659040 */
     "To remain fiscally responsible, I'm convinced that your Afterlife needs more Training Centers.",
     0x10e2c},
    {2, /* 0x6590a0 */
     "Besides, those homegrown Angels and Demons are so much friendlier than those snooty imported ones.",
     0x10210},
    {2, "Boss, ya know those Training Centers you've got?" /* 0x659104 */, 0x10167},
    {2, "Well, they're working just a little bit too well..." /* 0x659138 */, 0x10168},
    {1, "What our synaptically challenged friend is trying to say is:" /* 0x65916c */, 0x10e02},
    {1, "We're drowning in unemployed Angels and Demons, you fool!" /* 0x6591ac */, 0x10e03},
    {1, "Turn off your Training Centers before someone starts a revolt!" /* 0x6591e8 */, 0x10e04},
    {1, "Ports, for lack of a better word, are good.  Ports work." /* 0x659228 */, 0x10e2d},
    {1, "Unfortunately, your Ports DON'T work, because they're overflowing." /* 0x659264 */, 0x10e2e},
    {2, "I guess that means you should build some more Ports, Boss." /* 0x6592a8 */, 0x10211},
    {1, /* 0x659e8c */
     "Both Heaven and Hell are suffering from rather substantial SOUL losses, Your Jumboness.",
     0x10ecf},
    {2, /* 0x659ee4 */
     "Jasper's right, Boss.  You'd better look at the Graphview, find out what kinds of Fate Structures you need,",
     0x1021f},
    {2, "And get zoning!" /* 0x659f50 */, 0x10220},
    {2, "Jiminy, Bossperson, Heaven's Limbos are dangerously overcrowded." /* 0x6592e4 */, 0x10212},
    {1, "I'd chuckle maniacally, but Hell's Limbos are similarly afflicted." /* 0x659328 */, 0x10e2f},
    {1, "If you intend to fully utilize your Limbo Bars, you'd better build more of them." /* 0x65936c */, 0x10e30},
    {1, /* 0x6593c0 */
     "It appears that neither Heaven nor Hell are evolving as well as they could, Your Humongousness.",
     0x10e31},
    {2, /* 0x659420 */
     "Well, that's because the Boss hasn't tapped into any of those Infinity Rocks with an Ad Infinitum Siphon.",
     0x10213},
    {1, "True.  One wonders how long it will be before he actually figures it out." /* 0x65948c */, 0x10e32},
    {1, /* 0x6594d8 */
     "It's considered tres gauche to have an Ad Infinitum Siphon totally disconnected from any structures.",
     0x10e33},
    {2, "Yeah, and it's a bad idea, too!" /* 0x659540 */, 0x10214},
    {1, "Hmmm...it appears as though Heaven could use a few more Ad Infinitum Siphons." /* 0x659560 */, 0x10e34},
    {2, "Yeah?  Well, it looks like Hell could use some, too.  So there <nyah>." /* 0x6595b0 */, 0x10215},
    {1, /* 0x659db0 */
     "A few piddling SOULs are drifting away from your Afterlife because they can't find their expected Fate "
     "Structures.",
     0x10ece},
    {2, /* 0x659e24 */
     "Now, now, Wormnoggin, ALL SOULs are important.  You should fix the leak when you get the time, Boss.",
     0x1021e},
    {1, "While you've still got the cash, I'd suggest building a Bank or two." /* 0x6588b0 */, 0x10db1},
    {1, "With a Bank you can get a loan, and loans can be VERY helpful." /* 0x6588f8 */, 0x10db2},
    {2, "Wormwood is right, but I'd build my Banks in Heaven." /* 0x658938 */, 0x10119},
    {2, "Our lending terms are MUCH nicer." /* 0x658970 */, 0x1011a},
    {2, "Hmmm.  Now, don't panic, Boss, but you're running low on cash." /* 0x6587b4 */, 0x10146},
    {2, "It may be time to take out a loan." /* 0x6587f4 */, 0x10147},
    {1, "I suggest using one of the friendly Banks of Hell." /* 0x658818 */, 0x10dea},
    {1, /* 0x65884c */
     "They're blissfully unencumbered by those odiously high interest rates charged by Heaven's Banks.",
     0x10deb},
    {1, /* 0x659780 */
     "I'm afraid that the Fate Structures in both Hell and Heaven have gone seriously, how shall we put it...",
     0x10e37},
    {2, "Whack-a-ding-hoy?" /* 0x6597e8 */, 0x10197},
    {1, /* 0x6597fc */
     "Hmmm.  Close enough.  The sad truth is, your Reward and Punishment Structures are so far out of alignment,",
     0x10e38},
    {1, /* 0x659868 */
     "that I fear you'll have to use the Macromanager to get them running at peak efficiency again.",
     0x10e39},
    {2, /* 0x6595f8 */
     "Dribblenerts, Boss, Heaven's got a few Reward Structures that are way out of alignment,",
     0x10194},
    {2, "in a purely resource-management kinda way, that is." /* 0x659650 */, 0x10195},
    {1, /* 0x659684 */
     "It grieves me to no end to inform Your Fatuousness that a few of Hell's Punishments are also out of balance.",
     0x10e35},
    {1, /* 0x6596f4 */
     "Perhaps you should find the time to track down the offending structures, and make the necessary adjustments.",
     0x10e36},
    {2, "Yeah, but do Heaven first!" /* 0x659764 */, 0x10196},
    {1, /* 0x6598c8 */
     "For some unfathomable reason, the EMBOs on the Planet just don't believe in an Afterlife as much as they "
     "should.",
     0x10df1},
    {2, "And that's a very bad thing." /* 0x65993c */, 0x10155},
    {1, /* 0x65995c */
     "Halobrain is right.  Without belief, your Afterlife is nothing, and without your Afterlife...we DON'T GET PAID!",
     0x10df2},
    {2, /* 0x6599cc */
     "Hey, that's right!  You'd better get those folks believing in an Afterlife, Boss, or we're ALL in trouble!",
     0x10156},
};
static const AdvCase k_both_cases[] = {
    {5, 0, 3}, {10, 4, 3}, {15, 7, 2}, {20, 9, 2}, {25, 11, 3},
    {30, 14, 3}, {35, 17, 2}, {40, 19, 2}, {45, 21, 3}, {50, 24, 3},
    {55, 27, 2}, {60, 29, 4}, {65, 29, 4}, {70, 33, 2}, {75, 35, 3},
    {80, 38, 2}, {85, 40, 2}, {90, 42, 5}, {95, 47, 3}, {100, 50, 3},
    {105, 53, 3}, {110, 56, 3}, {115, 59, 2}, {120, 61, 2}, {125, 63, 2},
    {130, 65, 4}, {135, 69, 4}, {160, 73, 4}, {165, 77, 5}, {185, 82, 4},
    {190, 86, 0}, {ADV_DEFAULT, 3, 1},
};

/* 0x4196ae: a button without an issue, rand_range(1, 13). */
static const AdvLine k_quips[13] = {
    {1, "Oh well, C'est la vie." /* 0x6544d8 */, 0x10d9b},
    {1, "Oh, bite me." /* 0x6544f0 */, 0x10db0},
    {2, "How sad." /* 0x654500 */, 0x1011d},
    {2, "And we all know how painful that can be." /* 0x65450c */, 0x1011f},
    {2, "Schadenwawa?" /* 0x654538 */, 0x10122},
    {2, "Whatever." /* 0x654548 */, 0x10135},
    {1, "If I wasn't already dead, I'd get a pretty little gun and shoot myself." /* 0x654554 */, 0x10de6},
    {1, "Now there's an interesting turn of phrase." /* 0x65459c */, 0x10df3},
    {2, "Whack-a-ding-hoy?" /* 0x6545c8 */, 0x10197},
    {2, /* 0x6545dc */
     "Are there one or two Rs in 'cherub'?  Oh, never mind, I'll just use my spell checker.",
     0x1018d},
    {2, "Oh, pooh, don't be such a meanie." /* 0x654634 */, 0x101a2},
    {2, "Are you done yet?" /* 0x654658 */, 0x101c4},
    {1, "Adorable." /* 0x65466c */, 0x10e5e},
};

typedef struct AdvTable {
    const AdvLine *lines;
    const AdvCase *cases;
    size_t n_cases;
} AdvTable;

#define TABLE(n) {k_##n##_lines, k_##n##_cases, sizeof k_##n##_cases / sizeof k_##n##_cases[0]}
static const AdvTable k_tables[3] = {TABLE(heaven), TABLE(hell), TABLE(both)};

/* 0x41801f: the labels (short UI strings, kept inline). */
static const struct {
    uint8_t code;
    const char *label;
} k_labels[] = {
    {5, "PENNIES!"},    {10, "Pennies!"},   {15, "Pennies"},   {20, "GATES!"},    {25, "FATES!"},
    {30, "Gates"},      {35, "LOSSES!"},    {40, "KStation"},  {45, "KStation"},  {50, "KStation"},
    {55, "Too Much"},   {60, "TOPIAS!"},    {65, "Topias"},    {70, "Topias"},    {75, "TCenters"},
    {80, "ROADS!"},     {85, "TCenters"},   {90, "TCENTERS"},  {95, "Ports"},     {100, "Losses!"},
    {105, "Limbos"},    {110, "Siphons"},   {115, "Siphons"},  {120, "Siphons"},  {125, "Losses"},
    {130, "Banks"},     {135, "Loans"},     {140, "ROADS!"},   {145, "ROADS!"},   {150, "Roads"},
    {155, "Roads"},     {160, "Balance!"},  {165, "Balance"},  {170, "Vibes"},    {171, "Vibes"},
    {175, "Diversity"}, {180, "Diversity"}, {185, "Belief"},   {190, "Karma"},    {195, "Karma"},
    {200, "Good"},      {205, "Evil"},      {210, "Heaven"},   {215, "Hell"},     {ADV_NONE, "-----"},
};

const char *advisor_issue_label(unsigned code)
{
    for (size_t i = 0; i < sizeof k_labels / sizeof k_labels[0]; i++)
        if (k_labels[i].code == code)
            return k_labels[i].label;
    return "Nada";
}

static const AdvCase *find_case(const AdvTable *t, unsigned code)
{
    const AdvCase *def = NULL;
    for (size_t i = 0; i < t->n_cases; i++) {
        if (t->cases[i].issue == code && code != ADV_DEFAULT)
            return &t->cases[i];
        if (t->cases[i].issue == ADV_DEFAULT)
            def = &t->cases[i];
    }
    return def;
}

/* One line; `first` lines don't check the stamp (it was just read). */
static void say_line(const AdvLine *l, bool first, uint32_t stamp)
{
    if (g_adv_talk.silenced || (!first && stamp != g_adv_talk.stamp))
        return;
    akos_talk_say(l->talker, &g_adv_talk.text_rect[l->talker], l->text, l->voice);
    akos_talk_wait(l->talker);
}

static void say(int which, unsigned code)
{
    const AdvCase *c = find_case(&k_tables[which], code);
    if (!c)
        return;
    uint32_t stamp = g_adv_talk.stamp;
    for (int i = 0; i < c->count; i++)
        say_line(&k_tables[which].lines[c->first + i], i == 0, stamp);
}

/* 0x419b65 */
void advisor_say_heaven(unsigned code) { say(0, code); }

/* 0x41b768 */
void advisor_say_hell(unsigned code) { say(1, code); }

/* 0x41db2b */
void advisor_say_both(unsigned code) { say(2, code); }

void advisor_say_quip(int n)
{
    if (n >= 1 && n <= 13)
        say_line(&k_quips[n - 1], false, g_adv_talk.stamp);
}

int advisor_speech_length(int which, unsigned code)
{
    if (which < 0 || which > 2)
        return 0;
    const AdvCase *c = find_case(&k_tables[which], code);
    return c ? c->count : 0;
}

const char *advisor_speech_text(int which, unsigned code, int i)
{
    if (which < 0 || which > 2)
        return NULL;
    const AdvCase *c = find_case(&k_tables[which], code);
    if (!c || i < 0 || i >= c->count)
        return NULL;
    return k_tables[which].lines[c->first + i].text;
}
