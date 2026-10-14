/*
 * State shared between the controls' source files (port-only header; in
 * the original these are globals of the one controls module).
 */
#ifndef AFTERLIFE_UI_CONTROLS_INT_H
#define AFTERLIFE_UI_CONTROLS_INT_H

#include <stdbool.h>
#include <stdint.h>

#include "ui/wm.h"

extern int *ctl_pause_flag;          /* 0x5fa61c game_paused, a game global */
extern WmWindow *ctl_repeat_arrow;   /* 0x524790: scroll arrow held down */
extern int ctl_repeat_delay;         /* 0x524794: idle ticks before repeating */
extern WmWindow *ctl_blink_field;    /* 0x6726c4: text field with the cursor */
extern bool ctl_blink_phase;         /* 0x6726c8 */
extern WmWindow *ctl_msgbox_dlg;     /* 0x6726b0 */
extern uint32_t ctl_msgbox_deadline; /* 0x6726b8: timeGetTime() deadline, 0 = none */
extern int ctl_msgbox_res;           /* 0x6726bc: chosen button, 0 while open */

/* One turn of a modal loop: ctl_hooks.pump (sys_pump), or the port's
 * fallback. */
void ctl_modal_pump(void);

#endif
