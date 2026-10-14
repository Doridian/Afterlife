/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Map rotation (WP2, map_rotate_cells 0x4514a4).
 *
 * The cell grids store each building's front-edge strips and the shapes of
 * roads, rails and water for the current rotation (g_state.view_dir_x/y).
 * Turning the view by a quarter rewrites them for all three planes.
 */
#ifndef GAME_ROTATE_H
#define GAME_ROTATE_H

/* 0x4514a4: rewrites the grids of planes 1-3 from the rotation in
 * g_state.view_dir_x/y to (new_dx, new_dy), one quarter turn away, then
 * stores the new rotation. `ccw` picks the turn direction the shapes are
 * remapped for (the control panel passes 0 for the turn
 * (-1,1) -> (-1,-1) -> (1,-1) -> (1,1) -> (-1,1) and 1 for the reverse).
 * Also clears g_state.unk_5f4cd0[i][3..4]. Leaves Heaven selected, like
 * the original (it selects 1, 2, 3 in turn). Water tiles 0x34-0x40 become
 * four random plain water cells (draws random numbers). */
void map_rotate_cells(int new_dx, int new_dy, int ccw);

/* One quarter turn of the view as the control panel's rotate buttons do
 * it: dir = +1 steps (1,1) -> (1,-1) -> (-1,-1) -> (-1,1) -> (1,1) (flag 1),
 * dir = -1 the other way (flag 0). Reselects the plane that was selected. */
void world_rotate(int dir);

#endif
