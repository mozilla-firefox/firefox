# Picking a display for a probe

Settle the display before reading any number.

## 1. Pick the display rung by what the measurement needs

Name what the measurement requires (a computed value, the platform's own widget
and theme layer, a window manager, an input seat), take the cheapest rung that
supplies it, and confirm it did.

**`--headless` is the right default for a theming probe, on any platform.**
Computed styles, element rects, custom-property values, media-query state and
the forced-color adjustment all resolve normally with no display at all. What it
changes:

- **The host's look and feel is not the user's** (`SKILL.md` §2).
- **Native event synthesis is a stand-in.** Async pan/zoom (APZ) itself runs
  headless, but the native pan handler is the harness's own and momentum comes
  from `apz.test.headless.simulate_momentum`, so a gesture-driven measurement
  measures the stand-in.
- **No drag service on Linux**, so `EventUtils.startDragSession` throws before
  the first assertion there.
- **It has noise of its own.** Some host-specific assertions fail only here
  (its platform file, section HEADLESS). Treat a
  headless-only failure as suspect until checked elsewhere.

**When the widget backend matters, the next rung is an isolated real display,
never the user's session**, where a stolen focus or a closed window produces
failures indistinguishable from real ones. Before any non-headless run, read
the host's platform file, section DISPLAYS: it lists the rungs the host has, in
rising order of what each supplies (backend, window manager, input seat), and
the environment a rung must strip to stay off the user's desktop.

Three rules hold whatever supplies the display: allocate a fresh one rather than
a fixed number; never kill a display server or delete a lock file you did not
create (the host is shared with the user and other agents); grep the log for the
display's own startup failure separately, since a display that never came up
otherwise scores as a wall of test failures.

**Ask for the user's live session** only when the measurement needs the real
backend plus a window manager plus an input seat and no isolated rung supplies
them. Say what you will run and
roughly how long, ask them to stay off mouse, keyboard and window switching, and
tell them when the desktop is theirs again. Ask once for a whole batch rather
than per iteration. **A run they touched is unscored**; discard it.

**A try run is the isolated real display you do not have locally.** When no rung
can supply what the measurement needs, report the reproduction as
environment-limited and ask the user how to proceed.

## 2. The devtools MCP path

The server's binary, profile, pref and screenshot rules are in
[`devtools-mcp.md`](devtools-mcp.md); read it before the first
`restart_firefox`.

## 3. Run the probe

Before running a scratch browser-chrome probe, read §2 "Register and run it" in
[`scratch-probe.md`](scratch-probe.md).
