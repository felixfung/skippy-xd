# Panel click forwarding PoC (PR #127)

Reimplemented on origin/master at 25402b4 (rather than the old panelclick
branch). Enable in your configuration:

```ini
[panel]
allowClick = true
```

Build normally with `meson setup build && meson compile -C build`.
Run the resulting skippy-xd binary using this configuration. If a daemon is
already running, stop that daemon and start the newly built binary; sending
commands to an old daemon does not load this code.

The preview stays tinted and panel clicks do not dismiss the overview. Button
press/release and wheel buttons are forwarded with XSendEvent. Coordinates
undo preview scaling/cropping, then translate to a receiving control within
the source panel. Releases stay paired with the press target. Unmatched
releases and synthetic input are ignored. State resets on preview unmap.

No new runtime library dependency. No focus, popup, hover, or drag support is
added. Applications can reject synthetic events, and synthetic presses do not
create server-side grabs or change the server's pointer/button state. A panel
menu taking a grab may bypass the forwarding path. `bindings/enforceFocus`
still applies. Keep this option disabled for unsupported panels.

## Validation

The full application and isolated forwarding test compiled in the development
environment. The forwarding test builds with `-Wall -Wextra -Werror`.
The Xvfb integration test could not connect to an isolated display in that
restricted environment, so actual X event delivery is NOT yet validated.

On an X11 development machine, run:

```sh
xvfb-run -a meson test -C build panel-click --print-errorlogs
```

The test uses separate sender/receiver connections and covers an obscured panel,
nested control targeting, scaling/cropping, root coordinates, press-only event
selection, release outside the control, unmatched release, wheel buttons and
synthetic-event rejection. Without a display the test returns skip status 77.

Then test actual panels (especially gkrellm, used in PR #127) with left/right
click and wheel, both transparency modes, and allowClick disabled. A passing
protocol test alone does not establish toolkit compatibility.

## Experimental GTK follow-up

GTK 3's GDK routing checks its cached top-level-under-pointer before routing
button events to client-side widgets. The follow-up sends synthetic EnterNotify
to the panel owner before a button sequence and LeaveNotify afterward. This is
an experiment for Xfce, not a confirmed compatibility fix. Debug logging now
includes source/target XIDs, coordinates and selected core event masks.

Synthetic crossing events may affect hover state. Popup grabs, failed sequences,
and simultaneous physical interaction still require broader testing. This does
not implement general hover/motion forwarding.
