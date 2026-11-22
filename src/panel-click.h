#ifndef SKIPPY_PANEL_CLICK_H
#define SKIPPY_PANEL_CLICK_H

#include <X11/Xlib.h>

/* Remember the receiver of each press, including wheel/extended buttons. */
typedef struct {
    Window target[256];
} PanelClickState;

/* Events use source drawable coordinates after undoing preview scaling/cropping.
 * Returns whether XSendEvent queued the event, not whether the panel accepted it. */
Bool panel_click_send(Display *dpy, Window client, Window drawable,
        double scale, int crop_x, int crop_y, PanelClickState *state,
        const XButtonEvent *input, Bool debug);
#endif
