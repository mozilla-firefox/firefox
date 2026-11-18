# IP Protection preferences

This document describes preferences affecting Firefox's IP Protection.
These preferences are normally hidden and should not be used unless you really
know what you are doing.

## Feature enablement and experiments

`browser.ipProtection.enabled` (boolean, default: `false`)

: Master feature toggle controlled by Nimbus and testing harnesses.

`browser.ipProtection.userEnabled` (boolean, default: `false`)

: For testing; simulates user‑enabled state.

`browser.ipProtection.added` (boolean, default: `false`)

: Tracks whether the toolbar button was auto-placed next to the FxA button.
  Once true, the widget is not reinserted automatically after manual removal.

`browser.ipProtection.features.autoStart` (boolean, default: `false`)

: Feature flag enabling the auto-start capability.

`browser.ipProtection.features.siteExceptions` (boolean, default: `false`)

: Feature flag enabling the site exceptions capability.

`browser.ipProtection.features.siteInclusions` (boolean, default: `true`)

: Feature flag enabling the site inclusions capability.

`browser.ipProtection.optedOut` (boolean, default: `false`)

: Set when the user has opted out of the service.

## Startup and caching

`browser.ipProtection.autoStartEnabled` (boolean, default: `false`)

: Enables the auto-start helper so the proxy connects during browser startup.

`browser.ipProtection.autoStartPrivateEnabled` (boolean, default: `false`)

: Enables auto-start specifically for private browsing windows.

`browser.ipProtection.autoRestoreEnabled` (boolean, default: `false`)

: Enables auto-restore of the VPN connection when a browser session is restored.

`browser.ipProtection.stateCache` (string, default: `""`)

: Caches the latest `IPProtectionStates` value for use during startup.

`browser.ipProtection.entitlementCache` (string, default: `""`)

: Cached entitlement JSON string used during startup to avoid network requests.

`browser.ipProtection.usageCache` (string, default: `""`)

: Cached proxy usage JSON used during startup.

`browser.ipProtection.hasUpgraded` (boolean, default: `false`)

: Cached flag indicating the user has a paid subscription.

`browser.ipProtection.upgradeNotAvailable` (boolean, default: `false`)

: When `true`, suppresses all upgrade-related messaging (settings link, feature
  callouts, locations subview promo) regardless of the user's subscription state.
  Intended to be flipped via Nimbus in regions or configurations where the VPN
  upgrade path is not offered.

`browser.ipProtection.cacheDisabled` (boolean, default: `false`)

: Turns off all startup caches. Used primarily by xpcshell tests.

## Networking and routing

`browser.ipProtection.guardian.endpoint` (string, default: `"https://vpn.mozilla.org/"`)

: Endpoint for the server‑side infrastructure.

`browser.ipProtection.guardian.timeout` (integer, default: `30000`)

: Milliseconds to wait for a proxy activation or a proxy pass rotation to finish
  before aborting the attempt and moving to the error state.

`browser.ipProtection.serviceWaitTimeout` (integer, default: `90000`)

: Milliseconds `start()` holds traffic for a service that is not `READY` yet
  but may still become so, as during startup. A wait is not a request, so it
  is not bounded by the guardian timeout.

`browser.ipProtection.guardian.retryAfter` (integer, default: `500`)

: Milliseconds to delay before retrying a proxy pass fetch that failed with a
  transient error (a 5xx response, or a network error while offline).

`browser.ipProtection.productVpn.endpoint` (string, default: `"https://www.mozilla.org/"`)

: Endpoint for the production mozilla webservice.

`browser.ipProtection.mode` (integer, default: `0`)

: `IPPMode` value new connections start in; anything else is treated as
  `MODE_FULL`. Read at connection start, so it leaves a running connection
  alone.

  :::{seealso}
  [IPPMode](Constants.md#proxy-modes)
  :::

`browser.ipProtection.inclusion.match_patterns` (string, default: `""`)

: JSON array of URL match patterns restricting which requests are proxied.
  When empty, all traffic is proxied according to `mode`.

  :::{seealso}
  [Match Patterns Documentation](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/Match_patterns)
  :::

`browser.ipProtection.override.serverlist` (string)

: A JSON payload that overrides the server list. Follows the Remote Settings schema.

## UI Features

`browser.ipProtection.bandwidth.enabled` (boolean, default: `true`)

: Controls whether bandwidth usage information is displayed in the status panel
  and in `about:preferences`. `IPPUsageHelper` keeps this pref in sync with
  the entitlement's `limitedBandwidth` field.

`browser.ipProtection.bandwidthThreshold` (integer)

: Last recorded bandwidth usage threshold percentage, used for telemetry.

`browser.ipProtection.bandwidthResetDate` (string)

: Stores the bandwidth quota reset date.

`browser.ipProtection.bandwidthWarningDismissedThreshold` (integer)

: Bandwidth threshold percentage at which the user last dismissed the low-bandwidth warning.

`browser.ipProtection.egressLocation` (string, default: `""`)

: A country code of the currently selected VPN egress location,
  or a empty string for the default location.

`browser.ipProtection.locationUnlockMessageDismissed` (boolean, default: `false`)

: Tracks whether the message confirming the extra locations (given when Firefox becomes
  the default browser) has been dismissed.

`browser.ipProtection.egressLocationEnabled` (boolean, default: `false`)

: Controls whether the VPN egress location is displayed in the status panel.

`browser.ipProtection.onboardingMessageMask` (integer, default: `0`)

: Bitmask tracking which onboarding messages have already been shown.

`browser.ipProtection.userEnableCount` (integer, default: `0`)

: Number of times the user has enabled the proxy (capped at 3, used for onboarding).

## Diagnostics

`browser.ipProtection.log` (boolean, default: `false`)

: Enable/disable logging.

`browser.ipProtection.everOpenedPanel` (boolean, default: `false`)

: Tracks if the user has ever opened the VPN panel.

`browser.ipProtection.openedPanelSince160` (boolean, default: `false`)

: Tracks if the user has opened the VPN panel since release 160.

`browser.ipProtection.siteInclusionPromptShown` (boolean, default: `false`)

: Tracks if the panel has shown the prompt to always turn on VPN for a site, so
  it is only shown once.

`browser.ipProtection.hasSeenFeature` (boolean, default: `false`, `true` on
Nightly)

: Tracks if the VPN UI has ever been exposed to the user; `IPPL10nHelper` sets
  it to `true` as soon as that happens. While it is `true` the UI is shown
  regardless of the localization coverage of `browser/ipProtection.ftl`. A
  profile data upgrade sets it to `true` on profiles created before Firefox 155,
  which are assumed to have already seen the feature, so the gate only applies
  to newer ones.
  The gate is off on Nightly, where translations always lag behind. To test the
  feature on another channel in a locale still below the coverage threshold, set
  it to `true` in `about:config` and restart; conversely, to exercise the gate
  itself, set it to `false`.

`browser.ipProtection.l10nGateVersion` (integer, default: `0`)

: Firefox major version the l10n coverage gate was last enforced for; `0` while
  the gate never hid the feature. `IPPL10nHelper` records the current major
  version the first time it hides the UI and stops gating as soon as the browser
  is updated to a different major version, so a profile is never withheld the
  feature for more than one release cycle.

## Android / GPI

`browser.ipProtection.gpi.authJwt` (string)

: Cached GPI authentication JWT.

`browser.ipProtection.gpi.authJwtExpiresAt` (string)

: Expiry timestamp of the cached GPI JWT.

`browser.ipProtection.gpi.authJwtRenewAfter` (string)

: Timestamp after which the GPI JWT should be renewed.
