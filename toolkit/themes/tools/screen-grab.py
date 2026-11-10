#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Grab the screen to a PNG and print its pixel size as "W H".

What the compositor put on screen, not a fresh paint: a Marionette or
`--screenshot` capture re-renders the page by default (Marionette reads back
the widget's layers only under `remote.screenshot.use_readback`), so a
retained-display-list or compositor paint bug is invisible to it and visible
here. firefox-probe.py's
`grab` step runs it; you never call it directly.

It grabs with Pillow's ImageGrab under the interpreter that runs
firefox-probe.py (`./mach python --virtualenv wpt`, which pins Pillow).

    screen-grab.py out.png [--crop X,Y,W,H] [--any-display]

--crop takes four integers in screen pixels; write a negative X or Y as
`--crop=-1920,0,800,600`, since argparse reads a bare `-1920,...` as a flag.

Linux: it grabs the X root window of $DISPLAY, and refuses unless the X server
owning that display (the PID in /tmp/.X<n>-lock) is Xvfb, since any other
server is usually the user's live desktop; --any-display skips that check for
an isolated server of another kind. Run it inside a private display:

    env -u WAYLAND_DISPLAY GDK_BACKEND=x11 MOZ_ENABLE_WAYLAND=0 \
        xvfb-run -a -s "-screen 0 1600x1000x24" <driver that calls this>

A crop must lie inside the root window.

macOS: it grabs the logged-in desktop. A grab without --crop covers the main
display in device pixels (2x on Retina), and all_screens has no effect there.
A --crop is in points, and Pillow returns the cropped region at 1x, so a Retina
crop is downsampled. Without the Screen Recording permission for the terminal
the grab returns the wallpaper alone, with no windows, and still exits 0.

Windows: it grabs every screen. An uncropped grab's (0, 0) is the virtual
desktop's top-left corner, so when a monitor sits left of or above the primary
one, screen coordinates (mozInnerScreenX/Y included) are offset from PNG
coordinates by that monitor's extent.

The page's content origin on that screen is `mozInnerScreenX/Y` (read it from
the page) in CSS pixels; desktop-theme-screenshots' pixel-diff.py takes it
unscaled as `--offset X,Y` together with `--dpr`, and scales both itself.
"""

import argparse
import os
import sys

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument(
    "--crop",
    help="X,Y,W,H in screen pixels (--crop=-1920,0,800,600 for a negative X)",
)
ap.add_argument(
    "--any-display",
    action="store_true",
    help="Linux: grab $DISPLAY even when its X server is not Xvfb",
)
args = ap.parse_args()

crop = None
if args.crop:
    try:
        crop = [int(v) for v in args.crop.split(",")]
    except ValueError:
        crop = []
    if len(crop) != 4:
        ap.error(f"--crop takes four integers X,Y,W,H, got {args.crop!r}")
    if crop[2] <= 0 or crop[3] <= 0:
        ap.error(f"--crop needs a positive width and height, got {args.crop!r}")

try:
    from PIL import ImageGrab
except ImportError:
    sys.exit(
        f"screen-grab: Pillow is missing; run this under `./mach python --virtualenv wpt`, "
        f"or install it with `{sys.executable} -m pip install Pillow`"
    )

if sys.platform in ("darwin", "win32"):
    x, y, w, h = crop or (0, 0, 0, 0)
    img = ImageGrab.grab(bbox=(x, y, x + w, y + h) if crop else None, all_screens=True)
    img.save(args.out)
    print(img.width, img.height)
    sys.exit(0)


def x_server_command(display):
    """The command name of the local X server owning `display`, or None."""
    host, _, rest = display.partition(":")
    number = rest.split(".")[0]
    if host not in ("", "unix") or not number.isdigit():
        return None
    try:
        with open(f"/tmp/.X{number}-lock", encoding="ascii") as f:
            pid = f.read().strip()
        with open(f"/proc/{pid}/comm", encoding="utf-8") as f:
            return f.read().strip()
    except (OSError, ValueError):
        return None


display = os.environ.get("DISPLAY")
if not display or os.environ.get("WAYLAND_DISPLAY"):
    sys.exit(
        "screen-grab: needs an X display: set DISPLAY, unset WAYLAND_DISPLAY, "
        "GDK_BACKEND=x11 (references/platform-linux.md, section DISPLAYS)"
    )
server = x_server_command(display)
if server != "Xvfb" and not args.any_display:
    sys.exit(
        f"screen-grab: DISPLAY={display} is served by {server or 'an unknown X server'}, "
        "not Xvfb, so it is probably the live desktop; run under `xvfb-run -a` "
        "(references/platform-linux.md, section DISPLAYS), or pass --any-display for another isolated server"
    )

try:
    # Pass the display explicitly: with xdisplay=None Pillow falls back to
    # gnome-screenshot or grim, which shoot the live Wayland desktop.
    img = ImageGrab.grab(xdisplay=display)
except OSError as e:
    sys.exit(f"screen-grab: cannot grab DISPLAY={display}: {e}")
if crop:
    x, y, w, h = crop
    if x < 0 or y < 0 or x + w > img.width or y + h > img.height:
        sys.exit(
            f"screen-grab: --crop {x},{y},{w},{h} reaches outside the "
            f"{img.width}x{img.height} root window"
        )
    img = img.crop((x, y, x + w, y + h))
img.save(args.out)
print(img.width, img.height)
