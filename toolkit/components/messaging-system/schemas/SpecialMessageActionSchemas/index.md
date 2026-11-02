# User Actions

A subset of actions are available to messages via fields like `action` on buttons for CFRs.

## Usage

For CFRs, you should add the action `type` in `action` and any additional parameters in `data`. For example:

```json
"action": {
  "type": "OPEN_PREFERENCES_PAGE",
  "data": { "category": "sync" },
}
```

## Available Actions

### `OPEN_APPLICATIONS_MENU`

* args: (none)

Opens the applications menu.

### `OPEN_FIREFOX_VIEW`

* args: (none)

Opens the Firefox View pseudo-tab.

### `OPEN_PRIVATE_BROWSER_WINDOW`

* args: (none)

Opens a new private browsing window.

### `OPEN_SIDEBAR`

* args: `string` id of the pane to open, e.g., viewHistorySidebar

Opens a sidebar pane.

### `OPEN_TAB_IN_SPLITVIEW`

* args: (none)

Opens the selected tab in a splitview with about:opentabs.

### `OPEN_URL`

* args: `string` (a url)

Opens a given url.

Example:

```json
"action": {
  "type": "OPEN_URL",
  "data": { "args": "https://foo.com" },
}
```

### `OPEN_ABOUT_PAGE`

* args:
```ts
{
  args: string, // (a valid about page without the `about:` prefix)
  entrypoint?: string, // URL search param used for referrals
}
```

Opens a given about page.

Only pages in the `ALLOWED_ABOUT_PAGES` allowlist are permitted; any other
page throws an error. As of writing, the allowed pages are: `addons`,
`profiles`, `translations`, `keyboard`, `logins`, `preferences`,
`privatebrowsing`, `protections`, `referrals`, `settings`, `welcome`,
`newtab`, `home`, and `robots`.

Example:

```json
"action": {
  "type": "OPEN_ABOUT_PAGE",
  "data": { "args": "privatebrowsing" },
}
```

### `OPEN_PREFERENCES_PAGE`

* args:
```ts
{
  // Top-level category, optionally with a "-subcategory" (e.g.
  // "general-cfrfeatures"). Passed as `category` (preferred) or `args`.
  category?: string,
  entrypoint?: string, // URL search param used for referrals
}
```

Opens `about:preferences` with an optional category accessible via a `#` in the URL (e.g. `about:preferences#home`).

Only real top-level categories and subcategories that map to a named pane will
route; unknown values silently fall back to the default pane.

Example:

```json
"action": {
  "type": "OPEN_PREFERENCES_PAGE",
  "data": { "category": "general-cfrfeatures" },
}
```

### `SHOW_FIREFOX_ACCOUNTS`

* args: (none)

Opens Firefox accounts sign-up page. Encodes some information that the origin was from snippets by default.

### `FXA_SIGNIN_FLOW`

* args:

```ts
{
  // a valid `where` value for `openUILinkIn`. Only `tab` and `window` have been tested, and `tabshifted`
  // is unlikely to do anything different from `tab`.
  where?: "tab" | "window" = "tab",

  entrypoint?: string // URL search params string to pass along to FxA. Defaults to "activity-stream-firstrun".
  extraParams?: object // Extra parameters to pass along to FxA. See FxAccountsConfig.promiseConnectAccountURI.
}
```

* example:
```json
"action": {
  "type": "FXA_SIGNIN_FLOW",
  "needsAwait": true,
  "navigate": "actionResult",
  "data": {
    "entrypoint": "onboarding",
    "extraParams": {
      "utm_content": "migration-onboarding"
    }
  }
}
```

Opens a Firefox accounts sign-up or sign-in page, and does the work of closing the resulting tab or window once
sign-in completes. Returns a Promise that resolves to `true` if sign-in succeeded, or to `false` if the sign-in
window or tab closed before sign-in could be completed. In messaging surfaces using `aboutwelcome` templates, setting `needsAwait` ensures that the UI will wait for the Promise to resolve. The `navigate` and `dismiss` properties should be assigned the string value "actionResult" for the UI to respect the resolved boolean value before proceeding to the next step.

Encodes some information that the origin was from about:welcome by default.

### `FXA_AIWINDOW_SIGNIN_FLOW`

Opens a customized AI Window Firefox accounts sign-up or sign-in flow, and redirects user to AI Window after successful authentication.

Returns a Promise that resolves to `true` if sign-in succeeded, or to `false` if the sign-in
window or tab closed before sign-in could be completed.

- args:
```ts
{
  data?: {
    // Identifies the surface that requested the launch, recorded as the
    // `trigger` extra key on Smart Window telemetry. Defaults to "asrouter".
    source?: string;
  }
}
```

### `SHOW_MIGRATION_WIZARD`

* args:
  * `source` (optional): key of the browser to pre-select, e.g. `"chrome"`
  * `tabModal` (optional): `true` to open the wizard as a dialog over the current tab instead of in about:preferences

Opens import wizard to bring in settings and data from another browser.

### `PIN_CURRENT_TAB`

* args: (none)

Pins the currently focused tab.

### `HIGHLIGHT_FEATURE`

Can be used to highlight (show a light blue overlay) a certain button or part of the browser UI.

* args: `string` a [valid targeting defined in the UITour](https://searchfox.org/mozilla-central/rev/7fd1c1c34923ece7ad8c822bee062dd0491d64dc/browser/components/uitour/UITour.jsm#108)

### `INSTALL_ADDON_FROM_URL`

Can be used to install an addon from addons.mozilla.org.

* args:
```ts
{
  url: string,
  telemetrySource?: string
};
```

### `OPEN_PROTECTION_REPORT`

Opens `about:protections`

### `OPEN_PROTECTION_PANEL`

Opens the protection panel behind on the lock icon of the awesomebar

### `OPEN_SMARTWINDOW_MONITOR_CREATE`

Opens the Smart Window Monitor toolbar panel straight to the create form.

* args: (none)

### `DISABLE_STP_DOORHANGERS`

Disables all Social Tracking Protection messages

* args: (none)

### `OPEN_AWESOME_BAR`

Focuses and expands the awesome bar.

* args: (none)

### `CANCEL`

No-op action used to dismiss CFR notifications (but not remove or block them)

* args: (none)

### `DISABLE_DOH`

User action for turning off the DoH feature

* args: (none)

### `ACCEPT_DOH`

User action for continuing to use the DoH feature

* args: (none)

### `CONFIGURE_HOMEPAGE`

Action for configuring the user homepage and restoring defaults.

* args:
```ts
{
  homePage: "default" | null;
  newtab: "default" | null;
  layout: {
    search: boolean;
    topsites: boolean;
    highlights: boolean;
    topstories: boolean;
  }
}
```

### `PIN_FIREFOX_TO_TASKBAR`

Action for pinning Firefox to the user's taskbar.

- args:

```ts
{
  privatePin?: boolean; // Pin private browsing mode
  fireAndForget?: boolean; // Don't wait for user confirmation before resolving the action
  promptOnly?: boolean; // Only pin via the OS consent prompt and never fall back to silent pinning. Set automatically for action_only messages
}
```

### `PIN_FIREFOX_TO_START_MENU`

Action for pinning Firefox to the user's Windows Start Menu in Windows MSIX builds only.

- args: (none)

### `PIN_AND_DEFAULT`
Action for pinning Firefox to the user's taskbar and setting it as the default browser.

- args:

```ts
{
  privatePin?: boolean; // Pin private browsing mode
  fireAndForget?: boolean; // Don't wait for user confirmation before resolving the action
  promptOnly?: boolean; // Only pin via the OS consent prompt and never fall back to silent pinning. Set automatically for action_only messages
}
```

### `SET_DEFAULT_BROWSER`

Action for setting the default browser to Firefox on the user's system.

- args: (none)

### `SET_DEFAULT_BROWSER_OPEN_WITH`

Action for setting Firefox as the default browser via the OS "Open with" picker
(IOpenWithLauncher), which guarantees an OS-level prompt. Claiming the `https`
handler this way sets the whole web-browser default (both `http` and `https`)
with a single picker.

Windows only.

- args: (none)

### `SET_DEFAULT_PDF_HANDLER`

Action for setting the default PDF handler to Firefox on the user's system.

Windows only.

- args:
```ts
{
  // Only set Firefox as the default PDF handler if the current PDF handler is a
  // known browser.
  onlyIfKnownBrowser?: boolean;
  // If the OS hands the stub PDF back to Firefox after the user picks Firefox
  // in the open-with dialog, open a follow-up PDF in a new tab instead of
  // suppressing the launch.
  openInFirefox?: boolean;
}
```

### `DECLINE_DEFAULT_PDF_HANDLER`

Action for declining to set the default PDF handler to Firefox on the user's
system. Prevents the user from being asked again about this.

Windows only.

- args: (none)

### `SET_DEFAULT_PROTOCOL_HANDLER`

Action for setting Firefox as the default handler for a protocol (scheme), such
as `mailto`, on the user's system.

Windows only.

- args:
```ts
{
  // The protocol to claim, e.g. "mailto".
  protocol: string;
  // URL passed to the OS default-app picker.
  url?: string;
  // If the OS hands the URL back to Firefox after the user picks Firefox in the
  // open-with dialog, open the protocol's default URL in a new tab.
  openInFirefox?: boolean;
}
```

### `SHOW_SPOTLIGHT`

Action for opening a spotlight tab or window modal using the content passed to the dialog.

### `BLOCK_MESSAGE`

Disable a message by adding to an indexedDb list of blocked messages

* args: `string` id of the message

### `SET_PREF`

Action for setting various browser prefs

Prefs that can be changed with this action can be found in the `allowList`
definition for Special Message Actions in
<a href="https://searchfox.org/mozilla-central/search?q=allowedPrefs&path=toolkit%2Fcomponents%2Fmessaging-system&case=false&regexp=false">SearchFox</a>.

Any pref that begins with `messaging-system-action.` is also allowed.
If the pref is not present in the list above and does not begin
with `messaging-system-action.`, it will be created and prepended with
`messaging-system-action.`. For example, `example.pref` will be created as
`messaging-system-action.example.pref`.

* args:
```ts
{
  pref: {
    name: string;
    value: string | boolean | number;
  }
}
```

#### Special `value` options

**`{ timestamp: true }`** — Instead of setting a literal value, sets the pref to the current time as a Unix millisecond timestamp string (i.e. `Date.now().toString()`).

Example:
```json
"action": {
  "type": "SET_PREF",
  "data": {
    "pref": {
      "name": "messaging-system-action.lastSeen",
      "value": { "timestamp": true }
    }
  }
}
```

**`{ copyFromPref: string }`** — Copies the value from another existing preference into the target pref. The source and target prefs must be of the same type, otherwise an error is thrown.

Example:
```json
"action": {
  "type": "SET_PREF",
  "data": {
    "pref": {
      "name": "messaging-system-action.myCopiedPref",
      "value": { "copyFromPref": "browser.startup.homepage" }
    }
  }
}
```

**Omitted or `null` value** — Resets the pref to its default value by calling `clearUserPref`.

Example:
```json
"action": {
  "type": "SET_PREF",
  "data": {
    "pref": {
      "name": "browser.startup.homepage"
    }
  }
}
```

### `DESTROY_UIWIDGET`

Destroys a customizable UI widget by ID. Only widgets in the allowlist are permitted.

- args:
```ts
{
  widget_id: string; // The id of the widget to destroy (e.g. "fxms-bmb-button")
}
```

### `MULTI_ACTION`

Action for running multiple actions. Actions should be included in an array of actions.

* args:
```ts
interface MultiAction {
  type: "MULTI_ACTION";
  data: {
    actions: Array<UserAction>;
    // Set to true if the actions should be executed in the order they are
    // listed in the `actions` array. If false, the actions will be executed in
    // parallel, with no guarantee of order. Defaults to false. If collectSelect
    // is true and there are multiselect actions, they will be executed in the
    // order they are rendered in the UI.
    orderedExecution?: boolean;
  };
  // Set to true if this action is for the primary button and you're using the
  // "multiselect" tile. This is what allows the primary button to perform the
  // actions specified by the user's checkbox/radio selections. It will combine
  // all the actions for all the selected checkboxes/radios into the above
  // `actions` array before executing them.
  collectSelect?: boolean;
}
```

* example:
```json
"action": {
  "type": "MULTI_ACTION",
  "data": {
    "actions": [
      {
        "type": "OPEN_URL",
        "args": "https://www.example.com"
      },
      {
        "type": "OPEN_AWESOME_BAR"
      }
    ],
    "orderedExecution": true
  }
}
```


### `RELOAD_BROWSER`

* args: (none)

Action for reloading the current browser.


### `FOCUS_URLBAR`

Focuses the urlbar in the window the message was displayed in

* args: (none)

### `BOOKMARK_CURRENT_TAB`

Bookmarks the tab that was selected when the message was displayed

- args:
```ts
{
  // Whether the bookmark dialog should be visible or not.
  shouldHideDialog?: boolean;
  // Whether the bookmark confirmation hint should be visible or not.
  shouldHideConfirmationHint?: boolean;
}
```

### `SET_BOOKMARKS_TOOLBAR_VISIBILITY`

Sets the visibility of the bookmarks toolbar.

- args:
```ts
{
  visibility?: string; // "always", "never", or "newtab"
}
```


### `SET_TERMS_OF_USE_INTERACTED`

Notify Firefox that the Terms of Use policy was interacted with.

- args: (none)

### `CREATE_NEW_SELECTABLE_PROFILE`

Creates a new user profile and launches it in a separate instance.

Any message that uses this action should have `canCreateSelectableProfiles` as part of the targeting, to ensure we don't accidentally show a message where the action will not work.

- args: (none)

### `RESET_PROFILE`

Opens the refresh confirmation dialog, which resets the current profile and restarts the browser

Any message that uses this action should have `canResetProfile` as part of the targeting, to ensure we don't show a message where the action will not work.

- args: (none)

### `SUBMIT_ONBOARDING_OPT_OUT_PING`

Submits a Glean `onboarding-opt-out` ping.  Should only be used during preonboarding (but this is not enforced).

- args: (none)

### `SET_SEARCH_MODE`

Sets search mode for a specific browser instance and focuses the urlbar.

- args:

```ts
interface SearchMode {
  // The name of the search engine to restrict to. Can be left empty to use source
  // restriction instead.
  engineName?: string;
  // A result source to restrict to. One of the values in UrlbarShared.RESULT_SOURCE.
  // Defaults to 3 (SEARCH).
  source?: number;
  // How search mode was entered. This is recorded in event telemetry. One of the
  // values in UrlbarShared.SEARCH_MODE_ENTRY. Defaults to "other".
  entry?: string;
  // If true, we will preview search mode. Search mode preview does not record
  // telemetry and has slighly different UI behavior. The preview is exited in
  // favor of full search mode when a query is executed. False should be
  // passed if the caller needs to enter search mode but expects it will not
  // be interacted with right away. Defaults to true.
  isPreview?: boolean;
}
```

* example:

```json
"action": {
  "type": "SET_SEARCH_MODE",
  "data": {
    "engineName": "test_engine",
    "source": 3,
    "entry": "messagingSystem",
    "isPreview": false,
  }
}
```

### `SUMMARIZE_PAGE`

Summarize current page content.

* args: optional `string` entry value to identify initiator default "message"

### `OPEN_ORGANIZE_TABS_PANEL`

Opens the Smart Window "Organize Tabs" panel. Unlike `OPEN_PANEL`, the panel is
built on demand rather than already present in the DOM.

- args:
```ts
{
  data?: {
    // Identifies the surface that opened the panel, recorded as the `source`
    // extra key on the panel's telemetry. Defaults to "message".
    source?: string;
  }
}
```

### `OPEN_PANEL`

Open a panel associated with a given widget.

* args:
```ts
{
  data: {
    // id of the anchor or widget to attach the panel to
    anchor_id: string,
    widget_id: string,
    // id of the panel to open
    panel_id: string,
  }
}
```

### `CREATE_TASKBAR_TAB`

Creates a taskbar tab from the current URL and asks to pin it to the taskbar. Windows only.

- args: (none)

### `PIN_TASKBAR_TAB`

Pins a specific web app to the taskbar.

- args:
```ts
{
  url: string;      // The HTTP/HTTPS URL of the web app to pin
  name: string;     // Display name for the web app
  iconUrl: string;  // URL of the icon to use (256x256 PNG recommended)
}
```

### `RESTORE_SESSION`

Restores the previous Firefox session if possible.

- args: (none)

### `RESTART_APP`

Restart the application.

- args: (none)

### `CREATE_GROUP_FROM_CURRENT_TAB`

Creates a new tab group at the position of the current tab containing only that tab.

If the current tab is already in a tab group, this action creates a new tab and places that new tab into a new tab group after the current tab's tab group.

- args: (none)

### `IPPROTECTION_ENROLL`

Enrolls the user in IP Protection. Initiates a Firefox Accounts sign-in flow if needed, then enrolls and entitles the user, and opens the IP Protection panel.

- args: (none)

### `CONFIRM_LAUNCH_ON_LOGIN`

Configures Firefox to launch on Windows login.

- args: (none)

### `REMOVE_LAUNCH_ON_LOGIN`

Removes Firefox from Windows login items.

- args: (none)

### `ENABLE_CLOSED_BROWSER_NOTIFICATIONS`

Turns on web notifications delivered while Firefox is closed, by setting the user-owned pref `app.backgroundNotifications.helper.enabled`.

The helper that delivers them also needs `app.backgroundNotifications.helper.available`, which Nimbus owns, so this action has no effect unless the feature is available to the client.

Windows only; a no-op on other platforms.

- args: (none)

### `DISABLE_CLOSED_BROWSER_NOTIFICATIONS`

Turns off web notifications delivered while Firefox is closed, and stops the helper that delivers them.

This writes only the user-owned pref, never the Nimbus-owned one, so a user opting out is not unenrolled from an experiment that made the feature available.

Windows only; a no-op on other platforms.

- args: (none)

### `SET_BROWSER_ICON`

Changes the browser icon to the one identified by `id` using `CustomIconManager`. Icon ids that are not present in the catalog are ignored. The `"default"` id reverts to the browser's own icon. Windows only, and not supported on MSIX (packaged) builds.

- args:
```ts
{
  id: string;  // The id of the icon to switch to, as listed in the CustomIconManager ICON_CATALOG
}
```

### `GET_REFERRAL_CODE`

Gets the user's referral code (or generates one if it doesn't exist), and opens about:referrals with the code as a param. Throws an error if referrals are not enabled.

### `SELECT_AND_MARK_MENU_ITEM`

Open the app menu and mark an item inside it, to point the user at a feature.
Marking of a nested item is cancelled if the user opens a different subview.

* args:
```ts
{
  data: {
    // ids of the items to select and mark. The first is in the app menu's
    // main view; each later id is marked once the user opens the subview
    // holding it.
    itemIds: string[],
  }
}
```
