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

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct {
	ClientWin *before;
	ClientWin *after;
	float gap;
	float lambda;
} SepConstraint;

float
intersectArea(ClientWin *cw1, ClientWin *cw2,
		unsigned int *total_width, unsigned int *total_height)
{
	int dis = cw1->mainwin->distance / 2;

	float disx = (float) dis / (float) *total_width;
	float disy = (float) dis / (float) *total_height;

	float x1 = cw1->fx - disx;
	float y1 = cw1->fy - disy;
	float w1 = (float) cw1->src.width / (float) *total_width + 2 * disx;
	float h1 = (float) cw1->src.height / (float) *total_height + 2 * disy;

	float x2 = cw2->fx - disx;
	float y2 = cw2->fy - disy;
	float w2 = (float) cw2->src.width / (float) *total_width + 2 * disx;
	float h2 = (float) cw2->src.height / (float) *total_height + 2 * disy;

	float left = MAX(x1, x2);
	float top = MAX(y1, y2);
	float right = MIN(x1 + w1, x2 + w2);
	float bottom = MIN(y1 + h1, y2 + h2);

	if (right < left || bottom < top)
		return 0;

	return (right - left) * (bottom - top);
}

static inline float
f_abs(float x)
{
	return x < 0 ? -x : x;
}

static inline float
safe_positive(float x)
{
	return MAX(x, 1e-6);
}

static inline float
clampf(float x, float low, float high)
{
	return MAX(low, MIN(high, x));
}

static inline int
round_pixel(float x)
{
	return x < 0 ? (int) ceil(x - 0.5) : (int) floor(x + 0.5);
}

static float
body_width(ClientWin *cw, unsigned int *total_width)
{
	return (float) cw->src.width / (float) *total_width;
}

static float
body_height(ClientWin *cw, unsigned int *total_height)
{
	return (float) cw->src.height / (float) *total_height;
}

static float
body_mass(ClientWin *cw,
		unsigned int *total_width, unsigned int *total_height)
{
	float m = (float) cw->src.width * (float) cw->src.height
		/ (float) *total_width / (float) *total_height;

	return safe_positive(m);
}

static void
body_center(ClientWin *cw, float *x, float *y,
		unsigned int *total_width, unsigned int *total_height)
{
	*x = cw->fx + body_width(cw, total_width) / 2.0;
	*y = cw->fy + body_height(cw, total_height) / 2.0;
}

static void
body_center_at(ClientWin *cw, float fx, float fy,
		float *x, float *y,
		unsigned int *total_width, unsigned int *total_height)
{
	*x = fx + body_width(cw, total_width) / 2.0;
	*y = fy + body_height(cw, total_height) / 2.0;
}

static void
padded_rect(ClientWin *cw,
		unsigned int *total_width, unsigned int *total_height,
		float gapx, float gapy,
		float *left, float *top, float *right, float *bottom)
{
	float w = body_width(cw, total_width);
	float h = body_height(cw, total_height);

	*left = cw->fx - gapx / 2.0;
	*right = cw->fx + w + gapx / 2.0;
	*top = cw->fy - gapy / 2.0;
	*bottom = cw->fy + h + gapy / 2.0;
}

static inline void
unit_attraction(float dx, float dy, float *ux, float *uy)
{
	const float soft = 0.05;

	float dist2 = dx * dx + dy * dy + soft * soft;
	float dist = sqrt(dist2);

	*ux = dx / dist;
	*uy = dy / dist;
}

static ClientWin *
scatter_find(ClientWin *cw)
{
	while (cw->layout_parent != cw) {
		cw->layout_parent = cw->layout_parent->layout_parent;
		cw = cw->layout_parent;
	}

	return cw;
}

static void
scatter_union(ClientWin *a, ClientWin *b)
{
	ClientWin *ra = scatter_find(a);
	ClientWin *rb = scatter_find(b);

	if (ra != rb)
		rb->layout_parent = ra;
}

/*
 * Deterministic crowded-center scatter.
 *
 * This replaces repeated random pair nudges.
 *
 * Purpose:
 *   - if several windows have nearly identical centers, their pairwise force
 *     directions are degenerate;
 *   - create a coherent 2D seed so expansion/contraction can infer meaningful
 *     left/right, above/below, and diagonal relations.
 *
 * This version uses a rectangular 3x3-perimeter stencil instead of circular
 * 8-way directions.
 *
 * Direction order:
 *   corner pair, corner pair, horizontal pair, vertical pair
 *
 * This keeps prefixes balanced while making the seed more rectangular and
 * window-layout-like.
 */
static void
scatter_crowded_centers(dlist *windows,
		unsigned int *total_width, unsigned int *total_height,
		float center_threshold,
		float min_strength,
		float max_strength)
{
	static const float dirx[8] = {
		-1.0,  1.0,  1.0, -1.0,
		-1.0,  1.0,  0.0,  0.0
	};

	static const float diry[8] = {
		-1.0,  1.0, -1.0,  1.0,
		 0.0,  0.0, -1.0,  1.0
	};

	if (dlist_len(windows) <= 1)
		return;

	foreach_dlist (dlist_first(windows)) {
		ClientWin *cw = iter->data;
		cw->layout_parent = cw;
		cw->layout_x = 0;
		cw->layout_y = 0;
	}

	for (dlist *iter = dlist_first(windows); iter; iter = iter->next) {
		ClientWin *cw1 = iter->data;
		float x1, y1;

		body_center(cw1, &x1, &y1, total_width, total_height);

		for (dlist *jter = iter->next; jter; jter = jter->next) {
			ClientWin *cw2 = jter->data;
			float x2, y2;

			body_center(cw2, &x2, &y2, total_width, total_height);

			if (f_abs(x2 - x1) <= center_threshold
					&& f_abs(y2 - y1) <= center_threshold)
				scatter_union(cw1, cw2);
		}
	}


	foreach_dlist (dlist_first(windows)) {
		ClientWin *root_candidate = iter->data;
		ClientWin *root = scatter_find(root_candidate);

		if (root != root_candidate)
			continue;

		int count = 0;
		float avg_width = 0;
		float avg_height = 0;

		float min_cx = INFINITY;
		float max_cx = -INFINITY;
		float min_cy = INFINITY;
		float max_cy = -INFINITY;

		foreach_dlist_vn (jter, dlist_first(windows)) {
			ClientWin *cw = jter->data;
			float cx, cy;

			if (scatter_find(cw) != root)
				continue;

			body_center(cw, &cx, &cy, total_width, total_height);

			count++;
			avg_width += body_width(cw, total_width);
			avg_height += body_height(cw, total_height);
			min_cx = MIN(min_cx, cx);
			max_cx = MAX(max_cx, cx);
			min_cy = MIN(min_cy, cy);
			max_cy = MAX(max_cy, cy);
		}

		if (count <= 1)
			continue;

		avg_width /= count;
		avg_height /= count;

		float spread_x = max_cx - min_cx;
		float spread_y = max_cy - min_cy;
		float spread = MAX(spread_x, spread_y);

		float degeneracy =
			1.0 - clampf(spread / safe_positive(center_threshold),
					0.0, 1.0);

		float scatter_strength =
			min_strength + degeneracy * (max_strength - min_strength);

		float mean_offset_x = 0;
		float mean_offset_y = 0;
		int rank = 0;

		foreach_dlist_vn (jter, dlist_first(windows)) {
			ClientWin *cw = jter->data;

			if (scatter_find(cw) != root)
				continue;

			int shell = rank / 8;
			int dir = rank % 8;
			float radius = scatter_strength * (float) (shell + 1);

			cw->layout_x = radius * avg_width * dirx[dir];
			cw->layout_y = radius * avg_height * diry[dir];

			mean_offset_x += cw->layout_x;
			mean_offset_y += cw->layout_y;

			rank++;
		}

		mean_offset_x /= count;
		mean_offset_y /= count;

		foreach_dlist_vn (jter, dlist_first(windows)) {
			ClientWin *cw = jter->data;

			if (scatter_find(cw) != root)
				continue;

			cw->fx += cw->layout_x - mean_offset_x;
			cw->fy += cw->layout_y - mean_offset_y;
		}

	}
}

static bool
choose_horizontal_constraint(ClientWin *cw1, ClientWin *cw2,
		unsigned int *total_width, unsigned int *total_height,
		float gapx, float gapy,
		float overlap_x_px, float overlap_y_px,
		float relation_bias,
		float closing_bias,
		float aspect_bias)
{
	float cx1, cy1, cx2, cy2;
	float old_cx1, old_cy1, old_cx2, old_cy2;

	body_center(cw1, &cx1, &cy1, total_width, total_height);
	body_center(cw2, &cx2, &cy2, total_width, total_height);

	body_center_at(cw1, cw1->fx2, cw1->fy2,
			&old_cx1, &old_cy1, total_width, total_height);
	body_center_at(cw2, cw2->fx2, cw2->fy2,
			&old_cx2, &old_cy2, total_width, total_height);

	float required_x =
		(body_width(cw1, total_width)
		 + body_width(cw2, total_width)) / 2.0 + gapx;

	float required_y =
		(body_height(cw1, total_height)
		 + body_height(cw2, total_height)) / 2.0 + gapy;

	required_x = safe_positive(required_x);
	required_y = safe_positive(required_y);

	float relation_x = f_abs(cx2 - cx1) / required_x * aspect_bias;
	float relation_y = f_abs(cy2 - cy1) / required_y / aspect_bias;

	if (relation_x > relation_y + relation_bias)
		return true;

	if (relation_y > relation_x + relation_bias)
		return false;

	float old_dx = old_cx2 - old_cx1;
	float old_dy = old_cy2 - old_cy1;
	float new_dx = cx2 - cx1;
	float new_dy = cy2 - cy1;

	float closing_x = (f_abs(old_dx) - f_abs(new_dx))
		/ required_x * aspect_bias;
	float closing_y = (f_abs(old_dy) - f_abs(new_dy))
		/ required_y / aspect_bias;

	if (closing_x > closing_y + closing_bias)
		return true;

	if (closing_y > closing_x + closing_bias)
		return false;

	if (overlap_x_px / aspect_bias < overlap_y_px * aspect_bias)
		return true;

	if (overlap_y_px * aspect_bias < overlap_x_px / aspect_bias)
		return false;

	return (((int) cw1->layout_index + (int) cw2->layout_index) & 1) == 0;
}

static dlist *
add_separation_constraint(dlist *constraints,
		ClientWin *before, ClientWin *after, float gap)
{
	SepConstraint *c = calloc(1, sizeof(*c));

	c->before = before;
	c->after = after;
	c->gap = MAX(gap, 0);

	return dlist_add(constraints, c);
}

static void
free_constraint_list(dlist *constraints)
{
	if (!constraints)
		return;

	foreach_dlist (dlist_first(constraints))
		free(iter->data);

	dlist_free(constraints);
}

static void
build_separation_constraints(dlist *windows,
		dlist **xcons, dlist **ycons,
		unsigned int *total_width, unsigned int *total_height,
		float gapx, float gapy,
		float slop_px, float corner_slop_px,
		float relation_bias,
		float closing_bias,
		float aspect_bias)
{
	*xcons = NULL;
	*ycons = NULL;

	float slopx = slop_px / (float) *total_width;
	float slopy = slop_px / (float) *total_height;

	for (dlist *iter = dlist_first(windows); iter; iter = iter->next) {
		ClientWin *cw1 = iter->data;

		for (dlist *jter = iter->next; jter; jter = jter->next) {
			ClientWin *cw2 = jter->data;
			float l1, t1, r1, b1;
			float l2, t2, r2, b2;

			padded_rect(cw1, total_width, total_height,
					gapx, gapy, &l1, &t1, &r1, &b1);
			padded_rect(cw2, total_width, total_height,
					gapx, gapy, &l2, &t2, &r2, &b2);

			float overlap_x = MIN(r1, r2) - MAX(l1, l2);
			float overlap_y = MIN(b1, b2) - MAX(t1, t2);

			if (overlap_x <= 0 || overlap_y <= 0)
				continue;

			float overlap_x_px = overlap_x * (float) *total_width;
			float overlap_y_px = overlap_y * (float) *total_height;

			if (corner_slop_px > 0
					&& overlap_x_px <= corner_slop_px
					&& overlap_y_px <= corner_slop_px)
				continue;

			float cx1, cy1, cx2, cy2;

			body_center(cw1, &cx1, &cy1, total_width, total_height);
			body_center(cw2, &cx2, &cy2, total_width, total_height);

			bool horizontal =
				choose_horizontal_constraint(cw1, cw2,
						total_width, total_height,
						gapx, gapy,
						overlap_x_px, overlap_y_px,
						relation_bias,
						closing_bias,
						aspect_bias);

			if (horizontal) {
				if (overlap_x_px <= slop_px)
					continue;

				if (cx2 > cx1 || (cx2 == cx1
							&& cw2->layout_index > cw1->layout_index)) {
					*xcons = add_separation_constraint(*xcons,
							cw1, cw2,
							body_width(cw1, total_width) + gapx - slopx);
				}
				else {
					*xcons = add_separation_constraint(*xcons,
							cw2, cw1,
							body_width(cw2, total_width) + gapx - slopx);
				}
			}
			else {
				if (overlap_y_px <= slop_px)
					continue;

				if (cy2 > cy1 || (cy2 == cy1
							&& cw2->layout_index > cw1->layout_index)) {
					*ycons = add_separation_constraint(*ycons,
							cw1, cw2,
							body_height(cw1, total_height) + gapy - slopy);
				}
				else {
					*ycons = add_separation_constraint(*ycons,
							cw2, cw1,
							body_height(cw2, total_height) + gapy - slopy);
				}
			}
		}
	}
}

static float
solve_axis_constraints(dlist *windows, dlist *constraints,
		unsigned int *total_width, unsigned int *total_height,
		bool solve_x,
		int max_iterations,
		float tolerance)
{
	if (!constraints)
		return 0;

	foreach_dlist (dlist_first(windows)) {
		ClientWin *cw = iter->data;

		if (solve_x)
			cw->fx = cw->vx;
		else
			cw->fy = cw->vy;
	}

	float max_violation = 0;

	for (int i = 0; i < max_iterations; i++) {
		max_violation = 0;

		foreach_dlist (dlist_first(constraints)) {
			SepConstraint *c = iter->data;
			ClientWin *before = c->before;
			ClientWin *after = c->after;

			float before_weight =
				body_mass(before, total_width, total_height);
			float after_weight =
				body_mass(after, total_width, total_height);

			float before_inv_weight = 1.0 / before_weight;
			float after_inv_weight = 1.0 / after_weight;

			float before_pos = solve_x ? before->fx : before->fy;
			float after_pos = solve_x ? after->fx : after->fy;

			float value = after_pos - before_pos - c->gap;
			float violation = -value;

			if (violation > max_violation)
				max_violation = violation;

			float denom = before_inv_weight + after_inv_weight;

			if (denom <= 0)
				continue;

			float new_lambda = c->lambda + violation / denom;

			if (new_lambda < 0)
				new_lambda = 0;

			float delta = new_lambda - c->lambda;

			if (delta == 0)
				continue;

			c->lambda = new_lambda;

			if (solve_x) {
				before->fx -= delta * before_inv_weight;
				after->fx += delta * after_inv_weight;
			}
			else {
				before->fy -= delta * before_inv_weight;
				after->fy += delta * after_inv_weight;
			}
		}

		if (max_violation <= tolerance)
			break;
	}

	return max_violation;
}

static float
max_residual_penetration_px(dlist *windows,
		unsigned int *total_width, unsigned int *total_height,
		float gapx, float gapy,
		float slop_px, float corner_slop_px)
{
	float max_residual = 0;

	for (dlist *iter = dlist_first(windows); iter; iter = iter->next) {
		ClientWin *cw1 = iter->data;

		for (dlist *jter = iter->next; jter; jter = jter->next) {
			ClientWin *cw2 = jter->data;
			float l1, t1, r1, b1;
			float l2, t2, r2, b2;

			padded_rect(cw1, total_width, total_height,
					gapx, gapy, &l1, &t1, &r1, &b1);
			padded_rect(cw2, total_width, total_height,
					gapx, gapy, &l2, &t2, &r2, &b2);

			float overlap_x = MIN(r1, r2) - MAX(l1, l2);
			float overlap_y = MIN(b1, b2) - MAX(t1, t2);

			if (overlap_x <= 0 || overlap_y <= 0)
				continue;

			float overlap_x_px = overlap_x * (float) *total_width;
			float overlap_y_px = overlap_y * (float) *total_height;

			if (corner_slop_px > 0
					&& overlap_x_px <= corner_slop_px
					&& overlap_y_px <= corner_slop_px)
				continue;

			float residual = MIN(overlap_x_px, overlap_y_px) - slop_px;

			if (residual > max_residual)
				max_residual = residual;
		}
	}

	return max_residual;
}

static float
resolve_separation_constraints(dlist *windows,
		unsigned int *total_width, unsigned int *total_height,
		float gapx, float gapy,
		int passes,
		float slop_px, float corner_slop_px,
		float relation_bias,
		float closing_bias,
		float target_residual_px,
		float aspect_bias)
{
	foreach_dlist (dlist_first(windows)) {
		ClientWin *cw = iter->data;
		cw->vx = cw->fx;
		cw->vy = cw->fy;
	}

	float residual =
		max_residual_penetration_px(windows,
				total_width, total_height,
				gapx, gapy,
				slop_px, corner_slop_px);

	float tolerance_x = 0.001 / (float) *total_width;
	float tolerance_y = 0.001 / (float) *total_height;

	int solver_iterations = 64 + 16 * dlist_len(windows);

	for (int pass = 0; pass < passes; pass++) {
		if (residual <= target_residual_px)
			break;

		dlist *xcons = NULL;
		dlist *ycons = NULL;

		build_separation_constraints(windows,
				&xcons, &ycons,
				total_width, total_height,
				gapx, gapy,
				slop_px, corner_slop_px,
				relation_bias, closing_bias,
				aspect_bias);

		if (!xcons && !ycons)
			break;

		if (xcons)
			solve_axis_constraints(windows, xcons,
					total_width, total_height,
					true,
					solver_iterations,
					tolerance_x);

		if (ycons)
			solve_axis_constraints(windows, ycons,
					total_width, total_height,
					false,
					solver_iterations,
					tolerance_y);

		free_constraint_list(xcons);
		free_constraint_list(ycons);

		float next_residual =
			max_residual_penetration_px(windows,
					total_width, total_height,
					gapx, gapy,
					slop_px, corner_slop_px);

		float improvement = residual - next_residual;
		residual = next_residual;

		if (improvement <= 0.001)
			break;
	}

	return residual;
}

static float
layout_compactness(dlist *windows,
		unsigned int *total_width, unsigned int *total_height,
		float aspect_bias)
{
	float energy = 0;
	float weight = 0;

	for (dlist *iter = dlist_first(windows); iter; iter = iter->next) {
		ClientWin *cw1 = iter->data;

		for (dlist *jter = iter->next; jter; jter = jter->next) {
			ClientWin *cw2 = jter->data;
			float x1, y1, x2, y2;

			body_center(cw1, &x1, &y1, total_width, total_height);
			body_center(cw2, &x2, &y2, total_width, total_height);

			float dx = (x2 - x1) / aspect_bias;
			float dy = (y2 - y1) * aspect_bias;

			float dist = sqrt(dx * dx + dy * dy + 1e-8);

			float m1 = body_mass(cw1, total_width, total_height);
			float m2 = body_mass(cw2, total_width, total_height);
			float w = m1 * m2;

			energy += w * dist;
			weight += w;
		}
	}

	if (weight <= 0)
		return 0;

	return energy / weight;
}

static void
apply_position_step(dlist *windows, float max_position_step)
{
	foreach_dlist (dlist_first(windows)) {
		ClientWin *cw = iter->data;
		float len = sqrt(cw->vx * cw->vx + cw->vy * cw->vy);

		if (len > max_position_step && len > 0) {
			cw->vx *= max_position_step / len;
			cw->vy *= max_position_step / len;
		}

		cw->fx += cw->vx;
		cw->fy += cw->vy;
	}
}

static void
apply_attraction_step(dlist *windows,
		unsigned int *total_width, unsigned int *total_height,
		float aspect_bias,
		float attraction_step,
		float max_position_step)
{
	foreach_dlist (dlist_first(windows)) {
		ClientWin *cw = iter->data;
		cw->vx = 0;
		cw->vy = 0;
	}

	for (dlist *iter = dlist_first(windows); iter; iter = iter->next) {
		ClientWin *cw1 = iter->data;

		for (dlist *jter = dlist_first(windows); jter; jter = jter->next) {
			ClientWin *cw2 = jter->data;
			float x1, y1, x2, y2;
			float ux, uy;

			if (cw1 == cw2)
				continue;

			body_center(cw1, &x1, &y1, total_width, total_height);
			body_center(cw2, &x2, &y2, total_width, total_height);

			unit_attraction(x2 - x1, y2 - y1, &ux, &uy);

			float m = body_mass(cw2, total_width, total_height);

			cw1->vx += attraction_step * m * ux / aspect_bias;
			cw1->vy += attraction_step * m * uy * aspect_bias;
		}
	}

	apply_position_step(windows, max_position_step);
}

static void
apply_repulsion_step(dlist *windows,
		unsigned int *total_width, unsigned int *total_height,
		float repulsion_step,
		float max_position_step)
{
	foreach_dlist (dlist_first(windows)) {
		ClientWin *cw = iter->data;
		cw->vx = 0;
		cw->vy = 0;
	}

	for (dlist *iter = dlist_first(windows); iter; iter = iter->next) {
		ClientWin *cw1 = iter->data;

		for (dlist *jter = dlist_first(windows); jter; jter = jter->next) {
			ClientWin *cw2 = jter->data;
			float x1, y1, x2, y2;
			float ux, uy;

			if (cw1 == cw2)
				continue;

			if (intersectArea(cw1, cw2,
						total_width, total_height) <= 0)
				continue;

			body_center(cw1, &x1, &y1, total_width, total_height);
			body_center(cw2, &x2, &y2, total_width, total_height);

			unit_attraction(x2 - x1, y2 - y1, &ux, &uy);

			float m = body_mass(cw2, total_width, total_height);

			cw1->vx -= repulsion_step * m * ux;
			cw1->vy -= repulsion_step * m * uy;
		}
	}

	apply_position_step(windows, max_position_step);
}

static void
save_positions(dlist *windows)
{
	foreach_dlist (dlist_first(windows)) {
		ClientWin *cw = iter->data;
		cw->fx2 = cw->fx;
		cw->fy2 = cw->fy;
	}
}

static void
save_best_positions(dlist *windows)
{
	foreach_dlist (dlist_first(windows)) {
		ClientWin *cw = iter->data;
		cw->best_fx = cw->fx;
		cw->best_fy = cw->fy;
	}
}

static void
restore_best_positions(dlist *windows)
{
	foreach_dlist (dlist_first(windows)) {
		ClientWin *cw = iter->data;
		cw->fx = cw->best_fx;
		cw->fy = cw->best_fy;
	}
}

static void
set_layout_indices(dlist *windows)
{
	int i = 0;

	foreach_dlist (dlist_first(windows)) {
		ClientWin *cw = iter->data;
		cw->layout_index = i++;
	}
}

void
layout_cosmos(MainWin *mw, dlist *windows,
		unsigned int *total_width, unsigned int *total_height)
{
	set_layout_indices(windows);

	const float aratio = (float) mw->width / (float) mw->height;
	const float aspect_bias = sqrt(aratio) / 1.4;

	const float attraction_step = 3e-2;
	const float repulsion_step = 1e-2;
	const float max_position_step = 0.05;

	const int expansion_projection_passes = 8;
	const int collapse_projection_passes = 16;

	const float expansion_slop_px = 0.0;
	const float expansion_corner_slop_px = 0.0;

	const float collision_slop_px = 0.10;
	const float collision_corner_slop_px = 0.25;
	const float residual_sleep_px = 0.25;

	const float relation_bias = 0.05;
	const float closing_bias = 0.02;

	const float scatter_center_threshold = 0.10;
	const float scatter_min_strength = 0.10;
	const float scatter_max_strength = 0.35;

	const int progress_window = 32;
	const int stable_windows_required = 2;
	const int max_collapse_iterations = 2000;

	const float compactness_sleep_px = 0.05;

	// convert pixel coordinates to normalized layout coordinates
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

		if (*total_width == 0)
			*total_width = 1;

		if (*total_height == 0)
			*total_height = 1;

		foreach_dlist (dlist_first(windows)) {
			ClientWin *cw = iter->data;

			cw->fx = (float) cw->x / (float) *total_width;
			cw->fy = (float) cw->y / (float) *total_height;
		}
	}

	const int distance = mw->distance;
	const float gapx = (float) distance / (float) *total_width;
	const float gapy = (float) distance / (float) *total_height;

	// scatter crowded centers into deterministic rectangular shell clouds
	scatter_crowded_centers(windows,
			total_width, total_height,
			scatter_center_threshold,
			scatter_min_strength,
			scatter_max_strength);

	// expansion
	{
		int iterations = 0;

		while (iterations < 1000) {
			float residual =
				max_residual_penetration_px(windows,
						total_width, total_height,
						gapx, gapy,
						expansion_slop_px,
						expansion_corner_slop_px);

			if (residual <= 0)
				break;

			save_positions(windows);

			apply_repulsion_step(windows,
					total_width, total_height,
					repulsion_step,
					max_position_step);

			resolve_separation_constraints(windows,
					total_width, total_height,
					gapx, gapy,
					expansion_projection_passes,
					expansion_slop_px,
					expansion_corner_slop_px,
					relation_bias,
					closing_bias,
					0.0,
					aspect_bias);


			iterations++;
		}

	}

	// contraction
	{
		int iterations = 0;
		int window_iterations = 0;
		int stable_windows = 0;
		bool done = false;

		float best_compactness =
			layout_compactness(windows,
					total_width, total_height,
					aspect_bias);

		float window_start_best_compactness = best_compactness;

		save_best_positions(windows);

		while (!done && iterations < max_collapse_iterations) {
			save_positions(windows);

			apply_attraction_step(windows,
					total_width, total_height,
					aspect_bias,
					attraction_step,
					max_position_step);

			float residual =
				resolve_separation_constraints(windows,
						total_width, total_height,
						gapx, gapy,
						collapse_projection_passes,
						collision_slop_px,
						collision_corner_slop_px,
						relation_bias,
						closing_bias,
						residual_sleep_px,
						aspect_bias);

			float compactness =
				layout_compactness(windows,
						total_width, total_height,
						aspect_bias);

			if (residual <= residual_sleep_px
					&& compactness < best_compactness) {
				best_compactness = compactness;
				save_best_positions(windows);
			}

			window_iterations++;

			if (window_iterations >= progress_window) {
				float progress_px =
					(window_start_best_compactness
					 - best_compactness)
					* (float) MAX(*total_width, *total_height);

				bool compactness_stable =
					progress_px <= compactness_sleep_px;

				bool residual_stable =
					residual <= residual_sleep_px;

				if (compactness_stable && residual_stable)
					stable_windows++;
				else
					stable_windows = 0;

				window_start_best_compactness = best_compactness;
				window_iterations = 0;

				if (stable_windows >= stable_windows_required)
					done = true;
			}

			iterations++;

		}

		restore_best_positions(windows);

		printfdf(false, "(): %d collapse iterations", iterations);
	}

	// convert normalized layout coordinates back to pixels
	{
		int minx = INT_MAX, maxx = INT_MIN;
		int miny = INT_MAX, maxy = INT_MIN;

		foreach_dlist (dlist_first(windows)) {
			ClientWin *cw = iter->data;

			cw->x = round_pixel(cw->fx * (float) *total_width);
			cw->y = round_pixel(cw->fy * (float) *total_height);

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
