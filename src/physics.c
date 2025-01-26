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
#include "physics.h"

unsigned int
intersectArea(ClientWin *cw1, ClientWin *cw2) {
	int dis = cw1->mainwin->distance / 2;
	int x1 = cw1->x - dis, x2 = cw2->x - dis;
	int y1 = cw1->y - dis, y2 = cw2->y - dis;
	int w1 = cw1->src.width + dis, w2 = cw2->src.width + dis;
	int h1 = cw1->src.height + dis, h2 = cw2->src.height + dis;

	int left   = MAX(x1, x2);
	int top    = MAX(y1, y2);
	int right  = MIN(x1 + w1, x2 + w2);
	int bottom = MIN(y1 + h1, y2 + h2);

	if (right < left || bottom < top)
		return 0;

	return (right - left) * (bottom - top);
}

bool
newPositionFromCollision(ClientWin *cw1, ClientWin *cw2,
		int *dx, int *dy, unsigned int *totalwidth, unsigned int *totalheight)
{
	if (intersectArea(cw1, cw2) == 0)
		return false;

	int dis = cw1->mainwin->distance / 2;
	int x1 = cw1->x - dis, x2 = cw2->x - dis;
	int y1 = cw1->y - dis, y2 = cw2->y - dis;
	//int w1 = cw1->src.width, w2 = cw2->src.width;
	//int h1 = cw1->src.height, h2 = cw2->src.height;

	int attempts = 0;
	bool axis_attempted[2];
	axis_attempted[0] = axis_attempted[1] = false;

	while (*dx == 0 && *dy == 0 && attempts < 2)
	{
		int axis = abs(x1-x2) > abs(y1-y2) ? 0 : 1;
		if (axis_attempted[axis])
			axis = !axis;
		axis_attempted[axis] = true;

		if (axis == 0)
			*dx = x1 > x2? dis: -dis;
		else
			*dy = y1 > y2? dis: -dis;

		x1 = cw1->x;
		y1 = cw1->y;
		int w1 = cw1->src.width;
		int h1 = cw1->src.height;

		if (x1 + *dx < 0)
			*dx = -x1;
		if (*totalwidth < x1 + *dx + w1)
			*dx = *totalwidth - x1 - w1;

		if (y1 + *dy < 0)
			*dy = -y1;
		if (*totalheight < y1 + *dy + h1)
			*dy = *totalheight - y1 - h1;

		attempts++;
	}

	return true;
}
