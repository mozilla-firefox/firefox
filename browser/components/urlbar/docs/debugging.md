# Debugging & Logging

The address bar and the search bars log through `UrlbarShared.getLogger()`.
Its `loglevel` pref sets the level for every logger, and the toolbox to open
depends on the process the code runs in.

## Logging from code

`UrlbarShared.getLogger({ prefix })` returns a console-like logger. The prefix
helps identify the source module of a message, and the logger for a given
prefix is created once.

```JavaScript
const logger = UrlbarShared.getLogger({ prefix: "MyModule" });
logger.debug("Starting a query", { queryContext });
```

The module is content-safe, so it can be called from a privileged content
process such as the New Tab one. Providers don't call it directly. `UrlbarProvider` has a
`logger` getter that prefixes its messages with `Provider.<name>`, and a
provider's `this.logger` messages therefore read `URLBar: Provider.<name>:`.

`browser.urlbar.loglevel` sets the maximum level to log, and defaults to
`Error`. Set it to `Debug` or `All` to see more. Each logger reads the pref when
it logs, so a change takes effect without a restart.

## Where the output appears

A logger created in a chrome realm uses `console.createInstance`. Its output
goes to the Browser Console, and also to the terminal on a local build, where
`devtools.console.stdout.chrome` defaults to true. Mochitests set the pref, so
this output reaches the test log.

A content process can't call `console.createInstance`, so the logger there wraps
the global `console`. It prefixes each message with `URLBar: <prefix>:`,
checks the level itself, and reads the level through
{doc}`UrlbarContentPrefs <utilities>`, which forwards to the parent. Its output
goes to the console of the page it runs in. It doesn't reach the terminal
unless `devtools.console.stdout.content` is true, and it never reaches a
mochitest's log.

A test that needs to see what content-process code did has two options that
reach the log. `dump()` works in content code. `info()` works in a function
that the test passes to `SpecialPowers.spawn`, which forwards it to the test.
`console.error()` doesn't reach the log;
[Failures are silent in a content process](message-path.md#failures-are-silent-in-a-content-process)
explains why.

## Logging through MOZ_LOG

A logger created in a chrome realm logs to the `URLBar` MOZ_LOG module, so
MOZ_LOG and about:logging can turn it on and send its output to a log file or
a profile. For example, set `URLBar:5` in about:logging, or `MOZ_LOG=URLBar:5`
in the environment, to log every level.

The content-process logger doesn't log to the `URLBar` module, and that module
doesn't control it. It wraps the global `console`, so its output goes to the
`console` MOZ_LOG module, and only `browser.urlbar.loglevel` sets its level.
Its messages start with `URLBar: <prefix>:`, like the module's, but `URLBar:5`
doesn't turn them on. The content process could log to the `URLBar` module
itself, but only `console.createInstance` names a module, and a content realm
can't call it.

## Which toolbox to open

Open the toolbox of the process that runs the code you are looking at.

| Code | Where it runs | Open |
| --- | --- | --- |
| `UrlbarParentController`, providers, the muxer, `ProvidersManager` | Parent process | Browser Toolbox (Ctrl+Alt+Shift+I) or the Browser Console (Ctrl+Shift+J) |
| The address bar's input and view | Browser window | Browser Toolbox |
| The New Tab bar's input and view | The content process of about:newtab | The page's toolbox, opened from the context menu's Inspect item |
| `UrlbarChildController` | With its input, so the same as the input | The toolbox of that input |

The two controllers log under different prefixes, `Controller` for the parent
and `ChildController` for the child, so one `loglevel` setting shows a query
crossing the boundary from both sides. The two sides log to separate consoles.

To run a test over the message path, see
{doc}`Testing Over the Message Path <testing>`.
