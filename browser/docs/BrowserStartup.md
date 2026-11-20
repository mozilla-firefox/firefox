# Browser Startup

## Invoking your code on browser startup

The first rule of running code during startup is: don't.

We take performance very seriously and ideally your component/feature should
initialize only when needed.

If you have established that you really must run code during startup, register
a category manager entry in `browser/components/BrowserComponents.manifest`.
See [the category manager indirection docs](./CategoryManagerIndirection.md) for
more details on the mechanism.

### Available category manager entries (in order)

Categories come in two types: per-application (fired once at startup) and
per-window (fired for each browser window). The following categories
are listed in the order they fire.

#### Per-application categories (invoked once, not per-window)

| Category | Notes |
|---|---|
| `browser-before-ui-startup` | Please don't do it unless absolutely necessary. This will run code before the first browser window appears on the screen and make Firefox seem slow. |
| `browser-first-window-ready` | After the first browser window's `browser-window-delayed-startup` call. |
| `browser-idle-startup` | Preferred entrypoint for most startup code. Runs during a user-idle period after startup. |
| `browser-best-effort-idle-startup` | Like `browser-idle-startup`, but your code **may not run at all** if the browser is shut down quickly. |

#### Per-window categories (invoked for each browser window)

| Category | Notes |
|---|---|
| `browser-window-before-initial-xul-layout-document-preparation` | Before XUL layout; before the tab bar visibility update. |
| `browser-window-before-initial-xul-layout` | Before XUL layout; after the tab bar visibility update and document icon are set. |
| `browser-window-domcontentloaded-before-tabbrowser` | During `DOMContentLoaded`, before the `Tabbrowser` is created. |
| `browser-window-domcontentloaded-tabbrowser` | During `DOMContentLoaded`, creates the `Tabbrowser`. **Internal category - do not add consumers here.** |
| `browser-window-domcontentloaded` | During `DOMContentLoaded`, after the `Tabbrowser` is created. Preferred entry point for `DOMContentLoaded` initialization. |
| `browser-window-load-before-sessionstore-init` | During the window `load` event, before SessionStore initializes the window. For components that must be ready before session restore runs. |
| `browser-window-load` | During the window `load` event, after SessionStore has initialized the window. |
| `browser-window-delayed-startup` | After the window's first paint, for code that can run after the window is visible. |
| `browser-window-sessionstore-initialized` | Fired from the window's delayed startup once SessionStore is initialized. |
| `browser-window-idle-tasks` | Invoked once all windows have been restored, for per-window work that can wait until the browser is idle. Each consumer runs in its own idle callback, and is dropped if the window closes before it runs. |
| `browser-window-location-change` | Fires on every `XULBrowserWindow.onLocationChange` for the window. |
| `browser-window-unload-begin` | At the very start of window unload, before checking whether the window finished loading. Runs even if the window never fully loaded. |
| `browser-window-unload` | During window unload, before `Tabbrowser` is destroyed. Preferred entry point for window unload cleanup. |
| `browser-window-unload-delayed-startup` | During window unload, before `Tabbrowser` is destroyed. Teardown counterpart to `browser-window-delayed-startup`; only fires if the window's delayed startup actually ran. |
| `browser-window-unload-tabbrowser` | During window unload, destroys the `Tabbrowser`. **Internal category - do not add consumers here.** |
| `browser-window-final-unload` | During window unload, after `Tabbrowser` is destroyed. |

Other useful points in startup are:

- `BrowserGlue`'s `_onWindowsRestored`, and/or the observer service's
  `sessionstore-windows-restored` topic, and/or a category manager call that
  should replace the `BrowserGlue` list of direct calls. This fires after
  session restore has completed restoring all windows (but before all pages
  that may have been restored have necessarily loaded). Note that this is
  guaranteed to fire even if automatic session restore is not enabled.

## How do first run/first startup experiments work?

Why does synchronously reading Nimbus feature values work for
customizing display features like `about:welcome` onboarding and the
default browser prompt? The key invariant is that the display
decisions wait for `sessionstore-windows-restored` to show
customizable UI, and therefore we just need Nimbus available to read
at that point. This is arranged either via the `--first-startup`
flag; or, for subsequent startups, the relevant Nimbus features being
marked `isEarlyStartup: true`. When `isEarlyStartup: true`, Nimbus
caches all its feature variables as Gecko preferences, ready to be
read during early startup. (See [the early startup
docs](https://experimenter.info/faq/early-startup/what-do-it-do).)

Customizable display features like `about:welcome` or the default
browser prompt are used in
[`_maybeShowDefaultBrowserPrompt()`](https://searchfox.org/mozilla-central/rev/a965e3c683ecc035dee1de72bd33a8d91b1203ed/browser/components/BrowserGlue.sys.mjs#4685),
which is invoked as part of a startup idle task. Startup idle tasks
are [scheduled in response to
`sessionstore-windows-restored`](https://searchfox.org/mozilla-central/rev/a965e3c683ecc035dee1de72bd33a8d91b1203ed/browser/components/BrowserGlue.sys.mjs#2423).

Now, why is `sessionstore-windows-restored` late enough for a
first startup experiment? The answer is subtle.

During Firefox launch, [`final-ui-startup` is
notified](https://searchfox.org/mozilla-central/rev/a965e3c683ecc035dee1de72bd33a8d91b1203ed/toolkit/xre/nsAppRunner.cpp#5764-5765),
and in response `SessionStore` is initialized. Additionally,
Nimbus initialization is [started but not
awaited](https://searchfox.org/firefox-main/rev/a9a644f66adb0cbb132f042bb7df24032f56346a/browser/components/BrowserGlue.sys.mjs#450-452).

Then the [command line is
handled](https://searchfox.org/mozilla-central/rev/a965e3c683ecc035dee1de72bd33a8d91b1203ed/toolkit/xre/nsAppRunner.cpp#5775-5778).
When `--first-startup` is passed, we [spin the event loop to allow
Nimbus time to complete its initialization and first
fetch](https://searchfox.org/mozilla-central/rev/a965e3c683ecc035dee1de72bd33a8d91b1203ed/browser/components/BrowserContentHandler.sys.mjs#677)
before continuing to process the command line. See [the
`FirstStartup`
module](/toolkit/modules/toolkit_modules/FirstStartup.md).
(Important caveat: `--first-startup` is only used on Windows; see [Bug
1872934](https://bugzilla.mozilla.org/show_bug.cgi?id=1872934), for
example.)

This races with `SessionStore`, which itself waits for the first
browser window to be shown -- in particular, the
`sessionstore-windows-restored` notification waits for the [first
browser window's `browser-delayed-startup-finished`
notification](https://searchfox.org/mozilla-central/rev/a965e3c683ecc035dee1de72bd33a8d91b1203ed/browser/components/sessionstore/SessionStore.sys.mjs#2147-2159).

This first `browser-delayed-startup-finished` notification **is not
guaranteed** to be after `--first-startup` has spun the event loop!
But, when launched with only `--first-startup` and flags considered
very early in `nsAppRunner.cpp` -- as [the stub installer
does](https://searchfox.org/mozilla-central/rev/a965e3c683ecc035dee1de72bd33a8d91b1203ed/browser/installer/windows/nsis/stub.nsi#1424-1456)
-- then the first window **is guaranteed** to be after the event loop
has been spun, and therefore `sessionstore-windows-restored` is after
as well. (As a counter-example: try `firefox.exe --browser
--first-startup` and witness the [`--browser`
flag](https://searchfox.org/mozilla-central/rev/a965e3c683ecc035dee1de72bd33a8d91b1203ed/browser/components/BrowserContentHandler.sys.mjs#505-508)
creating a window before spinning the event loop, inadvertently racing
against `sessionstore-windows-restored`.) Making this deterministic
is tracked by [Bug
1944431](https://bugzilla.mozilla.org/show_bug.cgi?id=1944431).

Together, this means that first-startup experiments will be loaded in
time to impact display features such as `about:welcome` and the
default browser prompt, and we should not have a "split brain"
scenario in which the Nimbus feature is intermittently unavailable to
the relevant display features.
