# The devtools MCP as a probe: binary, profile, prefs, keys, privileged path

The server's state outlives your calls, so each reading measures the binary,
profile and prefs the last call left behind.

## 1. Binary and profile

The server's first call launches the binary in the server's configuration,
headless only under `--headless`, before you can pass another. Under the
in-tree `.mcp.json`, that is this checkout's build with the profile path
`<objdir>/tmp/profile-default` (`docs/ai-agent-tools/firefox-devtools-mcp.md`,
"Installation and Configuration"). That default profile belongs to the binary
that created it, and another tree's binary exits at once on it, so the restart
`SKILL.md` §1 prescribes takes a fresh `profilePath` of your own.

## 2. Prefs

- A fresh `profilePath` still starts in the server's accumulated launch prefs,
  so a profile created after another run's restart on the same server arrives
  in that run's regime.
- Pass every regime pref explicitly in each restart's `prefs`, the default
  value included, and read the regime back afterwards
  (`desktop-theme-colors` §1). A pref set live survives a restart unless the
  launch configuration sets it, in which case it reverts to that value.
- Issue `restart_firefox` only while a browser is running (`list_pages`
  first starts one); issued with none, it drops every pref.
- `get_firefox_info` echoes the launch configuration, not the live profile's
  values. Read prefs back through `evaluate_privileged_script`
  (`Services.prefs`), not `get_firefox_prefs`, which failed with
  `Cannot read properties of null (reading 'exists')` under
  `MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1` (checked 2026-09).
- `set_firefox_prefs` needs `MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1` in the launch
  environment.

`docs/ai-agent-tools/firefox-devtools-mcp.md`, "Preferences", explains how a
restart merges prefs and why they persist in the profile.

## 3. Startup cache and environment

After a script or markup edit, restart with `MOZ_PURGE_CACHES=1` in `env`: the
kept profile's startup cache serves the scripts loaded at startup (`.mjs`,
`.sys.mjs`, subscripts) and `browser.xhtml` as first cached. A nonempty `env`
list replaces the configured environment, so restate
`MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1` in the same list
(`docs/ai-agent-tools/firefox-devtools-mcp.md`, "Environment variables" and
"Profile and startup cache").

## 4. Command timeout

A sweep of more than a handful of cells outlives the command timeout, so
detach the loop: an async function that pushes rows onto `window.__sweep` and
returns `"started"` at once, then a second call that polls
`window.__sweep.done` and returns the rows. The limits are in
`docs/ai-agent-tools/firefox-devtools-mcp.md`, "Script Timeouts".

## 5. Screenshots

Pass `saveTo` an absolute path under `~/.firefox-devtools-mcp/output` and copy
the file into `artifacts/` afterwards.
`docs/ai-agent-tools/firefox-devtools-mcp.md`, "Saving Output to Files", says
where else a path may point.

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
the steps. Restate `MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1` in every later restart
that sets another variable (§3).
