# Opening tabs and loading URLs

{searchfox}`URILoadingHelper.sys.mjs <browser/modules/URILoadingHelper.sys.mjs>`
opens tabs and windows for chrome code. It lives in `browser/modules/` rather
than in the tabbrowser, and
{searchfox}`utilityOverlay.js <browser/base/content/utilityOverlay.js>` exposes
most of it as window globals, so a browser window script calls
`openTrustedLinkIn(url, "tab")` without importing anything. Every key of the
`params` object these functions take is in {ref}`uri-loading-api` at the end of
this page.

Reach for it before `gBrowser.addTab`. Which principal triggers the load,
whether the load belongs in a tab at all, whether that tab opens in front or
behind, where it is inserted and which container it inherits are all decided
here. `addTab` decides none of them.

## Which function to call

| Function | Use it when |
| --- | --- |
| `openTrustedLinkIn(url, where, params)` | the URL is authored by chrome: an `about:` page, a support link, a hard-coded destination |
| `openWebLinkIn(url, where, params)` | the URL came from content or from the user |
| `openLinkIn(url, where, params)` | you already hold the principal that should trigger the load |
| `openUILink(url, event, params)` | you have the originating event and want `where` derived from its modifiers |
| `switchToTabHavingURI(uri, openNew, params)` | the destination should be focused if it is already open |

**The trusted/web split is a security decision, not a style choice.**
{js:func}`~URILoadingHelper.openTrustedLinkIn` loads with the system principal, which bypasses the URI load
checks and lets a `javascript:` or `data:` target inherit chrome privileges.
{js:func}`~URILoadingHelper.openWebLinkIn` supplies a fresh null principal and
throws if it is handed a system principal — but nothing guards the other direction, so passing a
content-derived URL to `openTrustedLinkIn` is a privilege escalation that no
lint rule will catch for you.

{js:func}`~URILoadingHelper.openLinkIn` throws unless
`params.triggeringPrincipal` is set. Supplying it is
all the other two do.

```js
// A destination chrome chose.
openTrustedLinkIn("about:preferences#general", "tab");

// A URL that came from a page, or from the urlbar.
openWebLinkIn(url, "tab", { userContextId });
```

## Where the load lands

`where` decides that, and not all of the answers are tabs:

| `where` | Result |
| --- | --- |
| `"current"` | loads in the selected browser, or in `params.targetBrowser` |
| `"tab"` | a new tab in the topmost suitable window |
| `"tabshifted"` | a new tab, with the foreground decision inverted |
| `"window"` | a new browser window |
| `"chromeless"` | a new window with no navigation UI, sized by `params.width` and `params.height`, optionally centered with `params.shouldCenter` |
| `"save"` | saves the target instead of loading it |

An unrecognised `where` does nothing at all, and neither does a falsy `url` or
`where` — no throw, no warning. `"save"` is worth knowing about even if you
never pass it, because
{searchfox}`whereToOpenLink <toolkit/modules/BrowserUtils.sys.mjs>` returns it
for a modified click: it needs `initiatingDoc` or `isContentWindowPrivate` in
`params`, and without either it logs an error and gives up.

**`"current"` is not a guarantee.** Three cases redirect the load into a new
tab: the Firefox View tab, a `userContextId` that differs from the target
browser's, and a pinned tab being sent to a different host — the last unless the
caller passes `allowPinnedTabHostChange`, which only the urlbar does. The
foreground decision is made before the redirect, so a bounced load selects its
new tab even when the caller asked for `inBackground: true`.

**`"current"` also means whatever is selected now.** For a caller that arrived
through an actor message or after an `await`, that may no longer be the tab the
user acted in. Pass `params.targetBrowser`, honoured for `"current"` only, to
pin the load to a specific tab.

A new window inherits the source window's private state. `params.private` and
`params.forceNonPrivate` override it, which is what separates a context menu's
"Open in New Window" from its "Open in New Private Window".

## Foreground or background

Only `"tab"` and `"tabshifted"` have a choice to make, and
{searchfox}`willLoadInBackground <toolkit/modules/BrowserUtils.sys.mjs>` makes
it: an explicit `params.inBackground` wins; otherwise `params.forceForeground`
opens in front, and failing that the `browser.tabs.loadInBackground` preference
decides. `"tabshifted"` then inverts the answer, which is how a modifier can
flip the preference for one click.

`openTrustedLinkIn`, `openWebLinkIn` and `openUILink` all set `forceForeground`
themselves, so a load through them opens in front unless the caller passes
`inBackground` explicitly. Only a direct `openLinkIn` caller gets the preference
by default.

## Deriving `where` from an event

{js:func}`~URILoadingHelper.openUILink` unwraps the event — a middle click arrives wrapped in one or two
command events — and hands it to `whereToOpenLink`, which maps the modifiers:

| Input | `where` |
| --- | --- |
| accel (`Cmd` on macOS, `Ctrl` elsewhere) | `"tab"` |
| accel + `Shift` | `"tabshifted"` |
| middle click | `"tab"`, per `browser.tabs.opentabfor.middleclick` |
| `Shift` | `"window"` |
| `Alt` | `"save"`, per `browser.altClickSave`, off by default |
| no modifier | `"current"` |

`openUILink`'s third argument is the `params` bag, and it also reads
`ignoreButton` and `ignoreAlt` from there: `ignoreButton` for middle-click paste,
which must not be mistaken for a request to open a window, and `ignoreAlt` where
`Alt` is unavailable, as in a menu. The older positional form of this argument
has no way to carry a triggering principal, which `openUILink` requires, so pass
the object.

`whereToOpenLink` and `willLoadInBackground` are `BrowserUtils` members rather
than window globals, and a caller that has an event but wants to open the load
itself can use the first directly.

## Focusing a tab that may already be open

{js:func}`~URILoadingHelper.switchToTabHavingURI` is a `browser.js` global
rather than a utilityOverlay one.
It searches the current window and then every other browser window of the same
privateness — `about:addons` is the one exception — and compares the URL exactly.
Fragments and query strings therefore have to match unless you opt out:

- `ignoreFragment: "whenComparing"` ignores the fragment when matching, and
  `"whenComparingAndReplace"` also loads the requested URL into the tab it found.
- `ignoreQueryString` ignores the query string; `replaceQueryString` ignores it
  and then loads the requested URL.
- `adoptIntoActiveWindow` moves a tab found in another window into this one,
  rather than raising that window.

It returns whether an existing tab was found, so a `false` means either that
`openNew` opened one or that nothing happened. Everything else in `params` is
forwarded to `openTrustedLinkIn`.

## What you get back

Nothing. `openLinkIn` and its wrappers return `undefined`, and none of them waits
for the load. For the browser element, pass a callback:
`resolveOnContentBrowserCreated` is called on all three targets, and
`resolveOnNewTabCreated` only on the new-tab path. Both hand you the browser as
soon as it exists, which is well before the load finishes — waiting for that is
`BrowserTestUtils.browserLoaded` in a test, and a progress listener
({doc}`progress-listeners`) in product code.

## Calling from outside a browser window

The globals exist only in the documents that load `utilityOverlay.js`. Anywhere
else, get a window and call the method on it:

```js
let win =
  lazy.BrowserWindowTracker.getTopWindow() ??
  (await lazy.BrowserWindowTracker.promiseOpenWindow());
win.openTrustedLinkIn(url, "tab");
```

Importing `URILoadingHelper` is not a way around needing a window, since every
function takes one as its first argument. It saves nothing but the forwarder.

- A parent actor resolves the window from the browser it is talking to:
  `browser.documentGlobal`, or `this.browsingContext.topChromeWindow`.
- A content-privileged `about:` page cannot open a tab; send a message and open
  it from the parent.
- `about:preferences` and other chrome-privileged `about:` pages do load
  `utilityOverlay.js`, so the globals work. From one of its subdialogs, reach the
  browser window with `window.windowRoot.window`.

## When gBrowser.addTab is right

When you need something `openLinkIn` cannot express: a lazy browser
({doc}`lazy-browsers`, via `createLazyBrowser` and `lazyTabTitle`), a tab with
no load (`skipLoad`), a tab
created outside the strip (`insertTab`), a specific process
(`preferredRemoteType`), or the bulk-restore and tab-group options. That covers
session restore and tab duplication, a discarded tab created for an extension,
the placeholder tab that `adoptTab` swaps a browser into, and a test that wants a
tab without a load — `BrowserTestUtils.addTab`, not `gBrowser.addTab` directly.

`addTab` throws without a `triggeringPrincipal`. `addTrustedTab` and `addWebTab`
supply one on the same terms as their `openLinkIn` counterparts, so they are what
callers reach for.

**Pass `inBackground` rather than selecting the tab afterwards.**

```js
// Leaves the tab without an owner.
gBrowser.selectedTab = gBrowser.addTrustedTab(url);

// Selects it and sets the owner.
gBrowser.addTrustedTab(url, { inBackground: false });
```

A tab's `owner` is the tab to return to when it closes, which
`browser.tabs.selectOwnerOnClose` honours by default. `addTab` opens in the
background, and a background tab is given no owner unless it has an opener, so
selecting the tab after the call leaves it ownerless and closing it falls through
to the adjacent tab instead of back to where the user was.

Two more things `addTab` leaves to its caller: it reads no preference, so
`browser.tabs.loadInBackground` has no effect on it, and it inherits
`userContextId` only from an opener tab, so a bare `addTab` in a container tab
opens in the default container. Without an opener the new tab is also appended at
the end of the strip rather than next to the current one.

(uri-loading-api)=

## URILoadingHelper API reference

Generated from the JSDoc in
{searchfox}`URILoadingHelper.sys.mjs <browser/modules/URILoadingHelper.sys.mjs>`.
The window globals drop the leading `window` argument these take.

```{js:autofunction} URILoadingHelper.openTrustedLinkIn
```

```{js:autofunction} URILoadingHelper.openWebLinkIn
```

```{js:autofunction} URILoadingHelper.openLinkIn
```

```{js:autofunction} URILoadingHelper.openUILink
```

```{js:autofunction} URILoadingHelper.switchToTabHavingURI
```

```{js:autofunction} URILoadingHelper.getTargetWindow
```

```{js:autofunction} URILoadingHelper.guessUserContextId
```
