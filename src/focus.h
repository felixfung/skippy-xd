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

#ifndef SKIPPY_FOCUS_H
#define SKIPPY_FOCUS_H

static inline void
printfdfXFocusChangeEvent(session_t *ps, XFocusChangeEvent *evf, bool print)
{
	printfdfWindowName(ps, "(): event window = ", evf->window);
	printfdf(print, "(): event window id = %#010lx", evf->window);

	if(evf->mode == NotifyNormal) {
		printfdf(print, "(): evf->mode = NotifyNormal");
	}
	else if(evf->mode == NotifyGrab) {
		printfdf(print, "(): evf->mode = NotifyGrab");
	}
	else if(evf->mode == NotifyUngrab) {
		printfdf(print, "(): evf->mode = NotifyUngrab");
	}
	else if(evf->mode == NotifyWhileGrabbed) {
		printfdf(print, "(): evf->mode = NotifyWhileGrabbed");
	}
	else {
		printfdf(print, "(): evf->mode = %i (not recognized)", evf->mode);
	}

	if(evf->detail == NotifyAncestor) {
		printfdf(print, "(): evf->detail = NotifyAncestor");
	}
	else if(evf->detail == NotifyVirtual) {
		printfdf(print, "(): evf->detail = NotifyVirtual");
	}
	else if(evf->detail == NotifyInferior) {
		printfdf(print, "(): evf->detail = NotifyInferior");
	}
	else if(evf->detail == NotifyNonlinear) {
		printfdf(print, "(): evf->detail = NotifyNonlinear");
	}
	else if(evf->detail == NotifyNonlinearVirtual) {
		printfdf(print, "(): evf->detail = NotifyNonlinearVirtual");
	}
	else if(evf->detail == NotifyPointer) {
		printfdf(print, "(): evf->detail = NotifyPointer");
	}
	else if(evf->detail == NotifyPointerRoot) {
		printfdf(print, "(): evf->detail = NotifyPointerRoot");
	}
	else if(evf->detail == NotifyDetailNone) {
		printfdf(print, "(): evf->detail = NotifyDetailNone");
	}
	else {
		printfdf(print, "(): evf->detail = %i (not recognized)", evf->detail);
	}
}

static inline void
clear_focus_all(dlist *focuslist)
{
	dlist *elem = dlist_first(focuslist);
	while (elem)
	{
		ClientWin *cw = (ClientWin *)elem->data;
		if (cw)
			cw->focused = 0;
		elem = elem->next;
	}
}

/**
 * @brief Focus the mini window of a client window.
 */
static inline void
focus_miniw_adv(session_t *ps, ClientWin *cw, bool move_ptr) {
	if (!cw || !ps)
		return;

	printfdfWindowName(ps, "(): window = ", cw->wid_client);

	if (unlikely(!cw))
	{
		printfdf(false, "(): if (unlikely(!cw))");
		return;
	}
	assert(cw->mini.window);

	if (move_ptr)
	{
		printfdf(false, "(): if (move_ptr)");
		XWarpPointer(ps->dpy, None, cw->mini.window, 0, 0, 0, 0, cw->mini.width / 2, cw->mini.height / 2);
	}
	XSetInputFocus(ps->dpy, cw->mini.window, RevertToParent, CurrentTime);
	XFlush(ps->dpy);

	ps->mainwin->client_to_focus = cw;
	ps->mainwin->client_to_focus->focused = 1;
	clientwin_render(cw);

	printfdf(false, "(): ");
	printfdf(false, "(): client_to_focus = %p", ps->mainwin->client_to_focus);
}

static inline void
focus_miniw(session_t *ps, ClientWin *cw) {
	focus_miniw_adv(ps, cw, ps->o.movePointer);
}

/**
 * @brief Focus the mini window of next client window in list.
 */
static inline void
focus_miniw_next(session_t *ps, ClientWin *cw) {
	cw->focused = false;
	clientwin_render(cw);
	dlist *e = dlist_find_data(cw->mainwin->focuslist, cw);
	if (!e) {
		printfef(false, "() (%#010lx): Client window not found in list.", cw->src.window);
		return;
	}
	if (e->next)
		focus_miniw(ps, e->next->data);
	else
		focus_miniw(ps, dlist_first(e)->data);
}

/**
 * @brief Focus the mini window of previous client window in list.
 */
static inline void
focus_miniw_prev(session_t *ps, ClientWin *cw) {
	cw->focused = false;
	clientwin_render(cw);
	dlist *cwlist = dlist_first(cw->mainwin->focuslist);
	dlist *tgt = NULL;

	if (cw == cwlist->data)
		tgt = dlist_last(cwlist);
	else
		foreach_dlist (cwlist) {
			if (iter->next && cw == iter->next->data) {
				tgt = iter;
				break;
			}
		}

	if (!tgt) {
		printfef(false, "() (%#010lx): Client window not found in list.", cw->src.window);
		return;
	}

	focus_miniw(ps, (ClientWin *) tgt->data);
}

void focus_up(ClientWin *cw);
void focus_down(ClientWin *cw);
void focus_left(ClientWin *cw);
void focus_right(ClientWin *cw);

#endif /* SKIPPY_FOCUS_H */
