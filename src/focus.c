/* Skippy-xd
 *
 * Copyright (C) 2004 Hyriand <hyriand@thegraveyard.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "skippy.h"

#define HALF_H(w) ((w).x + (w).width / 2)
#define HALF_V(w) ((w).y + (w).height / 2)
#define SQR(x) ((x) * (x))

enum {
	DIR_UP,
	DIR_DOWN,
	DIR_LEFT,
	DIR_RIGHT,
};

/**
 * @brief Return the absolute (root) geometry of a client window's mini window.
 *
 * When pseudo-transparency is used, the mini window is a child of the main
 * window, so its coordinates are relative to the monitor it belongs to. For
 * spatial focus navigation to work across monitors we always need root
 * coordinates.
 */
static inline SkippyWindow
cw_abs(ClientWin *cw) {
	SkippyWindow w = cw->mini;
	if (cw->mainwin->ps->o.pseudoTrans) {
		w.x += cw->mainwin->x;
		w.y += cw->mainwin->y;
	}
	return w;
}

static void
focus_dir(ClientWin *cw, int dir) {
	session_t * const ps = cw->mainwin->ps;
	SkippyWindow base = cw_abs(cw);

	ClientWin *candidate = NULL;
	float best = 0.0;

	foreach_dlist (cw->mainwin->focuslist) {
		ClientWin *win = (ClientWin *) iter->data;
		if (win == cw)
			continue;

		SkippyWindow w = cw_abs(win);

		bool qualifies = false;
		float dx = 0.0, dy = 0.0;
		switch (dir) {
			case DIR_UP:
				qualifies = w.y + w.height <= base.y;
				dx = HALF_H(w) - HALF_H(base);
				dy = (float) base.y - w.y - w.height;
				break;
			case DIR_DOWN:
				qualifies = base.y + base.height <= w.y;
				dx = HALF_H(w) - HALF_H(base);
				dy = (float) w.y - base.y - base.height;
				break;
			case DIR_LEFT:
				qualifies = w.x + w.width <= base.x;
				dx = (float) base.x - w.x - w.width;
				dy = HALF_V(w) - HALF_V(base);
				break;
			case DIR_RIGHT:
				qualifies = base.x + base.width <= w.x;
				dx = (float) w.x - base.x - base.width;
				dy = HALF_V(w) - HALF_V(base);
				break;
		}

		if (!qualifies)
			continue;

		float distance = sqrtf(SQR(dx) + SQR(dy));
		if (!candidate || distance < best) {
			candidate = win;
			best = distance;
		}
	}

	if (!candidate)
		return;

	cw->focused = false;
	clientwin_render(cw);
	XFlush(ps->dpy);

	focus_miniw(ps, candidate);
}

void focus_up(ClientWin *cw) { focus_dir(cw, DIR_UP); }
void focus_down(ClientWin *cw) { focus_dir(cw, DIR_DOWN); }
void focus_left(ClientWin *cw) { focus_dir(cw, DIR_LEFT); }
void focus_right(ClientWin *cw) { focus_dir(cw, DIR_RIGHT); }
