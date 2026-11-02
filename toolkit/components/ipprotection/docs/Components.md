# Components

This page summarizes the main components and how to extend the system safely.

The implementation is split across three layers:

- `toolkit/components/ipprotection` — platform-independent core (state machine,
  proxy/network stack, core helpers).
- `browser/components/ipprotection` — desktop-specific UI layer (panel, toolbar
  button, alert/infobar managers, and onboarding helpers).
- `mobile/shared/modules/geckoview/` — Android GeckoView entry point.

## Component Diagram

A diagram of all the main components is the following:

```{mermaid}
:align: center
:caption: IP Protection architecture

flowchart LR

  %% browser/components/ipprotection
  subgraph Browser["browser/components/ipprotection (UI layer)"]
    IPProtection
    IPProtectionPanel

    subgraph BrowserHelpers["Browser Helpers"]
      UIHelper["UI Helper"]
      IPPOnboardingMessage["Onboarding Message"]
      IPPOptOutHelper["Opt-Out Helper"]
      IPPUsageHelper["Usage Helper"]
      IPPL10nHelper["L10n Coverage Helper"]
      IPProtectionAlertManager["Alert Manager"]
      IPProtectionInfobarManager["Infobar Manager"]
    end
  end

  %% toolkit/components/ipprotection
  subgraph Toolkit["toolkit/components/ipprotection (core)"]
    IPProtectionService
    IPProtectionActivator

    subgraph CoreHelpers["Core Helpers"]
      IPPStartupCache["Startup Cache Helper"]
      IPProtectionServerlist
      IPPProxyManager
      IPPAutoActivation["Auto-Activation Helper"]
      IPPNimbusHelper["Nimbus Eligibility Helper"]
      IPPSessionPrefManager["Session Pref Manager"]
      IPPSiteRuleManager
    end

    subgraph FxaAuth["FxA Authentication (fxa/)"]
      IPPAuthProvider
      IPPFxaActivateAuthProvider["Activate Auth Provider"]
      IPPSignInWatcher["Sign-in Observer"]
      GuardianClient
    end

    subgraph Proxy["Proxy stack"]
      IPPChannelFilter
      IPPNetworkErrorObserver
    end
  end

  %% Android layer
  subgraph Android["Android Glue"]
    GeckoViewIPProtection

    subgraph AndroidAuth["Android Authentication"]
      IPPAndroidAuthProvider["Android FxA Auth Provider"]
      IPPGpiAuthProvider["GPI Auth Provider"]
      IPPAndroidSignInWatcher["Android Sign-in Observer"]
    end
  end

  %% Activator wiring
  BrowserHelpers -- "addHelpers()" --> IPProtectionActivator
  IPProtectionActivator --> IPProtectionService
  IPProtectionActivator --> CoreHelpers
  IPProtectionActivator -- "setAuthProvider()" --> IPPFxaActivateAuthProvider
  GeckoViewIPProtection -- "setAuthProvider()" --> IPProtectionActivator

  %% Service wiring
  IPProtectionService --> CoreHelpers
  IPProtectionService -- "authProvider" --> IPPAuthProvider

  %% FxA auth wiring
  IPPFxaActivateAuthProvider -->|extends| IPPAuthProvider
  IPPFxaActivateAuthProvider --> IPPSignInWatcher
  IPPFxaActivateAuthProvider --> GuardianClient

  %% Android auth wiring
  IPPAndroidAuthProvider -->|extends| IPPFxaActivateAuthProvider
  IPPAndroidAuthProvider --> IPPAndroidSignInWatcher
  IPPGpiAuthProvider -->|extends| IPPAuthProvider

  %% UI wiring
  IPProtection --> IPProtectionPanel
  IPProtection --> IPProtectionService

  %% Proxy wiring
  IPPProxyManager --> IPPChannelFilter
  IPPProxyManager --> IPPNetworkErrorObserver
  IPPNetworkErrorObserver -- "error events (401)" --> IPPProxyManager
```

## Toolkit components (`toolkit/components/ipprotection`)

GuardianClient

: HTTP client for the Guardian backend. Manages communication with Guardian to
  retrieve account information, obtain proxy tokens, and run the FxA enrollment
  flow.

IPPChannelFilter

: Main network component. It processes network requests and decides which ones
  should go through the proxy, applying the mode it was given by
  `IPPProxyManager`.

IPPProxyManager

: Implements the proxy activation/deactivation and exposes the current status.
  It owns a connection's country and mode: both can be passed to `start()`, and
  changed on a live connection with `switch()` and `setMode()`.

IPProtectionService

: The main service. It is initialized during browser startup, initializes helpers
  and other components, and implements the state machine that drives the feature.

IPProtectionActivator

: Entry point that assembles the full helper list and initialises
  `IPProtectionService`. It owns the ordered list of core helpers and exposes
  `addHelpers()` so that the browser layer can register additional,
  browser-specific helpers before `init()` is called. It also exposes
  `setAuthProvider()` to set the active authentication provider.

IPPSiteRuleManager

: Classifies a principal as included/excluded/default for the proxy, and is the
  single source of truth for that decision. It consults multiple rule-providers like permissions, prefs, enterprise-policies and others.
  `getRule(principal, browserId)` takes an optional `browserId`; when it is given, tab-specific rules are applied too.

### Additional proxy/telemetry components

IPProtectionServerlist

: Provides the available proxy endpoints (server list) to the proxy manager.

IPPNetworkErrorObserver

: Observes network errors related to the proxy and notifies the proxy manager
  (for example, authentication or connectivity failures).

### Core helpers

The core helper list is defined in `IPProtectionActivator.sys.mjs` (toolkit).

IPPAutoActivation

: Brings the proxy up on its own on desktop: auto-start at startup,
  auto-restore of a session the user left the VPN on for, and a connection in
  inclusion mode while at least one site is marked as always using the VPN.
  `IPProtectionActivator` picks the activation helper per build: this one on
  desktop, `IPPAlwaysOn` on enterprise, and none on Android, where the embedder
  decides through `IPProtectionController`.

IPPStartupCache

: Exposes cached information to keep the state machine responsive during startup
  (last known state and entitlement JSON object).

IPPNimbusHelper

: Monitors the Nimbus feature (`NimbusFeatures.ipProtection`) and triggers a
  state recomputation on updates. Exposes a `hidesFeature()` veto that
  `#computeState()` consults to return `UNAVAILABLE` when the device is not
  eligible.

### FxA authentication (`toolkit/components/ipprotection/fxa`)

Authentication is abstracted behind `IPPAuthProvider`, which lives in the
toolkit root. `IPProtectionService` always interacts with the provider through
this interface, so all FxA dependencies are fully contained in the `fxa/`
sub-directory. On desktop, the concrete provider and its helpers are registered
from `IPProtectionHelpers.sys.mjs` in the browser layer. On Android, the
selection is done by `GeckoViewIPProtection` at initialisation time (see below).

IPPAuthProvider

: Base class that defines the authentication interface used by
  `IPProtectionService`. The default implementation is a no-op that keeps
  the service in an unauthenticated/inactive state.

IPPFxaActivateAuthProvider

: Concrete FxA implementation of `IPPAuthProvider`. It obtains short-lived
  OAuth tokens from `fxAccounts` via `getToken()`, passes them to
  `GuardianClient` for all Guardian HTTP calls, and enrolls the user by sending
  the FxA Bearer token directly to Guardian's activate endpoint, without using a
  hidden tab or window.

IPPSignInWatcher

: Observes user authentication state. It informs the state machine when the
  user signs in or out.

IPPSessionPrefManager

: Sets session-scoped preferences while the
  proxy is active and resets them when it deactivates, preserving any
  user-set values.

### Android authentication

On Android, two concrete `IPPAuthProvider` implementations are available and
selected at initialisation time by `GeckoViewIPProtection` based on the FxA
sign-in state stored in `toolkit.ipProtection.android.authProvider`.

IPPAndroidAuthProvider

: Android variant of the FxA provider. Extends `IPPFxaActivateAuthProvider`
  and uses `IPPAndroidSignInWatcher` to track the FxA sign-in state.

IPPGpiAuthProvider

: Google Play Integrity implementation of `IPPAuthProvider`. Used when the
  user is not signed into FxA. Obtains a short-lived GPI token from the Android
  layer via `EventDispatcher` and exchanges it for an Auth JWT with Guardian.
  The JWT is cached in `browser.ipProtection.gpi.authJwt` so that subsequent
  proxy starts require no network requests.

IPPAndroidSignInWatcher

: Monitors the FxA sign-in state on Android by listening to
  `IPP:AuthStateChanged` events from the Android layer. Exposes `isSignedIn`
  and dispatches `IPPSignInWatcher:StateChanged` when the state changes.

## Android GeckoView entry point

GeckoViewIPProtection

: GeckoView module that owns the IP Protection lifecycle on Android. On
  `GeckoView:IPProtection:Init` it reads the `toolkit.ipProtection.android.authProvider`
  preference to determine which auth provider to activate: `IPPAndroidAuthProvider`
  (with its FxA helpers) when the user is signed in, or `IPPGpiAuthProvider`
  otherwise. When the FxA sign-in state changes the Android layer clears the
  preference and calls `Uninit` followed by `Init` so that the correct
  provider is selected again.

## Browser components (`browser/components/ipprotection`)

IPProtection

: Manages the UI integration and interactions with the panel.

IPProtectionPanel

: Controls the feature's panel UI.

IPProtectionHelpers

: Registers browser-specific helpers with `IPProtectionActivator` via
  `addHelpers()`: `IPPL10nHelper`, `UIHelper`, `IPPOnboardingMessage`,
  `IPPOptOutHelper`, `IPPUsageHelper`, `IPProtectionAlertManager`, and
  `IPProtectionInfobarManager`. It also registers `IPPFxaActivateAuthProvider`
  (and its FxA helpers) via `setAuthProvider()` and `addHelpers()`.

UIHelper

: Shows and hides the UI based on the current state machine state.

IPPL10nHelper

: Gates the UI on localization coverage. A user who never saw the feature does
  not see it until `browser/ipProtection.ftl` is localized enough in the locale
  Firefox is effectively displaying (`mozILocaleService.isLocalizedEnough`).
  Exposes a `hidesFeature()` veto that `IPProtectionService.#computeState()`
  consults to return `UNAVAILABLE`. As soon as the state machine reaches a state
  where the UI is shown, it sets `browser.ipProtection.hasSeenFeature`, so a
  later version adding untranslated strings never takes the feature away from a
  user who already saw it. The gate is also bound to a single release cycle: the
  Firefox major version is recorded in `browser.ipProtection.l10nGateVersion`
  the first time the UI is hidden, and the gate gives up once the browser is
  updated to a different major version. Being a browser helper, the gate only
  exists on desktop. Set `browser.ipProtection.hasSeenFeature` to `true` to
  bypass the gate while testing in a locale below the threshold.

IPPOptOutHelper

: Handles the user opt-out flow and clears stored state accordingly.

IPPOnboardingMessage

: Handles the onboarding message flow for new users.

IPPUsageHelper

: Tracks bandwidth usage warning state and fires state-change events when
  usage crosses the 75% or 90% thresholds.

IPProtectionAlertManager

: Manages alert notifications related to IP Protection.

IPProtectionInfobarManager

: Manages infobar notifications displayed to the user.

## How to implement new components

Do not modify the state machine. New functionality should be added via helper
classes to keep the core simple and robust.

Recommended steps:

1. Decide whether the helper belongs in the **toolkit** layer (no UI, no chrome
   dependency) or the **browser** layer (UI or chrome integration required).

2. Create a helper class with the methods `init()`, `initOnStartupCompleted()`
   and `uninit()` as appropriate for lifecycle needs.

3. If your helper reacts to state changes, listen to the
   `IPProtectionService:StateChanged` event.

4. Register your helper:

   - **Toolkit helpers**: add it to the `coreHelpers` array in
     `toolkit/components/ipprotection/IPProtectionActivator.sys.mjs`.
   - **Browser helpers**: call `IPProtectionActivator.addHelpers([...])` in
     `browser/components/ipprotection/IPProtectionHelpers.sys.mjs`.

   Be mindful of ordering if your helper depends on others. For example,
   `IPPNimbusHelper` is registered last to avoid premature state updates
   triggered by Nimbus' immediate callback.

5. If your component needs to recompute the service state, call
   `IPProtectionService.updateState()` after updating the helper data it
   relies on; the recomputation is synchronous.
