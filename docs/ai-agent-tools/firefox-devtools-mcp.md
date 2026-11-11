# Firefox DevTools MCP for AI Agent Testing

## Overview

The `firefox-devtools-mcp` is a Model Context Protocol (MCP) server that
enables AI assistants (such as Claude Code) to automate and interact with
Firefox through WebDriver protocols (BiDi and Classic). This tool is
particularly useful for:

- Automating browser interactions for testing and debugging
- Attempting to reproduce issues on live websites
- Capturing Firefox debug logs (MOZ_LOG) while controlling the browser
- Dynamically changing Firefox configuration without restarting

The MCP server provides programmatic access to a Firefox build, including
DevTools capabilities such as page navigation, DOM inspection, network
monitoring, console logs, user input simulation, and Firefox logging. It isn't
complete yet, but can already be useful.

This documentation refers to the official Mozilla MCP for
Firefox development: <https://github.com/mozilla/firefox-devtools-mcp>

### Key Features

- **WebDriver BiDi Protocol**: Uses the modern WebDriver BiDi protocol via
  Selenium WebDriver
- **WebDriver Classic Fallback**: Uses WebDriver Classic (Marionette) for
  features not yet ported to BiDi (notably Chrome context usage)
- **Local Firefox Support**: Can connect to custom Firefox builds (e.g., local
  development builds), or typical release/beta/nightly/esr builds, present on
  the machine
- **Real-time Interaction**: Provides live access to browser state and network
  activity
- **Firefox Output Capture**: stdout/stderr capturing, including setting and
  reading MOZ_LOG

## Prerequisites

### Required Software

- **Node.js**: Version 20.19.0 or higher
- **Firefox**: system installation (all channels supported) or local build
- **Claude Code** (or compatible MCP client): For AI-assisted automation

## Installation and Configuration

The Firefox DevTools MCP server is **automatically configured** when using
Claude Code in the Firefox repository, via the `.mcp.json` file at the root
of the repository.

That configuration runs `./mach firefox-devtools-mcp`, which starts the server
on the checkout's own build. Unless you pass `--firefox-path` yourself, the
command adds it with the path of the build, and unless you also pass
`--profile-path`, it adds `--profile-path` with `<objdir>/tmp/profile-default`,
the profile `./mach run` uses. Given your own `--firefox-path` and no
`--profile-path`, the server runs on a temporary profile. Without a build
it warns and passes neither, so the server uses the system Firefox and a
temporary profile. `.mcp.json` also sets the `remote.prefs.recommended=false`
and `browser.smartwindow.enabled=true` prefs and passes
`--enable-privileged-context` and `--enable-script`, which together enable
every tool module (see [Available Capabilities](#available-capabilities)).

On Windows, the server is started with the Python interpreter found as
`python3` on the `PATH`, which the MozillaBuild shell provides. If Claude Code
is started outside of the MozillaBuild shell, set `MACH_PYTHON` to the
MozillaBuild Python before starting it, for instance:

```powershell
$env:MACH_PYTHON = "C:\mozilla-build\python3\python3.exe"
```

For installation in other projects or at user scope, Claude Code can configure
the MCP server with a single command:

```bash
claude mcp add firefox-devtools npx @mozilla/firefox-devtools-mcp-moz
```

This command registers the MCP server with your Claude Code configuration for
the current project (typically `project/.claude/`).

If a global installation is preferred (so that it works from multiple project
directories), it can be installed at the user scope:

```bash
claude mcp add firefox-devtools npx @mozilla/firefox-devtools-mcp-moz --scope user
```

This changes `~/.claude.json`, either in the project section, or in the global
section (if installed at the user scope).

Without options, this form starts the system Firefox on a temporary profile
with only the `basic` tool preset, which leaves out `restart_firefox`,
`send_bidi_command`, the network, console, debugging, profiler and prefs tools,
and the privileged tools. Add `--tool-preset mozilla` to get them.

After installation and/or configuration restart Claude Code to load the new MCP
server:

```bash
# Exit Claude Code, then restart it, continuing the last session
claude -c
```

The MCP server will automatically start when Claude Code initializes.

### Configuration options

The MCP server supports several command-line options. It's preferable to start
with no options, but they exist:

```{eval-rst}
.. list-table::
   :widths: 25 75
   :header-rows: 1

   * - Option
     - Description
   * - ``--firefox-path <path>``
     - Path to Firefox executable. Without it, the server uses the system
       Firefox; ``./mach firefox-devtools-mcp`` passes the checkout's build.
   * - ``--headless``
     - Run Firefox without UI (useful for automated testing)
   * - ``--viewport <size>``
     - Set initial viewport size (e.g., ``1280x720``)
   * - ``--profile-path <path>``
     - Keep a persistent profile under this directory. The server runs Firefox
       on a ``firefox_devtools_mcp_profile`` subdirectory of it, created on
       first use with a copy of the directory's ``prefs.js``, unless the path
       is already such a subdirectory. Without it, Firefox gets a temporary
       profile.
   * - ``--start-url <url>``
     - Initial URL to load (default: ``about:blank``)
   * - ``--accept-insecure-certs``
     - Ignore TLS certificate errors (use with caution)
   * - ``--firefox-arg <arg>``
     - Additional arguments to pass to Firefox
   * - ``--env <KEY=VALUE>``
     - Set environment variables for Firefox (can be used multiple times)
   * - ``--pref <name=value>``
     - Set a Firefox pref at startup (can be used multiple times). ``true``
       and ``false`` become booleans and a value of digits becomes an
       integer, so a string pref cannot take a numeric value this way.
   * - ``--tool-preset <name>``
     - Tool modules to enable: ``slim``, ``basic`` (the default),
       ``developer``, ``mozilla`` or ``all``. Each preset includes the ones
       before it, and ``mozilla`` enables every module.
   * - ``--tools <module>...``
     - Explicit list of tool modules to enable, overriding ``--tool-preset``
   * - ``--enable-script``, ``--enable-privileged-context``
     - Deprecated forms that add the ``developer`` and ``mozilla`` presets
       respectively. They are ignored when ``--tools`` is given.
   * - ``--unrestricted-save-paths``
     - Let tools save files anywhere (see "Saving Output to Files" below)
   * - ``--output-file <path>``
     - Path where Firefox output (stdout/stderr) will be captured. Without it,
       output is captured under ``~/.firefox-devtools-mcp/logs/`` only when
       environment variables are set.
```

Example with multiple options:

```json
{
   "type": "stdio",
   "command": "npx",
   "args": [
    "@mozilla/firefox-devtools-mcp-moz",
    "--firefox-path",
    "/home/developer/firefox/obj-x86_64-pc-linux-gnu/dist/bin/firefox",
    "--headless",
    "--viewport",
    "1920x1080",
    "--env",
    "MOZ_LOG=HTMLMediaElement:5"
  ]
}
```

## Available Capabilities

The `firefox-devtools-mcp` provides tools through the MCP protocol. Tool names
are generally self-describing. Simply ask your AI assistant to perform tasks - it
will use the appropriate tools.

Importantly, you can instruct the agent to run a different Firefox than the one
it picked, e.g. your own build, or some specific version, by telling it the
path: it isn't necessary to configure it.

Tools come in modules, which the `--tool-preset` and `--tools` options enable.
The list below is the one in version 0.10.7, grouped by module; the server is
under active development, so newer versions may have more:

**Page Management** (`pages`):
`list_pages`, `new_page`, `navigate_page`, `select_page`, `close_page`,
`get_page_text`

**Snapshots** (`snapshot`):
`take_snapshot`, `resolve_uid_to_selector`, `clear_snapshot`

**Input** (`input`):
`click_by_uid`, `hover_by_uid`, `fill_by_uid`, `fill_form_by_uid`,
`drag_by_uid_to_uid`, `upload_file_by_uid`, `press_key`, `type_text`

**Network** (`network`):
`list_network_requests`, `get_network_request`, `set_network_cache`

**Console** (`console`):
`list_console_messages`, `clear_console_messages`

**Screenshots and Screencasts** (`screenshot`, `screencast`):
`screenshot_page`, `screenshot_by_uid`, `screencast_start`, `screencast_stop`
(screencasts need Firefox 154 or later)

**Printing and Downloads** (`print`, `downloads`):
`print_page`, `list_downloads`, `clear_downloads`, `set_download_behavior`

**Dialogs & Navigation** (`utilities`):
`accept_dialog`, `dismiss_dialog`, `navigate_history`, `set_viewport_size`

**Firefox Management** (`management`, `launch`):
`get_firefox_info`, `get_firefox_output`, `close_firefox_session`,
`restart_firefox`

**Extensions** (`webextension`):
`install_extension`, `uninstall_extension`

**Profiler** (`profiler`, Firefox 154 or later):
`profiler_start`, `profiler_stop`, `profiler_is_active`

**Script Evaluation** (`script`):
`evaluate_script`

**Debugging** (`debugging`, Firefox 153 or later):
`enable_debugger`, `list_scripts`, `get_script_source`, `set_logpoint`,
`remove_logpoint`, `get_logpoint_results`

**Preferences** (`prefs`):
`get_firefox_prefs`, `set_firefox_prefs`

**Privileged Contexts (chrome/XUL code)** (`privileged`):
`list_privileged_contexts`, `select_privileged_context`,
`evaluate_privileged_script`, `list_extensions`

**Raw WebDriver BiDi** (`bidi`):
`send_bidi_command`

The `prefs` and `privileged` tools need system access (see
[Privileged Context Access](#privileged-context-access)).

## Restarting Firefox

`restart_firefox` closes the browser and records a new launch configuration,
which the next tool call starts Firefox with. While a browser is running, the
arguments it omits keep their current values.

### Preferences

The `prefs` passed to `restart_firefox` are merged into the launch
configuration, so a pref set by one restart still applies after every later
restart from the same server. `get_firefox_info` lists that configuration (the
binary, the profile path as given, the environment variables and the prefs),
not values read from the running browser. `set_firefox_prefs` changes the
running browser and leaves the launch configuration alone.

When no browser is running, `restart_firefox` builds the configuration afresh
from the binary, profile, environment, headless flag and start URL. It drops
every pref, its own and those from `--pref` included, and turns headless mode
off unless it is passed
([bug 2091473](https://bugzilla.mozilla.org/show_bug.cgi?id=2091473)).

On a persistent profile, geckodriver writes the launch prefs into the profile's
`user.js`, and Firefox saves them to `prefs.js` like any other changed pref.
A pref set at an earlier launch therefore keeps its value at a later launch on
the same profile that omits it, and a pref the launch sets is back at the
launch value after a restart, whatever was set while the browser ran. To start
from known values, pass every pref the run depends on, or a new `profilePath`.

### Environment variables

An `env` list with at least one entry replaces the whole launch environment,
and an empty or omitted one keeps it. A restart that adds a variable has to
repeat the ones still needed, `MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1` in particular:

```text
restart_firefox(env=["MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1", "MOZ_LOG=HTMLMediaElement:5"])
```

### Profile and startup cache

A restart keeps the profile, and with it the startup cache, which Firefox
reuses until the profile is opened by a different build. To start with an empty
startup cache, add `MOZ_PURGE_CACHES=1` to the `env` list. A `profilePath` is
resolved like `--profile-path`, so Firefox runs on its
`firefox_devtools_mcp_profile` subdirectory.

## Script Timeouts

`evaluate_script` fails after 5 seconds unless its `timeout` argument (in
milliseconds) allows longer. `evaluate_privileged_script` has a fixed timeout of
10 seconds, so for longer work, start it without awaiting it and read the
result from a later call.

## Saving Output to Files

Tools with a `saveTo` argument (e.g. the screenshot and script tools) write the
result to a file instead of returning it. `saveTo: true` writes a generated name
under `~/.firefox-devtools-mcp/output/`. A relative path has to stay within the
server's working directory, and an absolute one within
`~/.firefox-devtools-mcp/output/`; neither may point into the profile or the
captured output file. Starting the server with `--unrestricted-save-paths` lifts
these limits.

## Working with MOZ_LOG

Firefox's MOZ_LOG system provides detailed debug output from various modules.
The `firefox-devtools-mcp` captures this output automatically when environment
variables are set.

You can enable MOZ_LOG with something like this:

```text
Developer: "Enable HTMLMediaElement logging at level 5"

AI uses: restart_firefox(env=["MOZ_LOG=HTMLMediaElement:5"])
```

### Common MOZ_LOG Modules

For a useful list of available MOZ_LOG modules and their descriptions, see
`toolkit/content/aboutLogging/aboutLogging.mjs` in the Firefox repository, or search
searchfox.org with something like this [searchfox query](https://searchfox.org/firefox-main/search?q=LazyLogModule.*%5C%28%22&path=&case=false&regexp=true).

## Privileged Context Access

For debugging privileged Firefox code (chrome/XUL), enable system access:

```text
restart_firefox(env=["MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1"])
```

Then use `list_privileged_contexts`, `select_privileged_context`, and
`evaluate_privileged_script` to access `gBrowser`, `Components`, and other
privileged APIs. `get_firefox_prefs`, `set_firefox_prefs` and `list_extensions`
need the same variable. A later restart that sets other variables has to repeat
it (see [Environment variables](#environment-variables)).

At the moment, the MCP relies on WebDriver Classic (Marionette) for Chrome
context APIs. While WebDriver BiDi and Classic are meant to work seamlessly
together, some WebDriver BiDi features might not work when getting executed for
chrome browsing contexts. Chrome support for WebDriver BiDi is tracked on the
following [meta bug](https://bugzilla.mozilla.org/show_bug.cgi?id=1722679).

## Troubleshooting

Try to update to the latest version:

```bash
./mach npx @mozilla/firefox-devtools-mcp-moz@latest
```

then quit when seeing "Ready to accept tool requests".

### MCP Server Not Working

1. Verify the `.mcp.json` at the root of the repository is present.
2. Verify Node.js is available via `./mach npx --version`.
3. On Windows, if the server fails to start with only a `CONNECTION_CLOSED`
   error, `python3` is likely not on the `PATH`. Either start Claude Code from
   the MozillaBuild shell, or set `MACH_PYTHON` as described in
   [Installation and Configuration](#installation-and-configuration).
4. Ask in #ai4dev or #developers on chat.mozilla.org
