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
			&& mw->ps->o.exposeLayout == LAYOUT_BOXY)
		layout_boxy(mw, windows, total_width, total_height);
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

// new layout algorithm
//
//
// design principles:
//
// 0. there are non-unique ways to put windows into a screen without overlapping
// 1. for user intuition, we want the layout to be close to the original
//    window positions
// 2. when two windows hold identical positions, then we have tradeoffs
// 3. screens are often either long (when rotated 90) or wide,
//    and layouts should capitalize on that
// 4. sizes of windows will enlarge/shrink by the same factor,
//    since size is an important visual cue to identify windows
// 5. the screen size aspect ratio,
//    the windows orignal total occupied screen space aspect ratio,
//    (and windows may be overlapping or hold identical spaces,
//    and the final occupied screen aspect ratio
//    do not have to be identical
// 6. boxy layout slots are more screen efficient,
//    as well as being easier for user's eyes
//
// algorithm:
//
// 1. scan through windows to find the smallest window size,
//    this forms the slot size
// 2. LOOP:
//    create 2D array of slots,
//    where each element holds the number of windows in that slot,
//    map each slot to the occupying windows,
// 3. if there are any empty rows/columns, remove and go to start of loop
// 3. a. expand:
//       loop slots right->left, bottom->top, for each slot with collision,
//       insert right/below new row/columns based on screen aspect ratio
//       move windows to right/down, sorted by "affinity":
//       which is the number of slots in that direction minus those
//           in opposite direction, times the windows' number of slots
//       so the higher the affinity, the further from the original slot
//    b. contract:
//       because expansion goes only to right and down,
//       we often get layouts in top-left half e.g.
//
//        o o o o
//        o o
//        o
//
//       rotate contract row/column
//       routine is grab rightmost/tpomost bottom/rightmost slot,
//       move right/down while slot above/left is empty
//       then move up/left
//
// 4. END LOOP when no windows have been moved
// 5. move windows to slots
//
//
// normal window has aspect ratio around (2.5,1)
// dramatic aspect ratio defined as aspect ratio bigger than 10
// so aspect ratio of (25,1) or (1,4)

#ifndef ASPECT_TOLERANCE
#define ASPECT_TOLERANCE 1.4
#endif

#ifndef TRIANGULAR_TOLERANCE
#define TRIANGULAR_TOLERANCE 0.7
#endif

void
layout_boxy(MainWin *mw, dlist *windows,
		unsigned int *total_width, unsigned int *total_height)
{
	// find screen aspect ratio
	//
	float screen_aspect = (float) mw->width / (float) mw->height;

	// find slot size
	// offset window positions by virtual desktop
	// initialize destination position as source position
	//
	int slot_width=INT_MAX, slot_height=INT_MAX;
	foreach_dlist (windows) {
		ClientWin *cw = (ClientWin *) iter->data;
		if (!cw->mode) continue;

		slot_width = MIN(slot_width,  cw->src.width);
		slot_height = MIN(slot_height, cw->src.height);

		{
			int screencount = wm_get_desktops(mw->ps);
			if (screencount == -1)
				screencount = 1;
			int desktop_dim = ceil(sqrt(screencount));

			int win_desktop = wm_get_window_desktop(mw->ps, cw->wid_client);
			int current_desktop = wm_get_current_desktop(mw->ps);

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
	// minimal slot size required,
	// otherwise windows too small create round-off issus
	if (slot_width < mw->width/5)
		slot_width = mw->width/5;
	if (slot_height < mw->height/5)
		slot_height = mw->height/5;

	//printfdf("(): slot size: (%d,%d)", slot_width, slot_height);

	// array declaration
	// properly allocated in the beginning of each iteration of the loop
	// and freed at the end of the loop
	// we declare it here because the final coordinate calculations
	// use this 2D array
	dlist** slot2cw;
	int* slot2n;
	int slot_minx=INT_MAX, slot_miny=INT_MAX,
		slot_maxx=INT_MIN, slot_maxy=INT_MIN;

// main calculation loop
bool recalculate = true;
for (int max_iterations=0; recalculate && max_iterations<100; max_iterations++)
{
	recalculate = false;

	// create 2D arrays of slots:
	//
	// 1. a list of pointers of windows that occupy the slot
	// 2. the number of windows on that slot
	//

	slot_minx=INT_MAX; slot_miny=INT_MAX;
	slot_maxx=INT_MIN; slot_maxy=INT_MIN;
	// first do a pass to find the min/max slots,
	// so that we can declare the 2D array with the right dimensions
	foreach_dlist (windows) {
		ClientWin *cw = (ClientWin *) iter->data;
		if (!cw->mode) continue;

		int slotx  = floor((float) cw->x / (float) slot_width);
		int sloty  = floor((float) cw->y / (float) slot_height);
		int slotxx = slotx + ceil((float) cw->src.width / (float) slot_width);
		int slotyy = sloty + ceil((float) cw->src.height / (float) slot_height);

		printfdf(false,"(): window %p coord: (%d,%d) (%d,%d)", cw, cw->x, cw->y, cw->src.width, cw->src.height);
		printfdf(false,"(): window %p slot: (%d,%d) (%d,%d)", cw, slotx, sloty, slotxx, slotyy);
		slot_minx  = MIN(slot_minx, slotx);
		slot_miny  = MIN(slot_miny, sloty);
		slot_maxx  = MAX(slot_maxx, slotxx);
		slot_maxy  = MAX(slot_maxy, slotyy);
	}

		//printfdf("(): slot maxes: (%d,%d) (%d,%d)", slot_minx, slot_miny, slot_maxx, slot_maxy);
	int number_of_slots = (slot_maxx -slot_minx) * (slot_maxy -slot_miny);

	//printfdf("(): slot layout: %dx%d, %d slots",
			//slot_maxx-slot_minx, slot_maxy-slot_miny, number_of_slots);

	// allocate and initailize 2D arrays
	//
	slot2cw = malloc(number_of_slots * sizeof(dlist*));
	for (int j=slot_miny; j<slot_maxy; j++) {
		for (int i=slot_minx; i<slot_maxx; i++) {
			slot2cw[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx] = NULL;
		}
	}

	slot2n = malloc(number_of_slots * sizeof(int));
	for (int j=slot_miny; j<slot_maxy; j++) {
		for (int i=slot_minx; i<slot_maxx; i++) {
			slot2n[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx] = 0;
		}
	}

	// populate 2D arrays
	//
	foreach_dlist (windows) {
		ClientWin *cw = (ClientWin *) iter->data;
		if (!cw->mode) continue;

		cw->slots = 0; // reset

		int slotx  = floor((float) cw->x / (float) slot_width);
		int sloty  = floor((float) cw->y / (float) slot_height);
		int slotxx = slotx + ceil((float) cw->src.width / (float) slot_width);
		int slotyy = sloty + ceil((float) cw->src.height / (float) slot_height);
		if (slotxx == slotx)
			slotxx++;
		if (slotyy == sloty)
			slotyy++;

		for (int j=sloty; j<slotyy && j<slot_maxy; j++) {
			for (int i=slotx; i<slotxx && i<slot_maxx; i++) {
				// map slot to window(s)
				if (slot2cw[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx] == NULL) {
					slot2cw[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx]
						= dlist_add(NULL, cw);
				}
				else {
					dlist_add(slot2cw[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx], cw);
				}
//printfdf("(): (%d,%d) window++", i, j);
				// add slot number
				slot2n[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx]++;

				cw->slots++;
			}
		}
	}

	/*printf("Slot occupancy:\n");
	for (int j=slot_miny; j<slot_maxy; j++) {
		for (int i=slot_minx; i<slot_maxx; i++) {
			int slot = slot2n[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx];
			if (slot == 0)
				printf("  ");
			else
				printf("%d ", slot);
		}
		printf("\n");
	}*/

	// remove empty rows and columns
	//
	for (int j=slot_miny; j<slot_maxy; j++) {
		bool row_empty = true;
		for (int i=slot_minx; row_empty && i<slot_maxx; i++) {
			if (slot2n[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx] > 0)
				row_empty = false;
		}
		if (row_empty) {
			//printfdf("(): prune row %d",j);
			foreach_dlist (windows) {
				ClientWin *cw = iter->data;
				int sloty  = floor((float) cw->y / (float) slot_height);
				if (sloty >= j)
					cw->y -= slot_height;
			}
			recalculate = true;
		}
	} for (int i=slot_minx; i<slot_maxx; i++) {
		bool column_empty = true;
		for (int j=slot_miny; column_empty && j<slot_maxy; j++) {
			if (slot2n[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx] > 0)
				column_empty = false;
		}
		if (column_empty) {
			//printfdf("(): prune column %d",i);
			foreach_dlist (windows) {
				ClientWin *cw = iter->data;
				int slotx  = floor((float) cw->x / (float) slot_width);
				if (slotx >= i)
					cw->x -= slot_width;
			}
			recalculate = true;
		}
	}

	// expansion:
	//
	// loop slots right->left, bottom->top, for each slot with collision,
	// insert right/below new row/columns based on screen aspect ratio
	// move windows to right/down, sorted by affinity
	//
	for (int j=slot_maxy-1; !recalculate && j>=slot_miny; j--) {
		for (int i=slot_maxx-1; !recalculate && i>=slot_minx; i--) {
			if (slot2n[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx] > 1) {
				recalculate = true;
				printfdf(false,"(): Collision on slot (%d,%d) with %d windows",
						i, j, slot2n[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx]);

				// insert new row or column
				// based on estimated used screen aspect ratio
				// we favour adding new row below current slot
				int ii = i, jj = j + 1;
				float estimated_aspect = (float) (slot_width * slot_maxx)
					/ (float) (slot_height * slot_maxy);
				if (estimated_aspect < screen_aspect * ASPECT_TOLERANCE) {
					ii = i + 1;
					jj = j;
				}

				// find window with highest affinity to neighbouring slot
				ClientWin *moving_window = NULL;//dlist_first(slot2cw[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx]);
				int max_affinity = INT_MIN;
				foreach_dlist (slot2cw[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx]) {
					ClientWin *slotw = (ClientWin*) iter->data;
					int affinity = boxy_affinity(slotw, slot_width, slot_height, i, j, ii-i, jj-j);
					if (affinity > max_affinity) {
						max_affinity = affinity;
						moving_window = slotw;
					}
					max_affinity = MAX(max_affinity, affinity);
					printfdf(false,"(): window %p has affinity %d", slotw, affinity);
				}

				{
					int slotx  = floor((float) moving_window->x / (float) slot_width);
					int sloty  = floor((float) moving_window->y / (float) slot_height);
					int slotxx = slotx + ceil((float) moving_window->src.width / (float) slot_width);
					int slotyy = sloty + ceil((float) moving_window->src.height / (float) slot_height);
					if (slotxx == slotx)
						slotxx++;
					if (slotyy == sloty)
						slotyy++;

					printfdf(false,"(): moving window %p (%d,%d) -> (%d,%d) which has size (%d,%d)",
							moving_window, slotx, sloty, slotx+ii-i, sloty+jj-j, slotxx-slotx, slotyy-sloty);
				}

						moving_window->x += (ii - i) * slot_width;
						moving_window->y += (jj - j) * slot_height;
			}
		}
	}

	// rotate contraction to solve triangular non-optimal
	{
		// check triangular score
		int top_left_occupancy = 0;
		int total_occupancy = 0;
		for (int j=slot_miny; j<slot_maxy; j++) {
			for (int i=slot_minx; i<slot_maxx; i++) {
				int slot_occupancy = slot2n[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx] > 0;
				total_occupancy += slot_occupancy;
				if ((slot_maxx - slot_minx) * (j - slot_miny)
						+ (slot_maxy - slot_miny) * (i - slot_minx)
						< (slot_maxx - slot_minx) * (slot_maxy - slot_miny) + 1){
					printfdf(false,"(): (%d,%d) in top left", i,j);
					top_left_occupancy += slot_occupancy;
				}
			}
		}
		float occupancy_ratio = (float)top_left_occupancy / (float)total_occupancy;
		printfdf(false,"(): top left occupancy: %d total occupancy: %d triangular ratio: %f",
				top_left_occupancy, total_occupancy, occupancy_ratio);

		// if too triangular, perform rotate contraction
		if (occupancy_ratio > TRIANGULAR_TOLERANCE) {

			// determine whether to perform row or column rotate contraction
			int pivotx = 0, pivoty = 0, ii = 0, jj = 0;
			bool pivoted = false;
			float estimated_aspect = (float) (slot_width * slot_maxx)
				/ (float) (slot_height * slot_maxy);
            printfdf(false,"(): aspect %f %f",estimated_aspect, screen_aspect*ASPECT_TOLERANCE);
			if (estimated_aspect < screen_aspect * ASPECT_TOLERANCE) {
				ii = 1;
				for (int j=slot_maxy-1; j>slot_miny && !pivoted; j--) {
					for (int i=slot_maxx-1; i>=slot_minx && !pivoted; i--) {
						if (slot2n[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx] > 0) {
							printfdf(false,"(): perform row rotate contraction at (%d,%d)",
									i, j);
							pivotx = i;
							pivoty = j;
							pivoted = true;
						}
					}
				}
			}
			else {
				jj = 1;
				for (int i=slot_maxx-1; i>slot_minx && !pivoted; i--) {
					for (int j=slot_maxy-1; j>=slot_miny && !pivoted; j--) {
						if (slot2n[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx] > 0) {
							printfdf(false,"(): perform column rotate contraction at (%d,%d)",
									i, j);
							pivotx = i;
							pivoty = j;
							pivoted = true;
						}
					}
				}
			}

			// rotate contraction:
			// identify next empty slot as target
			int targetx = pivotx, targety = pivoty;
			
			while (targetx-jj < slot_maxx-1 && targety-ii < slot_maxy-1
					&& slot2n[(targety-ii -slot_miny) * (slot_maxx - slot_minx)
					+ targetx-jj -slot_minx] > 0) { // notice swapping and -ve of ii,jj
				targetx += ii;
				targety += jj;
			}

			if (!(targetx == pivotx && targety == pivoty)
					&& targetx-jj >= 0
					&& targety-ii >= 0
					&& slot2n[(targety-ii -slot_miny) * (slot_maxx - slot_minx)
					+ targetx-jj -slot_minx] == 0) { // notice swapping and -ve of ii,jj
				targetx -= jj;
				targety -= ii;

				// find window from pivot slot to move, associated with max affinity
				ClientWin *moving_window = NULL;
				int max_affinity = INT_MIN;
				foreach_dlist (slot2cw[(pivoty-slot_miny) * (slot_maxx - slot_minx)
						+ pivotx-slot_minx]) {
					ClientWin *slotw = (ClientWin*) iter->data;
					int affinity = boxy_affinity(slotw, slot_width, slot_height,
							pivotx, pivoty,
							targetx - pivotx, targety - pivoty);
					if (affinity > max_affinity) {
						bool collision = false;
						{
							int slotx_old  = floor((float) slotw->x / (float) slot_width);
							int sloty_old  = floor((float) slotw->y / (float) slot_height);
							int slotxx_old = slotx_old + ceil((float) slotw->src.width / (float) slot_width);
							int slotyy_old = sloty_old + ceil((float) slotw->src.height / (float) slot_height);
							if (slotxx_old == slotx_old)
								slotxx_old++;
							if (slotyy_old == sloty_old)
								slotyy_old++;

							int slotx_new = slotx_old + targetx - pivotx;
							int sloty_new = sloty_old + targety - pivoty;
							int slotxx_new = slotx_new + slotxx_old - slotx_old;
							int slotyy_new = sloty_new + slotyy_old - sloty_old;

							for (int j=sloty_new; !collision && j<slotyy_new && j<slot_maxy; j++) {
								for (int i=slotx_new; !collision && i<slotxx_new && i<slot_maxx; i++) {
									if (slot2n[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx] > 0) {
										if (slot2n[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx] == 1
												&& slotx_old <= i && i < slotxx_old
												&& sloty_old <= j && j < slotyy_old) {
											// do not count own window slots
											continue;
										}
										printfdf(false,"(): collision at (%d,%d), window dimensions (%d,%d,%d,%d)",
												i,j, slotx_old, sloty_old, slotxx_old-slotx_old, slotyy_old-sloty_old);
										collision = true;
									}
								}
							}
						}
						if (!collision) {
							max_affinity = affinity;
							moving_window = slotw;
						}
					}
					max_affinity = MAX(max_affinity, affinity);
					printfdf(false,"(): window %p has affinity %d", slotw, affinity);
				}

				// move window
				if (moving_window != NULL) {
					printfdf(false,"(): rotate contraction from (%d,%d) -> (%d,%d)",
							pivotx, pivoty, targetx, targety);
					recalculate = true;
					moving_window->x += (targetx - pivotx) *slot_width;
					moving_window->y += (targety - pivoty) *slot_height;
				}
			}
		}
	}

	if (recalculate && max_iterations<100-1) {
		for (int i=0; i<number_of_slots; i++)
			dlist_free (slot2cw[i]);
		free(slot2cw);
		free(slot2n);
	}
}

	// move windows to slots,
	// from the 2D array calculate the centre of window and move
	//
	foreach_dlist (windows) {
		ClientWin *cw = (ClientWin *) iter->data;
		if (!cw->mode) continue;
		cw->x = -cw->src.width / 2 - slot_minx * slot_width;
		cw->y = -cw->src.height / 2 - slot_miny * slot_height;
	}
	for (int j=slot_miny; j<slot_maxy; j++) {
		for (int i=slot_minx; i<slot_maxx; i++) {
			foreach_dlist (slot2cw[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx]) {
				ClientWin *cw = (ClientWin *) iter->data;
				if (!cw->mode) continue;

				cw->x += i * (slot_width + mw->distance) / cw->slots;
				cw->y += j * (slot_height + mw->distance) / cw->slots;
			}
		}
	}

	// and finally, calculate new total used screen dimension
	//
	int minx=INT_MAX, miny=INT_MAX, maxx=INT_MIN, maxy=INT_MIN;
	foreach_dlist (windows) {
		ClientWin *cw = (ClientWin *) iter->data;
		if (!cw->mode) continue;
		//printfdf("(): window %p coord: (%d,%d) (%d,%d)", cw, cw->x, cw->y, cw->x+cw->src.width, cw->y+cw->src.height);

		minx = MIN(minx, cw->x);
		miny = MIN(miny, cw->y);
		maxx = MAX(maxx, cw->x + cw->src.width);
		maxy = MAX(maxy, cw->y + cw->src.height);
	}

	if (minx < 0) {
		foreach_dlist (windows) {
			ClientWin *cw = (ClientWin *) iter->data;
			if (!cw->mode) continue;
			cw->x -= minx;
		}
		maxx -= minx;
		minx = 0;
	}

	if (miny < 0) {
		foreach_dlist (windows) {
			ClientWin *cw = (ClientWin *) iter->data;
			if (!cw->mode) continue;
			cw->y -= miny;
		}
		maxy -= miny;
		miny = 0;
	}

	*total_width = maxx - minx;
	*total_height = maxy - miny;

	for (int j=slot_miny; j<slot_maxy; j++)
		for (int i=slot_minx; i<slot_maxx; i++)
			dlist_free (slot2cw[(j-slot_miny) * (slot_maxx - slot_minx) + i-slot_minx]);
	free(slot2cw);
	free(slot2n);
}

int boxy_affinity(
		ClientWin *cw, int slot_width, int slot_height, int x, int y, int ii, int jj
		// x, y is coordinate the window asks for
		// ii, jj is direction of potential move
		)
{
	// cw->src.x cw->src.y should be taken into account also
	float slotx  = (float) cw->x / (float) slot_width;
	float sloty  = (float) cw->y / (float) slot_height;
	float slotxx = slotx + (float) cw->src.width / (float) slot_width;
	float slotyy = sloty + (float) cw->src.height / (float) slot_height;

	//printfdf("(): affinity for window %p (%d,%d)->(%d,%d) (%d,%d,%d,%d)",
			//cw, x,y,x+ii,y+jj,slotx,sloty,slotxx,slotyy);
	return (int)((float)ii * (slotxx - (float)x - (float)x + slotx)
					  + (float)jj * (slotyy - (float)y - (float)y + sloty));
}
