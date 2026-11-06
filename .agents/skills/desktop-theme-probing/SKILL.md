---
name: desktop-theme-probing
description: Use when measuring anything off a running Firefox (geometry, a computed style, media-query or pseudo-class state, a pref, window state, focus, a timing) through the devtools MCP, `firefox-probe.py`, a Marionette chrome-context probe or a scratch browser-chrome test; when writing or editing such a probe, even a small edit; when it needs `--no-headless`, `xvfb-run`, a headless compositor or the live desktop, or a headless run cannot see a value; when a chrome CSS, `.xhtml` or script edit does not show after a reload, or a launch dies on `Missing chrome or resource URL`; when a probe launch's state differs from a user's (remote-control stripes, an empty profile, about:blank); and before trusting a number a probe printed.
allowed-tools:
  - Bash(./mach python --virtualenv wpt .claude/skills/desktop-theme-probing/scripts/firefox-probe.py:*)
  - Bash(./mach python --virtualenv wpt .agents/skills/desktop-theme-probing/scripts/firefox-probe.py:*)
  - Bash(./mach python --virtualenv wpt -- .claude/skills/desktop-theme-probing/scripts/firefox-probe.py:*)
  - Bash(./mach python --virtualenv wpt -- .agents/skills/desktop-theme-probing/scripts/firefox-probe.py:*)
  - Bash(./mach python --virtualenv wpt .claude/skills/desktop-theme-probing/scripts/marker-probe.py:*)
  - Bash(./mach python --virtualenv wpt .agents/skills/desktop-theme-probing/scripts/marker-probe.py:*)
  - Bash(./mach python --virtualenv wpt -- .claude/skills/desktop-theme-probing/scripts/marker-probe.py:*)
  - Bash(./mach python --virtualenv wpt -- .agents/skills/desktop-theme-probing/scripts/marker-probe.py:*)
---

# How the desktop-theme family measures

This skill owns getting a trustworthy number out of a running Firefox. The
siblings own subjects: `desktop-theme-layout` geometry, `desktop-theme-colors`
what a declaration paints in each color regime (forced colors, prefers-contrast,
the color scheme), `desktop-theme-tokens` what a token resolves to,
`desktop-theme-screenshots` the before/after image pair.

**Measure before you theorise, and after you fix.**

**Name the invariant as numbers first**: what would be true if the page were
correct, in quantities you can read back (e.g. `documentElement.scrollHeight`
unchanged across an interaction, the used `border-color` equal to the surface
behind it, `element.height <= innerHeight`). Record the value before the change
as well as after.

When measuring says the mechanism is platform behavior rather than this
component's, report the measurement and stop there: reducing it to a standalone
testcase is outside this family.

**The host's and the target's platform file.** The host's platform file is
`references/platform-linux.md`, `references/platform-macos.md` or
`references/platform-windows.md`, for the OS the probe runs on; read it whole
before the first probe. The target's platform file is the one for the platform
the bug or change concerns; when that is not the host, read it whole too before
the first probe, since it says what users there see and which of it this host
can reproduce, simulate or not reach at all. All three have the same sections:
HEADLESS, DISPLAYS, FONT, SCROLLBARS, CONTRAST, COMPOSITING, MENUS, PATHS and
GRABS. Take facts about measuring (HEADLESS, DISPLAYS, MENUS, PATHS, GRABS) from
the host's file, and facts about what users see (CONTRAST, COMPOSITING, the FONT
and SCROLLBARS defaults) from the target's.

## 1. Pick the instrument

**Content document: the devtools MCP.** `evaluate_script` runs in the page and
returns JSON. Read every value you need in one call and return an object.
Before the first reading, `get_firefox_info` and compare the binary path with
this checkout's objdir: a reading off another tree's binary measures that tree's
CSS with no error. When it differs, `restart_firefox` with `firefoxPath`,
`headless: true` and a fresh `profilePath`.

**Chrome document: the MCP's privileged path**, `evaluate_privileged_script`
after a restart with `MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1`. Use it for `Services`,
prefs, and seeding profile state such as form history or engines.

Before the first `restart_firefox`, sweep or key press, read §1-§7 in
[`references/devtools-mcp.md`](references/devtools-mcp.md): a restart applies
the server's launch prefs over yours and a nonempty `env` replaces the
configured one, a
sweep outlives the command timeout, and `press_key` performs no default action.

**A one-shot chrome read without the MCP: the bundled probe runner.**
`.claude/skills/desktop-theme-probing/scripts/firefox-probe.py --context
chrome` launches a build, evaluates chrome-context JS and prints the result. It
needs the checkout's Marionette, so run it through `./mach python --virtualenv
wpt` from the checkout:

```sh
./mach python --virtualenv wpt .claude/skills/desktop-theme-probing/scripts/firefox-probe.py \
  --binary <objdir>/dist/bin/firefox --context chrome \
  --setpref ui.useAccessibilityTheme=1 -e 'return { ... };'
  # chrome's regime under that pref: the host's platform file, section CONTRAST
```

The help of either bundled script (`firefox-probe.py`, `marker-probe.py`) needs
`--` before the script path (`./mach python --virtualenv wpt --
…/firefox-probe.py --help`), since mach takes a bare `--help` for itself.
`--script-file FILE` replaces `-e` for a longer snippet.

Read [`references/probe-runner.md`](references/probe-runner.md) before writing
or running a snippet through it: it has what the help leaves out, such as the
pref read-back, parsing the output, teardown, the chrome sandbox, `--async` and
`--no-headless`. Before any non-headless run (`--no-headless`, `xvfb-run`, a
headless compositor or the live desktop), read §1 "Pick the display rung by what
the measurement needs" in
[`references/running-a-probe.md`](references/running-a-probe.md). On Linux, take
each rung's commands from the host's platform file, section DISPLAYS.

`<objdir>` is `obj-*/` in the checkout, or the path that
`./mach environment | rg -A1 '^object directory:'` prints under its label.
The binary's path under it is in the host's platform file, section PATHS.

**Shape the snippet for the instrument.** A snippet in the wrong shape fails
before anything runs, or exits 0 having printed `null`.

- `evaluate_script` and `evaluate_privileged_script` take a function expression.
- `firefox-probe.py -e` takes a bare body ending in `return`.
- `firefox-probe.py --async` takes a bare body that may `await` and must
  `return` its result. §2 "The chrome sandbox and `--async`" in
  `references/probe-runner.md` has the timeout triage.
- The harness form is a callback passed to `SpecialPowers.spawn`.
- In every instrument an element's `ownerGlobal` is gone (bug 2033243). In a
  privileged realm use `element.documentGlobal`, which is chrome-only like
  `isInstance`; in a page realm use `element.ownerDocument.defaultView` and
  `instanceof`.

**When the MCP is unusable, use the test harness as the instrument** (§3).

## 2. Keep the probe honest

### Confirm the build you are measuring

**Restart after an edit, don't reload**: a running instance caches the chrome
stylesheets it loaded. §2 "What is live at the next launch" in
[`references/build-freshness.md`](references/build-freshness.md) says which
edits need `./mach build faster` first. After a script or markup edit, restart
the MCP's kept profile with `MOZ_PURGE_CACHES=1`. A launch that fails, or a
service missing from a realm, can mean the build does not match the checkout;
before rebuilding, or concluding the service is absent, read §1 "Signatures and
repairs" there.

### A probe launch is not a user's profile

`firefox-probe.py`, the MCP and a mochitest differ from a user's profile in five
ways. Undo what the reading depends on, and name in the bug what you left.

- **The remote-control cue.** Every window's root gets `remotecontrol`, which
  stripes the address bar, stops its animation and adds a robot icon that takes
  space in it. Run `document.documentElement.removeAttribute("remotecontrol")`
  in every window you read, a window opened later included.
- **The automation pref set**: Marionette's `geckoinstance.py` for
  `firefox-probe.py`, `testing/profiles` for a mochitest, and for the MCP
  geckodriver's defaults (`testing/geckodriver/src/prefs.rs`) and its launch
  prefs, plus the remote agent's `RecommendedPreferences.sys.mjs` unless
  `remote.prefs.recommended=false`, which the tree's `.mcp.json` passes. All
  three start on a blank page and override prefs of individual features, so
  search the set's file for the feature's pref branch (about:newtab renders an
  empty root under `geckoinstance.py`'s activity-stream prefs).
  `geckoinstance.py` and `RecommendedPreferences.sys.mjs` also hide the
  bookmarks toolbar, so read the toolbar's state in an MCP launch rather than
  assume it. When the subject depends on a pref, pass the user's value back and
  read it back. A Nimbus variable maps to a pref only as its entry in
  `FeatureManifest.yaml` says (`fallbackPref`, `setPref`, or none); a pref named
  after the variable is accepted and the feature silently reads its default.
- **The empty profile.** UI that lists stored data (e.g. history, bookmarks,
  form history, engines, downloads, logins) shows its empty state; seed the
  store through the product's API (`--chrome-setup`, the privileged context).
- **The start page.** `firefox-probe.py` opens on about:blank, so chrome that
  reflects the loaded page (e.g. its identity, actions, history) shows its
  empty-page state; load a page (`--url`) before reading it.
- **The host's look and feel.** Before the first reading of a system color, font
  size, scrollbar or color scheme, see the host's platform file, section
  HEADLESS; without it the reading describes this host, not the user's.

Before the first probe of a surface, read how its browser-chrome tests open it
and wait for it, as §1 "Open the surface the way its tests do" in
[`references/probe-recipes.md`](references/probe-recipes.md) says. Before
reading a part inside a shadow root, installing a theme or testing another
platform's rule, read the heading there named for that task.

### Reach and read the state

- **Before probing a stylesheet you did not load from the product,** read
  `docs/code-quality/coding-style/css_guidelines.md` "Chrome-only media
  features go by the stylesheet, not the document": it lists the sheets that
  parse chrome-only features and `env(hairline)`.
- **Never drive internal state by hand to set up a measurement.** Calling
  `hidePopover()` or stripping an attribute puts the element in a configuration
  the product never produces; the tell is a computed value that makes no sense,
  such as an element with `open` set that does not match `:popover-open`.
  Re-open the page instead. Forcing state is legitimate for a picture
  (`desktop-theme-screenshots` §1); when you cannot reach the state through the
  UI, say so in the commit message and the bug.
- **Read an element-state pseudo-class through the inspector's lock.** Lock with
  `InspectorUtils.addPseudoClassLock(el, pc)`, one call per pseudo-class of a
  compound state (e.g. `:hover`, `:active`, `:focus-visible`, `:focus-within`,
  `:checked`, `:disabled`, `:open`, `:popover-open`), and assert
  `InspectorUtils.hasPseudoClassLock(el, pc)` after every lock: a pseudo-class
  not backed by element state (`:-moz-window-inactive`), or a typo, locks as a
  silent no-op. Before locking outside chrome, read §5 "Lock a pseudo-class" in
  [`references/probe-recipes.md`](references/probe-recipes.md). A lock cannot
  set an attribute state such as `[open]` or `[checked]` (drive it as the next
  bullet says), and a media-query state comes from a pref, such as
  `ui.prefersReducedMotion=1`.
- **Drive an attribute state (e.g. `open`, `checked`) through the product's
  entry point, and assert it landed before reading.** When the entry point is
  out of reach, setting the attribute is acceptable for a computed-style read,
  said beside the reading and removed in the same call. In an `--async` probe,
  call that entry point (e.g. a panel's own `show()`, a command) and assert that
  the DOM changed (a label or a value, not a count): a driven state that does
  not take leaves the previous one in place, and a poll returns it as a reading.
  Read the window's real size back too: `resizeTo` stops at the root's
  `min-width`.
- **In a widget with a shadow root, read the state where the widget sets it.** A
  custom element often sets `selected` or `open` on a part inside its shadow
  root, not on the host, so select through `el.shadowRoot` and lock the
  pseudo-class on that same part.
- **Finish whatever animates before reading.** Two frames do not finish a CSS
  transition, so a locked or driven state reads a value partway between the two
  states: `for (let a of el.getAnimations({ subtree: true })) if
  (el.documentGlobal.CSSTransition.isInstance(a)) a.finish();` (`instanceof`
  checks one global's prototype and silently skips an animation from another
  document; `finish()` throws on an infinite animation such as a spinner). A
  scroll animates too: set `general.smoothScroll` to false before a scroll
  measurement, or a read one call later catches it mid-flight.
- **An element outside the flat tree reads `""` for every property**: an
  unslotted child of a shadow host (a `<panel>`'s content until its first open),
  a node still inside a `<template>`. A popup declared inside an
  `<html:template>` is not in the document until its first use, so
  `getElementById` returns null. Open the panel once through its product entry
  point, or check that
  `getComputedStyle(el).display` is non-empty. A popup that closed between two
  calls keeps its descendants in the DOM with every rect at 0; filter on
  `getClientRects().length` before reading.
- **When a variant resolves to the same value in every regime the display can
  reach, prove the rule applies instead of reading values.**
  `InspectorUtils.getMatchingCSSRules(el)` with the lock on lists the rules
  matching `el`; the rule you expected and do not find there is the finding.
  Before writing the filter, read §6 "Match rules to an element" in
  [`references/probe-recipes.md`](references/probe-recipes.md). §4's marker
  values show the same thing as a paint.
- **Print the subject's own state beside every reading** (e.g. `disabled`,
  `open`, `checked`, the pseudo-classes locked, the root's `uidensity`, which
  normal density removes, so it reads `null`): a rule scoped by one silently
  does not match.
- **Under Nova a small window compacts itself** unless `browser.uidensity` has
  a user value (css_guidelines "UI density" gives the sizes); set it to 0 for
  normal density.
- **Read the element the declaration lands on, found from the rule's
  selector.** A component often paints a state on a wrapper or an inner part
  rather than on the element that holds the state, such as a background on a
  wrapper around the icon, and the holder then reads unchanged.
- **Restore every pref and inline style the probe set**, inside the same call
  that set them.
- **Match the pixel-sampling instrument to the document.** `ctx.drawWindow()` on
  the chrome window does not composite content; before sampling pixels, read §1
  "Chrome and content canvases" and §2 "The compositor's output" in
  [`references/measuring-pixels.md`](references/measuring-pixels.md).

## 3. Write a scratch probe

A throwaway browser-chrome test controls window size, reaches both processes,
and works when the MCP does not. Before writing one, read
[`references/scratch-probe.md`](references/scratch-probe.md): among other traps,
a harness error scores as `Passed: 0` rather than as an error. Delete it in the
same turn (§6).

## 4. Compare variants in one run

Set the property inline and re-measure in the same task:

```js
for (let [name, value] of [["A", "calc(100% - 8px)"], ["B", "calc(100vh - 8px)"]]) {
  el.style.maxHeight = value;
  await new Promise(r =>
    content.requestAnimationFrame(() => content.requestAnimationFrame(r)));
  out[name] = { /* … */ };
}
el.style.maxHeight = "";
```

Set markers whenever the tokens that could paint the property resolve to the
same value in the regime under test (`token-resolve.py --effective` shows
whether they differ), since a real read then cannot tell which rule painted.
Probe in the regime the task is about (`desktop-theme-colors` §1). Set a
distinct marker per token on the root (`root.style.setProperty("--x-hover",
"rgb(255, 0, 0)")`, one color per token), read the property in each state, and
map the color back to the token. Wherever forced colors are `active` (a content
page, or chrome under `desktop-theme-colors` §2's override), the adjustment
replaces a literal marker, read or shot, on any property it forces (`color`,
`background-color`, the rest of `desktop-theme-colors` §3). Mark with distinct
system colors (e.g. `Highlight`, `Mark`) there, or set `forced-color-adjust:
none` on the marked element when its own colors are not the subject. Restore the
root's `style` attribute in the same task. `scripts/marker-probe.py` is that
probe as one command; read its help first. A reading that maps to no token means
the marked tokens do not paint there, or one is redefined between the root and
the element; mark the redefining element instead.

Wait two `requestAnimationFrame`s, not one, before reading back. Awaiting rules
out the default synchronous `-e` mode, so this pattern needs the harness or
`--async` (§1).

Read each variant's computed value back and assert it took: a plain inline style
loses to any `!important` declaration. `el.style.setProperty(prop, value,
"important")` wins within the element's tree, but an `!important` `:host` rule
beats even that on the host; append a `<style>` to its shadow root. Don't reach
for `adoptedStyleSheets` from a privileged sandbox; spreading the content-realm
array throws on `Symbol.iterator`.

The same loop does differentials: toggle the thing you suspect (e.g. a class, a
pref, an attribute) and read the same list of values on either side. To test
removing a declaration, delete it from its rule's CSSOM
(`rule.style.removeProperty(prop)`, the rule from
[`references/probe-recipes.md`](references/probe-recipes.md) §6, a shadow
root's sheet included), read, and set it back with its priority; §7 there does
this for a custom property.

A change with no inline toggle (e.g. a wrapper element removed, declarations
moved between elements, a different element made the scroller) gets a markup
differential: each side's files on the live paths, the same sweep once per side,
the two tables diffed cell by cell. Procedure and noise sources:
[`references/markup-differential.md`](references/markup-differential.md).

## 5. Judge the result against a variant matrix

The regression test that replaces the scratch probe is the deliverable. An
assertion can pass for the wrong reason: `scrollHeight > clientHeight` is true
when `clientHeight` is 0. Run the new test against each candidate, including the
unpatched baseline and any intermediate form, and check that every assertion
fails for exactly the variants it should:

| CSS | assertion A | assertion B |
|---|---|---|
| upstream | fails | fails |
| intermediate | passes | fails |
| final | passes | passes |

An assertion that never fails anywhere tests nothing. One that fails
everywhere is measuring the environment.

## 6. Leave the tree clean

Delete the scratch test, revert the manifest entry, remove what the run
installed under the objdir's `_tests`, restore any CSS you edited for a probe,
and confirm with `git status`.

**Run a before half by hand as one command that reverts the changed paths in
the working tree, launches, and restores them**, with a `trap` on exit and a
timeout that covers every launch in it: a call that outruns the tool's default
timeout moves to the background with the base files still in place.
`--worktree` below means the files on disk, not `git worktree`, so this works
in any clone and leaves the index alone.

- **A committed change**: check `git diff --quiet -- <paths>` first, then
  `git restore --source=<base> --worktree -- <paths>`, run, and
  `git restore --source=HEAD --worktree -- <paths>`. In a reviewer's tree, where
  HEAD is the base and the patch is a ref, place the patch's side the same way
  with `--source=<patch-ref>`.
- **An uncommitted change**, a jj working-copy commit included (git sees it as
  uncommitted, so the `git diff --quiet` check sends it here): save the diff to
  a file, `git apply -R` it, check that `git diff --stat` prints nothing (a diff
  saved before a later edit no longer reverses), run, then `git apply` the saved
  file. Don't run jj commands between the halves, since jj snapshots the
  working copy.

After any run, a killed one included, confirm with `git status` that only your
change is left.

Re-read whatever a pref the probe set controlled; a LookAndFeel override
(`ui.*`) may need a restart before it reads back at its default.
`firefox-probe.py` removes its browser log on success and keeps
`firefox-probe-gecko-*.log` in the system temp directory (the host's platform
file, section PATHS, names it) after a failure, and those names do not say which
run wrote them. Pass `--gecko-log artifacts/<prefix>-gecko.log` so the logs you
leave are yours by name; a log given this way stays after a success too, so
delete it by name. Never delete the temp-directory ones by glob: other runs on
the host write there too.
