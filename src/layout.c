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
window_center(const ClientWin *window, float *center_x, float *center_y)
{
	*center_x = window->x + window->src.width / 2.0f;
	*center_y = window->y + window->src.height / 2.0f;
}

static void
set_window_center(ClientWin *window, float center_x, float center_y)
{
	window->x = (int) lroundf(center_x - window->src.width / 2.0f);
	window->y = (int) lroundf(center_y - window->src.height / 2.0f);
}

static void
grid_offset(size_t rank, size_t count, size_t columns,
		float cell_width, float cell_height,
		float *offset_x, float *offset_y)
{
	size_t rows = (count + columns - 1) / columns;
	size_t row = rank / columns;
	size_t column = rank % columns;
	size_t row_count = count - row * columns;

	if (row_count > columns)
		row_count = columns;

	*offset_x = cell_width
		* ((float) column - ((float) row_count - 1.0f) / 2.0f);
	*offset_y = cell_height
		* ((float) row - ((float) rows - 1.0f) / 2.0f);
}

static unsigned int
run_scatter(MainWin *mw, ClientWin **windows, size_t count,
		float scale_x, float scale_y, float padding)
{
	const float center_threshold = 0.10f;
	const float clearance = 0.02f;
	float threshold_x = center_threshold * scale_x;
	float threshold_y = center_threshold * scale_y;
	bool *gridded = calloc(count, sizeof(*gridded));
	bool *members = calloc(count, sizeof(*members));
	unsigned int groups = 0;

	if (!gridded || !members) {
		free(members);
		free(gridded);
		return groups;
	}

	for (;;) {
		bool found = false;

		for (size_t seed = 0; seed < count; seed++) {
			if (gridded[seed])
				continue;

			float seed_x, seed_y;
			window_center(windows[seed], &seed_x, &seed_y);
			size_t member_count = 0;

			for (size_t i = 0; i < count; i++) {
				float x, y;

				members[i] = false;
				if (gridded[i])
					continue;
				window_center(windows[i], &x, &y);

				if (fabsf(x - seed_x) <= threshold_x
						&& fabsf(y - seed_y) <= threshold_y) {
					members[i] = true;
					member_count++;
				}
			}

			if (member_count <= 1)
				continue;

			size_t mode_count = 0;
			double mode_area = 0;
			int mode_width = 0;
			int mode_height = 0;

			for (size_t i = 0; i < count; i++) {
				if (!members[i])
					continue;

				size_t geometry_count = 0;
				for (size_t j = 0; j < count; j++) {
					if (members[j]
							&& windows[j]->src.width
								== windows[i]->src.width
							&& windows[j]->src.height
								== windows[i]->src.height)
						geometry_count++;
				}

				double area = (double) windows[i]->src.width
					* (double) windows[i]->src.height;
				if (geometry_count > mode_count
						|| (geometry_count == mode_count
							&& area > mode_area)) {
					mode_count = geometry_count;
					mode_area = area;
					mode_width = windows[i]->src.width;
					mode_height = windows[i]->src.height;
				}
			}

			double total_mass = 0;
			double weighted_x = 0;
			double weighted_y = 0;
			for (size_t i = 0; i < count; i++) {
				if (!members[i])
					continue;

				float x, y;
				double mass = (double) windows[i]->src.width
					* (double) windows[i]->src.height;
				window_center(windows[i], &x, &y);
				total_mass += mass;
				weighted_x += mass * x;
				weighted_y += mass * y;
			}

			float center_x = (float) (weighted_x / total_mass);
			float center_y = (float) (weighted_y / total_mass);
			float screen_aspect = mw->width > 0 && mw->height > 0
				? (float) mw->width / (float) mw->height : 1.0f;
			float cell_width = mode_width + padding;
			float cell_height = mode_height + padding;
			float window_aspect = (float) mode_width / mode_height;
			size_t columns = (size_t) ceilf(sqrtf(
				(float) member_count * screen_aspect / window_aspect));
			if (columns < 1)
				columns = 1;
			if (columns > member_count)
				columns = member_count;
			size_t rows = (member_count + columns - 1) / columns;

			double weighted_offset_x = 0;
			double weighted_offset_y = 0;
			size_t rank = 0;
			for (size_t i = 0; i < count; i++) {
				if (!members[i])
					continue;

				float offset_x, offset_y;
				double mass = (double) windows[i]->src.width
					* (double) windows[i]->src.height;
				grid_offset(rank, member_count, columns,
						cell_width, cell_height, &offset_x, &offset_y);
				weighted_offset_x += mass * offset_x;
				weighted_offset_y += mass * offset_y;
				rank++;
			}

			float mean_offset_x = (float) (weighted_offset_x / total_mass);
			float mean_offset_y = (float) (weighted_offset_y / total_mass);
			float reserve_half_x = columns * cell_width / 2.0f
				+ fabsf(mean_offset_x);
			float reserve_half_y = rows * cell_height / 2.0f
				+ fabsf(mean_offset_y);

			rank = 0;
			for (size_t i = 0; i < count; i++) {
				if (!members[i])
					continue;

				float offset_x, offset_y;
				grid_offset(rank, member_count, columns,
						cell_width, cell_height, &offset_x, &offset_y);
				set_window_center(windows[i],
						center_x + offset_x - mean_offset_x,
						center_y + offset_y - mean_offset_y);
				gridded[i] = true;
				rank++;

				float x, y;
				window_center(windows[i], &x, &y);
				float actual_half_x = fabsf(x - center_x)
					+ (windows[i]->src.width + padding) / 2.0f;
				float actual_half_y = fabsf(y - center_y)
					+ (windows[i]->src.height + padding) / 2.0f;
				reserve_half_x = fmaxf(reserve_half_x, actual_half_x);
				reserve_half_y = fmaxf(reserve_half_y, actual_half_y);
			}

			float outsider_scale = 1.0f;
			for (size_t i = 0; i < count; i++) {
				if (members[i])
					continue;

				float x, y;
				window_center(windows[i], &x, &y);
				float distance_x = fabsf(x - center_x);
				float distance_y = fabsf(y - center_y);
				float required_x = reserve_half_x
					+ (windows[i]->src.width + padding) / 2.0f
					+ clearance;
				float required_y = reserve_half_y
					+ (windows[i]->src.height + padding) / 2.0f
					+ clearance;
				float scale_for_x = distance_x > 0
					? required_x / distance_x : INFINITY;
				float scale_for_y = distance_y > 0
					? required_y / distance_y : INFINITY;
				float scale_for_window = fminf(scale_for_x, scale_for_y);

				if (scale_for_window > outsider_scale)
					outsider_scale = scale_for_window;
			}

			if (outsider_scale > 1.0f && isfinite(outsider_scale)) {
				for (size_t i = 0; i < count; i++) {
					if (members[i])
						continue;

					float x, y;
					window_center(windows[i], &x, &y);
					set_window_center(windows[i],
							center_x + outsider_scale * (x - center_x),
							center_y + outsider_scale * (y - center_y));
				}
			}

			groups++;
			found = true;
			break;
		}

		if (!found)
			break;
	}

	free(members);
	free(gridded);
	return groups;
}

static unsigned int
run_expansion(AabbWorld *world)
{
	const float clearance = 0.02f;
	float center_x, center_y;

	if (!aabb_world_center_of_mass(world, &center_x, &center_y))
		return 0;

	float expansion_scale = aabb_world_required_dilation(world, clearance);

	if (expansion_scale <= 1.0f)
		return 0;

	return aabb_world_dilate(world, center_x, center_y, expansion_scale) ? 1 : 0;
}

static unsigned int
run_contraction(AabbWorld *world, size_t count)
{
	const float contraction_rate = 0.02f;
	const float movement_tolerance = 0.01f;
	unsigned int iteration = 0;

	for (; iteration < 1000; iteration++) {
		float center_x, center_y;
		if (!aabb_world_center_of_mass(world, &center_x, &center_y))
			break;

		for (size_t i = 0; i < count; i++) {
			float x, y;
			aabb_body_get_position(world, (AabbBodyId) i, &x, &y);
			aabb_body_set_drive(world, (AabbBodyId) i,
					contraction_rate * (center_x - x),
					contraction_rate * (center_y - y));
		}

		AabbStepResult result = aabb_world_step(world, 1.0f);

		if (result.max_movement <= movement_tolerance) {
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

	if (!items) {
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
	float scale_y = MAX(1, max_y - min_y)
		* (float) mw->width / (float) mw->height / 1.4f;
	float padding = (float) mw->distance + 1.0f;
	unsigned int scatter_groups = run_scatter(mw, items, count,
			scale_x, scale_y, padding);

	min_x = INT_MAX;
	max_x = INT_MIN;
	min_y = INT_MAX;
	max_y = INT_MIN;

	for (size_t i = 0; i < count; i++) {
		min_x = MIN(min_x, items[i]->x);
		max_x = MAX(max_x, items[i]->x + items[i]->src.width);
		min_y = MIN(min_y, items[i]->y);
		max_y = MAX(max_y, items[i]->y + items[i]->src.height);
	}

	scale_x = MAX(1, max_x - min_x);
	scale_y = MAX(1, max_y - min_y)
		* (float) mw->width / (float) mw->height / 1.4f;

	AabbWorld *world = aabb_world_create(count, padding, scale_x, scale_y);

	if (!world) {
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
			free(items);
			return;
		}
	}

	unsigned int expansion_iterations = run_expansion(world);
	unsigned int contraction_iterations = run_contraction(world, count);
	unsigned int settle_iterations = run_final_settle(world);

	printfdf(false, "(): %u scatter groups", scatter_groups);
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
	free(items);
}
