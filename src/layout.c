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

// this function redirects to different functions
// which performs the expose layout
// by calaculating cw->x, cw->y (new coordinates)
// and total_width, total_height
// given cw->src.x, cw->src.y (original coordinates)

void layout_run(MainWin *mw, dlist *windows,
		unsigned int *total_width, unsigned int *total_height,
		enum layoutmode layout) {
	if ((mw->ps->o.mode == PROGMODE_EXPOSE && mw->ps->o.exposeLayout == LAYOUT_COSMOS)
	|| (mw->ps->o.mode == PROGMODE_SWITCH && mw->ps->o.switchLayout == LAYOUT_COSMOS)) {
		foreach_dlist (dlist_first(windows)) {
			ClientWin *cw = iter->data;

			// virtual desktop offset
			{
				int screencount = wm_get_desktops(mw->ps);
				if (screencount == -1)
					screencount = 1;
				int desktop_dim = ceil(sqrt(screencount));

				int win_desktop = wm_get_window_desktop(mw->ps, cw->wid_client);
				int current_desktop = wm_get_current_desktop(mw->ps);
				if (win_desktop == -1)
					win_desktop = current_desktop;

				int win_desktop_x = win_desktop % desktop_dim;
				int win_desktop_y = win_desktop / desktop_dim;

				int current_desktop_x = current_desktop % desktop_dim;
				int current_desktop_y = current_desktop / desktop_dim;

				cw->src.x += (win_desktop_x - current_desktop_x) * (mw->width + mw->distance);
				cw->src.y += (win_desktop_y - current_desktop_y) * (mw->height + mw->distance);
			}

			cw->x = cw->src.x;
			cw->y = cw->src.y;
		}

		dlist *sorted_windows = dlist_dup(windows);
		dlist_sort(sorted_windows, sort_cw_by_id, 0);
		dlist_sort(sorted_windows, sort_cw_by_row, 0);
		if (mw->ps->o.exposeLayout == LAYOUT_COSMOS)
			layout_cosmos(mw, sorted_windows, total_width, total_height);
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

float
intersectArea(ClientWin *cw1, ClientWin *cw2,
		unsigned int *total_width, unsigned int *total_height) {
	int dis = cw1->mainwin->distance / 2;
	float disx = (float)dis / (float) *total_width;
	float disy = (float)dis / (float) *total_height;
	float x1 = cw1->fx - disx, x2 = cw2->fx - disx;
	float y1 = cw1->fy - disy, y2 = cw2->fy - disy;
	float w1 = (float)cw1->src.width / (float) *total_width + 2*disx,
		  w2 = (float)cw2->src.width / (float) *total_width + 2*disx;
	float h1 = (float)cw1->src.height / (float) *total_height + 2*disy,
		  h2 = (float)cw2->src.height / (float) *total_height + 2*disy;

	float left   = MAX(x1, x2);
	float top    = MAX(y1, y2);
	float right  = MIN(x1 + w1, x2 + w2);
	float bottom = MIN(y1 + h1, y2 + h2);

	if (right < left || bottom < top)
		return 0;

	return (right - left) * (bottom - top);
}

static inline float
ABS(float x) {
	if (x < 0)
		return -x;
	return x;
}

static void
com(ClientWin *cw, float *x, float *y,
		unsigned int *total_width, unsigned int *total_height) {
	*x = cw->fx + (float)cw->src.width / 2.0 / *total_width;
	*y = cw->fy + (float)cw->src.height / 2.0 / *total_height;
}

static inline void
inverse2(float dx, float dy, float *ax, float *ay) {
	float dist = sqrt(dx*dx + dy*dy);
	if (dist < 0.01) {
		*ax = *ay = 0;
		return;
	}

	float acc = 1e-2 / dist / dist;

	*ax = acc * dx / dist;
	*ay = acc * dy / dist;
}

void
layout_cosmos(MainWin *mw, dlist *windows,
		unsigned int *total_width, unsigned int *total_height)
{
	// convert pixel coordinates (x,y) to float coordinates (fx,fy)
	// 0 <= fx, fy <= 1
	// normalized by screen width/height
	{
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

		foreach_dlist (dlist_first(windows)) {
			ClientWin *cw = iter->data;
			cw->fx = (float)cw->x / (float)*total_width;
			cw->fy = (float)cw->y / (float)*total_height;
		}
	}

	// scatter windows with identical centre of mass
	{
		srand(0);
		int iterations = -1;
		bool colliding = true;
		while (colliding && iterations <= 1000) {
			colliding = false;

			for (dlist *iter1 = dlist_first(windows);
					iter1; iter1=iter1->next) {
				for (dlist *iter2 = dlist_first(windows);
						iter2; iter2=iter2->next) {
					ClientWin *cw1 = iter1->data;
					ClientWin *cw2 = iter2->data;
					if (cw1 == cw2)
						continue;

					float x1, y1, x2, y2;
					com(cw1, &x1, &y1, total_width, total_height);
					com(cw2, &x2, &y2, total_width, total_height);
					float dx = x2 - x1;
					float dy = y2 - y1;
					float delta = 0.05;
					if (ABS(dx) <= delta && ABS(dy) <= delta) {
						colliding = true;
						float randx = (float)rand()/(float)(RAND_MAX/delta/2) - delta;
						float randy = (float)rand()/(float)(RAND_MAX/delta/2) - delta;
						cw1->fx += randx;
						cw1->fy += randy;
					}
				}
			}
			iterations++;
		}
		printfdf(false, "(): %d iterations to resolve identical COM", iterations);
		printfdf(false, "():");
	}

	// cosmic expansion
	{
		foreach_dlist (dlist_first(windows)) {
			ClientWin *cw = iter->data;
			cw->vx = cw->vy = 0;
		}

		int iterations = 0;
		float deltat = 1e-1;
		float aratio = (float)mw->width / (float)mw->height;
		bool colliding = true;
		while (colliding && iterations < 1000) {
			colliding = false;

			for (dlist *iter1 = dlist_first(windows);
					iter1; iter1=iter1->next) {
				for (dlist *iter2 = dlist_first(windows);
						iter2; iter2=iter2->next) {
					ClientWin *cw1 = iter1->data;
					ClientWin *cw2 = iter2->data;
					if (cw1 == cw2)
						continue;

					if (intersectArea(cw1, cw2, total_width, total_height) > 0) {
						colliding = true;
						float m1 = cw1->src.width * cw1->src.height
								/ (float)*total_width / (float)*total_height,
							  m2 = cw2->src.width * cw2->src.height
								/ (float)*total_width / (float)*total_height;
						float x1, x2, y1, y2;
						com(cw1, &x1, &y1, total_width, total_height);
						com(cw2, &x2, &y2, total_width, total_height);
						float dx = x2 - x1;
						float dy = y2 - y1;
						float vx=0, vy=0;
						inverse2(dx, dy, &vx, &vy);
						cw1->vx -= m2 * vx;
						cw1->vy -= m2 * vy / aratio /* * 2.0*/;
						float speed = sqrt(cw1->vx * cw1->vx + cw1->vy * cw1->vy);
						if (speed > 1) {
							cw1->vx /= speed;
							cw1->vy /= speed;
						}
					}
				}
			}

			foreach_dlist (dlist_first(windows)) {
				ClientWin *cw = iter->data;
				cw->fx += cw->vx * deltat;
				cw->fy += cw->vy * deltat;

				cw->vx = 0;
				cw->vy = 0;
			}
			printfdf(false,"():");

			iterations++;
		}
		printfdf(true, "(): %d expansion iterations", iterations);
		printfdf(true, "():");
	}

	// gravitational collapse
	{
		int iterations = 0;
		float deltat = 1e-1;
		float aratio = (float)mw->width / (float)mw->height;
		bool stable = false;
		int dis = mw->distance;
		float disx = (float) dis / (float) *total_width;
		float disy = (float) dis / (float) *total_height;
		while (!stable && iterations < 1000) {
			stable = true;

			for (dlist *iter1 = dlist_first(windows);
					iter1; iter1=iter1->next) {
				for (dlist *iter2 = dlist_first(windows);
						iter2; iter2=iter2->next) {
					ClientWin *cw1 = iter1->data;
					ClientWin *cw2 = iter2->data;
					if (cw1 == cw2)
						continue;

					float m1 = (float) cw1->src.width * (float) cw1->src.height
							/ (float)*total_width / (float)*total_height,
						  m2 = (float) cw2->src.width * (float) cw2->src.height
							/ (float)*total_width / (float)*total_height;
					float x1, x2, y1, y2;
					com(cw1, &x1, &y1, total_width, total_height);
					com(cw2, &x2, &y2, total_width, total_height);
					float dx = x2 - x1;
					float dy = y2 - y1;
					float vx=0, vy=0;
					inverse2(dx, dy, &vx, &vy);
					cw1->vx += m2 * vx;
					cw1->vy += m2 * vy / aratio /* * 2.0*/;
				}
			}

			foreach_dlist (dlist_first(windows)) {
				ClientWin *cw1 = iter->data;
				cw1->fx2 = cw1->fx;
				cw1->fy2 = cw1->fy;

				float speed = sqrt(cw1->vx * cw1->vx + cw1->vy * cw1->vy);
				float vx = 0, vy = 0;
				while (speed > 0) {
					vx = cw1->vx / speed * disx;
					vy = cw1->vy / speed * disx;

					cw1->fx += vx * deltat;
					cw1->fy += vy * deltat;

					for (dlist *iter2 = dlist_first(windows);
							iter2; iter2=iter2->next) {
						ClientWin *cw2 = iter2->data;
						if (cw1 == cw2 || intersectArea(cw1, cw2, total_width, total_height) == 0)
							continue;

						float overlapx = MIN(cw1->fx + (float)cw1->src.width / (float)*total_width - cw2->fx,
											cw2->fx + (float)cw2->src.width / (float)*total_width - cw1->fx);
						float overlapy = MIN(cw1->fy + (float)cw1->src.height / (float)*total_height - cw2->fy,
											cw2->fy + (float)cw2->src.height / (float)*total_height - cw1->fy);

						if (overlapy < overlapx) {
							if (vy > 0)
								cw1->fy = cw2->fy - (float)cw1->src.height / (float)*total_height - disy;
							else
								cw1->fy = cw2->fy + (float)cw2->src.height / (float)*total_height + disy;
						}
						else {
							if (vx > 0)
								cw1->fx = cw2->fx - (float)cw1->src.width / (float)*total_width - disx;
							else
								cw1->fx = cw2->fx + (float)cw2->src.width / (float)*total_width + disx;
						}
					}

					speed -= disx;
				}
			}

			foreach_dlist (dlist_first(windows)) {
				ClientWin *cw = iter->data;
				cw->vx = 0;
				cw->vy = 0;
			}

			foreach_dlist (dlist_first(windows)) {
				ClientWin *cw1 = iter->data;
				if (ABS(cw1->fx - cw1->fx2) > 1e-7
				 || ABS(cw1->fy - cw1->fy2) > 1e-7)
					stable = false;
			}
			iterations++;
		}
		printfdf(true, "(): %d collapse iterations", iterations);
		printfdf(true, "():");
	}

	{
		int minx = INT_MAX, maxx = INT_MIN;
		int miny = INT_MAX, maxy = INT_MIN;
		foreach_dlist (dlist_first(windows)) {
			ClientWin *cw = iter->data;
			cw->x = (float)cw->fx * (float)*total_width;
			cw->y = (float)cw->fy * (float)*total_height;

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
	}
}
