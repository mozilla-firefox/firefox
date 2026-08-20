(tabbrowser-async-tab-switcher)=

# Async tab switcher

At a very high level, the async tab switcher is responsible for telling tabs with out-of-process (or “remote”) `<xul:browser>`’s to render and upload their contents to the compositor, and then update the UI to show that content as a tab switch. Similarly, the async tab switcher is responsible for telling tabs that have been switched away from to stop rendering their content, and for the compositor to release those contents.

## Briefly introducing Layers and the Compositor

For out-of-process tabs, the presentation portion of Gecko computes the final contents of a tab inside the tabs content process, and then uploads that information to the compositor. This uploaded information is usually referred to as *layers*.

The compositor is what eventually presents these layers to the user as pixels. The compositor can retain several sets of layers without necessarily showing them to the user, but this consumes memory. Layers that are no longer needed are released.

From here forward, "contents of a tab" will be referred to as that tab's *layers*.

(async-tab-switcher-useful-properties)=

## renderLayers, hasLayers, docShellIsActive

`<xul:browser>`'s have a number of useful properties exposed on them that the async tab switcher uses:

`renderLayers`

: For remote `<xul:browser>`'s, setting this to `true` from `false` means to ask the content process to render the layers for that `<xul:browser>` and upload them to the compositor. Setting this to `false` from `true` means to ask the content process to stop rendering the layers and for the compositor to release the layers. Setting this property to `true` when it is already `true` or `false` when it is already `false` is a no-op. When this property returns `true`, this means that layers have been requested for this tab, but there is no guarantee that the layers have been received by the compositor yet. Similarly, when this property returns `false`, this means that this browser has been asked to stop rendering layers, but there is no guarantee that the layers have been released by the compositor yet.

  For non-remote `<xul:browser>`'s, `renderLayers` is an alias for `docShellIsActive`.

`hasLayers`

: For remote `<xul:browser>`'s, this read-only property returns `true` if the compositor has layers for this tab, and `false` otherwise.

  For non-remote `<xul:browser>`'s, `hasLayers` returns the value for `docShellIsActive`.

`docShellIsActive`

: For remote `<xul:browser>`'s, setting `docShellIsActive` to `true` also sets `renderLayers` to true, and then sends a message to the content process to set its top-level docShell active state to `true`. Similarly, setting `docShellIsActive` to `false` also sets `renderLayers` to false, and then sends a message to the content process to set its top-level docShell active state to `false`.

  For non-remote `<xul:browser>`'s, `docShellIsActive` forwards to the `isActive` property on the `<xul:browser>`'s top-level docShell.

  Setting a docShell to be active causes the tab's visibilitychange event to fire to indicate that the tab has become visible. Media that was waiting to be played until the tab is selected will also begin to play.

  An active docShell is also required in order to generate a print preview of the loaded document.

## Requirements

There are a number of requirements that the tab switcher must satisfy. In no particular order, they are:

1. The switcher must be prepared to switch between any mixture of remote and non-remote tabs. Non-remote tabs include tabs pointed at <about:addons>, <about:config>, and others
2. We want to avoid switching the toolbar state (for example, the URL bar input, security indicators, toolbar button states) until we are ready to show the layers of the tab that we're switching to
3. Only one tab should appear to be selected in the tab strip at any given time
4. We want to avoid switching keyboard focus to a selected tab until the layers for the tab are ready - but only if the user doesn’t change focus between the start and end of the async tab switch
5. If the layers for a tab are not available after a certain amount of time, we should “complete” the tab switch by displaying the “tab switch spinner” - an animated spinner against a white background. This way, we at least show the user some activity, despite the fact that we don’t have the layers of the tab to show them
6. The printing UI uses tabs to show print preview, which requires that the print-previewed tab is in the background and yet also have its docShell be "active" - a state that's usually reserved for the selected tab. See {ref}`async-tab-switcher-useful-properties`
7. `<xul:tab>`'s and `<xul:browser>`'s might be created or destroyed at any time during an async tab switch
8. It should be possible to render layers for a tab, despite it not having been set as active (this is used for {ref}`async-tab-switcher-warming`)

## Lifecycle

Per window, an async tab switcher instance is only supposed to exist if one or more tabs still need to have their layers loaded or unloaded. This means that an async tab switcher instance might exist even though a tab switch appears to the user to have completed. This also means that an async tab switcher might continue to exist and handle a new tab switch if the user initiates that tab switch before some background tabs have had their layers unloaded.

There’s only one async tab switcher at a time per window, and it’s owned by that window’s `gBrowser`.

A window starts without an async tab switcher, and only once a tab switch (or warming) is initiated by the user is the switcher instantiated.

Once the switcher determines that the tab that the user has requested is being shown, and all background tabs have been properly unloaded or destroyed, the async tab switcher cleans up and destroys itself.

(async-tab-switcher-states)=

## Tab states

While the async tab switcher exists, it maps each `<xul:tab>` in the window to one of the following internal states:

`STATE_UNLOADED`

: Layers for this `<xul:tab>` are not being uploaded to the compositor, and we haven't requested that the tab start doing so. This tab is fully in the background.

  When a tab is in `STATE_UNLOADED`, this means that the associated `<xul:browser>` either does not exist, or will have its `renderLayers` and `hasLayers` properties both return `false`.

  If a tab is in this state, it must have either initialized there, or transitioned from `STATE_UNLOADING`.

  When logging states, this state is indicated by the `unloaded` string.

`STATE_LOADING`

: Layers for this `<xul:tab>` have not yet been reported as "received" by the compositor, but we've asked the tab to start rendering. This usually means that we want to switch to the tab, or at least to warm it up.

  When a tab is in `STATE_LOADING`, this means that the associated `<xul:browser>` will have its `renderLayers` property return `true` and its `hasLayers` property return `false`.

  If a tab is in this state, it must have either initialized there, or transitioned from `STATE_UNLOADED`.

  When logging states, this state is indicated by the `loading` string.

`STATE_LOADED`

: Layers for this `<xul:tab>` are available on the compositor and can be displayed. This means that the tab is either being shown to the user, or could be very quickly shown to the user.

  If a tab is in this state, it must have either initialized there, or transitioned from `STATE_LOADING`.

  When a tab is in `STATE_LOADED`, this means that the associated `<xul:browser>` will have its `renderLayers` and `hasLayers` properties both return `true`.

  When logging states, this state is indicated by the `loaded` string.

`STATE_UNLOADING`

: Layers for this `<xul:tab>` were at one time available on the compositor, but we've asked the tab to unload them to preserve memory. This usually means that we've switched away from this tab, or have stopped warming it up.

  When a tab is in `STATE_UNLOADING`, this means that the associated `<xul:browser>` will have its `renderLayers` property return `false` and its `hasLayers` property return `true`.

  If a tab is in this state, it must have either initialized there, or transitioned from `STATE_LOADED`.

  When logging states, this state is indicated by the `unloading` string.

Having a tab render its layers is done by settings its state to `STATE_LOADING`. Once the layers have been received, the switcher will automatically set the state to `STATE_LOADED`. Similarly, telling a tab to stop rendering is done by settings its state to `STATE_UNLOADING`. The switcher will automatically set the state to `STATE_UNLOADED` once the layers have fully unloaded.

## Stepping through a simple tab switch

In our simple scenario, suppose the user has a single browser window with two tabs: a tab at index **0** and a tab at index **1**. Both tabs are completed loaded, and **0** is currently selected and displaying its content.

The user chooses to switch to tab **1**. An async tab switcher is instantiated, and it immediately attaches a number of event handlers to the window. Among them are handlers for the `MozLayerTreeReady` and `MozLayerTreeCleared` events.

The switcher then creates an internal mapping from `<xul:tab>>`'s to states. That mapping is:

```none
// This is using the logging syntax laid out in the `Tab states` section.
0:(loaded) 1:(unloaded)
```

Be sure to refer to {ref}`async-tab-switcher-states` for an explanation of the terminology and {ref}`async-tab-switcher-logging` syntax for states.

This last example translates to:

The tab at index **0**, is in `STATE_LOADED` and the tab at index **1** is in `STATE_UNLOADED`.

Now that initialization done, the switcher is asked to request **1**. It does this by putting **1** into `STATE_LOADING` and requesting that **1**'s layers be rendered. The new state mapping is:

```none
0:(loaded) 1:(loading)
```

At this point, the user is still looking at tab **0**, and none of the UI is showing any visible indication of tab change.

Now the switcher is waiting, so it goes back to the event loop. During this time, if any code were to ask the tabbrowser which tab is selected, it'd return **1**, since it's *logically* selected despite not being *visually* selected.

Eventually, the layers for **1** are uploaded to the compositor, and the `<xul:browser>` for **1** fires its `MozLayerTreeReady` event. This is when the switcher changes its internal state again:

```none
0:(loaded) 1:(loaded)
```

So now layers for both **0** and **1** are uploading and available on the compositor. At this point, the switcher updates the visual state of the browser, and flips the `<xul:deck>` to display **1**, and the user experiences the tab switch.

The switcher isn't done, however. After a predefined amount of time (dictated by `UNLOAD_DELAY`), tabs that aren't currently selected but in `STATE_LOADED` are put into `STATE_UNLOADING`. Now the internal state looks like this:

```none
0:(unloading) 1:(loaded)
```

Having requested that **0** go into `STATE_UNLOADING`, the switcher returns back to the event loop. The user, meanwhile, continues to use `1`.

Eventually, the layers for **0** are cleared from the compositor, and the `<xul:browser>` for **0** fires its `MozLayerTreeCleared` event. This is when the switcher changes its internal state once more:

```none
0:(unloaded) 1:(loaded)
```

The tab at **0** is now in `STATE_UNLOADED`. Since the last requested tab **1** is in `STATE_LOADED` and all other background tabs are in `STATE_UNLOADED`, the switcher decides its work is done. It deregisters its event handlers, and then destroys itself.

(async-tab-switcher-unloading-background)=

## Unloading background tabs

While an async tab switcher exists, it will periodically scan the window for tabs that are in `STATE_LOADED` but are also in the background. These tabs will then be put into `STATE_UNLOADING`. Only once all background tabs have settled into the `STATE_UNLOADED` state are the background tabs considered completely cleared.

The background scanning interval is `UNLOAD_DELAY`, in milliseconds.

## Perceived performance optimizations

We use a few tricks and optimizations to help improve the perceived performance of tab switches.

1. Sometimes users switch between the same tabs quickly. We want to optimize for this case by not releasing the layers for tabs until some time has gone by. That way, quick switching just resolves in a re-composite in the compositor, as opposed to a full re-paint and re-upload of the layers from a remote tab’s content process.
2. When a tab hasn’t ever been seen before, and is still in the process of loading (right now, dubiously checked by looking for the “busy” attribute on the `<xul:tab>`) we show a blank content area until its layers are finally ready. The idea here is to shift perceived lag from the async tab switcher to the network by showing the blank space instead of the tab switch spinner.
3. “Warming” is a nascent optimization that will allow us to preemptively render and cache the layers for tabs that we think the user is likely to switch to soon. After a timeout (`browser.tabs.remote.warmup.unloadDelayMs`), “warmed” tabs that aren’t switched to have their layers unloaded and cleared from the cache.
4. On platforms that support `occlusionstatechange` events (as of this writing, only macOS) and `sizemodechange` events (Windows, macOS and Linux), we stop rendering the layers for the currently selected tab when the window is minimized or fully occluded by another window.

5\. Based on the browser.tabs.remote.tabCacheSize pref, we keep recently used tabs'
layers around to speed up tab switches by avoiding the round trip to the content
process. This uses a simple array (`_tabLayerCache`) inside `Tabbrowser.sys.mjs`, which
we examine when determining if we want to unload a tab's layers or not. This is still
experimental as of Nightly 62.

(async-tab-switcher-warming)=

## Warming

Tab warming allows the browser to proactively render and upload layers to the compositor for tabs that the user is likely to switch to. The simplest example is when a user's mouse cursor is hovering over a tab. When this occurs, the async tab switcher is told to put that tab into a warming list, and to set its state to `STATE_LOADING`, even though the user hasn't yet clicked on it.

Warming a tab queues up a timer to unload background tabs (if no such timer already exists), which will clear out the warmed tab if the user doesn't eventually click on it. The unload will occur even if the user continues to hover the tab.

If the user does happen to click on the warmed tab, the tab can be in either one of two states:

`STATE_LOADING`

: In this case, the user requested the tab switch before the layers were rendered and received by the compositor. We'll at least have shaved off the time between warming and selection to display the tab's contents to the user.

`STATE_LOADED`

: In this case, the user requested the tab switch after the layers had been rendered and received by the compositor. We can switch to the tab immediately.

Warming is controlled by the following preferences:

`browser.tabs.remote.warmup.enabled`

: Whether or not the warming optimization is enabled.

`browser.tabs.remote.warmup.maxTabs`

: The maximum number of tabs that can be warming simultaneously. If the number of warmed tabs exceeds this amount, all background tabs are unloaded (see {ref}`async-tab-switcher-unloading-background`).

`browser.tabs.remote.warmup.unloadDelayMs`

: The amount of time to wait between the first tab being warmed, and unloading all background tabs (see {ref}`async-tab-switcher-unloading-background`).

(async-tab-switcher-logging)=

## Logging

The async tab switcher has some logging capabilities that make it easier to debug and reason about its behaviour. Setting the hidden `browser.tabs.remote.logSwitchTiming` pref to true will put logging into the Browser Console.

Alternatively, setting the `useDumpForLogging` property to true within the source code of the tab switcher will dump those logs to stdout.
