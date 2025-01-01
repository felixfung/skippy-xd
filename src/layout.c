/* Skippy - Seduces Kids Into Perversion
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

// this is the "abstract" function to determine exposd window layout
// if you want to introduce new layout algorithm/implementation,
// do it here
// by introducing new function and hook here
//
// here, ((ClientWin*) windows)->src.x, ((ClientWin*) windows)->src.y
// hold the original coordinates
// ((ClientWin*) windows)->src.width, ((ClientWin*) windows)->src.height
// hold the window size
// ((ClientWin*) windows)->x, ((ClientWin*) windows)->y
// hold the final window position
// better NOT to change the final window size...
// which is implicitly handled by MainWin transformation
//
// in summary, use this function to implement the exposed layout
// by controlling the windows position
// and calculating the final screen width and height
// = total windows width and height + minimal distance between windows
void layout_run(MainWin *mw, dlist *windows,
		unsigned int *total_width, unsigned int *total_height,
		enum layoutmode layout) {
	if (layout == LAYOUTMODE_EXPOSE
			&& mw->ps->o.exposeLayout == LAYOUT_BOXY) {
		// set up deterministic random seed
		// when two windows have the same centre of mass
		srand(0);

		foreach_dlist (dlist_first(windows)) {
			ClientWin *cw = iter->data;
			cw->x = cw->src.x;
			cw->y = cw->src.y;
		}

		dlist *sorted_windows = dlist_dup(windows);
		layout_boxy(mw, sorted_windows, total_width, total_height);
		dlist_free(sorted_windows);
	}
	else {
		// to get the proper z-order based window ordering,
		// reversing the list of windows is needed
		dlist_reverse(windows);
		layout_xd(mw, windows, total_width, total_height);
		// reversing the linked list again for proper focus ordering
		dlist_reverse(windows);
	}
}

// original legacy layout
//
//
void
layout_xd(MainWin *mw, dlist *windows,
		unsigned int *total_width, unsigned int *total_height)
{
	int sum_w = 0, max_h = 0, max_w = 0;

	dlist *slots = NULL;

	windows = dlist_first(windows);
	*total_width = *total_height = 0;

	// Get total window width and max window width/height
	foreach_dlist (windows) {
		ClientWin *cw = (ClientWin *) iter->data;
		if (!cw->mode) continue;
		sum_w += cw->src.width;
		max_w = MAX(max_w, cw->src.width);
		max_h = MAX(max_h, cw->src.height);
	}

	// Vertical layout
	foreach_dlist (windows) {
		ClientWin *cw = (ClientWin*) iter->data;
		if (!cw->mode) continue;
		dlist *slot_iter = dlist_first(slots);
		for (; slot_iter; slot_iter = slot_iter->next) {
			dlist *slot = (dlist *) slot_iter->data;
			// Calculate current total height of slot
			int slot_h = - mw->distance;
			foreach_dlist_vn(slot_cw_iter, slot) {
				ClientWin *slot_cw = (ClientWin *) slot_cw_iter->data;
				slot_h = slot_h + slot_cw->src.height + mw->distance;
			}
			// Add window to slot if the slot height after adding the window
			// doesn't exceed max window height
			if (slot_h + mw->distance + cw->src.height < max_h) {
				slot_iter->data = dlist_add(slot, cw);
				break;
			}
		}
		// Otherwise, create a new slot with only this window
		if (!slot_iter)
			slots = dlist_add(slots, dlist_add(NULL, cw));
	}

	dlist *rows = dlist_add(NULL, NULL);
	{
		int row_y = 0, x = 0, row_h = 0;
		int max_row_w = sqrt(sum_w * max_h);
		foreach_dlist_vn (slot_iter, slots) {
			dlist *slot = (dlist *) slot_iter->data;
			// Max width of windows in the slot
			int slot_max_w = 0;
			foreach_dlist_vn (slot_cw_iter, slot) {
				ClientWin *cw = (ClientWin *) slot_cw_iter->data;
				slot_max_w = MAX(slot_max_w, cw->src.width);
			}
			int y = row_y;
			foreach_dlist_vn (slot_cw_iter, slot) {
				ClientWin *cw = (ClientWin *) slot_cw_iter->data;
				cw->x = x + (slot_max_w - cw->src.width) / 2;
				cw->y = y;
				y += cw->src.height + mw->distance;
				rows->data = dlist_add(rows->data, cw);
			}
			row_h = MAX(row_h, y - row_y);
			*total_height = MAX(*total_height, y);
			x += slot_max_w + mw->distance;
			*total_width = MAX(*total_width, x);
			if (x > max_row_w) {
				x = 0;
				row_y += row_h;
				row_h = 0;
				rows = dlist_add(rows, 0);
			}
			dlist_free(slot);
		}
		dlist_free(slots);
		slots = NULL;
	}

	*total_width -= mw->distance;
	*total_height -= mw->distance;

	foreach_dlist (rows) {
		dlist *row = (dlist *) iter->data;
		int row_w = 0, xoff;
		foreach_dlist_vn (slot_cw_iter, row) {
			ClientWin *cw = (ClientWin *) slot_cw_iter->data;
			row_w = MAX(row_w, cw->x + cw->src.width);
		}
		xoff = (*total_width - row_w) / 2;
		foreach_dlist_vn (cw_iter, row) {
			ClientWin *cw = (ClientWin *) cw_iter->data;
			cw->x += xoff;
		}
		dlist_free(row);
	}

	dlist_free(rows);
}

#include "physics.h"

void
layout_boxy(MainWin *mw, dlist *windows,
		unsigned int *total_width, unsigned int *total_height)
{
	float aratio = (float)mw->width / (float)mw->height;
	float expansion_coefficient = 1.1;

	int iterations = 0;
	bool colliding = true;
	while (true) {
		int minx = INT_MAX, maxx = INT_MIN;
		int miny = INT_MAX, maxy = INT_MIN;
		foreach_dlist (dlist_first(windows)) {
			ClientWin *cw = iter->data;
			minx = MIN(minx, cw->x);
			maxx = MAX(maxx, cw->x + cw->src.width);
			miny = MIN(miny, cw->y);
			maxy = MAX(maxy, cw->y + cw->src.height);
		}

		foreach_dlist (dlist_first(windows)) {
			ClientWin *cw = iter->data;
			cw->x -= minx;
			cw->y -= miny;
		}

		*total_width = maxx - minx;
		*total_height = maxy - miny;

		if (!colliding || iterations >= 100)
			break;
		colliding = false;

		// expand frame
		*total_width = expansion_coefficient * (float)(*total_height);
		*total_height = expansion_coefficient * (float)(*total_height);
		if (*total_width < aratio * (float)(*total_height))
			*total_width = aratio * (float)(*total_height);
		else /*if (*total_height < aratio / (float)(*total_width))*/
			*total_height = (float)(*total_width) / aratio;

		// collision detection and movement between all window pairs
		// this is of course O(n^2) complexity
		dlist_sort(windows, sort_cw_by_id, 0);
		dlist_sort(windows, sort_cw_by_column, 0);

		for (dlist *iter1 = dlist_first(windows)->next;
				iter1; iter1=iter1->next) {
			for (dlist *iter2 = dlist_first(windows);
					iter2 != iter1; iter2=iter2->next) {
				ClientWin *cw1 = iter1->data;
				ClientWin *cw2 = iter2->data;

				int dx=0, dy=0;
				bool positionChanging = newPositionFromCollision(cw1, cw2,
						&dx, &dy, total_width, total_height);

				if (dx==0 && dy==0 && positionChanging)
					expansion_coefficient += 0.1;

				if (positionChanging) {
					cw1->x += dx;
					cw1->y += dy;
					colliding = true;
				}
			}
		}

		iterations++;
	}
	printfdf(true, "(): %d collision iterations", iterations);
}
