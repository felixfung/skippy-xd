#include "panel-click.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* GTK routes synthetic buttons using its cached top-level-under-pointer.
 * Bracket a click sequence with crossing events to establish that state while
 * the actual panel remains covered. NoEventMask addresses the window owner,
 * including toolkits selecting input through XI2 instead of core masks. */
static void
panel_crossing(Display *dpy, Window client, Window root,
        const XButtonEvent *button, int type)
{
    XEvent event = {0};
    XCrossingEvent *crossing = &event.xcrossing;
    crossing->type = type;
    crossing->display = dpy;
    crossing->window = client;
    crossing->root = root;
    crossing->time = button->time;
    crossing->x_root = button->x_root;
    crossing->y_root = button->y_root;
    crossing->mode = NotifyNormal;
    crossing->detail = NotifyAncestor;
    crossing->same_screen = True;
    crossing->state = button->state;
    if (type == LeaveNotify && button->button >= 1 && button->button <= 5)
        crossing->state &= ~(Button1Mask << (button->button - 1));
    if (XTranslateCoordinates(dpy, root, client,
                button->x_root, button->y_root, &crossing->x, &crossing->y,
                &crossing->subwindow))
        XSendEvent(dpy, client, False, NoEventMask, &event);
}

static Bool
panel_buttons_down(const PanelClickState *state)
{
    for (unsigned i = 1; i < 256; ++i)
        if (state->target[i] != None)
            return True;
    return False;
}

Bool
panel_click_send(Display *dpy, Window client, Window drawable,
        double scale, int crop_x, int crop_y, PanelClickState *state,
        const XButtonEvent *input, Bool debug)
{
    if ((input->type != ButtonPress && input->type != ButtonRelease)
            || input->send_event || input->button == 0 || input->button >= 256
            || !isfinite(scale) || scale <= 0)
        return False;

    int sx = (int) floor(input->x / scale) + crop_x;
    int sy = (int) floor(input->y / scale) + crop_y;
    Window target = state->target[input->button], child;
    int x, y;

    if (input->type == ButtonPress) {
        state->target[input->button] = None;
        target = None;
        /* Search inside the real panel, even when another root window covers it.
         * Keep the deepest ancestor that selects presses: propagation goes up,
         * never down from the client to its controls. */
        Window current = client;
        for (unsigned depth = 0; current != None && depth < 64; ++depth) {
            XWindowAttributes attr;
            if (!XGetWindowAttributes(dpy, current, &attr)
                    || attr.map_state != IsViewable
                    || !XTranslateCoordinates(dpy, drawable, current,
                        sx, sy, &x, &y, &child)
                    || x < 0 || y < 0 || x >= attr.width || y >= attr.height)
                return False;
            if (attr.do_not_propagate_mask & ButtonPressMask)
                target = None;
            if (attr.all_event_masks & ButtonPressMask)
                target = current;
            current = child;
        }
    } else {
        /* Release must go to the press receiver, even outside that control. */
        state->target[input->button] = None;
    }

    if (target == None)
        return False;
    XWindowAttributes attr;
    if (!XGetWindowAttributes(dpy, target, &attr)
            || attr.map_state != IsViewable
            || !XTranslateCoordinates(dpy, drawable, target,
                sx, sy, &x, &y, &child))
        return False;

    XEvent event;
    memset(&event, 0, sizeof(event));
    event.xbutton = *input;
    event.xbutton.display = dpy;
    event.xbutton.window = target;
    event.xbutton.root = attr.root;
    event.xbutton.subwindow = child;
    event.xbutton.x = x;
    event.xbutton.y = y;
    event.xbutton.same_screen = True;
    if (!XTranslateCoordinates(dpy, drawable, attr.root, sx, sy,
                &event.xbutton.x_root, &event.xbutton.y_root, &child))
        return False;
    if (input->type == ButtonPress && !panel_buttons_down(state))
        panel_crossing(dpy, client, attr.root, &event.xbutton, EnterNotify);
    if (debug)
        fprintf(stderr, "panel_click_send(): client=%#lx target=%#lx type=%d "
                "local=(%d,%d) root=(%d,%d) core_mask=%#lx\n",
                client, target, input->type, x, y, event.xbutton.x_root,
                event.xbutton.y_root, attr.all_event_masks);
    long mask = input->type == ButtonPress ? ButtonPressMask : ButtonReleaseMask;
    if (!XSendEvent(dpy, target, False, mask, &event))
        return False;
    if (input->type == ButtonPress)
        state->target[input->button] = target;
    if (input->type == ButtonRelease && !panel_buttons_down(state))
        panel_crossing(dpy, client, attr.root, &event.xbutton, LeaveNotify);
    XFlush(dpy);
    return True;
}
