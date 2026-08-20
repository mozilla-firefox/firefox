# Targeting attributes

When you create ASRouter messages such as contextual feature recommendations or onboarding cards, you may choose to include **targeting information** with those messages.

Targeting information must be captured in [an expression](./targeting-guide.md) that has access to the following attributes. You may combine and compare any of these attributes as needed.

Please note that some targeting attributes require stricter controls on the telemetry than can be collected, so when in doubt, ask for review.

## Available attributes

```{contents}
:local:
:depth: 1
```

### `addonsInfo`
Provides information about the add-ons the user has installed.

Note that the `name`, `userDisabled`, and `installDate` is only available if `isFullData` is `true` (this is usually not the case right at start-up).

**Due to an existing bug, `userDisabled` is not currently available**

#### Examples
* Has the user installed the unicorn addon?
```java
addonsInfo.addons["unicornaddon@mozilla.org"]
```

* Has the user installed and disabled the unicorn addon?
```java
addonsInfo.isFullData && addonsInfo.addons["unicornaddon@mozilla.org"].userDisabled
```

#### Definition
```ts
declare const addonsInfo: Promise<AddonsInfoResponse>;
interface AddonsInfoResponse {
  // Does this include extra information requiring I/O?
  isFullData: boolean;
  // addonId should be something like activity-stream@mozilla.org
  [addonId: string]: {
    // Version of the add-on
    version: string;
    // (string) e.g. "extension"
    type: AddonType;
    // Version of the add-on
    isSystem: boolean;
    // Is the add-on a webextension?
    isWebExtension: boolean;
    // The name of the add-on
    name: string;
    // Is the add-on disabled?
    // CURRENTLY UNAVAILABLE due to an outstanding bug
    userDisabled: boolean;
    // When was it installed? e.g. "2018-03-10T03:41:06.000Z"
    installDate: string;
  };
  // Has the user installed addons beyond the built in and system addons?
  hasInstalledAddons: boolean;
}
```
### `attributionData`

An object containing information on exactly how Firefox was downloaded

#### Examples
* Was the browser installed via the `"back_to_school"` campaign?
```java
attributionData && attributionData.campaign == "back_to_school"
```

#### Definition
```ts
declare const attributionData: AttributionCode;
interface AttributionCode {
  // Descriptor for where the download started from
  campaign: string,
  // A source, like addons.mozilla.org, or google.com
  source: string,
  // The medium for the download, like if this was referral
  medium: string,
  // Additional content, like an addonID for instance
  content: string
}
```

### `browserSettings`

* `update`, which has information about Firefox update channel

#### Examples

* Is updating enabled?
```java
browserSettings.update.enabled
```

* Is beta channel?
```js
browserSettings.update.channel == 'beta'
```

#### Definition

```ts
declare const browserSettings: {
  attribution: undefined | {
    // Referring partner domain, when install happens via a known partner
    // e.g. google.com
    source: string;
    // category of the source, such as "organic" for a search engine
    // e.g. organic
    medium: string;
    // identifier of the particular campaign that led to the download of the product
    // e.g. back_to_school
    campaign: string;
    // identifier to indicate the particular link within a campaign
    // e.g. https://mozilla.org/some-page
    content: string;
  },
  update: {
    // Is auto-downloading enabled?
    autoDownload: boolean;
    // What release channel, e.g. "nightly"
    channel: string;
    // Is updating enabled?
    enabled: boolean;
  }
}
```

### `buildId`

The build ID (`MOZ_BUILDID`) parsed as a number to allow for comparisons.

#### Examples

* Is the build from at least Jan 01 2025

```java
buildId >= 202501010000
```

#### Definition

```ts
declare const buildId: number;
```

### `currentDate`

The current date at the moment message targeting is checked.

#### Examples
* Is the current date after Oct 3, 2018?
```java
currentDate > "Wed Oct 03 2018 00:00:00"|date
```

#### Definition

```ts
declare const currentDate; ECMA262DateString;
// ECMA262DateString = Date.toString()
type ECMA262DateString = string;
```

### `devToolsOpenedCount`
Number of usages of the web console.

#### Examples
* Has the user opened the web console more than 10 times?
```java
devToolsOpenedCount > 10
```

#### Definition
```ts
declare const devToolsOpenedCount: number;
```

### `isDefaultBrowser`

Is Firefox the user's default browser?

#### Definition

```ts
declare const isDefaultBrowser: boolean;
```

### `isDefaultBrowserUncached`

Behaves the same as `isDefaultBrowser`, but retrieves the current value directly from shell service instead of using the cached value. This may not be as performant.

### `isOneClickSetDefaultEnabled`

Windows only. Can Firefox currently make itself the default browser by writing
the Windows UserChoice registry keys, instead of having to send the user into the
Windows Settings app to do it manually. Accounts for the
`browser.shell.setDefaultBrowserUserChoice` and
`browser.shell.setDefaultBrowserUserChoice.regRename` prefs as well as whether
Windows will currently accept a UserChoice change from a third-party browser.

`true` means a set-default request would be honored or that Firefox is already the default (the latter is a shortcut to `true` to avoid the risk of a UserChoice write when we don't need to attempt to set to default anyway).


If the UserChoice Protection Driver (UCPD) is running and Firefox isn't already
the default, this temporarily renames the `http` association key and renames it
back. UCPD versions where one-click still works permit this rename, while those where it doesn't do not.


Always `false` on macOS and Linux. Neither goes through UserChoice, and neither
is reliably one-click, since both can (but don't always) defer to an OS consent prompt. Supporting them needs its own handling (see bug 2060879).

#### Definition

```ts
declare const isOneClickSetDefaultEnabled: boolean;
```

### `isDefaultHandler`

Is Firefox the user's default handler for various file extensions and protocols?

Windows-only.

#### Definition

```ts
declare const isDefaultHandler: {
  pdf: boolean;
  html: boolean;
  mailto: boolean;
};
```

#### Examples
* Is Firefox the default PDF handler?
```ts
isDefaultHandler.pdf
```

* Is Firefox the default mailto protocol handler?
```ts
isDefaultHandler.mailto
```

* Prompt to set Firefox as default when on their configured email site
```ts
host == mailtoHandlerHost && !isDefaultHandler.mailto
```

### `defaultPDFHandler`

Information about the user's default PDF handler

Windows-only.

#### Definition

```ts
declare const defaultPDFHandler: {
  // Does the user have a default PDF handler registered?
  registered: boolean;

  // Is the default PDF handler a known browser?
  knownBrowser: boolean;
};
```

### `mailtoHandlerHost`

The host of the web service Firefox is configured to hand `mailto:` links to
(e.g. `"mail.google.com"`), or `null` if Firefox has no configured web handler
for `mailto:`. This reflects only Firefox's own handler setting, not the
OS-level `mailto:` default (see [`isDefaultHandler`](#isdefaulthandler)), and
says nothing about whether the preferred action is "always ask".

Windows-only.

#### Definition

```ts
declare const mailtoHandlerHost: string | null;
```

#### Examples

* Is the user currently on their configured mail host? (Pair with a trigger that
  exposes the navigated `host`, such as `openURL`.)
```java
host == mailtoHandlerHost && !isDefaultHandler.mailto
```

* Prompt only when on Gmail and Firefox isn't the OS default
```java
mailtoHandlerHost == "mail.google.com" && !isDefaultHandler.mailto
```

### `firefoxVersion`

The major Firefox version of the browser

#### Examples
* Is the version of the browser greater than 63?
```java
firefoxVersion > 63
```

#### Definition

```ts
declare const firefoxVersion: number;
```

### `launchOnLoginEnabled`

Is the launch on login option enabled?

```ts
declare const launchOnLoginEnabled: boolean;
```

### `launchOnLoginAllowedByPolicy`

Whether launch on login is allowed to be enabled, i.e. it has not been overridden
by Windows Settings or enterprise policy. Mirrors the `isAllowedByPolicy` value
from `getLaunchOnLoginEnablementDetails()`. Always `false` on non-Windows
platforms. Use together with `launchOnLoginEnabled` to target users who do not
have launch on login enabled but for whom it could be enabled.

```ts
declare const launchOnLoginAllowedByPolicy: boolean;
```

### `locale`
The current UI locale of the browser including country code, e.g. `en-US`. This is
the locale Firefox chose to render its UI in: the first match between Firefox's
available locales and the user's ranked OS/browser language preferences.

#### Examples
* Is the locale of the browser either English (US) or German (Germany)?
```java
locale in ["en-US", "de-DE"]
```

#### Definition
```ts
declare const locale: string;
```

[Source](https://searchfox.org/mozilla-central/source/browser/components/asrouter/modules/ASRouterTargeting.sys.mjs#651)

### `localeLanguageCode`
The current locale of the browser NOT including country code, e.g. `en`.
This is useful for matching all countries of a particular language.

#### Examples
* Is the locale of the browser any English locale?
```java
localeLanguageCode == "en"
```

#### Definition
```ts
declare const localeLanguageCode: string;
```

### `needsUpdate`

Does the client have the latest available version installed

```ts
declare const needsUpdate: boolean;
```

### `packageFamilyName`
Provides the package family name as given by the MSIX that Firefox was
installed from, or the empty string if not installed from MSIX.

#### Examples
* Is the user running MSIX Nightly?
```ts
"MozillaNightly" in packageFamilyName
```

#### Definition
```ts
declare const packageFamilyName: string;
```

### `pinnedSites`
The sites (including search shortcuts) that are pinned on a user's new tab page.

#### Examples
* Has the user pinned any site on `foo.com`?
```java
"foo.com" in pinnedSites|mapToProperty("host")
```

* Does the user have a pinned `duckduckgo.com` search shortcut?
```java
"duckduckgo.com" in pinnedSites[.searchTopSite == true]|mapToProperty("host")
```

#### Definition
```ts
interface PinnedSite {
  // e.g. https://foo.mozilla.com/foo/bar
  url: string;
  // e.g. foo.mozilla.com
  host: string;
  // is the pin a search shortcut?
  searchTopSite: boolean;
}
declare const pinnedSites: Array<PinnedSite>
```

### `previousSessionEnd`

Timestamp of the previously closed session.

#### Definition
```ts
declare const previousSessionEnd: UnixEpochNumber;
// UnixEpochNumber is UNIX Epoch timestamp, e.g. 1522843725924
type UnixEpochNumber = number;
```

### `primaryResolution`

An object containing the available width and available height of the primary monitor in pixel values. The values take into account the existence of docks and task bars.

#### Definition

```ts
interface primaryResolution {
  width: number;
  height: number;
}
```

### `profileAgeCreated`

The date the profile was created as a UNIX Epoch timestamp.

#### Definition

```ts
declare const profileAgeCreated: UnixEpochNumber;
// UnixEpochNumber is UNIX Epoch timestamp, e.g. 1522843725924
type UnixEpochNumber = number;
```

### `profileAgeReset`

The date the profile was reset as a UNIX Epoch timestamp (if it was reset).

#### Examples
* Was the profile never reset?
```java
!profileAgeReset
```

#### Definition
```ts
// profileAgeReset can be undefined if the profile was never reset
// UnixEpochNumber is number, e.g. 1522843725924
declare const profileAgeReset: undefined | UnixEpochNumber;
// UnixEpochNumber is UNIX Epoch timestamp, e.g. 1522843725924
type UnixEpochNumber = number;
```

### `providerCohorts`

Information about cohort settings (from prefs, including shield studies) for each provider.

#### Examples
* Is the user in the "foo_test" cohort for cfr?
```java
providerCohorts.cfr == "foo_test"
```

#### Definition

```ts
declare const providerCohorts: {
  [providerId: string]: string;
}
```

### `region`

The user's home region as a country code. Firefox distinguishes between two region concepts:

- **Home region** (`Region.home`): the region determined when the user first set up their
  profile, queried from `location.services.mozilla.com` and then persisted. This is
  what this targeting attribute exposes.
- **Current region** (`Region.current`): the most recently detected region via IP-based
  geolocation, which may differ from home if the user is traveling.

Can be `""` if the location service request has not yet completed or encountered an error.

#### Examples
* Is the user in Canada?
```java
region == "CA"
```

#### Definition

```ts
declare const region: string;
```

[Source](https://searchfox.org/mozilla-central/source/browser/components/asrouter/modules/ASRouterTargeting.sys.mjs#835)

### `searchEngines`

Information about the user's default search engine, available appProvided config engines, and whether
each engine has entered search mode. If the user’s default engine is a third-party engine,
``current`` is ``null``.

#### Examples
* Is the current default search engine set to google?
```java
searchEngines.current == "google"
```
* Is google in the list of appProvided config engines?
```java
"google" in searchEngines.installed
```
* Has google been entered in search mode.
```java
searchEngines.hasEnteredSearchMode.google
```

#### Definition

```ts
declare const searchEngines: Promise<SearchEnginesResponse>;
interface SearchEnginesResponse: {
  current: SearchEngineId | null;
  installed: Array<SearchEngineId>;
  hasEnteredSearchMode: Record<SearchEngineId, boolean>;
}
// This is an identifier for a search engine such as "google" or "ddg"
type SearchEngineId = string;
```

### `sync`

Information about synced devices.

#### Examples
* Is at least 1 mobile device synced to this profile?
```java
sync.mobileDevices > 0
```

#### Definition

```ts
declare const sync: {
  desktopDevices: number;
  mobileDevices: number;
  totalDevices: number;
}
```

### `topFrecentSites`

Information about the browser's top 25 frecent sites.

**Please note this is a restricted targeting property that influences what telemetry is allowed to be collected and may not be used without data review. This attribute has received legal approval for use in message targeting.**

**When using this attribute, targeting must not allow inference that a user visited a single specific website. Avoid checking for a single domain; instead, use a broad list or bucket of domains.**

#### Examples
* Is any of a broad set of shopping-related domains in the user's top frecent sites with a last visit date greater than April 4th, 2018 (UNIX Epoch timestamp 1522843725924)?
```js
(["amazon.com", "ebay.com", "etsy.com", "walmart.com", "target.com",
  "bestbuy.com", "newegg.com", "costco.com", "homedepot.com", "wayfair.com"
  ] intersect topFrecentSites[.lastVisitDate > 1522843725924]|mapToProperty('host'))|length > 1
```

#### Definition
```ts
declare const topFrecentSites: Promise<Array<TopSite>>
interface TopSite {
  // e.g. https://foo.mozilla.com/foo/bar
  url: string;
  // e.g. foo.mozilla.com
  host: string;
  // Deprecated property unsupported in Firefox 145+, refer to bug 1987415 for guidance on future options.
  frecency: number;
  lastVisitDate: UnixEpochNumber;
}
// UnixEpochNumber is UNIX Epoch timestamp, e.g. 1522843725924
type UnixEpochNumber = number;
```

### `totalBookmarksCount`

Total number of bookmarks.

#### Definition

```ts
declare const totalBookmarksCount: number;
```

### `userActiveDaysWithHundredPlusSites`
The number of days in the past month where the user visited 100 or more URLs.
Derived from [`userMonthlyActivity`](#usermonthlyactivity).

#### Example
* Has the user visited 100+ sites on at least 5 days in the past month?
```java
userActiveDaysWithHundredPlusSites >= 5
```

#### Definition
```ts
declare const userActiveDaysWithHundredPlusSites: Promise;
```

### `usesFirefoxSync`

Does the user use Firefox sync?

#### Definition

```ts
declare const usesFirefoxSync: boolean;
```

### `isFxAEnabled`

Does the user have Firefox sync enabled? The service could potentially be turned off [for enterprise builds](https://searchfox.org/mozilla-central/rev/b59a99943de4dd314bae4e44ab43ce7687ccbbec/browser/components/enterprisepolicies/Policies.jsm#327).

#### Definition

```ts
declare const isFxAEnabled: boolean;
```

### `isFxASignedIn`

Is the user signed in to a Firefox Account?

#### Definition

```ts
declare const isFxASignedIn: Promise<boolean>
```

### `creditCardsSaved`

The number of credit cards the user has saved for Forms and Autofill.

#### Examples
```java
creditCardsSaved > 1
```

#### Definition

```ts
declare const creditCardsSaved: Promise<number>
```

### `addressesSaved`

The number of addresses the user has saved for Forms and Autofill.

#### Examples
```java
addressesSaved > 1
```

#### Definition

```ts
declare const addressesSaved: Promise<number>
```

### `archBits`

The number of bits used to represent a pointer in this build.

#### Definition

```ts
declare const archBits: number;
```

### `xpinstallEnabled`

Pref used by system administrators to disallow add-ons from installed altogether.

#### Definition

```ts
declare const xpinstallEnabled: boolean;
```

### `currentTabInstalledAsWebApp`

Returns whether the current tab has a matching Web App (Taskbar Tab) installed.

#### Definition

```ts
declare const currentTabInstalledAsWebApp: Promise<boolean>;
```

### `installedWebAppsCount`

Returns the number of Web Apps (Taskbar Tabs) the user has installed.

#### Definition

```ts
declare const installedWebAppsCount: Promise<number>;
```

### `currentTabGroups`

Returns the number of currently open tab groups.

### `tabsOpenInTopWindow`

Returns the number of tabs open in the top browser window.

#### Definition

```ts
declare const tabsOpenInTopWindow: number;
```

### `savedTabGroups`

Returns the number of tab groups the user has saved.

### `hasPinnedTabs`

Does the user have any pinned tabs in any windows.

#### Definition

```ts
declare const hasPinnedTabs: boolean;
```

### `hasAccessedFxAPanel`

Boolean pref that gets set the first time the user opens the FxA toolbar panel

#### Definition

```ts
declare const hasAccessedFxAPanel: boolean;
```

### `totalBlockedCount`

Total number of events from the content blocking database

#### Definition

```ts
declare const totalBlockedCount: number;
```

### `recentBookmarks`

An array of GUIDs of recent bookmarks as provided by [`NewTabUtils.getRecentBookmarks`](https://searchfox.org/mozilla-central/rev/c5c002f81f08a73e04868e0c2bf0eb113f200b03/toolkit/modules/NewTabUtils.sys.mjs#1059)

#### Definition

```ts
interface Bookmark {
  bookmarkGuid: string;
  url: string;
  title: string;
  ...
}
declare const recentBookmarks: Array<Bookmark>
```

### `userPrefs`

Information about user facing prefs configurable from `about:preferences`.

#### Examples
```java
userPrefs.cfrFeatures == false
```

#### Definition

```ts
declare const userPrefs: {
  cfrFeatures: boolean;
  cfrAddons: boolean;
}
```

### `userWeekdaysActiveInLastMonth`

The number of days in the past month the user was active on a weekday (Monday–Friday).
Derived from [`userMonthlyActivity`](#usermonthlyactivity).

#### Examples
* Has the user used Firefox on 2 or more weekdays in the past month?
```java
userWeekdaysActiveInLastMonth >= 2
```

#### Definition
```ts
declare const userWeekdaysActiveInLastMonth: Promise;
```

### `attachedFxAOAuthClients`

Information about connected services associated with the FxA Account.
Return an empty array if no account is found or an error occurs.

#### Definition

```
interface OAuthClient {
  // OAuth client_id of the service
  // https://docs.telemetry.mozilla.org/datasets/fxa_metrics/attribution.html#service-attribution
  id: string;
  lastAccessedDaysAgo: number;
}

declare const attachedFxAOAuthClients: Promise<OAuthClient[]>
```

#### Examples
```javascript
{
  id: "7377719276ad44ee",
  name: "Pocket",
  lastAccessTime: 1513599164000
}
```

### `relayProfileInfo`

Firefox Relay profile information including subscription tier and mask count.
Returns null if the user is not signed into Firefox, has no Relay account, or if an error occurs.

This attribute fetches data from the Relay API, including both profile information (subscription tier) and the total number of email masks created.

#### Definition

```
interface RelayProfileInfo {
  has_premium: boolean;  // true if user has premium subscription
  has_phone: boolean;    // true if user has phone masking
  has_vpn: boolean;      // true if user has VPN bundled
  masksCount: number;    // total number of email masks created
}

declare const relayProfileInfo: Promise<RelayProfileInfo | null>
```

#### Examples
```javascript
// Check if user has premium
relayProfileInfo.has_premium

// Get mask count from profile
relayProfileInfo.masksCount > 1
```

### `relayEmailMasksCount`

Number of Firefox Relay email masks created by the signed-in user.
Returns 0 if the user is not signed into Firefox, has no Relay account, or if an error occurs.

#### Definition

```
declare const relayEmailMasksCount: Promise<number>
```

#### Examples
```javascript
// Show to users with more than 1 mask
relayEmailMasksCount > 1

// Show to users with at least 5 masks
relayEmailMasksCount >= 5
```

### `isRelayFreeTier`

Boolean indicating if the signed-in user has a FREE tier Relay subscription (not premium).
Returns false if the user is not signed into Firefox, has no Relay account, or if an error occurs.

#### Definition

```
declare const isRelayFreeTier: Promise<boolean>
```

#### Examples
```javascript
// Show only to FREE tier Relay users
isRelayFreeTier

// Show only to premium Relay users
!isRelayFreeTier && relayEmailMasksCount > 0
```

### `platformName`

[Platform information](https://searchfox.org/mozilla-central/rev/c5c002f81f08a73e04868e0c2bf0eb113f200b03/toolkit/modules/AppConstants.sys.mjs#153).

#### Definition

```
declare const platformName = "linux" | "win" | "macosx" | "android" | "other";
```

### `memoryMB`

The amount of RAM available to Firefox, in megabytes.

#### Definition

```ts
declare const memoryMB = number;
```

### `messageImpressions`

Dictionary that maps message ids to impression timestamps. Timestamps are stored in
consecutive order. Can be used to detect first impression of a message, number of
impressions. Can be used in targeting to show a message if another message has been
seen.
Impressions are used for frequency capping so we only store them if the message has
`frequency` configured.
Impressions for badges might not work as expected: we add a badge for every opened
window so the number of impressions stored might be higher than expected. Additionally
not all badges have `frequency` cap so `messageImpressions` might not be defined.
Badge impressions should not be used for targeting.

#### Definition

```
declare const messageImpressions: { [key: string]: Array<UnixEpochNumber> };
```

### `blockedCountByType`

Returns a breakdown by category of all blocked resources in the past 42 days.

#### Definition

```
declare const blockedCountByType: { [key: string]: number };
```

#### Examples

```javascript
Object {
  trackerCount: 0,
  cookieCount: 34,
  cryptominerCount: 0,
  fingerprinterCount: 3,
  socialCount: 2
}
```

### `browserIsSelected`

A context property included for all triggers, indicating whether the tab the
trigger came from is the currently selected tab. For some triggers that don't
actually emit from tabs, this is always true. For other triggers, like
`openURL`, this can be false if the trigger happened in a background tab.

#### Definition

```ts
declare const browserIsSelected: boolean;
```

### `hasActiveAIWindow`

Whether any currently open window is an active Smart Window. Unlike
`isAIWindow`, which only reflects the window that fired the trigger, this checks
every window, so a message can be suppressed while a Smart Window is open
anywhere.

#### Definition

```ts
declare const hasActiveAIWindow: boolean;
```

### `isAIWindow`

A context property included for all triggers that evaluates to `true` when the
message comes from an AI Window, and `false` otherwise.

#### Definition

```ts
declare const isAIWindow: boolean;
```

#### Examples

* Target AI Windows only:
```javascript
isAIWindow
```

* Target Classic Windows only:
```javascript
!isAIWindow
```

* Target both AI Windows and Classic Windows:
```javascript
isAIWindow == isAIWindow
or equivalently
(isAIWindow || !isAIWindow)
```

### `isSmartTabGroupingAllowed`

Whether Smart Tab Grouping is available to this user, delegating to
`SmartTabGroupingManager.isAllowed`. Smart Tab Grouping is currently gated on an
English application locale, but that rule lives in the feature itself, so prefer
this attribute over a hand-written locale check: messages that depend on the
feature then stay in sync when the gate changes.

#### Definition

```ts
declare const isSmartTabGroupingAllowed: boolean;
```

#### Examples

* Only show a message when the feature can actually run:
```javascript
isSmartTabGroupingAllowed
```

### `userId`

A unique user id generated by Normandy (note that this is not clientId).

#### Definition

```ts
declare const userId: string;
```

### `profileRestartCount`

A session counter that shows how many times the browser was started.
More info about the details in [the telemetry docs](https://firefox-source-docs.mozilla.org/toolkit/components/telemetry/concepts/sessions.html).

#### Definition

```ts
declare const profileRestartCount: number;
```

### `homePageSettings`

An object reflecting the current settings of the browser home page (about:home)

#### Definition

```ts
declare const homePageSettings: {
  isDefault: boolean;
  isLocked: boolean;
  isWebExt: boolean;
  isCustomUrl: boolean;
  urls: Array<URL>;
}

interface URL {
  url: string;
  host: string;
}
```

#### Examples

* Default about:home
```javascript
Object {
  isDefault: true,
  isLocked: false,
  isCustomUrl: false,
  isWebExt: false,
  urls: [
    { url: "about:home", host: "" }
  ],
}
```

* Default about:home with locked preference
```javascript
Object {
  isDefault: true,
  isLocked: true,
  isCustomUrl: false,
  isWebExt: false,
  urls: [
    { url: "about:home", host: "" }
  ],
}
```

* Custom URL
```javascript
Object {
  isDefault: false,
  isLocked: false,
  isCustomUrl: true,
  isWebExt: false,
  urls: [
    { url: "https://www.google.com", host: "google.com" }
  ],
}
```

* Custom URLs
```javascript
Object {
  isDefault: false,
  isLocked: false,
  isCustomUrl: true,
  isWebExt: false,
  urls: [
    { url: "https://www.google.com", host: "google.com" },
    { url: "https://www.youtube.com", host: "youtube.com" }
  ],
}
```

* Web extension
```javascript
Object {
  isDefault: false,
  isLocked: false,
  isCustomUrl: false,
  isWebExt: true,
  urls: [
    { url: "moz-extension://123dsa43213acklncd/home.html", host: "" }
  ],
}
```

### `newtabAddonVersion`

The full version string of the built-in New Tab add-on that is actively in use.
Comparisons should be done with the `versionCompare` filter expression.

#### Definition

```ts
declare const newtabAddonVersion: string;
```

### `newtabSettings`

An object reflecting the current settings of the browser newtab page (about:newtab)

#### Definition

```ts
declare const newtabSettings: {
  isDefault: boolean;
  isWebExt: boolean;
  isCustomUrl: boolean;
  url: string;
  host: string;
}
```

#### Examples

* Default about:newtab
```javascript
Object {
  isDefault: true,
  isCustomUrl: false,
  isWebExt: false,
  url: "about:newtab",
  host: "",
}
```

* Custom URL
```javascript
Object {
  isDefault: false,
  isCustomUrl: true,
  isWebExt: false,
  url: "https://www.google.com",
  host: "google.com",
}
```

* Web extension
```javascript
Object {
  isDefault: false,
  isCustomUrl: false,
  isWebExt: true,
  url: "moz-extension://123dsa43213acklncd/home.html",
  host: "",
}
```

### `activeNotifications`

True when an infobar style message is displayed or when the awesomebar is
expanded to show a message (for example onboarding tips).

### `isMajorUpgrade`

A boolean. `true` if the browser just updated to a new major version.

### `hasActiveEnterprisePolicies`

A boolean. `true` if any Enterprise Policies are active.

### `userMonthlyActivity`

Returns an array of entries in the form `[int, unixTimestamp]` for each day of
user activity where the first entry is the total urls visited for that day.

### `doesAppNeedPin`

Checks if Firefox app can be and isn't pinned to OS taskbar/dock or Windows start menu in MSIX builds.

### `doesAppNeedPinUncached`

Does the same as `doesAppNeedPin`, but retrieves the current value directly from shell service instead of using the cached value. This may not be as performant.

### `doesAppNeedPrivatePin`

Checks if Firefox Private Browsing Mode can be and isn't pinned to OS taskbar/dock. Currently this only works on certain Windows versions.

### `isBackgroundTaskMode`

Checks if this invocation is running in background task mode.

### `backgroundTaskName`

A non-empty string task name if this invocation is running in background task
mode, or `null` if this invocation is not running in background task mode.

### `userPrefersReducedMotion`

Checks if user prefers reduced motion as indicated by the value of a media query for `prefers-reduced-motion`.

### `distributionId`

A string containing the id of the distribution, or the empty string if there
is no distribution associated with the build.

### `fxViewButtonAreaType`

A string of the name of the container where the Firefox View button is shown, null if the button has been removed.

### `alltabsButtonAreaType`

A string of the name of the container where the Tab Overflow button (or All Tabs button) is shown, null if the button has been removed.

### `hasMigratedBookmarks`

A boolean. `true` if the user ever used the Migration Wizard to migrate bookmarks since Firefox 113 released. Available in Firefox 113+; will not be true if the user had only ever migrated bookmarks prior to Firefox 113 being released.

### `hasMigratedCSVPasswords`

A boolean. `true` if CSV passwords have been imported via the migration wizard since Firefox 116 released. Available in Firefox 116+; ; will not be true if the user had only ever migrated CSV passwords prior to Firefox 116 being released.

### `hasMigratedHistory`

A boolean. `true` if the user ever used the Migration Wizard to migrate history since Firefox 113 released. Available in Firefox 113+; will not be true if the user had only ever migrated history prior to Firefox 113 being released.

### `hasMigratedPasswords`

A boolean. `true` if the user ever used the Migration Wizard to migrate passwords since Firefox 113 released. Available in Firefox 113+; will not be true if the user had only ever migrated passwords prior to Firefox 113 being released.

### `useEmbeddedMigrationWizard`

A boolean. `true` if the user is configured to use the embedded Migration Wizard in about:welcome.

### `isRTAMO`

A boolean. `true` when [RTAMO](first-run.md#return-to-amo-rtamo) has been used to download Firefox, `false` otherwise.

### `isPrivateWindow`

A boolean. `true` when the top window is in Private Browsing Mode; `false` otherwise.

### `isTaskbarTabWindow`

A boolean. `true` when the top window is a taskbar tab; `false` otherwise.

### `canRestoreLastSession`

A boolean. `true` when the user has a previous session saved that can be
restored; `false` otherwise. Typically false when the previous session has
already been restored, when the user has configured the browser to not save
sessions, or on first run.

### `autoRestoreSessionEnabled`

A boolean. `true` when the user has configured the browser to automatically
restore the previous session on startup; `false` otherwise.

### `canCreateSelectableProfiles`

A boolean. `true` when both the current install and current profile support creating additional profiles using the `SelectableProfileService`; `false` otherwise.

### `hasSelectableProfiles`

A boolean. `true` when the `toolkit.profiles.storeID` pref has a value. Indicates that the profile is part of a profile group managed by the `SelectableProfileService`, and the user has used the multiple profiles feature. `false` otherwise.

### `unhandledCampaignAction`

A string. A special message action to be executed on first-run. For example, `"SET_DEFAULT_BROWSER"` when the user selected to set as default via the [install marketing page](https://www.mozilla.org/firefox/new/) and set default has not yet been automatically triggered, `null` otherwise. Currently supported actions include `"PIN_AND_DEFAULT"`, `"PIN_FIREFOX_TO_TASKBAR"`, and `"SET_DEFAULT_BROWSER"`.

### `isMSIX`

A boolean. `true` when hasPackageId is `true` on Windows, `false` otherwise.

### `isDeviceMigration`

A boolean. `true` when [support.mozilla.org](https://support.mozilla.org) has been used to download the browser as part of a "migration" campaign, for device migration guidance, `false` otherwise.

### `screenImpressions`

An array that maps about:welcome screen IDs to their most recent impression timestamp. Should only be used for unique screen IDs to avoid unintentionally targeting messages with identical screen IDs.
#### Definition

```
declare const screenImpressions: { [key: string]: Array<UnixEpochNumber> };
```

### `systemArch`

The architecture of this Firefox build: x86, x86-64 or aarch64.

#### Definition

```ts
declare const systemArch: string | null;
```

### `tabNotesCount`

The total number of tab notes the user has stored in their current profile.

#### Definition

```ts
declare const tabNotesCount: Promise<number>;
```


### `totalSearches`

Returns the number of times a user has completed a search in the URL Bar. The number is arbitrarily capped at 100.

### `profileGroupId`

Returns the stable profile group ID used for data reporting.

### `currentProfileId`

The integer-valued identifier of the current selectable profile, as reported by `SelectableProfileService`, converted to a string.

### `profileGroupProfileCount`

The number of profiles in the current profile group or zero if either the
feature is not enabled or the user has not created any profiles.

### `backupsInfo`

Provides information about the backups a user has in the default directory.

#### Definition

```ts
declare const backupsInfo: {
  // True if exactly one backup was found.
  found: boolean;

  // True if multiple backups were found.
  multipleBackupsFound: boolean;

  // Absolute path to the selected backup to restore when `found` is true.
  // Null when no single file is selected (none or multiple).
  backupFileToRestore: string | null;
};
```

### `backupArchiveEnabled`

Indicates whether the archive function is enabled by BackupService.

### `backupRestoreEnabled`

Indicates whether the restore function is enabled by BackupService.

### `isEncryptedBackup`

Indicates whether a user has selected an encrypted or non-encrypted backup method during the spotlight onboarding flow. (Refers to the `messaging-system-action.backupChooser` pref.)

### `isSmartWindowOnboarding`

A boolean. `true` when a user downloads Firefox from a Smart Window marketing campaign (ie. `attributionData.campaign == "smart_window"`), `false` otherwise.

### `isFirstRun`

`true` on the first time Normandy runs on a new profile. Normandy sets the
`app.normandy.first_run` pref to `false` after its first run, so this will be
`true` exactly once per profile regardless of installation mechanism.

> ⚠ **NOTE:** This targeting attribute is only available for the JEXL expressions
> of experiments' advanced targeting configs, which are defined in the
> [experimenter repository](https://experimenter.info). Targeting expressions for
> messages do not have access to this attribute.

#### Definition

```ts
declare const isFirstRun: boolean;
```

[Source](https://searchfox.org/mozilla-central/source/toolkit/components/normandy/lib/ClientEnvironment.sys.mjs#121)

### `isFirstStartup`

`true` while Firefox is running through its first-startup sequence (i.e., when
`FirstStartup` is in the `IN_PROGRESS` state). Unlike `isFirstRun`, which tracks
the first Normandy run, this tracks the first startup of the browser itself.

> ⚠ **NOTE:** This targeting attribute is only available for the JEXL expressions
> of experiments' advanced targeting configs, which are defined in the
> [experimenter repository](https://experimenter.info). Targeting expressions for
> messages do not have access to this attribute.

#### Definition

```ts
declare const isFirstStartup: boolean;
```

[Source](https://searchfox.org/mozilla-central/source/toolkit/components/nimbus/lib/ExperimentManager.sys.mjs#233)

### `isNonStubFirstRun`

`true` during the first Nimbus `updateRecipes` pass on a new profile, `false`
on all subsequent passes. The `nimbus.firstUpdateComplete` pref is set to `true`
after the first `updateRecipes` pass completes; this attribute reads from that
pref.

This is the cross-platform equivalent of `isFirstStartup` for platforms where
`isFirstStartup` is always `false` (Mac, Linux, and MSIX). On Windows stub
installer builds, both `isFirstStartup` and `isNonStubFirstRun` will be `true`
during first-run enrollment.

> **NOTE:** This targeting attribute is only available for the JEXL expressions
> of experiments' advanced targeting configs, which are defined in the
> [experimenter repository](https://experimenter.info). Targeting expressions for
> messages do not have access to this attribute.

#### Definition

```ts
declare const isNonStubFirstRun: boolean;
```

[Source](https://searchfox.org/mozilla-central/source/toolkit/components/nimbus/lib/ExperimentManager.sys.mjs#235)

### `experimentsLoaded`

Boolean that's true once Nimbus has loaded remote experiments from Remote Settings at least once. Returns true if experiments are disabled. This generally shouldn't be used outside of the splash screen.

### `crashCount`

The total number of crashes per 180 days the user has experienced, as recorded by the [crash manager](https://searchfox.org/firefox-main/source/toolkit/components/crashes/CrashManager.in.sys.mjs#1006-1016) at crash time, independent of whether a crash report was submitted.

#### Definition

```ts
declare const crashCount: Promise<number>;
```

### `daysSinceLastCrash`

The number of days since the most recent crash, as recorded by the [crash manager](https://searchfox.org/firefox-main/source/toolkit/components/crashes/CrashManager.in.sys.mjs#1006-1016) at crash time, independent of whether a crash report was submitted. If there are no recorded crashes, returns `null`.

#### Definition

```ts
declare const daysSinceLastCrash: Promise<number|null>;
```

### `crashCountInLastDay`

The number of crashes the user has experienced in the last 24 hours, as recorded by the [crash manager](https://searchfox.org/firefox-main/source/toolkit/components/crashes/CrashManager.in.sys.mjs#1006-1016) at crash time, independent of whether a crash report was submitted.

#### Definition

```ts
declare const crashCountInLastDay: Promise<number>;
```

### `crashCountInLastWeek`

The number of crashes the user has experienced in the last 7 days, as recorded by the [crash manager](https://searchfox.org/firefox-main/source/toolkit/components/crashes/CrashManager.in.sys.mjs#1006-1016) at crash time, independent of whether a crash report was submitted.

#### Definition

```ts
declare const crashCountInLastWeek: Promise<number>;
```

### `previousSessionCrashed`

`true` if the previous browser session ended in a crash, as reported by [`SessionStartup`](https://searchfox.org/firefox-main/source/browser/components/sessionstore/SessionStartup.sys.mjs#437).

#### Definition

```ts
declare const previousSessionCrashed: boolean;
```

### `isLaunchOnLogin`

`true` if this Firefox launch was initiated by the OS on login. Detected via the `-os-autostart` command-line flag, which is only injected by the Windows launch-on-login paths. This attribute is always `false` on macOS and Linux. It also will not detect cases where a user has manually added Firefox to OS-level login items outside of Firefox's own launch-on-login setting.

#### Definition

```ts
declare const isLaunchOnLogin: boolean;
```
