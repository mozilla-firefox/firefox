# Progress listeners

`gBrowser` re-broadcasts web progress — loads starting and finishing, locations
changing, security state changing — to listeners registered on it. Little of the
behaviour is guessable from the signatures, so this page describes what each
listener type receives, what the tabbrowser does with raw web progress before
it reaches you, and what a tab switch replays.

## The two listener types

**Global listeners**, registered with `gBrowser.addProgressListener()`, receive
notifications for the **selected** browser only. Their callbacks take the same
arguments as the corresponding [nsIWebProgressListener][nsIWebProgressListener]
methods. Because a global listener follows the selected browser, switching tabs
has to bring it up to date: `gBrowser` re-sends the new browser's recorded state,
the replay covered in {ref}`progress-listeners-replay`.

**Tabs listeners**, registered with `gBrowser.addTabsProgressListener()`, receive
notifications for **every** tab, and their callbacks take the browser the
notification belongs to as an extra leading argument:

```js
let listener = {
  onLocationChange(browser, webProgress, request, location, flags) {
    if (browser == myTab.linkedBrowser) {
      // ...
    }
  },
};
gBrowser.addTabsProgressListener(listener);
// later
gBrowser.removeTabsProgressListener(listener);
```

## What is broadcast

Nine notifications reach listeners. Seven mirror
[nsIWebProgressListener][nsIWebProgressListener]; the last two exist only on
`gBrowser`.

| Notification | Received by | Notes |
| --- | --- | --- |
| `onStateChange` | tabs and global listeners | Dispatched separately to each type, and substituted for the initial `about:blank` as described below. |
| `onLocationChange` | tabs and global listeners | Also replayed to global listeners on a tab switch. |
| `onProgressChange` | tabs and global listeners | `nsIWebProgressListener2.onProgressChange64` is forwarded here, so implementing `onProgressChange` alone is enough. |
| `onStatusChange` | tabs and global listeners | The status text is also recorded for replay. |
| `onSecurityChange` | tabs and global listeners | Also replayed to global listeners on a tab switch. |
| `onContentBlockingEvent` | tabs and global listeners | Carries a trailing flag that is true when the event is simulated rather than observed. |
| `onRefreshAttempted` | tabs and global listeners | The only one whose return value is acted on. |
| `onLinkIconAvailable` | tabs and global listeners | `gBrowser`-specific: a favicon became available, carrying the icon URL and the original URL. |
| `onUpdateCurrentBrowser` | global listeners only | `gBrowser`-specific: a state snapshot rather than an observed change. |

Both listener types share the same dispatch rules:

- A listener is a **plain object**, not an XPCOM interface implementation.
- Dispatches only when `if (method in listener)` is true, so implementing only
  the notifications you care about is normal and expected.
- Each call is wrapped in try/catch. A listener that throws has its error logged
  and does not inhibit the remaining listeners.
- Global listeners run **before** tabs listeners.
- **The return value matters.** If any listener returns a falsy value, the
  aggregate result is falsy. `onRefreshAttempted`'s result decides whether a meta
  refresh proceeds, so a listener implementing it must return `true` unless it
  means to cancel the refresh.

## What sits between the browser and your listener

```{mermaid}
:align: center
:caption: How a notification reaches a listener

flowchart TD
    webProgress([browser webProgress])
    filter[status filter<br/>coalesces the stream]
    listener[TabProgressListener<br/>updates tab state]
    dispatch[gBrowser]
    globals[global listeners]
    tabs[tabs listeners]
    switched([tab switch])
    replay[updateCurrentBrowser<br/>replays the recorded state]

    webProgress --> filter --> listener --> dispatch
    dispatch -- selected browser only --> globals
    dispatch -- browser prepended --> tabs
    switched --> replay --> globals
```

Each tab gets its own `TabProgressListener`, wired to the browser through a
[status filter][filter] that coalesces the raw notification stream, which is why
fine-grained progress rates never reach a listener.

`TabProgressListener` translates web progress into tab state: it maintains the
tab's `busy` and `progress` attributes, the tabbrowser's own busy state, the tab's
title and icon at the right moments, and only then calls the registered listeners. It also
records the tab's last `stateFlags`, `status`, `message` and `totalProgress`, which
is what makes the replay below possible. Two things follow from that:

- The record is written *after* the listeners run, so a listener that reads it
  while handling a notification sees the previous value.
- `message` and `totalProgress` are cleared at `STATE_START` and `STATE_STOP`, so a
  status delivered before a load's `STATE_START` is shown at the time but is not
  replayed on a later tab switch.

Each notification is then dispatched twice, once to the global listeners and once
to the tabs listeners. Usually both types receive the same call. The exception is a tab
still on its initial `about:blank` document: the global listeners receive
`onUpdateCurrentBrowser(stateFlags, status, "", 0)` in place of `onStateChange`, so
they don't mistake a brand-new tab's blank document for a load, while the tabs
listeners still receive the real `onStateChange` and have to draw that distinction
themselves.

(progress-listeners-replay)=

## What a tab switch replays

A listener registered against `gBrowser` hears about the selected browser, so
selecting another tab has to bring it up to date. `updateCurrentBrowser()`
replays, **to global listeners only** — a tabs listener sees nothing here:

1. `onLocationChange` for the new browser's current URI, with the simulated flag
   set.
2. `onSecurityChange` and `onContentBlockingEvent`, if the browser has a
   `securityUI`.
3. `onUpdateCurrentBrowser` with the four recorded values, but **only when the
   recorded `stateFlags` is non-zero**.
4. A synthetic `onStateChange` carrying `STATE_START | STATE_IS_NETWORK` when the
   new tab is busy and the tabbrowser was not, or `STATE_STOP | STATE_IS_NETWORK`
   in the mirror case.

`onUpdateCurrentBrowser` has no `nsIWebProgressListener` counterpart. `gBrowser`
sends it to global listeners whenever it wants to hand them a state snapshot rather
than report a change it just observed — on a tab switch here, and in place of
`onStateChange` for the initial `about:blank` above. On a tab switch it carries the
recorded flags verbatim, so a consumer gated on `STATE_IS_NETWORK` reacts to the
separate synthetic `onStateChange` in step 4 instead.

## Waiting for progress in a test

For the common cases, use the helpers rather than a listener of your own:
`BrowserTestUtils.browserLoaded(browser)` for a load in a specific tab, and
`BrowserTestUtils.waitForLocationChange(gBrowser, url)` for a location change
anywhere in the window.

`waitForLocationChange` is itself a compact example of the tabs-listener pattern —
a plain object implementing one notification, removed again once it fires:

```js
function waitForLocationChange(tabbrowser, url) {
  return new Promise(resolve => {
    let listener = {
      onLocationChange(browser, webProgress, request, newURI) {
        if (newURI.spec != url) {
          return;
        }
        tabbrowser.removeTabsProgressListener(listener);
        resolve();
      },
    };
    tabbrowser.addTabsProgressListener(listener);
  });
}
```

[nsIWebProgressListener]: https://searchfox.org/firefox-main/source/uriloader/base/nsIWebProgressListener.idl
[filter]: https://searchfox.org/firefox-main/source/toolkit/components/statusfilter/nsBrowserStatusFilter.cpp
