# The devtools MCP as a probe: binary, profile, prefs, keys, privileged path

The server's state outlives your calls, so each reading measures the binary,
profile and prefs the last call left behind.

## 1. Binary and profile

The server's first call launches the binary in the server's configuration,
headless only under `--headless`, before you can pass another. Under the
in-tree `.mcp.json`, `./mach firefox-devtools-mcp` sets `--firefox-path` to the
build of the checkout the session started in and `--profile-path` to its
`<objdir>/tmp/profile-default`, and the server falls back to the system Firefox
only when that checkout has no build. That default profile belongs to the
binary that created it, and another tree's binary exits at once on it, so the
restart `SKILL.md` §1 prescribes takes a fresh `profilePath` of your own.

## 2. Prefs

A fresh `profilePath` is written from the server's current launch configuration,
prefs included. `restart_firefox` merges its `prefs` into that configuration,
so a profile created after another run's restart on the same server arrives in
that run's regime; `set_firefox_prefs` and `Services.prefs.set*` through
`evaluate_privileged_script` change only the running browser and its
`prefs.js`. In MCP 0.10.7 a `restart_firefox` issued while no browser is running
drops its `prefs`. A pref omitted from a later `restart_firefox` on the same
profile stays in its `prefs.js`, and one the launch configuration sets reads
back at that value after a restart, whatever was set live. Pass
every regime pref explicitly in each restart's `prefs`, the default value
included (`get_firefox_info` echoes that accumulated launch configuration, not
the live profile's values), and read the regime back afterwards
(`desktop-theme-colors` §1). Read prefs back through
`evaluate_privileged_script` (`Services.prefs`), not `get_firefox_prefs`, which
fails with `Cannot read properties of null (reading 'exists')` under
`MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1`. `set_firefox_prefs` itself needs that
variable in the launch environment.

## 3. Startup cache and environment

`restart_firefox` keeps the profile's startup cache, which serves the scripts
loaded at startup (`.mjs`, `.sys.mjs`, subscripts) and `browser.xhtml` as first
cached, until a restart sets `MOZ_PURGE_CACHES=1` in its `env`. A nonempty `env`
list replaces the configured environment and an empty one keeps it, so restate
`MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1` in the same list.

## 4. Command timeout

A call has a fixed BiDi command timeout, and a sweep of more
than a handful of cells outlives it, so detach the loop: an async function that
pushes rows onto `window.__sweep` and returns `"started"` at once, then a second
call that polls `window.__sweep.done` and returns the rows.

## 5. Screenshots

The screenshot tools' `saveTo` is confined to
`~/.firefox-devtools-mcp/output`; an absolute path elsewhere is refused and a
relative one resolves against the server's working directory, the checkout the
session started the server in, and is written there with no error as long as it
stays inside that directory. Pass an absolute path under that directory
and copy the file into `artifacts/` afterwards.

`screenshot_page` renders no focus ring (checked 2026-09), so a screenshot
cannot show focus state. To test
whether a ring is visible, give the focused element a plain
`outline: 4px solid red` and sample the pixels.

## 6. Keys

`press_key` delivers a keydown the page observes (the right target,
`defaultPrevented` false) but performs no default action: it does not scroll a
focused scroll container, and ArrowDown does not scroll the page (checked
2026-09). For a trusted key, synthesize it with `nsITextInputProcessor` in the
`browser.xhtml` context through the privileged path (§7):

```js
const win = Services.wm.getMostRecentWindow("navigator:browser");
const tip = Cc["@mozilla.org/text-input-processor;1"]
  .createInstance(Ci.nsITextInputProcessor);
tip.beginInputTransactionForTests(win);
const ev = new win.KeyboardEvent("", { key: "ArrowRight", code: "ArrowRight", keyCode: 39 });
tip.keydown(ev);
tip.keyup(ev);
```

The event reaches the focused element in the remote browser and performs its
default action. `windowUtils.sendKeyEvent` is gone and throws `is not a
function`.

Set `general.smoothScroll` to false first (`SKILL.md` §2 "Reach and read the
state"). Focus from content, press from chrome, read from content.

## 7. The privileged path

`docs/ai-agent-tools/firefox-devtools-mcp.md` "Privileged Context Access" has
the steps. A nonempty `restart_firefox` `env` list replaces the configured
environment (§3), so
restate `MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1` in every later restart that sets
another variable.
