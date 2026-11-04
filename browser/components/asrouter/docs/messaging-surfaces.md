# Messaging Surface Compatibility

Every message is rendered by a single messaging surface, chosen through the message's `template` field. The surface determines not only how a message looks, but which behaviors are available to it. A message that works correctly on one surface can silently do nothing, or throw, on another.

This page documents where those differences are: which Special Message Actions, window functions, triggers, and context properties work on which surfaces.

## Surfaces

The most commonly used surfaces are listed below. "Multistage content" means the surface renders one or more `aboutwelcome`-style screens and so exposes [window functions](#window-functions) to its content. The remaining surfaces render browser chrome directly and only use [Special Message Actions](#special-message-actions).

| Surface | `template` | Multistage content |
|---------|------------|--------------------|
| [about:welcome](./about-welcome.md) | (page, not a message template) | Yes |
| [Spotlight](./spotlight.md) | `spotlight` | Yes |
| [Feature Callout](./feature-callout.md) | `feature_callout` | Yes |
| [Infobar](./infobars.md) | `infobar` | No |
| Toast notification | `toast_notification` | No |
| [Moments page](./moments-page.md) | `update_action` | No |
| Toolbar badge | `toolbar_badge` | No |
| [Private browsing newtab](./private-browsing.md) | `pb_newtab` | No |
| [Menu message](./menu-message.md) | `menu_message` | No |
| [Sidebar chatbot promo](./sidebar-chatbot-promo.md) | `sidebar_chatbot_promo` | No |
| Bookmarks bar button | `bookmarks_bar_button` | No |

## Special Message Actions

Buttons, links, and other calls to action can use one or more of a set of predefined Special Message Actions (SMAs), defined on the `action` field. See [User Actions](/toolkit/components/messaging-system/docs/SpecialMessageActionSchemas/index.md) for the full list of valid actions.

Every SMA is dispatched through a single shared handler, [`SpecialMessageActions.handleAction`](https://searchfox.org/mozilla-central/source/toolkit/components/messaging-system/lib/SpecialMessageActions.sys.mjs), which every surface that supports actions calls into. Most surfaces do not restrict which SMAs they accept, so in general most action can be used from most surface. Some surfaces, however, enforce an allowlist and reject actions outside it, and more may do so in the future.

There are a few caveats to keep in mind:

- **Allowlisted surfaces.** Some templates only permit a fixed set of actions. For example, the `action_only` template runs an action with no UI, so it is restricted to a small allowlist checked by [`_isAllowedActionOnlyMessageAction`](https://searchfox.org/mozilla-central/source/browser/components/asrouter/modules/ASRouter.sys.mjs). A `MULTI_ACTION` is accepted only as the top level action and only when every nested action is itself allowlisted. See `ALLOWED_ACTION_MESSAGE_ACTIONS` in `ASRouter.sys.mjs` for the current baseline, which `MessagingSystemAllowlists.sys.mjs` can extend off-train.
- **Browser context.** The handler acts on the browser the message was shown in. Actions that operate on the current tab or window (for example `OPEN_URL`, `FOCUS_URLBAR`, `BOOKMARK_CURRENT_TAB`, `RELOAD_BROWSER`, and `PIN_CURRENT_TAB`) rely on that browser being a real browser window, and are not meaningful on a surface that is not attached to one.
- **Platform restrictions.** Some actions are OS-specific regardless of surface (for example `PIN_FIREFOX_TO_START_MENU` and `CREATE_TASKBAR_TAB` are Windows only). These are noted in the [User Actions](/toolkit/components/messaging-system/docs/SpecialMessageActionSchemas/index.md) docs.
- **`needsAwait` and `navigate: "actionResult"`.** Waiting for an asynchronous action's result before advancing or dismissing a screen is a multistage-content feature. It is honored by the [window-function](#window-functions) surfaces (about:welcome, Spotlight, Feature Callout, and the New Tab multistage surface) and has no effect on the chrome-only surfaces.

## Window Functions

Multistage surfaces render screens whose content calls a set of privileged `AW*` window functions (so named because they are exported onto the content `window`). Unlike SMAs, these are not provided by a single shared module. Each multistage surface defines its own subset and exports it onto the content window:

- **about:welcome** exposes the full set from [`AboutWelcomeChild.sys.mjs`](https://searchfox.org/mozilla-central/source/browser/components/aboutwelcome/actors/AboutWelcomeChild.sys.mjs).
- **Spotlight** defines its own subset in [`spotlight.js`](https://searchfox.org/mozilla-central/source/browser/base/content/spotlight.js).
- **Feature Callout** defines its own subset in [`FeatureCallout.sys.mjs`](https://searchfox.org/mozilla-central/source/browser/components/asrouter/modules/FeatureCallout.sys.mjs).

Calling a window function that a surface does not implement throws an unhandled exception. For example, a screen that calls `AWSelectTheme` works on about:welcome and Spotlight, but breaks on a Feature Callout. Before reusing a screen across surfaces, check the table below.

| Window function | about:welcome | Spotlight | Feature Callout |
|-----------------|:-------------:|:---------:|:---------------:|
| `AWGetFeatureConfig` | Yes | Yes | Yes |
| `AWGetSelectedTheme` | Yes | Yes | Yes |
| `AWGetInstalledAddons` | Yes | Yes | Yes |
| `AWEvaluateScreenTargeting` | Yes | Yes | Yes |
| `AWEvaluateAttributeTargeting` | Yes | Yes | Yes |
| `AWSendEventTelemetry` | Yes | Yes | Yes |
| `AWSendToDeviceEmailsSupported` | Yes | Yes | Yes |
| `AWSendToParent` | Yes | Yes | Yes |
| `AWFinish` | Yes | Yes | Yes |
| `AWSelectTheme` | Yes | Yes | No |
| `AWAddScreenImpression` | Yes | Yes | No |
| `AWWaitForMigrationClose` | Yes | Yes | No |
| `AWWaitForNimbus` | Yes | Yes | No |
| `AWEnsureAddonInstalled` | Yes | No | Yes |
| `AWEnsureLangPackInstalled` | Yes | No | No |
| `AWNegotiateLangPackForLanguageMismatch` | Yes | No | No |
| `AWSetRequestedLocales` | Yes | No | No |
| `AWGetFxAMetricsFlowURI` | Yes | No | No |
| `AWGetUnhandledCampaignAction` | Yes | No | No |
| `AWSendImpressionAction` | Yes | No | No |
| `AWNewScreen` | Yes | No | No |
| `AWFindBackupsInWellKnownLocations` | Yes | No | No |
| `AWPredictRemoteType` | No | Yes | No |
| `AWAdvanceScreens` | No | No | Yes |

This table reflects the functions each surface exports today, and should be updated when a surface gains or loses a window function.

## Triggers and Context Properties

Triggers are used to determine when a message should be shown. See [Trigger Listeners](/toolkit/components/messaging-system/docs/TriggerActionSchemas/index.md) for the full list.

Triggers are a routing-level concept handled by ASRouter, not a property of any one surface. When a trigger fires, ASRouter evaluates the targeting of every eligible message and shows the highest-priority match, whatever its `template`. In general, any trigger can therefore show a message on any ASRouter-routed surface.

The exceptions are the Feature Callout triggers, which are only useful for the `feature_callout` template because they exist to anchor a callout in a particular place:

- `featureCalloutCheck` (Firefox View)
- `pdfJsFeatureCalloutCheck` (PDF.js pages)
- `newtabFeatureCalloutCheck` (about:newtab)

Each trigger contributes its own context properties to the targeting context, such as `visitsCount` from `openURL`, `tabsClosedCount` from `nthTabClosed`, or `willShowDefaultPrompt` from `defaultBrowserCheck`. Because these come from the trigger rather than the surface, a context property is only available when its trigger fires, regardless of which surface renders the message. The context properties for each trigger are documented alongside it in [Trigger Listeners](/toolkit/components/messaging-system/docs/TriggerActionSchemas/index.md).

Surface-independent targeting attributes, such as browser state and user profile, are documented in [Targeting attributes](./targeting-attributes.md). For guidance on writing targeting expressions, see [Guide to targeting with JEXL](./targeting-guide.md).
