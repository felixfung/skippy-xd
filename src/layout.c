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

float
intersectArea(ClientWin *cw1, ClientWin *cw2,
		unsigned int *total_width, unsigned int *total_height)
{
	int dis = cw1->mainwin->distance / 2;

	float disx = (float)dis / (float)*total_width;
	float disy = (float)dis / (float)*total_height;

	float x1 = cw1->fx - disx;
	float y1 = cw1->fy - disy;
	float w1 = (float)cw1->src.width / (float)*total_width + 2 * disx;
	float h1 = (float)cw1->src.height / (float)*total_height + 2 * disy;

	float x2 = cw2->fx - disx;
	float y2 = cw2->fy - disy;
	float w2 = (float)cw2->src.width / (float)*total_width + 2 * disx;
	float h2 = (float)cw2->src.height / (float)*total_height + 2 * disy;

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

static float
bodyWidth(ClientWin *cw, unsigned int *total_width)
{
	return (float)cw->src.width / (float)*total_width;
}

static float
bodyHeight(ClientWin *cw, unsigned int *total_height)
{
	return (float)cw->src.height / (float)*total_height;
}

static float
bodyMass(ClientWin *cw,
		unsigned int *total_width, unsigned int *total_height)
{
	float m = (float)cw->src.width * (float)cw->src.height
		/ (float)*total_width / (float)*total_height;

	return safe_positive(m);
}

static void
bodyCenter(ClientWin *cw, float *x, float *y,
		unsigned int *total_width, unsigned int *total_height)
{
	*x = cw->fx + bodyWidth(cw, total_width) / 2.0;
	*y = cw->fy + bodyHeight(cw, total_height) / 2.0;
}

static void
bodyCenterAt(ClientWin *cw, float fx, float fy,
		float *x, float *y,
		unsigned int *total_width, unsigned int *total_height)
{
	*x = fx + bodyWidth(cw, total_width) / 2.0;
	*y = fy + bodyHeight(cw, total_height) / 2.0;
}

static void
paddedRect(ClientWin *cw,
		unsigned int *total_width, unsigned int *total_height,
		float gapx, float gapy,
		float *left, float *top, float *right, float *bottom)
{
	float w = bodyWidth(cw, total_width);
	float h = bodyHeight(cw, total_height);

	*left = cw->fx - gapx / 2.0;
	*right = cw->fx + w + gapx / 2.0;
	*top = cw->fy - gapy / 2.0;
	*bottom = cw->fy + h + gapy / 2.0;
}

static inline void
unitAttraction(float dx, float dy, float *ux, float *uy)
{
	const float soft = 0.05;

	float dist2 = dx * dx + dy * dy + soft * soft;
	float dist = sqrt(dist2);

	*ux = dx / dist;
	*uy = dy / dist;
}

static int
countWindows(dlist *windows)
{
	int n = 0;

	foreach_dlist (dlist_first(windows))
		n++;

	return n;
}

static void
collectWindows(dlist *windows, ClientWin **wins)
{
	int i = 0;

	foreach_dlist (dlist_first(windows))
		wins[i++] = iter->data;
}

static void
savePositions(ClientWin **wins, int n, float *fx, float *fy)
{
	for (int i = 0; i < n; i++) {
		fx[i] = wins[i]->fx;
		fy[i] = wins[i]->fy;
	}
}

static void
restorePositions(ClientWin **wins, int n, float *fx, float *fy)
{
	for (int i = 0; i < n; i++) {
		wins[i]->fx = fx[i];
		wins[i]->fy = fy[i];
	}
}

typedef struct {
	int before;
	int after;
	float gap;
} SepConstraint;

static bool
chooseHorizontalConstraint(ClientWin **wins,
		int i, int j,
		float *old_fx, float *old_fy,
		unsigned int *total_width, unsigned int *total_height,
		float gapx, float gapy,
		float overlapXpx, float overlapYpx,
		float relation_bias,
		float closing_bias)
{
	float cx1, cy1, cx2, cy2;
	float old_cx1, old_cy1, old_cx2, old_cy2;

	bodyCenter(wins[i], &cx1, &cy1, total_width, total_height);
	bodyCenter(wins[j], &cx2, &cy2, total_width, total_height);

	bodyCenterAt(wins[i], old_fx[i], old_fy[i],
			&old_cx1, &old_cy1, total_width, total_height);
	bodyCenterAt(wins[j], old_fx[j], old_fy[j],
			&old_cx2, &old_cy2, total_width, total_height);

	float required_x =
		(bodyWidth(wins[i], total_width)
		 + bodyWidth(wins[j], total_width)) / 2.0 + gapx;

	float required_y =
		(bodyHeight(wins[i], total_height)
		 + bodyHeight(wins[j], total_height)) / 2.0 + gapy;

	required_x = safe_positive(required_x);
	required_y = safe_positive(required_y);

	float relation_x = f_abs(cx2 - cx1) / required_x;
	float relation_y = f_abs(cy2 - cy1) / required_y;

	if (relation_x > relation_y + relation_bias)
		return true;

	if (relation_y > relation_x + relation_bias)
		return false;

	float old_dx = old_cx2 - old_cx1;
	float old_dy = old_cy2 - old_cy1;
	float new_dx = cx2 - cx1;
	float new_dy = cy2 - cy1;

	float closing_x = (f_abs(old_dx) - f_abs(new_dx)) / required_x;
	float closing_y = (f_abs(old_dy) - f_abs(new_dy)) / required_y;

	if (closing_x > closing_y + closing_bias)
		return true;

	if (closing_y > closing_x + closing_bias)
		return false;

	if (overlapXpx < overlapYpx)
		return true;

	if (overlapYpx < overlapXpx)
		return false;

	return ((i + j) & 1) == 0;
}

static void
buildSeparationConstraints(ClientWin **wins, int n,
		float *old_fx, float *old_fy,
		SepConstraint *xcons, int *xcount,
		SepConstraint *ycons, int *ycount,
		unsigned int *total_width, unsigned int *total_height,
		float gapx, float gapy,
		float slop_px, float corner_slop_px,
		float relation_bias,
		float closing_bias)
{
	*xcount = 0;
	*ycount = 0;

	float slopx = slop_px / (float)*total_width;
	float slopy = slop_px / (float)*total_height;

	for (int i = 0; i < n; i++) {
		for (int j = i + 1; j < n; j++) {
			float l1, t1, r1, b1;
			float l2, t2, r2, b2;

			paddedRect(wins[i], total_width, total_height,
					gapx, gapy, &l1, &t1, &r1, &b1);
			paddedRect(wins[j], total_width, total_height,
					gapx, gapy, &l2, &t2, &r2, &b2);

			float overlap_x = MIN(r1, r2) - MAX(l1, l2);
			float overlap_y = MIN(b1, b2) - MAX(t1, t2);

			if (overlap_x <= 0 || overlap_y <= 0)
				continue;

			float overlap_x_px = overlap_x * (float)*total_width;
			float overlap_y_px = overlap_y * (float)*total_height;

			if (corner_slop_px > 0
					&& overlap_x_px <= corner_slop_px
					&& overlap_y_px <= corner_slop_px)
				continue;

			float cx1, cy1, cx2, cy2;

			bodyCenter(wins[i], &cx1, &cy1,
					total_width, total_height);
			bodyCenter(wins[j], &cx2, &cy2,
					total_width, total_height);

			bool horizontal =
				chooseHorizontalConstraint(wins,
						i, j,
						old_fx, old_fy,
						total_width, total_height,
						gapx, gapy,
						overlap_x_px, overlap_y_px,
						relation_bias,
						closing_bias);

			if (horizontal) {
				if (overlap_x_px <= slop_px)
					continue;

				SepConstraint *c = &xcons[(*xcount)++];

				if (cx2 > cx1 || (cx2 == cx1 && j > i)) {
					c->before = i;
					c->after = j;
					c->gap = bodyWidth(wins[i], total_width)
						+ gapx - slopx;
				} else {
					c->before = j;
					c->after = i;
					c->gap = bodyWidth(wins[j], total_width)
						+ gapx - slopx;
				}

				c->gap = MAX(c->gap, 0);
			} else {
				if (overlap_y_px <= slop_px)
					continue;

				SepConstraint *c = &ycons[(*ycount)++];

				if (cy2 > cy1 || (cy2 == cy1 && j > i)) {
					c->before = i;
					c->after = j;
					c->gap = bodyHeight(wins[i], total_height)
						+ gapy - slopy;
				} else {
					c->before = j;
					c->after = i;
					c->gap = bodyHeight(wins[j], total_height)
						+ gapy - slopy;
				}

				c->gap = MAX(c->gap, 0);
			}
		}
	}
}

static float
solveAxisConstraints(ClientWin **wins, int n,
		SepConstraint *cons, int count,
		unsigned int *total_width, unsigned int *total_height,
		bool solve_x,
		float *desired,
		float *position,
		float *lambda,
		int max_iterations,
		float tolerance)
{
	if (count <= 0)
		return 0;

	for (int i = 0; i < n; i++)
		position[i] = desired[i];

	for (int i = 0; i < count; i++)
		lambda[i] = 0;

	float max_violation = 0;

	for (int iter = 0; iter < max_iterations; iter++) {
		max_violation = 0;

		for (int ci = 0; ci < count; ci++) {
			SepConstraint *c = &cons[ci];

			int before = c->before;
			int after = c->after;

			float before_weight =
				bodyMass(wins[before], total_width, total_height);
			float after_weight =
				bodyMass(wins[after], total_width, total_height);

			float before_inv_weight = 1.0 / before_weight;
			float after_inv_weight = 1.0 / after_weight;

			float value =
				position[after] - position[before] - c->gap;

			float violation = -value;

			if (violation > max_violation)
				max_violation = violation;

			float denom = before_inv_weight + after_inv_weight;

			if (denom <= 0)
				continue;

			float new_lambda = lambda[ci] + violation / denom;

			if (new_lambda < 0)
				new_lambda = 0;

			float delta = new_lambda - lambda[ci];

			if (delta == 0)
				continue;

			lambda[ci] = new_lambda;

			position[before] -= delta * before_inv_weight;
			position[after] += delta * after_inv_weight;
		}

		if (max_violation <= tolerance)
			break;
	}

	for (int i = 0; i < n; i++) {
		if (solve_x)
			wins[i]->fx = position[i];
		else
			wins[i]->fy = position[i];
	}

	return max_violation;
}

static float
maxResidualPenetrationPx(ClientWin **wins, int n,
		unsigned int *total_width, unsigned int *total_height,
		float gapx, float gapy,
		float slop_px, float corner_slop_px)
{
	float max_residual = 0;

	for (int i = 0; i < n; i++) {
		for (int j = i + 1; j < n; j++) {
			float l1, t1, r1, b1;
			float l2, t2, r2, b2;

			paddedRect(wins[i], total_width, total_height,
					gapx, gapy, &l1, &t1, &r1, &b1);
			paddedRect(wins[j], total_width, total_height,
					gapx, gapy, &l2, &t2, &r2, &b2);

			float overlap_x = MIN(r1, r2) - MAX(l1, l2);
			float overlap_y = MIN(b1, b2) - MAX(t1, t2);

			if (overlap_x <= 0 || overlap_y <= 0)
				continue;

			float overlap_x_px = overlap_x * (float)*total_width;
			float overlap_y_px = overlap_y * (float)*total_height;

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
resolveSeparationConstraints(ClientWin **wins, int n,
		float *old_fx, float *old_fy,
		SepConstraint *xcons, SepConstraint *ycons,
		unsigned int *total_width, unsigned int *total_height,
		float gapx, float gapy,
		int passes,
		float slop_px, float corner_slop_px,
		float relation_bias,
		float closing_bias,
		float target_residual_px,
		float *desired_x,
		float *desired_y,
		float *axis_position,
		float *lambda)
{
	for (int i = 0; i < n; i++) {
		desired_x[i] = wins[i]->fx;
		desired_y[i] = wins[i]->fy;
	}

	float residual =
		maxResidualPenetrationPx(wins, n,
				total_width, total_height,
				gapx, gapy,
				slop_px, corner_slop_px);

	float tolerance_x = 0.001 / (float)*total_width;
	float tolerance_y = 0.001 / (float)*total_height;

	int solver_iterations = 64 + 16 * n;

	for (int pass = 0; pass < passes; pass++) {
		if (residual <= target_residual_px)
			break;

		int xcount = 0;
		int ycount = 0;

		buildSeparationConstraints(wins, n,
				old_fx, old_fy,
				xcons, &xcount,
				ycons, &ycount,
				total_width, total_height,
				gapx, gapy,
				slop_px, corner_slop_px,
				relation_bias, closing_bias);

		if (xcount == 0 && ycount == 0)
			break;

		if (xcount > 0) {
			solveAxisConstraints(wins, n,
					xcons, xcount,
					total_width, total_height,
					true,
					desired_x,
					axis_position,
					lambda,
					solver_iterations,
					tolerance_x);
		}

		if (ycount > 0) {
			solveAxisConstraints(wins, n,
					ycons, ycount,
					total_width, total_height,
					false,
					desired_y,
					axis_position,
					lambda,
					solver_iterations,
					tolerance_y);
		}

		float next_residual =
			maxResidualPenetrationPx(wins, n,
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
layoutCompactness(ClientWin **wins, int n,
		unsigned int *total_width, unsigned int *total_height,
		float aratio)
{
	float energy = 0;
	float weight = 0;

	for (int i = 0; i < n; i++) {
		for (int j = i + 1; j < n; j++) {
			float x1, y1, x2, y2;

			bodyCenter(wins[i], &x1, &y1,
					total_width, total_height);
			bodyCenter(wins[j], &x2, &y2,
					total_width, total_height);

			float dx = x2 - x1;
			float dy = (y2 - y1) / aratio;

			float dist = sqrt(dx * dx + dy * dy + 1e-8);

			float m1 = bodyMass(wins[i], total_width, total_height);
			float m2 = bodyMass(wins[j], total_width, total_height);
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
applyPositionStep(ClientWin **wins, int n,
		float *step_x, float *step_y,
		float max_position_step)
{
	for (int i = 0; i < n; i++) {
		float len = sqrt(step_x[i] * step_x[i]
				+ step_y[i] * step_y[i]);

		if (len > max_position_step && len > 0) {
			step_x[i] *= max_position_step / len;
			step_y[i] *= max_position_step / len;
		}

		wins[i]->fx += step_x[i];
		wins[i]->fy += step_y[i];
	}
}

static void
applyAttractionStep(ClientWin **wins, int n,
		unsigned int *total_width, unsigned int *total_height,
		float aratio,
		float attraction_step,
		float max_position_step,
		float *step_x, float *step_y)
{
	for (int i = 0; i < n; i++) {
		step_x[i] = 0;
		step_y[i] = 0;
	}

	for (int i = 0; i < n; i++) {
		for (int j = 0; j < n; j++) {
			if (i == j)
				continue;

			float x1, y1, x2, y2;
			float ux, uy;

			bodyCenter(wins[i], &x1, &y1,
					total_width, total_height);
			bodyCenter(wins[j], &x2, &y2,
					total_width, total_height);

			unitAttraction(x2 - x1, y2 - y1, &ux, &uy);

			float m = bodyMass(wins[j],
					total_width, total_height);

			step_x[i] += attraction_step * m * ux;
			step_y[i] += attraction_step * m * uy / aratio;
		}
	}

	applyPositionStep(wins, n, step_x, step_y, max_position_step);
}

static void
applyRepulsionStep(ClientWin **wins, int n,
		unsigned int *total_width, unsigned int *total_height,
		float aratio,
		float repulsion_step,
		float max_position_step,
		float *step_x, float *step_y)
{
	for (int i = 0; i < n; i++) {
		step_x[i] = 0;
		step_y[i] = 0;
	}

	for (int i = 0; i < n; i++) {
		for (int j = 0; j < n; j++) {
			if (i == j)
				continue;

			if (intersectArea(wins[i], wins[j],
						total_width, total_height) <= 0)
				continue;

			float x1, y1, x2, y2;
			float ux, uy;

			bodyCenter(wins[i], &x1, &y1,
					total_width, total_height);
			bodyCenter(wins[j], &x2, &y2,
					total_width, total_height);

			unitAttraction(x2 - x1, y2 - y1, &ux, &uy);

			float m = bodyMass(wins[j],
					total_width, total_height);

			step_x[i] -= repulsion_step * m * ux;
			step_y[i] -= repulsion_step * m * uy / aratio;
		}
	}

	applyPositionStep(wins, n, step_x, step_y, max_position_step);
}

void
layout_cosmos(MainWin *mw, dlist *windows,
		unsigned int *total_width, unsigned int *total_height)
{
	int n = countWindows(windows);

	if (n <= 0)
		return;

	const float aratio = (float)mw->width / (float)mw->height;

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

			cw->fx = (float)cw->x / (float)*total_width;
			cw->fy = (float)cw->y / (float)*total_height;
		}
	}

	const int distance = mw->distance;
	const float gapx = (float)distance / (float)*total_width;
	const float gapy = (float)distance / (float)*total_height;

	ClientWin **wins = calloc(n, sizeof(*wins));

	float *step_x = calloc(n, sizeof(*step_x));
	float *step_y = calloc(n, sizeof(*step_y));

	float *old_fx = calloc(n, sizeof(*old_fx));
	float *old_fy = calloc(n, sizeof(*old_fy));

	float *desired_x = calloc(n, sizeof(*desired_x));
	float *desired_y = calloc(n, sizeof(*desired_y));
	float *axis_position = calloc(n, sizeof(*axis_position));

	int max_constraints = n * (n - 1) / 2;

	if (max_constraints < 1)
		max_constraints = 1;

	SepConstraint *xcons = calloc(max_constraints, sizeof(*xcons));
	SepConstraint *ycons = calloc(max_constraints, sizeof(*ycons));
	float *lambda = calloc(max_constraints, sizeof(*lambda));

	collectWindows(windows, wins);

	// scatter windows with nearly identical centers
	{
		srand(0);

		int iterations = -1;
		bool colliding = true;

		while (colliding && iterations <= 1000) {
			colliding = false;

			for (int i = 0; i < n; i++) {
				for (int j = 0; j < n; j++) {
					if (i == j)
						continue;

					float x1, y1, x2, y2;

					bodyCenter(wins[i], &x1, &y1,
							total_width, total_height);
					bodyCenter(wins[j], &x2, &y2,
							total_width, total_height);

					float delta = 0.1;

					if (f_abs(x2 - x1) <= delta
							&& f_abs(y2 - y1) <= delta) {
						colliding = true;

						float randx =
							(float)rand()
							/ (float)(RAND_MAX / delta / 2)
							- delta;

						float randy =
							(float)rand()
							/ (float)(RAND_MAX / delta / 2)
							- delta;

						wins[i]->fx += randx;
						wins[i]->fy += randy;
					}
				}
			}

			iterations++;
		}

		printfdf(false,
				"(): %d iterations to resolve identical COM",
				iterations);
		printfdf(false, "():");
	}

	// expansion
	{
		int iterations = 0;

		while (iterations < 1000) {
			float residual =
				maxResidualPenetrationPx(wins, n,
						total_width, total_height,
						gapx, gapy,
						expansion_slop_px,
						expansion_corner_slop_px);

			if (residual <= 0)
				break;

			savePositions(wins, n, old_fx, old_fy);

			applyRepulsionStep(wins, n,
					total_width, total_height,
					aratio,
					repulsion_step,
					max_position_step,
					step_x, step_y);

			resolveSeparationConstraints(wins, n,
					old_fx, old_fy,
					xcons, ycons,
					total_width, total_height,
					gapx, gapy,
					expansion_projection_passes,
					expansion_slop_px,
					expansion_corner_slop_px,
					relation_bias,
					closing_bias,
					0.0,
					desired_x,
					desired_y,
					axis_position,
					lambda);

			printfdf(false, "():");

			iterations++;
		}

		printfdf(false, "(): %d expansion iterations", iterations);
		printfdf(false, "():");
	}

	// contraction
	{
		int iterations = 0;
		int window_iterations = 0;
		int stable_windows = 0;
		bool done = false;

		float *best_fx = calloc(n, sizeof(*best_fx));
		float *best_fy = calloc(n, sizeof(*best_fy));

		float best_compactness =
			layoutCompactness(wins, n,
					total_width, total_height,
					aratio);

		float window_start_best_compactness = best_compactness;

		savePositions(wins, n, best_fx, best_fy);

		while (!done && iterations < max_collapse_iterations) {
			savePositions(wins, n, old_fx, old_fy);

			applyAttractionStep(wins, n,
					total_width, total_height,
					aratio,
					attraction_step,
					max_position_step,
					step_x, step_y);

			float residual =
				resolveSeparationConstraints(wins, n,
						old_fx, old_fy,
						xcons, ycons,
						total_width, total_height,
						gapx, gapy,
						collapse_projection_passes,
						collision_slop_px,
						collision_corner_slop_px,
						relation_bias,
						closing_bias,
						residual_sleep_px,
						desired_x,
						desired_y,
						axis_position,
						lambda);

			float compactness =
				layoutCompactness(wins, n,
						total_width, total_height,
						aratio);

			if (residual <= residual_sleep_px
					&& compactness < best_compactness) {
				best_compactness = compactness;
				savePositions(wins, n, best_fx, best_fy);
			}

			window_iterations++;

			if (window_iterations >= progress_window) {
				float progress_px =
					(window_start_best_compactness
					 - best_compactness)
					* (float)MAX(*total_width, *total_height);

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

			for (int i = 0; i < n; i++) {
				printfdf(true, "(): %p -> (%f,%f)",
						wins[i]->wid_client,
						wins[i]->fx,
						wins[i]->fy);
			}

			printfdf(true,
					"(): compactness=%f best=%f residual_px=%f window_iter=%d stable_windows=%d",
					compactness,
					best_compactness,
					residual,
					window_iterations,
					stable_windows);

			printfdf(true, "():");
			fflush(stdout);
		}

		restorePositions(wins, n, best_fx, best_fy);

		free(best_fx);
		free(best_fy);

		printfdf(true, "(): %d collapse iterations", iterations);
		printfdf(true, "():");
	}

	// convert normalized layout coordinates back to pixels
	{
		int minx = INT_MAX, maxx = INT_MIN;
		int miny = INT_MAX, maxy = INT_MIN;

		for (int i = 0; i < n; i++) {
			ClientWin *cw = wins[i];

			cw->x = (float)cw->fx * (float)*total_width;
			cw->y = (float)cw->fy * (float)*total_height;

			minx = MIN(minx, cw->x);
			maxx = MAX(maxx, cw->x + cw->src.width);
			miny = MIN(miny, cw->y);
			maxy = MAX(maxy, cw->y + cw->src.height);
		}

		for (int i = 0; i < n; i++) {
			ClientWin *cw = wins[i];

			cw->x -= minx;
			cw->y -= miny;
		}

		*total_width = maxx - minx;
		*total_height = maxy - miny;
	}

	free(step_x);
	free(step_y);
	free(old_fx);
	free(old_fy);
	free(desired_x);
	free(desired_y);
	free(axis_position);
	free(xcons);
	free(ycons);
	free(lambda);
	free(wins);
}
