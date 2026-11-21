---
name: profile-capture
description: >
  Record a Firefox performance profile with the Firefox MCP. Use when
  diagnosing slowness in Firefox: startup, page load, an interaction, a browser
  feature, a benchmark, a reduced test case, to obtain profiles - then analyze
  with profile-analysis.
allowed-tools:
  - mcp__firefox__*
  - Skill(profiler-analysis)
  - Read
  - Grep
  - Glob
---

# Capturing a Firefox profile

## Recording

Use the Firefox MCP:

```
profiler_start(preset="firefox-platform")
... reproduce ...
profiler_stop()   -> path of the saved profile
```

- **Page load**: `navigate_page(wait: "complete")`. `set_network_cache("bypass")`
  for a cold load, `"default"` for a warm one.
- **An interaction**: `take_snapshot`, then `click_by_uid`, `press_key`,
  `fill_by_uid`. When the interaction is short, repeat it so there is enough to
  sample. `evaluate_script` can run the action in a loop from inside the page.
- **Browser UI**: `evaluate_privileged_script` with a context id from
  `list_privileged_contexts`. This needs system access:
  `restart_firefox(env=["MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1"])`.

`profiler_is_active` tells whether a recording is already running.
`profiler_stop(discard=true)` throws a botched one away.

Presets are the keys of `presets` in
`devtools/shared/performance-new/prefs-presets.sys.mjs`. `firefox-platform` is
the default. Do not use `web-developer` for gecko investigation as it has no
native stacks. For a feature or thread no preset covers, pass `entries`,
`interval`, `features`, and `threads` explicitly instead of a preset. The two
cannot be combined.

Keep the recording short. Start the profiler after setup, stop it
as soon as the action is done.

## Startup

`profiler_start` is too late for startup:

```
restart_firefox(env=["MOZ_PROFILER_STARTUP=1"])
profiler_stop()
```

`profiler_stop` saves the profile the startup profiler has been recording. The
other `MOZ_PROFILER_STARTUP_*` variables go in `env` too; the binary lists them
with `MOZ_PROFILER_HELP=1`. `env` replaces what the previous `restart_firefox`
set, so repeat `MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1` and any other variable still
needed.

## Analysis

Invoke **profiler-analysis** with the profile path and say it is unsymbolicated,
so it loads with `--with-samply`. Don't read the profile JSON yourself.

## Which Firefox build

By default, the MCP uses objdir build; `get_firefox_info` shows which binary is running.
To profile a different Firefox, pass `firefoxPath` to `restart_firefox`.

## Don't profile debug builds for performance analysis

A debug or non-optimized build (`--enable-debug` or `--disable-optimize` in the
mozconfig) produces profiles that do not reflect what users run. Say so before
recording.
Profiles from debug builds can still be useful for debugging, but any performance
numbers from them can be discarded.
