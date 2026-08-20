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
#include "aabb.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// this function redirects to different functions
// which performs the expose layout
// by calaculating cw->x, cw->y (new coordinates)
// and total_width, total_height
// given cw->src.x, cw->src.y (original coordinates)

void layout_run(MainWin *mw, dlist *windows,
		unsigned int *total_width, unsigned int *total_height) {
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
		dlist *slot_iter = NULL;
		if ((mw->ps->o.mode == PROGMODE_SWITCH && mw->ps->o.switch_compact)
		 || (mw->ps->o.mode == PROGMODE_EXPOSE && mw->ps->o.expose_compact))
			slot_iter = dlist_first(slots);
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

static void
limit_vector(float *x, float *y, float maximum)
{
	float length = hypotf(*x, *y);

	if (length > maximum && length > 0) {
		*x *= maximum / length;
		*y *= maximum / length;
	}
}

static float
scatter_random(unsigned int *state)
{
	*state = *state * 1103515245u + 12345u;
	return (float) ((*state >> 8) & 0x00ffffffu) / 16777215.0f;
}

static unsigned int
run_scatter(AabbWorld *world, size_t count,
		float scale_x, float scale_y)
{
	const float center_threshold = 0.10f;
	float threshold_x = center_threshold * scale_x;
	float threshold_y = center_threshold * scale_y;
	unsigned int random_state = 0;
	unsigned int iteration = 0;

	for (; iteration < 1000; iteration++) {
		bool crowded = false;

		for (size_t i = 0; i < count; i++) {
			float x1, y1;
			aabb_body_get_position(world, (AabbBodyId) i, &x1, &y1);

			for (size_t j = i + 1; j < count; j++) {
				float x2, y2;
				aabb_body_get_position(world, (AabbBodyId) j, &x2, &y2);

				if (fabsf(x2 - x1) > threshold_x
						|| fabsf(y2 - y1) > threshold_y)
					continue;

				crowded = true;
				x2 += (2.0f * scatter_random(&random_state) - 1.0f)
					* threshold_x;
				y2 += (2.0f * scatter_random(&random_state) - 1.0f)
					* threshold_y;
				aabb_body_set_position(world, (AabbBodyId) j, x2, y2);
			}
		}

		if (!crowded)
			break;
	}

	return iteration;
}

static bool
bodies_overlap(AabbWorld *world, ClientWin **windows,
		size_t first, size_t second, float padding)
{
	float x1, y1, x2, y2;
	aabb_body_get_position(world, (AabbBodyId) first, &x1, &y1);
	aabb_body_get_position(world, (AabbBodyId) second, &x2, &y2);

	float required_x = (windows[first]->src.width
			+ windows[second]->src.width) / 2.0f + padding;
	float required_y = (windows[first]->src.height
			+ windows[second]->src.height) / 2.0f + padding;

	return fabsf(x2 - x1) < required_x && fabsf(y2 - y1) < required_y;
}

static void
pair_field(AabbWorld *world, ClientWin **windows,
		size_t first, size_t second,
		float scale_x, float scale_y,
		float *field_x, float *field_y)
{
	float x1, y1, x2, y2;
	aabb_body_get_position(world, (AabbBodyId) first, &x1, &y1);
	aabb_body_get_position(world, (AabbBodyId) second, &x2, &y2);

	float dx = (x2 - x1) / scale_x;
	float dy = (y2 - y1) / scale_y;
	float distance = hypotf(dx, dy);

	if (distance < 0.01f) {
		*field_x = 0;
		*field_y = 0;
		return;
	}

	float mass = (float) windows[second]->src.width
		* (float) windows[second]->src.height / (scale_x * scale_y);
	float magnitude = mass / (distance * distance);
	float aspect_ratio = scale_x / scale_y;

	*field_x = magnitude * dx / distance;
	*field_y = magnitude * dy / distance / aspect_ratio;
}

static AabbStepResult
pairwise_step(AabbWorld *world, ClientWin **windows, size_t count,
		float scale_x, float scale_y, float padding,
		float *drive_x, float *drive_y,
		bool attraction, bool contacts_only)
{
	const float field_strength = 0.10f;
	const float time_step = 0.10f;

	memset(drive_x, 0, count * sizeof(*drive_x));
	memset(drive_y, 0, count * sizeof(*drive_y));

	for (size_t i = 0; i < count; i++) {
		for (size_t j = 0; j < count; j++) {
			if (i == j)
				continue;
			if (contacts_only
					&& !bodies_overlap(world, windows, i, j, padding))
				continue;

			float field_x, field_y;
			pair_field(world, windows, i, j,
					scale_x, scale_y, &field_x, &field_y);

			float direction = attraction ? 1.0f : -1.0f;
			drive_x[i] += direction * field_strength * field_x;
			drive_y[i] += direction * field_strength * field_y;
		}
	}

	for (size_t i = 0; i < count; i++) {
		limit_vector(&drive_x[i], &drive_y[i], 1.0f);
		aabb_body_set_drive(world, (AabbBodyId) i,
				drive_x[i] * scale_x * time_step,
				drive_y[i] * scale_y * time_step);
	}

	return aabb_world_step(world, 1.0f);
}

static unsigned int
run_expansion(AabbWorld *world, ClientWin **windows, size_t count,
		float scale_x, float scale_y, float padding,
		float *drive_x, float *drive_y)
{
	const float penetration_tolerance = 0.02f;
	unsigned int iteration = 0;

	for (; iteration < 1000; iteration++) {
		if (aabb_world_max_penetration(world) <= penetration_tolerance)
			break;

		AabbStepResult result = pairwise_step(world, windows, count,
				scale_x, scale_y, padding,
				drive_x, drive_y, false, true);

		if (result.max_penetration <= penetration_tolerance) {
			iteration++;
			break;
		}
	}

	aabb_world_clear_drives(world);
	return iteration;
}

static unsigned int
run_contraction(AabbWorld *world, ClientWin **windows, size_t count,
		float scale_x, float scale_y, float padding,
		float *drive_x, float *drive_y)
{
	const float penetration_tolerance = 0.02f;
	const float movement_tolerance = 0.01f;
	unsigned int iteration = 0;

	for (; iteration < 1000; iteration++) {
		AabbStepResult result = pairwise_step(world, windows, count,
				scale_x, scale_y, padding,
				drive_x, drive_y, true, false);

		if (result.max_penetration <= penetration_tolerance
				&& result.max_movement <= movement_tolerance) {
			iteration++;
			break;
		}
	}

	aabb_world_clear_drives(world);
	return iteration;
}

static unsigned int
run_final_settle(AabbWorld *world)
{
	const float penetration_tolerance = 0.02f;
	unsigned int iteration = 0;

	aabb_world_clear_drives(world);
	for (; iteration < 256; iteration++) {
		if (aabb_world_max_penetration(world) <= penetration_tolerance)
			break;

		AabbStepResult result = aabb_world_step(world, 1.0f);
		if (result.max_penetration <= penetration_tolerance) {
			iteration++;
			break;
		}
	}

	return iteration;
}

void
layout_cosmos(MainWin *mw, dlist *windows,
		unsigned int *total_width, unsigned int *total_height)
{
	windows = dlist_first(windows);
	size_t count = (size_t) dlist_len(windows);

	*total_width = 0;
	*total_height = 0;
	if (count == 0)
		return;

	ClientWin **items = calloc(count, sizeof(*items));
	float *drive_x = calloc(count, sizeof(*drive_x));
	float *drive_y = calloc(count, sizeof(*drive_y));

	if (!items || !drive_x || !drive_y) {
		free(drive_y);
		free(drive_x);
		free(items);
		return;
	}

	size_t index = 0;
	int min_x = INT_MAX, max_x = INT_MIN;
	int min_y = INT_MAX, max_y = INT_MIN;

	foreach_dlist (windows) {
		ClientWin *cw = iter->data;
		items[index++] = cw;
		min_x = MIN(min_x, cw->x);
		max_x = MAX(max_x, cw->x + cw->src.width);
		min_y = MIN(min_y, cw->y);
		max_y = MAX(max_y, cw->y + cw->src.height);
	}

	float scale_x = MAX(1, max_x - min_x);
	float scale_y = MAX(1, max_y - min_y);
	float padding = (float) mw->distance + 1.0f;
	AabbWorld *world = aabb_world_create(count, padding, scale_x, scale_y);

	if (!world) {
		free(drive_y);
		free(drive_x);
		free(items);
		return;
	}

	for (size_t i = 0; i < count; i++) {
		AabbBodyDef definition = {
			.center_x = items[i]->x + items[i]->src.width / 2.0f,
			.center_y = items[i]->y + items[i]->src.height / 2.0f,
			.width = items[i]->src.width,
			.height = items[i]->src.height,
			.inverse_mass = 1.0f
				/ ((float) items[i]->src.width * items[i]->src.height)
		};

		if (aabb_world_add_body(world, &definition) == AABB_BODY_INVALID) {
			aabb_world_destroy(world);
			free(drive_y);
			free(drive_x);
			free(items);
			return;
		}
	}

	unsigned int scatter_iterations = run_scatter(world, count,
			scale_x, scale_y);
	unsigned int expansion_iterations = run_expansion(world, items, count,
			scale_x, scale_y, padding, drive_x, drive_y);
	unsigned int contraction_iterations = run_contraction(world, items, count,
			scale_x, scale_y, padding, drive_x, drive_y);
	unsigned int settle_iterations = run_final_settle(world);

	printfdf(false, "(): %u scatter iterations", scatter_iterations);
	printfdf(false, "(): %u expansion iterations", expansion_iterations);
	printfdf(false, "(): %u contraction iterations", contraction_iterations);
	printfdf(false, "(): %u settle iterations", settle_iterations);

	min_x = INT_MAX;
	max_x = INT_MIN;
	min_y = INT_MAX;
	max_y = INT_MIN;

	for (size_t i = 0; i < count; i++) {
		float center_x, center_y;
		aabb_body_get_position(world, (AabbBodyId) i, &center_x, &center_y);

		items[i]->x = (int) lroundf(center_x - items[i]->src.width / 2.0f);
		items[i]->y = (int) lroundf(center_y - items[i]->src.height / 2.0f);
		min_x = MIN(min_x, items[i]->x);
		max_x = MAX(max_x, items[i]->x + items[i]->src.width);
		min_y = MIN(min_y, items[i]->y);
		max_y = MAX(max_y, items[i]->y + items[i]->src.height);
	}

	for (size_t i = 0; i < count; i++) {
		items[i]->x -= min_x;
		items[i]->y -= min_y;
	}

	*total_width = (unsigned int) (max_x - min_x);
	*total_height = (unsigned int) (max_y - min_y);

	aabb_world_destroy(world);
	free(drive_y);
	free(drive_x);
	free(items);
}
