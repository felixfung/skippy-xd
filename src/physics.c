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

inline bool
isIntersecting(ClientWin *cw1, ClientWin *cw2) {
	int dis = cw1->mainwin->distance / 2;
	int x1 = cw1->x, x2 = cw2->x;
	int y1 = cw1->y, y2 = cw2->y;
	int w1 = cw1->src.width, w2 = cw2->src.width;
	int h1 = cw1->src.height, h2 = cw2->src.height;
	return ((x2 - dis <= x1 && x1 < x2 + w2 + dis)
			|| (x1 - dis <= x2 && x2 < x1 + w1 + dis))
		&& ((y2 - dis <= y1 && y1 < y2 + h2 + dis)
			 || (y1 - dis <= y2 && y2 < y1 + h1 + dis));
}

inline void
com(ClientWin *cw, int *x, int *y) {
	*x = cw->x + cw->src.width / 2;
	*y = cw->y + cw->src.height / 2;
}

inline void
newPositionFromCollision(ClientWin *cw1, ClientWin *cw2,
		int *dx, int *dy) {
	if (!isIntersecting(cw1, cw2))
		return;

    int x1=0, y1=0;
    com(cw1, &x1, &y1);
    int x2=0, y2=0;
    com(cw2, &x2, &y2);

	// if two windows have the same centre of mass,
	// move in random direction
	if (x1 == x2 && y1 == y2) {
		*dx = rand() % 50;
		*dy = rand() % 50;
		return;
	}

	float norm = sqrt((float)((x1-x2)*(x1-x2) + (y1-y2)*(y1-y2)));
	*dx = (float)(x1 - x2) / (float)norm * 50.0;
	*dy = (float)(y1 - y2) / (float)norm * 50.0;
}
