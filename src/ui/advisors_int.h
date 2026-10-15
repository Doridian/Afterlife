/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Internal interface between the advisor window (src/ui/advisors.c) and
 * the advice texts (src/ui/advisors_text.c).
 */
#ifndef AFTERLIFE_UI_ADVISORS_INT_H
#define AFTERLIFE_UI_ADVISORS_INT_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/surface.h"

/* Issue codes: lower is more urgent. The realms keep their five most
 * urgent ones (PlaneStats.unk_808b); ADV_NONE marks an empty entry. */
#define ADV_NONE 0xdc
#define ADV_ISSUES 5

/* The state the speaking code shares with the window. */
typedef struct AdvisorTalk {
    /* 0x6392c8: set by closing / minimizing (and by the news pop-up,
     * advisor_silence) to stop a running speech between lines. */
    bool silenced;
    /* 0x6392f8: the original stores timeGetTime() per button press, and a
     * speech stops when a newer press (from within akos_talk_wait's pump)
     * changed it. The port counts presses instead. */
    uint32_t stamp;
    /* 0x639280 / 0x6392b0: the subtitle rectangles of Jasper and Aria. */
    GfxRect text_rect[3];
} AdvisorTalk;

extern AdvisorTalk g_adv_talk;

/* 0x41801f's switch: the button label of an issue code ("Nada" if unknown). */
const char *advisor_issue_label(unsigned code);

/* The three advice functions: the speech for issue `code`, line by line
 * through akos_talk_say / akos_talk_wait. */
void advisor_say_heaven(unsigned code); /* 0x419b65: in Heaven only */
void advisor_say_hell(unsigned code);   /* 0x41b768: in Hell only */
void advisor_say_both(unsigned code);   /* 0x41db2b: in both realms */
/* 0x4196ae's switch: quip n (1..13) for a button without an issue. */
void advisor_say_quip(int n);

/* Port-only, for tests: the number of lines of issue `code` in table
 * `which` (0 heaven, 1 hell, 2 both; the default speech if the code has
 * none), and line i's subtitle (NULL past the end). */
int advisor_speech_length(int which, unsigned code);
const char *advisor_speech_text(int which, unsigned code, int i);

#endif
