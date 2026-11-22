/* Run under an isolated X server: xvfb-run ./test-panel-click */
#include "panel-click.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static XButtonEvent receive(Display *dpy, int type)
{
    XSync(dpy, False);
    XEvent ev;
    assert(XCheckTypedEvent(dpy, type, &ev));
    assert(ev.xbutton.send_event);
    return ev.xbutton;
}

int main(void)
{
    Display *panel = XOpenDisplay(NULL), *sender = XOpenDisplay(NULL);
    if (!panel || !sender) return 77;
    Window root = DefaultRootWindow(panel);
    Window base = XCreateSimpleWindow(panel, root, 100, 100, 200, 100, 0, 0, 0);
    Window control = XCreateSimpleWindow(panel, base, 20, 10, 80, 40, 0, 0, 0);
    /* Parent deliberately selects presses only; child selects both. */
    XSelectInput(panel, base, ButtonPressMask);
    XSelectInput(panel, control, ButtonPressMask | ButtonReleaseMask);
    XMapWindow(panel, control);
    XMapWindow(panel, base);
    XSetWindowAttributes attrs = { .override_redirect = True };
    Window cover = XCreateWindow(sender, DefaultRootWindow(sender), 0, 0,
            640, 480, 0, CopyFromParent, InputOutput, CopyFromParent,
            CWOverrideRedirect, &attrs);
    XSelectInput(sender, cover, ButtonPressMask | ButtonReleaseMask);
    XSync(panel, False);
    XMapRaised(sender, cover);
    XSync(sender, False);
    PanelClickState state = {0};
    XButtonEvent input = { .type = ButtonPress, .button = 1,
        .x = 15, .y = 10, .time = CurrentTime, .state = ShiftMask };
    assert(panel_click_send(sender, base, base, 0.5, 0, 0, &state, &input, False));
    XButtonEvent event = receive(panel, ButtonPress);
    assert(event.window == control && event.subwindow == None);
    assert(event.x == 10 && event.y == 10);
    assert(event.x_root == 130 && event.y_root == 120);
    assert(event.state == ShiftMask);
    /* Release outside the child still belongs to its press. */
    input.type = ButtonRelease;
    input.x = 90;
    input.state = ShiftMask | Button1Mask;
    assert(panel_click_send(sender, base, base, 0.5, 0, 0, &state, &input, False));
    event = receive(panel, ButtonRelease);
    assert(event.window == control && event.x == 160);
    assert(event.state == (ShiftMask | Button1Mask));
    assert(!panel_click_send(sender, base, base, 0.5, 0, 0, &state, &input, False));
    /* Press-only selection verifies the press mask independently. */
    input.type = ButtonPress;
    input.button = 3;
    assert(panel_click_send(sender, base, base, 0.5, 0, 0, &state, &input, False));
    assert(receive(panel, ButtonPress).window == base);
    /* Wheel button and cropped drawable coordinates. */
    input.button = 4; input.x = 10; input.y = 10;
    assert(panel_click_send(sender, base, base, 1, 20, 10, &state, &input, False));
    assert(receive(panel, ButtonPress).window == control);
    input.type = ButtonRelease;
    assert(panel_click_send(sender, base, base, 1, 20, 10, &state, &input, False));
    assert(receive(panel, ButtonRelease).button == 4);
    input.send_event = True;
    assert(!panel_click_send(sender, base, base, 1, 0, 0, &state, &input, False));
    XSync(sender, False);
    XEvent unwanted;
    assert(!XCheckMaskEvent(sender, ButtonPressMask | ButtonReleaseMask, &unwanted));
    XDestroyWindow(sender, cover);
    XDestroyWindow(panel, base);
    XCloseDisplay(sender); XCloseDisplay(panel);
    puts("panel-click: covered panel, child targeting, coordinates, masks, release pairing, wheel: PASS");
    return 0;
}
