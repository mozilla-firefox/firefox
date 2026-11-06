# ASRouter New Tab Multistage Message

An inline multistage message surface for displaying multi-screen flows on the Firefox New Tab page.

Like `asrouter-newtab-message`, this component is packaged to take advantage of newtab train-hopping. The newtab extension knows to package this component at build-time both to the built-in instance of newtab and the train-hoppable XPI. **It is the responsibility of the owners of this component to maintain newtab train-hop compatibility. At this time, the owners of this component are the OMC team.**

## Overview

The ASRouter New Tab Multistage Message embeds the full `MultiStageAboutWelcome` React component inline on about:newtab, inside a shadow root managed by `ExternalComponentWrapper`. This allows the same multi-screen onboarding flows used in about:welcome and Spotlight to be surfaced directly on the New Tab page without a modal or overlay.

A dismiss button is rendered above the multistage content. Clicking it blocks the message permanently.

## Integration

The component is loaded dynamically through the newtab page's `ExternalComponentWrapper` using the `react-bundle` mount strategy. It is wrapped by the `MessageWrapper` component, which provides impression tracking and message lifecycle management.

The mount function exposed by the bundle is `window.mountMultistageMessage`. It receives props injected by `ExternalComponentWrapper` (which in turn receives them from `MessageWrapper` via `React.cloneElement`), sets up the AW handler bridge, renders the React tree, and returns a cleanup function.

### AW Handler Bridge

`MultiStageAboutWelcome` communicates with its host environment through a set of `window.AW*` globals. `mountMultistageMessage` installs a bridge that maps these to the newtab message lifecycle:

| Handler                          | Behaviour in this context                                                                                             |
|----------------------------------|-----------------------------------------------------------------------------------------------------------------------|
| `AWEvaluateScreenTargeting`      | Forwarded to ASRouter via `window.ASRouterMessage`                                                                    |
| `AWGetFeatureConfig`             | Returns the message `content` object                                                                                  |
| `AWFinish`                       | Sends a DISMISS `newtab_message` ping with completion data, then calls `handleClose` to close the message             |
| `AWSendToParent`                 | Forwarded to ASRouter as a `USER_ACTION`                                                                              |
| `AWAddScreenImpression`          | Forwarded to ASRouter via `window.ASRouterMessage` to record a per-screen impression for frequency capping            |
| `AWSendEventTelemetry`           | Forwards events as `newtab_message` pings; IMPRESSION events wait until the tab is visible                            |
| `AWGetSelectedTheme`             | Returns a resolved promise (themes not applicable in this context)                                                    |
| `AWGetInstalledAddons`           | Returns a resolved promise (not applicable in this context)                                                           |

All handlers are deleted from `window` when the component is unmounted.

### Message Configuration

Messages use the `newtab_message` template with `messageType: "ASRouterMultistageMessage"`:

```javascript
{
  id: "MY_NEWTAB_MULTISTAGE_MESSAGE",
  template: "newtab_message",
  content: {
    messageType: "ASRouterMultistageMessage",
    id: "MY_NEWTAB_MULTISTAGE_MESSAGE",
    transitions: false,
    backdrop: "transparent",
    wrapper_content_style: { height: "400px" },
    screens: [
      {
        id: "SCREEN_1",
        content: {
          position: "center",
          title: { raw: "Title" },
          primary_button: {
            label: { raw: "Primary" },
            action: { navigate: true },
          },
        },
      },
    ],
  },
  frequency: { lifetime: 3 },
  trigger: { id: "newtabMessageCheck" },
}
```

The `screens` array follows the standard aboutwelcome multistage screen schema. See the [about:welcome documentation](./about-welcome.md) for the full list of supported screen properties.

The `wrapper_content_style` allows for customizing the height of the newtab multistage message. If it's not specified, the default height of the message is set to 500px.

## Handler Functions

The following functions are injected by `MessageWrapper` and provide message lifecycle management:

### `handleClose` (Function)
Closes the message and removes it from the DOM without recording any telemetry. The message may appear again in future sessions.

### `handleDismiss` (Function)
Dismisses the message, records a DISMISS telemetry event, and removes it from the DOM. Internally calls `handleClose` after recording telemetry.

### `handleBlock` (Function)
Permanently blocks the message by adding its ID to ASRouter's block list. Blocked messages are never shown again across browser restarts. The dismiss button calls both `handleBlock` and `handleDismiss`.

### `handleClick` (Function)
Records a CLICK telemetry event for user interaction with the message.

**Parameters:**
- `elementId` (string, optional): An identifier for the clicked element, used for telemetry tracking.

## Telemetry

- **Impression**: fired once by `MessageWrapper`'s intersection observer when the component enters the viewport.
- **Screen impression**: recorded via `AWAddScreenImpression` when each screen becomes active, forwarded to `ASRouter.addScreenImpression` for per-screen frequency capping.
- **Screen impression telemetry**: an IMPRESSION event fired via `AWSendEventTelemetry` when each screen first renders (deferred until the tab is visible), with a per-screen `message_id` (`<message id>_<index>_<screen id>_<screen initials>`) and screen details in `event_context`, matching about:welcome.
- **Click**: fired via `AWSendEventTelemetry` for user interactions (e.g. button clicks). The multistage ping is forwarded unchanged as a `newtab_message` ping, so it keeps its event name (e.g. `CLICK_BUTTON`, `AUTO_DISMISS`), the screen-specific `message_id` (`<message id>_<screen index>_<screen id>`) and the clicked button as `event_context.source`.
- **Dismiss/Block**: fired by `handleDismiss` / `handleBlock` when the user clicks the dismiss button.
- **Finish**: when the flow finishes via `AWFinish` (e.g. a card stack auto-dismissing on its last card), a single DISMISS event is forwarded as a `newtab_message` ping with the message's `message_id`. Its `event_context` adds:
  - `completion_status`: each shown screen that has a primary button, mapped to `completed` (primary button clicked), `skipped` (secondary button clicked) or `expired` (neither).
  - `completion_count`: the number of `completed` screens.
  - `reason`: `engaged` if `completion_count` is above zero, `not_engaged` otherwise. Also recorded as `messaging_system.event_reason`.

## Strings

Since this component supports train-hopping, its strings must live within `newtab.ftl`, be statically provided through the `messageData` object (e.g. `{ raw: "..." }` values in screen content), or be included in the set in `l10nURLs` for the `ASROUTER_MULTISTAGE_MESSAGE` external component definition. Usage of new strings in `newtab.ftl` must be coordinated with the New Tab team to ensure translations are available in the target regions.

The dismiss button string key is `newtab-activation-window-message-dismiss-button`, defined in `newtab.ftl`.

## Testing

See `browser/components/asrouter/tests/browser/browser_asrouter_newtab_multistage_messages.js` for browser tests that exercise the component. This test file has the `newtab` tag and is included in train-hop compatibility CI jobs.

## See Also

- [ASRouter New Tab Message](./asrouter-newtab-message.md)
- [about:welcome Documentation](./about-welcome.md)
- [New Tab External Components](../../../extensions/newtab/docs/v2-system-addon/external_components_guide.md)
- [New Tab Train-hop Compatibility](../../../extensions/newtab/docs/v2-system-addon/train_hopping.md)
- [ASRouter Documentation](index.md)
