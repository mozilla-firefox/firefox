# Lazy browsers

A tab can sit in the strip with nothing loaded behind it. Session restore creates
every tab but the selected and pinned ones that way, the tab unloader returns a
loaded tab to that state, and `gBrowser.addTab`'s `createLazyBrowser` option asks
for one directly. Such a tab shows a label, an icon and a URL, but it has no
document, no docShell and no content process — and reading the wrong property off
it quietly gives it all three. This page describes what a lazy tab has, what
makes its browser real, what a discard tears down, and where this stops and
{doc}`tab-unloading` starts.

## What a lazy tab has

`tab.linkedBrowser` is set before `TabOpen` fires, for a lazy tab as much as an
eager one, so its presence says nothing. What a lazy tab lacks is a browser *in
the document*, and `tab.linkedPanel` is what reports that: it holds the id of the
browser's panel in `gBrowser.tabpanels` and is null until the browser is
inserted.

```js
if (!tab.linkedPanel) {
  // The tab has a <browser> element, but nothing is loaded in it.
}
```

The element itself is a `MozBrowser` that has never been connected, so the
properties that depend on a frame loader answer empty rather than throwing:
`browsingContext`, `docShell`, `contentWindow` and `contentDocument` are null,
`contentPrincipal` is undefined and `browserId` is 0. `permanentKey` is set from
the start, which is how session store keys the tab's state either way.

What the tab does carry:

- **Session store's lazy state** — a `url`, a `title` and a `userTypedValue`,
  held for as long as the browser is lazy and read with
  `SessionStore.getLazyTabValue(tab, key)`. This is where the substituted
  properties below get their answers, and what a consumer should read instead of
  going through the browser.
- **The `pending` attribute**, set by session store while the tab is waiting to
  be restored and removed when the restore actually starts. Under
  `browser.tabs.fadeOutUnloadedTabs` it fades the tab's favicon.
- **The `discarded` attribute**, set when the tab was explicitly unloaded rather
  than restored into this state. `browser.tabs.fadeOutExplicitlyUnloadedTabs`
  fades those tabs on their own.
- **The `discarded-skip-on-close` attribute**, set when the tab was explicitly
  unloaded and therefore should be skipped when switching from a closed tab (to
  avoid it from being re-loaded).

**The active tab is never lazy.** Selecting a tab inserts its browser, and
`discardBrowser` refuses the selected tab. Multi-selected tabs are another
matter: `gBrowser.selectedTabs` can contain lazy ones, which is why
`reloadWithFlags` has a separate path for them.

## The substituted properties

`#createLazyBrowser` defines the names in `#browserBindingProperties` as own
accessors on the `<browser>` element, shadowing the element's real
implementations until insertion deletes them again. The following answer
without touching the browser:

| Property | Answers with |
| --- | --- |
| `currentURI` | Session store's URL for the tab as an `nsIURI`, cached on the element, or `about:blank`. |
| `contentTitle` | Session store's title for the tab. |
| `userTypedValue` | Session store's value for the tab. |
| `remoteType` | `ChromeUtils.predictRemoteTypeForURI` for that URL and the tab's `usercontextid`. |
| `isRemoteBrowser` | Whether the element carries the `remote` attribute. |
| `audioMuted` | Whether the tab carries the `muted` attribute. |
| `fullZoom`, `textZoom` | `1`. |
| `tabHasCustomZoom` | `false`. |
| `permitUnload` | A function returning `{ permitUnload: true }`. |
| `didStartLoadSinceLastUserTyping` | A function returning `false`. |
| `getTabBrowser` | A function returning `gBrowser`. |

`reload` and `reloadWithFlags` are substituted with a version that inserts the
browser and defers the reload until session store has restored the tab.

**All others insert the browser, on read and on write alike.** So a consumer
can undo a tab's laziness by looking at it: a docShell, a content process, a
synchronous `about:blank` load and a `TabBrowserInserted` caused by nothing but
the read. On Nightly that path logs the property's name and a stack citing
[Bug 1345098](https://bugzilla.mozilla.org/show_bug.cgi?id=1345098) to the
browser console. The ones that read as harmless are the trap:

- `documentURI`, next to a `currentURI` that is safe.
- `webProgress`, `addProgressListener` and `removeProgressListener`, so a lazy
  tab cannot be wired up for {doc}`progress-listeners` without ceasing to be
  lazy.
- `sessionHistory`, `canGoBack` and `canGoForward`.
- `blockedPopups`, `imageDocument`, `characterSet` and `preferences`.
- `resumeMedia`, `audioPlaybackStarted` and `audioPlaybackStopped`, next to a
  safe `audioMuted`.

A property outside the 42 gets neither treatment: it reads through to the unbound
element and answers null or undefined, with nothing logged. So `tab.linkedPanel`
is the only reliable way to tell whether the browser is real.

To load something into a lazy tab, ask for the insertion rather than tripping
over it. `tabs.update` in the extension API and `gBrowser`'s own reload paths all
use this shape:

```js
if (tab.linkedPanel) {
  browser.fixupAndLoadURIString(url, options);
} else {
  // Wait for the load handler to be instantiated before loading.
  tab.addEventListener(
    "SSTabRestoring",
    () => browser.fixupAndLoadURIString(url, options),
    { once: true }
  );
  gBrowser.insertBrowser(tab);
}
```

## The cycle

```{mermaid}
:align: center
:caption: When a tab's browser enters and leaves the document.

---
config:
  flowchart:
    wrappingWidth: 400
---
flowchart TD
    add(["gBrowser.addTab"])
    lazy{"createLazyBrowser?"}
    ins1(["TabBrowserInserted<br/>insertedOnTabCreation: true"])
    open(["TabOpen"])
    touch["something reads the browser,<br/>or the tab is selected"]
    ins2(["TabBrowserInserted<br/>insertedOnTabCreation: falsy"])
    discard["discardBrowser"]
    disc(["TabBrowserDiscarded"])

    add --> lazy
    lazy -- "no" --> ins1 --> open
    lazy -- "yes" --> open
    open --> touch --> ins2
    ins2 --> discard --> disc
    disc -. "browser is lazy again" .-> touch

    classDef event fill:#dbeafe,stroke:#1e40af,color:#1a1a1a;
    class ins1,ins2,open,disc event;
```

## Making the browser real

`gBrowser.insertBrowser(tab)` is the public entry point. It does nothing if the
tab already has a `linkedPanel` or the window is closing, so calling it
defensively is fine. Otherwise, in order:

1. Deletes the substituted properties, unmasking the element's own, and consumes
   the browser parameters Tabbrowser has kept for the tab since it was created or
   last discarded — `uriIsAboutBlank`, `remoteType` and `usingPreloadedContent`.
2. Gives the panel a unique id and sets `tab.linkedPanel` to it.
3. Appends the panel to `tabpanels`. That runs the browser element's
   constructors, which fire notifications that can run code inspecting
   tabbrowser state, and performs a synchronous `about:blank` load. The tab has
   to be fully initialized before this point.
4. Wires a `TabProgressListener` to the browser through a status filter, as
   {doc}`progress-listeners` describes.
5. Rebinds `loadURI` and `fixupAndLoadURIString` to `URILoadingWrapper` and sets
   the default `droppedLinkHandler`.
6. Deactivates the docShell unless the content was preloaded, and sets
   `browsingContext.hasSiblings`, `browsingContext.isAppTab` from `tab.pinned`
   and the `usercontextid` attribute.
7. Dispatches `TabBrowserInserted` — but only if the tab is connected. Session
   restore creates its tabs outside the strip and dispatches the event itself
   for the whole batch once they are in the DOM.

Besides the property reads above and an explicit `insertBrowser` call, four
things insert a browser:

- **Selecting the tab.** The tab strip's `getRelatedElement` inserts the browser
  when the tab has no panel, and only for the selected tab, so asking for
  another tab's panel returns null rather than inserting anything.
- **A remoteness change.** `updateBrowserRemoteness` inserts before swapping the
  frame loader.
- **Showing the tab in a split view.** `showSplitViewPanels` inserts each of the
  view's tabs.
- **A docShell swap.** `#swapBrowserDocShells` inserts the receiving tab's
  browser. Moving a *lazy* tab to another window takes a different path:
  `swapBrowsersAndCloseOther` copies the tab's session state instead of swapping
  docShells, so the tab arrives in the new window still lazy.

`TabBrowserInserted` is also what makes session store restore the tab: it clones
the tab's state, starts the restore, and dispatches `SSTabRestoring` and then
`SSTabRestored` on the tab. The lazy state is dropped at that point, so
`getLazyTabValue` answers undefined from then on. `detail.insertedOnTabCreation`
distinguishes the two cases — true when `addTab` inserted the browser as it
created the tab, undefined for every later insertion.

## Discarding a browser

`gBrowser.discardBrowser(tab, options)` takes a loaded tab back to lazy. It
returns false without doing anything for a tab that is selected, closing,
already lazy or not remote, for a window that is closing, and when
`permitUnload` says no. A tab with an open dialog is also refused unless
`options.forceDiscard` is true, since a discard dismisses dialogs.

The `options` object accepts the following properties:

- `forceDiscard` (default: `false`): Allows discarding a tab with an open dialog.
- `renderDiscarded` (default: `forceDiscard`): Controls whether to render the tab as discarded.
- `skipOnClose` (default: `forceDiscard`): Whether to skip the unloaded tab when
  choosing a fallback tab after closing the selected tab, if alternatives exist.

Flush the tab's state first. `await gBrowser.prepareDiscardBrowser(tab)` hands
session store the latest data, and it is safe to call even if the discard is
then refused.

The discard resets the tab's sharing state and forgets its WebRTC streams,
aborts its dialogs, records the browser parameters for the eventual restore, has
session store take the tab back to lazy state, removes the progress listener and
its filter, closes and removes the findbar, and clears the tab's now-stale
`activemedia-blocked`, `busy`, `pendingicon`, `progress` and `soundplaying`
attributes. Only then does it destroy the browser, remove its panel, clear
`linkedpanel` and re-install the substituted properties, and dispatch
`TabBrowserDiscarded` last of all.

One `TabAttrModified` always precedes it, for `sharing`, which the sharing reset
dispatches whether or not the tab was sharing anything. A second one lists
whichever of the five stale attributes were set.

The `<browser>` element survives, so a cached reference is not dangling. It is an
element whose docShell and browsing context are gone and whose properties will
quietly build new ones, which makes it the more dangerous kind of stale
reference. A consumer holding a browser, its `browsingContext`, or anything keyed
on either has to drop it on `TabBrowserDiscarded`.

Callers include the tab unloader under memory pressure,
`gBrowser.explicitUnloadTabs` behind the tab context menu's unload command, and
the `tabs.discard` extension API.

## Where this page stops

This page covers what a lazy browser is and what inserting or discarding one
does. {doc}`tab-unloading` owns the other half: which tabs are eligible, how
they are prioritized, and what makes Firefox decide to discard one.
