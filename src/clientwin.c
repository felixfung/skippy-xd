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

#define INTERSECTS(x1, y1, w1, h1, x2, y2, w2, h2) \
	(((x1 >= x2 && x1 < (x2 + w2)) || (x2 >= x1 && x2 < (x1 + w1))) && \
	 ((y1 >= y2 && y1 < (y2 + h2)) || (y2 >= y1 && y2 < (y1 + h1))))

static int
clientwin_action(ClientWin *cw, enum cliop action);

int
clientwin_cmp_func(dlist *l, void *data) {
	ClientWin *cw = (ClientWin *) l->data;
	return cw->src.window == (Window) data
		|| cw->wid_client == (Window) data
		|| cw == (ClientWin *) data;
}

void clientwin_round_corners(ClientWin *cw);

int
clientwin_validate_panel(dlist *l, void *data) {
	ClientWin *cw = l->data;
	session_t *ps = cw->mainwin->ps;
	cw->paneltype = wm_identify_panel(ps, cw->wid_client);
	return cw->paneltype == WINTYPE_PANEL? ps->o.panel_show:
			cw->paneltype == WINTYPE_DESKTOP? ps->o.panel_show_desktop:
			0;
}

int
clientwin_filter_func(dlist *l, void *data) {
	ClientWin *cw = l->data;
	MainWin *mw = cw->mainwin;
	session_t *ps = mw->ps;

#ifdef CFG_XINERAMA
	if (mw->xin_active && !INTERSECTS(cw->src.x, cw->src.y, cw->src.width,
				cw->src.height, mw->xin_active->x_org, mw->xin_active->y_org,
				mw->xin_active->width, mw->xin_active->height))
		return false;
#endif

	CARD32 current_desktop = (*(CARD32 *)data);
	CARD32 w_desktop = wm_get_window_desktop(ps, cw->wid_client);
	bool filtered_in = true;

	if (w_desktop == -1) {
		if (ps->o.mode == PROGMODE_SWITCH)
			filtered_in = true;
		else
			filtered_in = ps->o.showSticky;
	}
	else {
		if (ps->o.mode == PROGMODE_SWITCH)
			filtered_in = (w_desktop == current_desktop)
				|| ps->o.switchShowAllDesktops;
		else if (ps->o.mode == PROGMODE_EXPOSE)
			filtered_in = (w_desktop == current_desktop)
				|| ps->o.exposeShowAllDesktops;
		if (ps->o.mode == PROGMODE_PAGING)
			filtered_in = true;
	}

	if (filtered_in)
		return wm_validate_window(mw->ps, cw->wid_client);
	else
		return false;
}

int
clientwin_check_group_leader_func(dlist *l, void *data)
{
	ClientWin *cw = (ClientWin *)l->data;
	return wm_get_group_leader(cw->mainwin->ps->dpy, cw->wid_client) == *((Window*)data);
}

int
clientwin_sort_func(dlist* a, dlist* b, void* data)
{
	unsigned int pa = ((ClientWin *) a->data)->src.x * ((ClientWin *) a->data)->src.y,
	             pb = ((ClientWin *) b->data)->src.x * ((ClientWin *) b->data)->src.y;
	return (pa < pb) ? -1 : (pa == pb) ? 0 : 1;
}

ClientWin *
clientwin_create(MainWin *mw, Window client) {
	session_t *ps = mw->ps;
	ClientWin *cw = NULL;

	XWindowAttributes attr = { };
	XGetWindowAttributes(ps->dpy, client, &attr);

	cw = smalloc(1, ClientWin);
	{
		static const ClientWin CLIENTWT_DEF = CLIENTWT_INIT;
		memcpy(cw, &CLIENTWT_DEF, sizeof(ClientWin));
	}

    cw->slots = 0;
	cw->mainwin = mw;
	cw->wid_client = client;
	cw->origin = None;
	cw->destination = None;
	cw->shadow = None;
	cw->pixmap = None;
	cw->cpixmap = None;

	if (ps->o.includeFrame)
		cw->src.window = wm_find_frame(ps, client);
	if (!cw->src.window)
		cw->src.window = client;
	cw->mini.format = mw->format;

	{
		XSetWindowAttributes sattr = {
			.border_pixel = 0,
			.background_pixel = 0,
			.colormap = mw->colormap,
			.event_mask = ButtonPressMask | ButtonReleaseMask | KeyPressMask
				| KeyReleaseMask | PointerMotionMask | FocusChangeMask,
			.override_redirect = !ps->o.pseudoTrans,
		};
		cw->mini.window = XCreateWindow(ps->dpy,
				(ps->o.pseudoTrans ? mw->window : ps->root), 0, 0, 1, 1, 0,
				mw->depth, InputOutput, mw->visual,
				CWColormap | CWBackPixel | CWBorderPixel | CWEventMask | CWOverrideRedirect, &sattr);
	}
	if (!cw->mini.window)
		goto clientwin_create_err;

	{
		static const char *PREFIX = "mini window of ";
		const int len = strlen(PREFIX) + 20;
		char *str = allocchk(malloc(len));
		snprintf(str, len, "%s%#010lx", PREFIX, cw->src.window);
		wm_wid_set_info(cw->mainwin->ps, cw->mini.window, str, None);
		free(str);
	}

	// Listen to events on the window. We don't want to miss any changes so
	// this is to be done as early as possible
	//XSelectInput(cw->mainwin->ps->dpy, cw->src.window, SubstructureNotifyMask | StructureNotifyMask);

	return cw;

clientwin_create_err:
	if (cw)
		clientwin_destroy(cw, False);

	return NULL;
}

client_disp_mode_t
clientwin_get_disp_mode(session_t *ps, ClientWin *cw, bool isViewable) {
	for (client_disp_mode_t *p = ps->o.clientDisplayModes; *p; p++) {
		switch (*p) {
			case CLIDISP_THUMBNAIL_ICON:
				if (isViewable && cw->origin && cw->icon_pict)
					return *p;
				break;
			case CLIDISP_THUMBNAIL:
				if (isViewable && cw->origin)
					return *p;
				break;
			case CLIDISP_ZOMBIE_ICON:
				if (cw->shadow && cw->icon_pict != NULL)
					return *p;
				break;
			case CLIDISP_ZOMBIE:
				if (cw->shadow) return *p;
				break;
			case CLIDISP_ICON:
				if (cw->icon_pict) return *p;
				break;
			case CLIDISP_FILLED:
			case CLIDISP_NONE:
				return *p;
			case CLIDISP_DESKTOP:
				return *p;
		}
	}

	return CLIDISP_NONE;
}

/**
 * @brief Update window data to prepare for rendering.
 */
bool
clientwin_update(ClientWin *cw) {
	session_t *ps = cw->mainwin->ps;
	clientwin_free_res2(ps, cw);

	// Get window attributes
	XWindowAttributes wattr = { };
	XGetWindowAttributes(ps->dpy, cw->src.window, &wattr);

	{
		Window tmpwin = None;
		XTranslateCoordinates(ps->dpy, cw->src.window, wattr.root,
				-wattr.border_width, -wattr.border_width,
				&cw->src.x, &cw->src.y, &tmpwin);

		if (wattr.width == 0 && wattr.height == 0) {
			XGetWindowAttributes(ps->dpy, cw->wid_client, &wattr);
			XTranslateCoordinates(ps->dpy, cw->wid_client, wattr.root,
					-wattr.border_width, -wattr.border_width,
					&cw->src.x, &cw->src.y, &tmpwin);
		}
	}

	cw->src.width = wattr.width;
	cw->src.height = wattr.height;

	bool isViewable = wattr.map_state == IsViewable;
	cw->zombie = !isViewable;

	cw->src.format = XRenderFindVisualFormat(ps->dpy, wattr.visual);

	if (ps->o.tooltip_show && !cw->tooltip)
		cw->tooltip = tooltip_create(cw->mainwin);

	if (isViewable) {
		static XRenderPictureAttributes pa = { .subwindow_mode = IncludeInferiors };

		if (cw->origin)
			free_picture(ps, &cw->origin);
		cw->origin = XRenderCreatePicture(ps->dpy,
				cw->src.window, cw->src.format, CPSubwindowMode, &pa);
		XRenderSetPictureFilter(ps->dpy, cw->origin, FilterBest, 0, 0);

		if (!cw->redirected) {
			XCompositeRedirectWindow(ps->dpy, cw->src.window,
					CompositeRedirectAutomatic);
			cw->redirected = true;
		}

		if (cw->cpixmap)
			free_pixmap(ps, &cw->cpixmap);
		cw->cpixmap = XCompositeNameWindowPixmap(ps->dpy, cw->src.window);

		if (cw->shadow)
			free_picture(ps, &cw->shadow);
		cw->shadow = XRenderCreatePicture(ps->dpy,
			cw->cpixmap, cw->src.format, CPSubwindowMode, &pa);
		XRenderSetPictureFilter(ps->dpy, cw->shadow, FilterBest, 0, 0);
	}

	// Get window icon
	if (cw->icon_pict)
		free_pictw(ps, &cw->icon_pict);
	cw->icon_pict = simg_load_icon(ps, cw->wid_client, ps->o.iconSize);
	if (!cw->icon_pict && ps->o.iconDefault)
		cw->icon_pict = clone_pictw(ps, ps->o.iconDefault);

	// Get window icon for filler
	if (cw->icon_pict_filler)
		free_pictw(ps, &cw->icon_pict_filler);
	cw->icon_pict_filler = simg_load_icon(ps, cw->wid_client, ps->o.fillerIconSize);
	if (!cw->icon_pict_filler && ps->o.iconFiller)
		cw->icon_pict_filler = clone_pictw(ps, ps->o.iconFiller);

	// modes are CLIDISP_THUMBNAIL_ICON, CLIDISP_THUMBNAIL, CLIDISP_ZOMBIE,
	// CLIDISP_ZOMBIE_ICON, CLIDISP_ICON, CLIDISP_FILLED, CLIDISP_NONE
	// if we ever got a thumbnail for the window,
	// the mode for that window always will be thumbnail
	cw->mode = clientwin_get_disp_mode(ps, cw, isViewable);
	printfdf(false, "(): (%#010lx): %d", cw->wid_client, cw->mode);

	return true;
}

static inline bool
clientwin_update2_desktop(session_t *ps, MainWin *mw, ClientWin *cw) {
	if (cw->pict_filled)
		free_pictw(ps, &cw->pict_filled);
	cw->pict_filled = simg_postprocess(ps,
			clone_pictw(ps, ps->o.fillSpec.img),
			ps->o.fillSpec.mode,
			1, 1,
			ps->o.fillSpec.alg, ps->o.fillSpec.valg,
			&ps->o.fillSpec.c);
	return cw->pict_filled;
}

static inline bool
clientwin_update2_filled(session_t *ps, MainWin *mw, ClientWin *cw) {
	if (cw->pict_filled)
		free_pictw(ps, &cw->pict_filled);
	cw->pict_filled = simg_postprocess(ps,
			clone_pictw(ps, ps->o.fillSpec.img),
			ps->o.fillSpec.mode,
			cw->mini.width, cw->mini.height,
			ps->o.fillSpec.alg, ps->o.fillSpec.valg,
			&ps->o.fillSpec.c);
	return cw->pict_filled;
}

static inline bool
clientwin_update2_icon(session_t *ps, MainWin *mw, ClientWin *cw) {
	if (cw->icon_pict_filled)
		free_pictw(ps, &cw->icon_pict_filled);

	cw->icon_pict_filled = simg_postprocess(ps,
			clone_pictw(ps, cw->icon_pict_filler),
			ps->o.iconFillSpec.mode,
			cw->mini.width, cw->mini.height,
			ps->o.iconFillSpec.alg, ps->o.iconFillSpec.valg,
			&ps->o.iconFillSpec.c);
	return cw->icon_pict_filled;
}

bool
clientwin_update2(ClientWin *cw) {
	MainWin *mw = cw->mainwin;
	session_t *ps = mw->ps;

	clientwin_free_res2(ps, cw);

	switch (cw->mode) {
		case CLIDISP_DESKTOP:
			if (!ps->o.pseudoTrans)
				clientwin_update2_desktop(ps, mw, cw);
			break;
		case CLIDISP_NONE:
			break;
		case CLIDISP_FILLED:
			clientwin_update2_filled(ps, mw, cw);
			break;
		case CLIDISP_ICON:
			clientwin_update2_icon(ps, mw, cw);
			break;
		case CLIDISP_ZOMBIE:
		case CLIDISP_ZOMBIE_ICON:
		case CLIDISP_THUMBNAIL:
		case CLIDISP_THUMBNAIL_ICON:
			break;
	}

	return true;
}

void
clientwin_destroy(ClientWin *cw, bool destroyed) {
	MainWin *mw = cw->mainwin;
	session_t * const ps = mw->ps;

	free_picture(ps, &cw->origin);
	free_picture(ps, &cw->destination);
	free_picture(ps, &cw->shadow);
	free_pixmap(ps, &cw->pixmap);
	free_pixmap(ps, &cw->cpixmap);
	free_pictw(ps, &cw->icon_pict);
	free_pictw(ps, &cw->icon_pict_filler);
	free_pictw(ps, &cw->icon_pict_filled);
	free_pictw(ps, &cw->pict_filled);

	if (cw->tooltip)
		tooltip_destroy(cw->tooltip);

	if (cw->src.window && !destroyed) {
		free_damage(ps, &cw->damage);
		// Stop listening to events, this should be safe because we don't
		// monitor window re-map anyway
		XSelectInput(ps->dpy, cw->src.window, 0);

		if (cw->redirected)
			XCompositeUnredirectWindow(ps->dpy, cw->src.window, CompositeRedirectAutomatic);
	}

	if (cw->mini.window) {
		// Somehow it doesn't work without unmapping firstly
		XUnmapWindow(ps->dpy, cw->mini.window);
		XDestroyWindow(ps->dpy, cw->mini.window);
	}

	free(cw);
}

static void
clientwin_repaint(ClientWin *cw, const XRectangle *pbound)
{
	session_t *ps = cw->mainwin->ps;
	Picture source = None;
	int s_x = 0, s_y = 0, s_w = cw->mini.width, s_h = cw->mini.height;
	if (pbound) {
		s_x = pbound->x;
		s_y = pbound->y;
		s_w = pbound->width;
		s_h = pbound->height;
	}

	switch (cw->mode) {
		case CLIDISP_DESKTOP:
			source = cw->origin;
			break;
		case CLIDISP_NONE:
			break;
		case CLIDISP_FILLED:
			if (!cw->pict_filled) return;
			source = cw->pict_filled->pict;
			break;
		case CLIDISP_ICON:
			if (!cw->icon_pict_filled) return;
			source = cw->icon_pict_filled->pict;
			break;
		case CLIDISP_ZOMBIE:
		// We will draw the icon later
		case CLIDISP_ZOMBIE_ICON:
			source = cw->shadow;
			break;
		case CLIDISP_THUMBNAIL:
		// We will draw the icon later
		case CLIDISP_THUMBNAIL_ICON:
			source = cw->origin;
			break;
	}

	if (!source) return;

	// Drawing main picture
	{
		Picture mask = cw->mainwin->normalPicture;
		if (cw->focused)
			mask = cw->mainwin->highlightPicture;
		else if (cw->zombie)
			mask = cw->mainwin->shadowPicture;

		if (!ps->o.pseudoTrans) {
			XRenderComposite(ps->dpy, PictOpSrc, source, mask,
					cw->destination, s_x, s_y, 0, 0, s_x, s_y, s_w, s_h);
		}
		else {
			XRenderComposite(ps->dpy, PictOpSrc, cw->mainwin->background, None,
					cw->destination, cw->mini.x + s_x, cw->mini.y + s_y, 0, 0,
					s_x, s_y, s_w, s_h);
			XRenderComposite(ps->dpy, PictOpOver, source, mask,
					cw->destination, s_x, s_y, 0, 0, s_x, s_y, s_w, s_h);
		}

		if (cw->paneltype != WINTYPE_WINDOW && ps->o.panel_tinting && ps->o.background)
			XRenderComposite(ps->dpy, PictOpOver, ps->o.background->pict, None,
					cw->destination, s_x, s_y, 0, 0, s_x, s_y, s_w, s_h);

		if ((CLIDISP_ZOMBIE_ICON == cw->mode || CLIDISP_THUMBNAIL_ICON == cw->mode)
		&& cw->paneltype == WINTYPE_WINDOW) {
			assert(cw->icon_pict && cw->icon_pict->pict);
			img_composite_params_t params = IMG_COMPOSITE_PARAMS_INIT;
			simg_get_composite_params(cw->icon_pict,
					cw->mini.width, cw->mini.height,
					ps->o.iconSpec.mode, ps->o.iconSpec.alg, ps->o.iconSpec.valg,
					&params);
			simg_composite(ps, cw->icon_pict, cw->destination,
					cw->mini.width, cw->mini.height, &params, NULL, mask, pbound);
		}
	}

	// Tinting
	if (cw->paneltype == WINTYPE_WINDOW)
	{
		XRenderColor *tint = &cw->mainwin->normalTint;
		if (cw->focused || cw->multiselect) {
			if (!ps->o.multiselect)
				tint = &cw->mainwin->highlightTint;
			else
				tint = &cw->mainwin->multiselectTint;
		}
		else if (cw->zombie)
			tint = &cw->mainwin->shadowTint;

		if (tint->alpha) {
#ifdef CFG_XINERAMA
			if (cw->mode == CLIDISP_DESKTOP)
			{
				XineramaScreenInfo *iter = cw->mainwin->xin_info;
				for (int i = 0; i < cw->mainwin->xin_screens; ++i)
				{
					s_x = iter->x_org * cw->mainwin->multiplier;
					s_y = iter->y_org * cw->mainwin->multiplier;
					s_w = iter->width * cw->mainwin->multiplier;
					s_h = iter->height * cw->mainwin->multiplier;

					XRenderFillRectangle(cw->mainwin->ps->dpy,
							PictOpOver, cw->destination, tint,
							s_x, s_y, s_w, s_h);

					XClearArea(cw->mainwin->ps->dpy, cw->mini.window, s_x, s_y, s_w, s_h, False);
					iter++;
				}
			}
			else {
#endif /* CFG_XINERAMA */
				XRenderFillRectangle(cw->mainwin->ps->dpy, PictOpOver,
						cw->destination, tint, s_x, s_y, s_w, s_h);
				XClearArea(cw->mainwin->ps->dpy, cw->mini.window, s_x, s_y, s_w, s_h, False);
#ifdef CFG_XINERAMA
			}
#endif /* CFG_XINERAMA */
		}

		if (ps->o.tooltip_show && ps->o.mode != PROGMODE_PAGING) {
			tooltip_handle(cw->tooltip, cw->focused);
		}

		clientwin_round_corners(cw);
	}

	XClearArea(cw->mainwin->ps->dpy, cw->mini.window, s_x, s_y, s_w, s_h, False);
}

void
clientwin_render(ClientWin *cw) {
	clientwin_repaint(cw, NULL);
}

void
clientwin_repair(ClientWin *cw) {
	session_t *ps = cw->mainwin->ps;

	int nrects = 0;
	XRectangle *rects = NULL;

	{
		XserverRegion rgn = XFixesCreateRegion(ps->dpy, 0, 0);
		XDamageSubtract(ps->dpy, cw->damage, None, rgn);
		if (cw->mode >= CLIDISP_ZOMBIE)
			rects = XFixesFetchRegion(ps->dpy, rgn, &nrects);
		XFixesDestroyRegion(ps->dpy, rgn);
	}
	for (int i = 0; i < nrects; i++) {
		XRectangle r = {
			.x = rects[i].x * cw->factor,
			.y = rects[i].y * cw->factor,
			.width = rects[i].width * cw->factor,
			.height = rects[i].height * cw->factor,
		};
		clientwin_repaint(cw, &r);
	}

	if (rects)
		XFree(rects);
	
	cw->damaged = false;
}

void
clientwin_schedule_repair(ClientWin *cw, XRectangle *area)
{
	cw->damaged = true;
}

void clientwin_round_corners(ClientWin *cw) {
	session_t* ps = cw->mainwin->ps;
	int dia = 2 * ps->o.cornerRadius;
	int w = cw->mini.width;
	int h = cw->mini.height;
	XGCValues xgcv;
	Pixmap mask = XCreatePixmap(ps->dpy, cw->mini.window, w, h, 1);
	GC shape_gc = XCreateGC(ps->dpy, mask, 0, &xgcv);

	XSetForeground(ps->dpy, shape_gc, 0);
	XFillRectangle(ps->dpy, mask, shape_gc, 0, 0, w, h);
	XSetForeground(ps->dpy, shape_gc, 1);
	if (dia > 0) {
		XFillArc(ps->dpy, mask, shape_gc, 0, 0, dia, dia, 0, 360 * 64);
		XFillArc(ps->dpy, mask, shape_gc, w-dia-1, 0, dia, dia, 0, 360 * 64);
		XFillArc(ps->dpy, mask, shape_gc, 0, h-dia-1, dia, dia, 0, 360 * 64);
		XFillArc(ps->dpy, mask, shape_gc, w-dia-1, h-dia-1, dia, dia, 0, 360 * 64);
	}
	XFillRectangle(ps->dpy, mask, shape_gc, ps->o.cornerRadius, 0, w-dia, h);
	XFillRectangle(ps->dpy, mask, shape_gc, 0, ps->o.cornerRadius, w, h-dia);
	XShapeCombineMask(ps->dpy, cw->mini.window, ShapeBounding, 0, 0, mask, ShapeSet);
	XFreePixmap(ps->dpy, mask);
	XFreeGC(ps->dpy, shape_gc);
}

void clientwin_prepmove(ClientWin *cw)
{
	if (cw->pixmap)
		XFreePixmap(cw->mainwin->ps->dpy, cw->pixmap);

	if (cw->destination)
		XRenderFreePicture(cw->mainwin->ps->dpy, cw->destination);

	cw->pixmap = XCreatePixmap(cw->mainwin->ps->dpy, cw->mini.window,
			cw->src.width, cw->src.height, cw->mainwin->depth);
	XSetWindowBackgroundPixmap(cw->mainwin->ps->dpy,
			cw->mini.window, cw->pixmap);

	cw->destination = XRenderCreatePicture(cw->mainwin->ps->dpy,
			cw->pixmap, cw->mini.format, 0, 0);
}

void
clientwin_move(ClientWin *cw, float f, int x, int y, float timeslice)
{
	cw->factor = f;
	{
		// animate window by changing these in time linearly:
		// here, cw->mini has destination coordinates, cw->src has original coordinates

		cw->mini.x = cw->src.x + (cw->x - cw->src.x + x) * timeslice;
		cw->mini.y = cw->src.y + (cw->y - cw->src.y + y) * timeslice;

		cw->mini.width = cw->src.width * f;
		cw->mini.height = cw->src.height * f;
	}

	XMoveResizeWindow(cw->mainwin->ps->dpy, cw->mini.window, cw->mini.x, cw->mini.y, cw->mini.width, cw->mini.height);

	clientwin_round_corners(cw);
}

void
clientwin_map(ClientWin *cw) {
	session_t *ps = cw->mainwin->ps;
	
	if (!cw->mode)
		return;

	if (cw->origin) {
		free_damage(ps, &cw->damage);
		cw->damage = XDamageCreate(ps->dpy, cw->src.window, XDamageReportDeltaRectangles);
		if (cw->paneltype == WINTYPE_WINDOW)
			XRenderSetPictureTransform(ps->dpy, cw->origin, &cw->mainwin->transform);
	}

	if (cw->shadow) {
		free_damage(ps, &cw->damage);
		cw->damage = XDamageCreate(ps->dpy, cw->src.window, XDamageReportDeltaRectangles);
		if (cw->paneltype == WINTYPE_WINDOW)
			XRenderSetPictureTransform(ps->dpy, cw->shadow, &cw->mainwin->transform);
	}

	clientwin_render(cw);

	XMapWindow(ps->dpy, cw->mini.window);
	XRaiseWindow(ps->dpy, cw->mini.window);

	if (ps->o.tooltip_show && ps->o.mode != PROGMODE_PAGING
			&& cw->paneltype == WINTYPE_WINDOW) {
		clientwin_tooltip(cw);
		tooltip_handle(cw->tooltip, cw->focused);
	}
}

void
clientwin_unmap(ClientWin *cw) {
	session_t *ps = cw->mainwin->ps;

	free_damage(ps, &cw->damage);
	free_picture(ps, &cw->destination);
	free_pixmap(ps, &cw->pixmap);

	XUnmapWindow(ps->dpy, cw->mini.window);
	XSetWindowBackgroundPixmap(ps->dpy, cw->mini.window, None);

	cw->focused = false;

	if (cw->tooltip)
		tooltip_unmap(cw->tooltip);
}

void
childwin_focus(ClientWin *cw) {
	if (!cw)
		return;

	session_t * const ps = cw->mainwin->ps;

	if (ps->o.movePointer)
		XWarpPointer(ps->dpy, None, cw->wid_client,
				0, 0, 0, 0, cw->src.width / 2, cw->src.height / 2);
	XRaiseWindow(ps->dpy, cw->wid_client);
	wm_activate_window(ps, cw->wid_client);
	XFlush(ps->dpy);
}

void
clientwin_tooltip(ClientWin *cw) {
	MainWin *mw = cw->mainwin;
	session_t *ps = mw->ps;

	if (cw->tooltip) {
		int win_title_len = 0;
		FcChar8 *win_title = wm_get_window_title(ps, cw->wid_client, &win_title_len);

		if (!win_title)
			win_title = wm_get_window_title(ps, cw->mini.window, &win_title_len);

		if (win_title) {
			tooltip_map(cw->tooltip, cw, win_title, win_title_len);
			free(win_title);
		}
	}
}

void
shadow_clientwindow(ClientWin* cw, enum cliop op) {
	MainWin *mw = cw->mainwin;
	session_t *ps = mw->ps;

	if (cw == mw->client_to_focus)
		mw->client_to_focus = NULL;
	if (cw == mw->client_to_focus_on_cancel)
		mw->client_to_focus_on_cancel = NULL;

	clientwin_action(cw, op);
	clientwin_unmap(cw);

	XFlush(ps->dpy);
	usleep(10000);

	clientwin_update(cw);

	clientwin_prepmove(cw);
	clientwin_move(cw, mw->multiplier, mw->xoff, mw->yoff, 1);
	clientwin_map(cw);

	focus_miniw(ps, cw);
}

int
close_clientwindow(ClientWin* cw, enum cliop op) {
	MainWin *mw = cw->mainwin;
	session_t *ps = mw->ps;

	if (cw == mw->client_to_focus)
		mw->client_to_focus = NULL;
	if (cw == mw->client_to_focus_on_cancel)
		mw->client_to_focus_on_cancel = NULL;

	clientwin_unmap(cw);
	clientwin_action(cw, op);
	XFlush(ps->dpy);
	usleep(10000);
	XFlush(ps->dpy);
	focus_miniw_next(ps, cw);
	XFlush(ps->dpy);

	{
		dlist *del = dlist_find(mw->clientondesktop,
				clientwin_cmp_func, (void *) cw->wid_client);
		mw->clientondesktop = dlist_remove(del);
	}

	{
		dlist *del = dlist_find(mw->clients,
				clientwin_cmp_func, (void *) cw->wid_client);
		mw->clients = dlist_remove(del);
	}

	{
		dlist *del = dlist_find(mw->focuslist,
				clientwin_cmp_func, (void *) cw->wid_client);
		mw->focuslist = dlist_remove(del);
	}

	clientwin_destroy((ClientWin *) cw, True);

	if (dlist_len(mw->focuslist) == 0)
		return 1;
	return 0;
}

int
select_clientwindow(ClientWin* cw, enum cliop op) {
	session_t *ps = cw->mainwin->ps;
	if (ps->o.multiselect) {
		cw->mainwin->client_to_focus->multiselect = !cw->mainwin->client_to_focus->multiselect;
		clientwin_render(cw);
		return 0;
	}
	else {
		return 1;
	}
}

int
clientwin_handle(ClientWin *cw, XEvent *ev) {
	if (! cw)
		return 1;

	MainWin *mw = cw->mainwin;
	session_t *ps = mw->ps;
	XKeyEvent * const evk = &ev->xkey;

	if (ev->type == KeyPress)
	{
		report_key(ev);
		report_key_modifiers(evk);
		if (debuglog) fputs("\n", stdout);

		if (arr_keycodes_includes(cw->mainwin->keycodes_Up, evk->keycode))
			focus_up(cw->mainwin->client_to_focus);
		else if (arr_keycodes_includes(cw->mainwin->keycodes_Down, evk->keycode))
			focus_down(cw->mainwin->client_to_focus);
		else if (arr_keycodes_includes(cw->mainwin->keycodes_Left, evk->keycode))
			focus_left(cw->mainwin->client_to_focus);
		else if (arr_keycodes_includes(cw->mainwin->keycodes_Right, evk->keycode))
			focus_right(cw->mainwin->client_to_focus);
		else if (arr_keycodes_includes(cw->mainwin->keycodes_Prev, evk->keycode))
			focus_miniw_prev(ps, cw->mainwin->client_to_focus);
		else if (arr_keycodes_includes(cw->mainwin->keycodes_Next, evk->keycode))
			focus_miniw_next(ps, cw->mainwin->client_to_focus);
		else if (arr_keycodes_includes(cw->mainwin->keycodes_Cancel, evk->keycode))
		{
			mw->refocus = true;
			return 1;
		}
		else if (arr_keycodes_includes(cw->mainwin->keycodes_Select, evk->keycode))
		{
			return select_clientwindow(cw, CLIENTOP_FOCUS);
		}
		cw->mainwin->pressed_key = true;
	}

	else if (ev->type == KeyRelease) {
		printfdf(false, "(): else if (ev->type == KeyRelease) {");
		printfdf(false, "(): keycode: %d:", evk->keycode);

		if (cw->mainwin->pressed_key) {
			if (mw->client_to_focus->mode != CLIDISP_DESKTOP) {
				if (arr_keycodes_includes(mw->keycodes_Iconify, evk->keycode)) {
					shadow_clientwindow(cw, CLIENTOP_ICONIFY);
				}
				else if (arr_keycodes_includes(mw->keycodes_Shade, evk->keycode)) {
					shadow_clientwindow(cw, CLIENTOP_SHADE_EWMH);
				}
				else if (arr_keycodes_includes(mw->keycodes_Close, evk->keycode)) {
					return close_clientwindow(cw, CLIENTOP_CLOSE_EWMH);
				}
			}
		}
		else
			printfdf(false, "(): KeyRelease %u ignored.", evk->keycode);
    }

	else if (ev->type == ButtonPress) {
		cw->mainwin->pressed_mouse = true;
		/* if (ev->xbutton.button == 1)
			cw->mainwin->pressed = cw; */
	}
	else if (ev->type == ButtonRelease) {
		printfdf(false, "(): else if (ev->type == ButtonRelease) {");
		const unsigned button = ev->xbutton.button;
		if (cw->mainwin->pressed_mouse) {
			if (button < MAX_MOUSE_BUTTONS) {
				if (mw->client_to_focus->mode != CLIDISP_DESKTOP) {
					if (ps->o.bindings_miwMouse[button] == CLIENTOP_DESTROY
					 || ps->o.bindings_miwMouse[button] == CLIENTOP_CLOSE_EWMH
					 || ps->o.bindings_miwMouse[button] == CLIENTOP_CLOSE_ICCCM)
						return close_clientwindow(cw, ps->o.bindings_miwMouse[button]);
					else if(ps->o.bindings_miwMouse[button] == CLIENTOP_ICONIFY
						 || ps->o.bindings_miwMouse[button] == CLIENTOP_SHADE_EWMH) {
						shadow_clientwindow(cw, ps->o.bindings_miwMouse[button]);
						return 0;
					}
				}

				//CLIENTOP_FOCUS, CLIENTOP_PREV, CLIENTOP_NEXT,
				return clientwin_action(cw,
						ps->o.bindings_miwMouse[button]);
			}
		}
		else
			printfdf(false, "(): ButtonRelease %u ignored.", button);
	}

	else if (ev->type == FocusIn) {
		printfdf(false, "(): else if (ev->type == FocusIn) {");
		XFocusChangeEvent *evf = &ev->xfocus;

		// for debugging XEvents
		// see: https://tronche.com/gui/x/xlib/events/input-focus/normal-and-grabbed.html
		printfdf(false, "(): main window id = %#010lx", cw->mainwin->window);
		printfdfWindowName(ps, "(): client window = ", cw->mainwin->window);
		printfdf(false, "(): client window id = %#010lx", cw->wid_client);
		printfdfXFocusChangeEvent(ps, evf, false);

		// printfef("(): usleep(10000);");
		// usleep(10000);

		// if (evf->detail == NotifyWhileGrabbed)
		if (evf->detail == NotifyNonlinear || evf->detail == NotifyAncestor)
			cw->focused = true;

		clientwin_render(cw);

		if (debuglog) fputs("\n", stdout);
		XFlush(ps->dpy);
	} else if (ev->type == FocusOut) {
		printfdf(false, "(): else if (ev->type == FocusOut) {");
		XFocusChangeEvent *evf = &ev->xfocus;

		// for debugging XEvents
		// see: https://tronche.com/gui/x/xlib/events/input-focus/normal-and-grabbed.html
		printfdf(false, "(): main window id = %#010lx", cw->mainwin->window);
		printfdfWindowName(ps, "(): client window = ", cw->mainwin->window);
		printfdf(false, "(): client window id = %#010lx", cw->wid_client);
		printfdfXFocusChangeEvent(ps, evf, false);

		// printfef("(): usleep(10000);");
		// usleep(10000);

		// if (evf->detail == NotifyWhileGrabbed)
		if (evf->detail == NotifyNonlinear || evf->detail == NotifyPointer)
			cw->focused = false;

		clientwin_render(cw);

		if (debuglog) fputs("\n", stdout);
		XFlush(ps->dpy);

	} else if(ev->type == MotionNotify) {
		printfdf(false, "(): else if (ev->type == MotionNotify) {");

		if (cw->mainwin->client_to_focus != cw) {
			XSetInputFocus(ps->dpy, cw->mini.window, RevertToParent, CurrentTime);
			cw->mainwin->client_to_focus = cw;
		}
	} else if(ev->type == LeaveNotify) {
		printfdf(false, "(): else if (ev->type == LeaveNotify) {");
	}
	return 0;
}

static int
clientwin_action(ClientWin *cw, enum cliop action) {
	MainWin *mw = cw->mainwin;
	session_t * const ps = mw->ps;
	const Window wid = cw->wid_client;

	switch (action) {
		case CLIENTOP_NO:
			break;
		case CLIENTOP_FOCUS:
			return select_clientwindow(cw, CLIENTOP_FOCUS);
		case CLIENTOP_ICONIFY:
			XIconifyWindow(ps->dpy, wid, ps->screen);
			break;
		case CLIENTOP_SHADE_EWMH:
			wm_shade_window_ewmh(ps, wid);
			break;
		case CLIENTOP_CLOSE_ICCCM:
			wm_close_window_icccm(ps, wid);
			break;
		case CLIENTOP_CLOSE_EWMH:
			wm_close_window_ewmh(ps, wid);
			break;
		case CLIENTOP_DESTROY:
			XDestroyWindow(cw->mainwin->ps->dpy, wid);
			break;
		case CLIENTOP_PREV:
			focus_miniw_prev(ps, cw->mainwin->client_to_focus);
			break;
		case CLIENTOP_NEXT:
			focus_miniw_next(ps, cw->mainwin->client_to_focus);
			break;
	}

	return 0;
}
