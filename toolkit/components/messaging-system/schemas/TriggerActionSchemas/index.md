# Triggers

Triggers can be used to decide when messages are shown.

## Usage

Messages can define a single `trigger` object used to determine when the message should be shown.
The `trigger` object must include an `id` string identifying the trigger action and may include optional trigger-specific properties such as `params`, `patterns`, or `regexPatterns`.

## Multiple triggers

A message may declare multiple triggers via the `triggers` array instead of the
singular `trigger`. The message becomes eligible when **any** one of the listed
triggers matches, which avoids duplicating a message once per trigger:

```javascript
{
  ...
  triggers: [
    { id: "openURL", params: ["example.com"] },
    { id: "frequentVisits", params: ["example.com"] }
  ]
  ...
}
```

When `trigger` and `triggers` are both present, `triggers` takes precedence.

**Targeting is evaluated using the context of whichever trigger fired.** Because
each trigger contributes its own context, a context value provided by one
trigger is only available when *that* trigger fires. A targeting expression that
depends on a specific trigger's context (such as `tabsClosedCount` from
`nthTabClosed`) evaluates to `false` when a different trigger fires and does not
supply that value. When using `triggers`, make sure the `targeting` expression
is valid for every trigger listed. If the triggers need genuinely different
targeting, use separate messages instead.

## Available trigger actions

- [`openArticleURL`](#openarticleurl)
- [`bookmarkAdded`](#bookmarkadded)
- [`openBookmarkedURL`](#openbookmarkedurl)
- [`visitBookmarkedURL`](#visitbookmarkedurl)
- [`userBookmarkFolderActivity`](#userbookmarkfolderactivity)
- [`frequentVisits`](#frequentvisits)
- [`openURL`](#openurl)
- [`newSavedLogin`](#newsavedlogin)
- [`formAutofill`](#formautofill)
- [`contentBlocking`](#contentblocking)
- [`defaultBrowserCheck`](#defaultbrowsercheck)
- [`deeplinkedToWindowsSettingsUI`](#deeplinkedtowindowssettingsui)
- [`captivePortalLogin`](#captiveportallogin)
- [`preferenceObserver`](#preferenceobserver)
- [`featureCalloutCheck`](#featurecalloutcheck)
- [`pdfJsFeatureCalloutCheck`](#pdfjsfeaturecalloutcheck)
- [`newtabFeatureCalloutCheck`](#newtabfeaturecalloutcheck)
- [`nthTabClosed`](#nthtabclosed)
- [`nthTabOpened`](#nthtabopened)
- [`tabGroupCreated`](#tabgroupcreated)
- [`tabGroupSaved`](#tabgroupsaved)
- [`tabGroupCollapsed`](#tabgroupcollapsed)
- [`activityAfterIdle`](#activityafteridle)
- [`messagesLoaded`](#messagesloaded)
- [`pageActionInUrlbar`](#pageactioninurlbar)
- [`onSearch`](#onsearch)
- [`sidebarToolOpened`](#sidebartoolopened)
- [`elementClicked`](#elementclicked)
- [`ipProtectionReady`](#ipprotectionready)
- [`ipProtectionPanelClosed`](#ipprotectionpanelclosed)
- [`ipProtectionBandwidthReset`](#ipprotectionbandwidthreset)
- [`selectableProfilesUpdated`](#selectableprofilesupdated)
- [`smartWindowNewTab`](#smartwindownewtab)
- [`nimbusUpdate`](#nimbusupdate)
- [`lastWindowClose`](#lastwindowclose)

### `openArticleURL`

Happens when the user loads a Reader Mode compatible webpage.

Supports filtering with `params`, [`patterns`](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/Match_patterns), and `regexPatterns`.

```javascript
// Optional set of hosts to filter out triggers only to certain websites
let params: string[];
// Optional set of Match patterns to filter out triggers only to certain websites
let patterns: string[];
// Optional regular expression patterns to filter out triggers only to certain websites
let regexPatterns: string[];
```

```javascript
{
  ...
  // Show the message when opening mozilla.org
  "trigger": { "id": "openArticleURL", "params": ["mozilla.org", "www.mozilla.org"] }
  ...
}
```

```javascript
{
  ...
  // Show the message when opening any HTTP, HTTPS URL.
  trigger: { id: "openArticleURL", patterns: ["*://*/*"] }
  ...
}
```

```javascript
{
  ...
  // Show the message when opening a URL that matches the regular expression.
  trigger: { id: "openArticleURL", regexPatterns: ["^https://.*\\.mozilla\\.org/.*"] }
  ...
}
```

### `bookmarkAdded`

Fires when the user adds a bookmark through any UI path, including the URL bar
star icon, the Bookmarks menu, the keyboard shortcut, the "Bookmark Link" and
"Bookmark All Tabs" commands, and the Library window.

Bulk and non-interactive sources (import, restore, sync) and tag operations are
ignored, so mass operations such as an add-on importing or syncing bookmarks do
not fire the trigger. It fires at most once per operation and does not fire in
private windows.

### `openBookmarkedURL`

Happens when the user bookmarks or navigates to a bookmarked URL.

Does not filter by host or patterns.

### `visitBookmarkedURL`

Fires when the user navigates to a URL that is already bookmarked. This does not fire when the user creates a bookmark, only when they open one. Does not
fire in private windows.

### `userBookmarkFolderActivity`

Happens when the user either creates a new bookmark folder or saves a bookmark
into a user-created folder. Does not fire if the active window is a private
window.

### `frequentVisits`

Happens every time a user navigates (or switches tab) to any of the `hosts`
or `patterns` arguments provided. This trigger adds an item to the targeting
context called `recentVisits`. It is a sorted array of timestamps for recent
visits to the hosts provided in the `hosts` property.
This can be used inside of the targeting expression:

```javascript
// Has at least 3 visits in the past hour
recentVisits[.timestamp > (currentDate|date - 3600 * 1000 * 1)]|length >= 3

```

```typescript
interface visit {
  host: string,
  timestamp: UnixTimestamp
};
// Host and timestamp for every visit to "Host"
let recentVisits: visit[];
```

Supports filtering with `params`, [`patterns`](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/Match_patterns), and `regexPatterns`.

```javascript
// Optional set of hosts to filter out triggers only to certain websites
let params: string[];
// Optional set of Match patterns to filter out triggers only to certain websites
let patterns: string[];
// Optional regular expression patterns to filter out triggers only to certain websites
let regexPatterns: string[];
```

```javascript
{
  ...
  // Show the message when visiting mozilla.org
  "trigger": { "id": "frequentVisits", "params": ["mozilla.org", "www.mozilla.org"] }
  ...
}
```

```javascript
{
  ...
  // Show the message when visiting any HTTP, HTTPS URL.
  trigger: { id: "frequentVisits", patterns: ["*://*/*"] }
  ...
}
```

```javascript
{
  ...
  // Show the message when visiting a URL that matches the regular expression.
  trigger: { id: "frequentVisits", regexPatterns: ["^https://.*\\.mozilla\\.org/.*"] }
  ...
}
```

### `openURL`

Happens every time the user loads a new URL that matches the provided `hosts` or `patterns`.
During a browsing session it keeps track of visits to unique urls that can be used inside targeting expression.

```javascript
// True on the third visit for the URL which the trigger matched on
visitsCount >= 3
```

`isAddressBarUrlNavigation` is true when the navigation was a direct
navigation to a URL via the address bar — typed, pasted, autofilled, or picked
from the dropdown as a bookmark/history/top-site match — as opposed to a
search query submitted through the address bar, a synced-device (remote tab)
pick, a link click, a redirect, or other programmatic navigation. This is
only known for the
navigation immediately following an address bar interaction; it's `false` for
navigations that happen any other way, including ones that just look similar
(e.g. a search redirecting to a URL that was also separately bookmarked).

```javascript
// Only match when the address bar resolved directly to a URL, not a search
isAddressBarUrlNavigation
```

Supports filtering with `params`, [`patterns`](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/Match_patterns), and `regexPatterns`.

```javascript
// Optional set of hosts to filter out triggers only to certain websites
let params: string[];
// Optional set of Match patterns to filter out triggers only to certain websites
let patterns: string[];
// Optional regular expression patterns to filter out triggers only to certain websites
let regexPatterns: string[];
```

```javascript
{
  ...
  // Show the message when opening mozilla.org
  "trigger": { "id": "openURL", "params": ["mozilla.org", "www.mozilla.org"] }
  ...
}
```

```javascript
{
  ...
  // Show the message when opening any HTTP, HTTPS URL.
  trigger: { id: "openURL", patterns: ["*://*/*"] }
  ...
}
```

```javascript
{
  ...
  // Show the message when opening a URL that matches the regular expression.
  trigger: { id: "openURL", regexPatterns: ["^https://.*\\.mozilla\\.org/.*"] }
  ...
}
```

The trigger context exposes two counters. `visitsCount` is per unique URL, so
it only grows when the same URL is loaded again. `totalVisitsCount` counts
every matched visit, so it grows on each matching page load regardless of the
URL. Pair `totalVisitsCount` with a broad pattern to count all page loads:

```javascript
{
  ...
  trigger: { id: "openURL", patterns: ["*://*/*"] },
  // Show the message on the third page load of the session.
  targeting: "totalVisitsCount >= 3"
  ...
}
```

Note that the `openURL` listener is shared by every active `openURL` message,
and `totalVisitsCount` counts matches against the combined `params`,
`patterns`, and `regexPatterns` of all of them, not just those of the message
being evaluated. Only use it with a trigger broad enough that the distinction
does not matter; prefer `visitsCount` for narrowly scoped triggers.

### `newSavedLogin`

Happens every time the user saves or updates a login via the login capture doorhanger.
Provides a `type` to differentiate between the two events that can be used in targeting.

Does not filter by host or patterns.

```typescript
let type = "update" | "save";
```

### `formAutofill`

Happens when the user saves, updates, or uses a credit card or address for form
autofill. To reduce the trigger's disruptiveness, it does not fire when the user
is manually editing these items in the manager in about:preferences. For the
same reason, the trigger only fires after a 10-second delay. The trigger context
includes an `event` and `type` that can be used in targeting. Possible events
include `add`, `update`, and `use`. Possible types are `card` and `address`.
This trigger is especially intended to be used in tandem with the
`creditCardsSaved` and `addressesSaved` [targeting attributes](/browser/components/asrouter/docs/targeting-attributes.md).

```js
{
  trigger: { id: "formAutofill" },
  targeting: "type == 'card' && event in ['add', 'update']"
}
```

### `contentBlocking`

Happens at the and of a document load and for every subsequent content blocked event, or when the tracking DB service hits a milestone.

Provides a context of the number of pages loaded in the current browsing session that can be used in targeting.

Does not filter by host or patterns.

The event it reports back is one of two things:
 * A combination of OR-ed [nsIWebProgressListener](https://searchfox.org/firefox-main/source/uriloader/base/nsIWebProgressListener.idl) `STATE_BLOCKED_*` flags
 * A string constant, such as [`"ContentBlockingMilestone"`](https://searchfox.org/mozilla-central/rev/8a2d8d26e25ef70c98c6036612aad534b76b9815/toolkit/components/antitracking/TrackingDBService.jsm#327-334)


### `defaultBrowserCheck`

Happens at startup, when opening a newtab and when navigating to about:home.
At startup, it reports the `source` as `startup`, and it provides a context
attribute `willShowDefaultPrompt` that can be used in targeting to avoid showing
a message when the built-in default browser prompt is going to be displayed.
This is important to avoid the negative UX of showing two promts back-to-back,
especially if both prompts offer similar affordances.
It also provides `defaultPromptSuppressed`, which is `true` when the prompt
would have been displayed but was disabled through the `setToDefaultPrompt`
Nimbus feature's `disablePrompt` variable. Messages replacing the prompt can
target it to show exactly when the prompt would have.
On the newtab/homepage, it reports the `source` as `newtab`.

```ts
let source = "startup" | "newtab";
let willShowDefaultPrompt = boolean | undefined;
let defaultPromptSuppressed = boolean | undefined;
```

#### Examples

* Only trigger on startup, not on newtab/homepage
* Don't show if the built-in prompt is going to be shown

```js
{
  trigger: { id: "defaultBrowserCheck" },
  targeting: "source == 'startup' && !willShowDefaultPrompt"
}
```

### `deeplinkedToWindowsSettingsUI`

Triggers when the user has indicated they want to set Firefox as the default web
browser and interaction with Windows Settings is necessary to finish setting
Firefox as default.

### `captivePortalLogin`

Happens when the user successfully goes through a captive portal authentication flow.

### `preferenceObserver`

Watch for changes on any number of preferences. Runs when a pref is added, removed or modified.

```js
// Register a message with the following trigger
{
  id: "preferenceObserver",
  params: ["pref name"]
}
```

### `featureCalloutCheck`

Used to display Feature Callouts in Firefox View. Can only be used for Feature Callouts.

### `pdfJsFeatureCalloutCheck`

Used to display Feature Callouts on PDF.js pages. Can only be used for Feature Callouts.

### `newtabFeatureCalloutCheck`

Used to display Feature Callouts on about:newtab. Can only be used for Feature Callouts.

### `nthTabClosed`

Happens when the user closes n or more tabs in a session

```js
// Register a message with the following trigger and
// include the tabsClosedCount context variable in the targeting.
// Here, the message triggers after two or more tabs are closed.
{
  trigger: { id: "nthTabClosed" },
  targeting: "tabsClosedCount >= 2"
}
```
```js
// The trigger also tracks the number of tabs currently open,
// and the currentTabsOpen context variable can be used in targeting
// to ensure a minimum number of tabs are open.
// Here, the message will trigger on the next tab closed
// after 4 tabs are opened (and remain open).
{
  trigger: { id: "nthTabClosed" },
  targeting: "currentTabsOpen >= 4"
}
```
```js
// The trigger also includes an optional action context variable
// when a caller marks the tab.smartWindowActionSource before close.
// Here, the message triggers when the close was attributed to a specific source
// (e.g., "close_current_tab" set by a toolcall)
{
  trigger: { id: "nthTabClosed" },
  targeting: "actionSource == 'close_current_tab'"
}
```

### `nthTabOpened`

Happens when the user opens n or more tabs in a session

```js
// Register a message with the following trigger and
// include the tabsOpenedCount context variable in the targeting.
// Here, the message triggers once two or more tabs are opened,
// even if the tabs were closed in between.
{
  trigger: { id: "nthTabOpened" },
  targeting: "tabsOpenedCount >= 2"
}
```
```js
// The trigger also tracks the number of tabs currently open,
// and the currentTabsOpen context variable can be used in targeting
// to ensure a minimum number of tabs are open.
// Here, the message will trigger on the next tab opened
// while 4 tabs remain open in the browser.
{
  trigger: { id: "nthTabOpened" },
  targeting: "currentTabsOpen >= 4"
}
```

### `tabGroupCreated`

Happens whenever a user creates a tab group.

```js
{
  trigger: { id: "tabGroupCreated" }
}
```
```js
// The trigger can also track the number of tab groups created in a
// session, by including the tabGroupsCreatedCount context variable in targeting.
// Here, the message triggers once two or more tab groups have been created,
// even if the tabs were closed in between.
{
  trigger: { id: "tabGroupCreated" },
  targeting: { "tabGroupsCreatedCount >= 2" }
}
```

### `tabGroupSaved`

Happens whenever a user uses the "Save and Close" action on a tab group.

```js
{
  trigger: { id: "tabGroupSaved" }
}
```
```js
// The trigger can also track the number of tab groups closed in a
// session, by including the tabGroupsClosedCount context variable in targeting.
// Here, the message triggers once two tab groups have been saved and closed.
{
  trigger: { id: "tabGroupSaved" },
  targeting: { "tabGroupsSavedCount >= 2" }
}
```

### `tabGroupCollapsed`

Happens whenever a user clicks a tab group label to collapse it. Can be used with the `currentTabGroups` targeting to ensure multiple groups have been collapsed.

```js
{
  trigger: { id: "tabGroupCollapsed" }
}
```
```js
// The trigger can also track the number of tab groups collapsed in a
// session, by including the tabGroupsCollapsedCount context variable in targeting.
// Here, the message triggers once four tab groups have been collapsed, or one group
// has been collapsed four times.
{
  trigger: { id: "tabGroupCollapsed" },
  targeting: { "tabGroupsCollapsedCount >= 4" }
}
```

### `activityAfterIdle`

Happens when the user resumes activity after n milliseconds of inactivity. Keyboard/mouse interactions and audio playback count as activity. The idle timer is reset when the OS is put to sleep or wakes from sleep.

No params or patterns. The `idleForMilliseconds` context variable is available in targeting. This value represents the number of milliseconds since the last user interaction or audio playback. `60000` is the minimum value for this variable (1 minute). In the following example, the message triggers when the user returns after at least 20 minutes of inactivity.

```js
// Register a message with the following trigger and include
// the idleForMilliseconds context variable in the targeting.
{
  trigger: { id: "activityAfterIdle" },
  targeting: "idleForMilliseconds >= 1200000"
}
```

### `messagesLoaded`

Happens as soon as a message is loaded. This trigger does not require any user interaction, and may happen potentially as early as app launch, or at some time after experiment enrollment. Generally intended for use in reach experiments, because most messages cannot be routed unless the surfaces they display in are instantiated in a tabbed browser window (a reach message will not be displayed but its trigger will still be recorded). However, it is still possible to safely use this trigger for a normal message, with some caveats. This is potentially relevant on macOS, where the app can be running with no browser windows open, or even on Windows, where closing all browser windows but leaving open a non-browser window (e.g. the Library) causes the app to remain running.

A `toast_notification` or `update_action` message can function normally under these circumstances. A `toolbar_badge` message will load with or without a window, but will not actually display until a window exists. But messages with templates like `infobar` will have no effect unless a window exists to display them in. Any message using this trigger, regardless of template, can exclude window-less or browser-less contexts by adding the following targeting. This isn't strictly necessary because the messaging surfaces will either work normally or fail gracefully, but it may be desirable to test reach only in certain contexts, so the context objects `browser` and `browserWindow` are provided, corresponding to the selected browser (`gBrowser.selectedBrowser`) and the most recently active chrome window, respectively.

```js
{
  trigger: { id: "messagesLoaded" },
  targeting: "browser && browserWindow"
}
```

### `pageActionInUrlbar`

Happens when a page action appears in the location bar. The specific page action(s) to watch for can be specified by id in the targeting expression. For example, to trigger when the reader mode button appears:

```js
{
  trigger: { id: "pageActionInUrlbar" },
  targeting: "pageAction == 'reader-mode-button'",
  params: ["example.com"],
  patterns: ["https://www.example.com/*"]
}
```

### `onSearch`

Happens when the user uses the search feature in the awesome bar.

The `isSuggestion` boolean context variable is available in targeting, and will evaluate to true if the search was initiated from a recommendation in the awesomebar.

The `searchSource` string context variable is also available in targeting, and returns the search source. It will be one of four values: `urlbar-handoff` if one of the faux-search inputs were used (such as the one present on the newtab page), `urlbar-searchmode` if the user has selected a search engine, `urlbar-persisted` if the user has changed tabs or windows and come back to their search term in the URL bar, `urlbar` if the user is doing a standard search by entering a term into the URL bar and pressing enter, or clicking on a search suggestion, or `searchbar` if the search was started from the separate search bar toolbar widget.

The `isOneOff` boolean context variable is available in targeting, and will be true if one of the one-off search features (typically found at the bottom of the awesomebar's dropdown menu) is used.

```js
{
  trigger: { id: "onSearch" },
  targeting: "isSuggestion && searchSource == 'urlbar-handoff' && isOneOff"
}
```

### `sidebarToolOpened`

Happens when the user opens a tool or extension panel in the sidebar

The `view` string context variable is available in targeting, and will correspond with which sidebar tool/extension has been opened (ex: "viewHistorySidebar", "viewBookmarksSidebar", etc).

The `clickCounts` object context variable is also available in targeting, and information about how many time a specific tool or extensions has been opened. The `SIDEBAR_TOOL_SURVEY` callout will be targeted to show if any tool/extension (excluding GenAI chatbot) has been clicked 5 times per-window and per-session. The `SIDEBAR_GENAI_SURVEY` callout will be targeted to show if the GenAI chatbot panel has been opened 2 times per-window and per-session.

```js
{
  trigger: { id: "sidebarToolOpened" },
  targeting: `'sidebar.position_start'|preferenceValue && view != 'viewGenaiChatSidebar' && clickCounts.totalToolsMinusGenai == 5 && !'messaging-system-action.sidebar-tools-microsurvey-complete-or-dismissed'|preferenceValue`
}
```

### `elementClicked`

Happens when an element in the browser chrome is clicked. The trigger will only fire if the element that is clicked has an ID that is within the trigger's params array.

The `elementId` string context variable is also available in targeting, and will correspond to the ID of the element that was clicked.

```js
{
  trigger: {
    id: "elementClicked",
    params: ["element1-id", "element2-id"]
  },
  targeting: "elementId == 'element1-id'"
}
```

### `ipProtectionReady`

Fired once the IP protection widget is created and available. Used as a trigger for the IP protection feature introduction callout, which anchors to the widget.

Targets users with the `browser.ipProtection.enabled` pref set to true, along with frequency caps.

```js

{
  trigger: { "ipProtectionReady" },
  targeting: "'browser.ipProtection.enabled' | preferenceValue && !(messageImpressions.IP_PROTECTION_INTRODUCTION_CALLOUT[messageImpressions.IP_PROTECTION_INTRODUCTION_CALLOUT | length - 1] < currentDate|date - (3600000 * 24))",

}
```

### `ipProtectionPanelClosed`

Fires on close of the IP Protection Panel. Can be used to trigger messages or actions after the user closes the IP protection panel.

The `hasUsedSiteExceptions` boolean context variable is available in targeting, and will evaluate to true if the user has added a site to their exceptions list either in-panel or at about:settings.

```js
{
  trigger: { id: "ipProtectionPanelClosed" },
  targeting: "!hasUsedSiteExceptions",
}
```

### `ipProtectionBandwidthReset`

Fires when the IP protection bandwidth quota resets at the start of a new month.

```js
{
  trigger: { id: "ipProtectionBandwidthReset" },
  targeting: "'browser.ipProtection.userEnableCount' | preferenceValue > 0",
}
```

### `selectableProfilesUpdated`

Fires to keep multi-profile feature users informed of changes to data collection settings. Within a profile group, any update to these shared profile settings triggers this event for all other running remote profile instances.

```js
{
  trigger: { id: "selectableProfilesUpdated" },
  template: "infobar",
  frequency: { lifetime: 1 }
}
```

### `tabSwitch`

Fires when the user switches between two tabs 2 times within one minute.
A currentTabsOpen context variable is included to be used in the targeting

```js
{
  trigger: { id: "tabSwitch" },
  targeting: `!'browser.tab.splitview.hasUsed'|preferenceValue && currentTabsOpen >=5`
}
```

### `smartWindowNewTab`

Occurs every time a user opens a new Smart Window tab.

```js
{
  trigger: { id: "smartWindowNewTab" },
  targeting: "isAIWindow && 'browser.smartwindow.firstrun.hasCompleted' | preferenceValue",
}
```

### `nimbusUpdate`

Fired after ASRouter's Nimbus experiment enrollment listener finishes reloading
messages from the `messaging-experiments` provider. Because messages are already
in `ASRouter.state.messages` by the time this trigger fires, any message using it
will be routed synchronously on the same tick as enrollment, without waiting for
a timer or restart.

This is the correct trigger for moments page (`update_action`) messages delivered
via Nimbus experiments. It replaces the deprecated `momentsUpdate` pseudo-trigger.

```js
{
  trigger: { id: "nimbusUpdate" },
  template: "update_action",
  content: {
    action: {
      id: "moments-wnp",
      data: { url: "https://www.mozilla.org/firefox/welcome/12", expireDelta: 172800000 }
    }
  }
}
```

Does not filter by host, patterns, or params.

### `lastWindowClose`

Fires when the user closes the last open browser window. Popup windows (opened
with `toolbar=no`) do not trigger it, and it's skipped if closing the window
would show (or just showed) the "closing multiple tabs" warning, so the two
don't stack.

Closing the window is delayed until the matched message resolves. Closing the window is
cancelled, the message shows, and the close is re-requested once it's done.
Only the `spotlight` template currently supports this trigger. If a button's action needs to finish
before the window actually closes, set`needsAwait: true` on that action.

```js
{
  trigger: { id: "lastWindowClose" },
  template: "spotlight",
  content: {
    template: "multistage",
    modal: "window",
    screens: [
      {
        content: {
          primary_button: {
            action: { type: "SOME_ACTION", needsAwait: true, dismiss: true }
          }
        }
      }
    ]
  }
}
```

### `splitViewUsed`

Fires after a configurable delay (default 15 seconds, `browser.tabs.splitview.trigger.delay_ms`)
of continuous use of Split View. Leaving Split View before the delay elapses cancels the
countdown; returning starts a fresh one.

```js
{
  trigger: { id: "splitViewUsed" }
}
```
```js
// The trigger also tracks the number of distinct Split Views the user has
// created (not re-entries into an existing one), via the splitViewCreateCount
// context variable. Here, the message is excluded for a user's first-ever
// Split View.
{
  trigger: { id: "splitViewUsed" },
  targeting: "splitViewCreateCount > 1"
}
```
