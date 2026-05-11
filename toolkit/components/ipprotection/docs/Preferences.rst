Preferences
===========

This document describes preferences affecting Firefox's IP Protection.
These preferences are normally hidden and should not be used unless you really
know what you are doing.

Feature enablement and experiments
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``browser.ipProtection.enabled`` (boolean, default: ``false``)
  Master feature toggle controlled by Nimbus and testing harnesses.

``browser.ipProtection.userEnabled`` (boolean, default: ``false``)
  For testing; simulates user‑enabled state.

``browser.ipProtection.added`` (boolean, default: ``false``)
  Tracks whether the toolbar button was auto-placed next to the FxA button.
  Once true, the widget is not reinserted automatically after manual removal.

``browser.ipProtection.features.autoStart`` (boolean, default: ``false``)
  Feature flag enabling the auto-start capability.

``browser.ipProtection.features.siteExceptions`` (boolean, default: ``false``)
  Feature flag enabling the site exceptions capability.

``browser.ipProtection.optedOut`` (boolean, default: ``false``)
  Set when the user has opted out of the service.

Startup and caching
~~~~~~~~~~~~~~~~~~~

``browser.ipProtection.autoStartEnabled`` (boolean, default: ``false``)
  Enables the auto-start helper so the proxy connects during browser startup.

``browser.ipProtection.autoStartPrivateEnabled`` (boolean, default: ``false``)
  Enables auto-start specifically for private browsing windows.

``browser.ipProtection.autoRestoreEnabled`` (boolean, default: ``false``)
  Enables auto-restore of the VPN connection when a browser session is restored.

``browser.ipProtection.stateCache`` (string, default: ``""``)
  Caches the latest ``IPProtectionStates`` value for use during startup.

``browser.ipProtection.entitlementCache`` (string, default: ``""``)
  Cached entitlement JSON string used during startup to avoid network requests.

``browser.ipProtection.usageCache`` (string, default: ``""``)
  Cached proxy usage JSON used during startup.

``browser.ipProtection.locationListCache`` (string, default: ``""``)
  Cached Guardian location list shared between ``IPProtectionService`` and
  ``GuardianClient``.

``browser.ipProtection.hasUpgraded`` (boolean, default: ``false``)
  Cached flag indicating the user has a paid subscription.

``browser.ipProtection.cacheDisabled`` (boolean, default: ``false``)
  Turns off all startup caches. Used primarily by xpcshell tests.

Networking and routing
~~~~~~~~~~~~~~~~~~~~~~

``browser.ipProtection.guardian.endpoint`` (string, default: ``"https://vpn.mozilla.org/"``)
  Endpoint for the server‑side infrastructure.

``browser.ipProtection.productVpn.endpoint`` (string, default: ``"https://www.mozilla.org/"``)
  Endpoint for the production mozilla webservice.

``browser.ipProtection.mode`` (integer, default: ``0``)
  Selects which requests are proxied by ``IPPChannelFilter``:
  ``0`` routes all traffic (``MODE_FULL``), ``1`` only private browsing windows
  (``MODE_PB``), ``2`` only requests classified as tracking (``MODE_TRACKER``).

``browser.ipProtection.inclusion.match_patterns`` (string, default: ``""``)
  JSON array of URL match patterns restricting which requests are proxied.
  When empty, all traffic is proxied according to ``mode``.

``browser.ipProtection.override.serverlist`` (string)
  A JSON payload that overrides the server list. Follows the Remote Settings schema.

UI Features
~~~~~~~~~~~

``browser.ipProtection.bandwidth.enabled`` (boolean, default: ``false``)
  Controls whether bandwidth usage information is displayed in the status panel.

``browser.ipProtection.bandwidth.maxInGb`` (integer, default: ``50``)
  Maximum bandwidth in GB used for messaging. Can be controlled by Nimbus via
  the ``ipProtection.bandwidthMax`` variable.

``browser.ipProtection.bandwidthThreshold`` (integer)
  Last recorded bandwidth usage threshold percentage, used for telemetry.

``browser.ipProtection.bandwidthResetDate`` (string)
  Stores the bandwidth quota reset date.

``browser.ipProtection.bandwidthWarningDismissedThreshold`` (integer)
  Bandwidth threshold percentage at which the user last dismissed the low-bandwidth warning.

``browser.ipProtection.egressLocation`` (string, default: ``""``)
  A country code of the currently selected VPN egress location,
  or a empty string for the default location.

``browser.ipProtection.egressLocationEnabled`` (boolean, default: ``false``)
  Controls whether the VPN egress location is displayed in the status panel.

``browser.ipProtection.siteExceptionsHintsEnabled`` (boolean, default: ``true``)
  Controls whether site exception confirmation hints are shown in the toolbar button.

``browser.ipProtection.onboardingMessageMask`` (integer, default: ``0``)
  Bitmask tracking which onboarding messages have already been shown.

``browser.ipProtection.openedPanelWithLocation`` (boolean, default: ``false``)
  Tracks whether the user has opened the panel with the egress location visible.

``browser.ipProtection.userEnableCount`` (integer, default: ``0``)
  Number of times the user has enabled the proxy (capped at 3, used for onboarding).

Diagnostics
~~~~~~~~~~~

``browser.ipProtection.log`` (boolean, default: ``false``)
  Enable/disable logging.

``browser.ipProtection.everOpenedPanel`` (boolean, default: ``false``)
  Tracks if the user has ever opened the VPN panel.

Android / GPI
~~~~~~~~~~~~~

``browser.ipProtection.gpi.authJwt`` (string)
  Cached GPI authentication JWT.

``browser.ipProtection.gpi.authJwtExpiresAt`` (string)
  Expiry timestamp of the cached GPI JWT.

``browser.ipProtection.gpi.authJwtRenewAfter`` (string)
  Timestamp after which the GPI JWT should be renewed.
