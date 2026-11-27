# Continuous Onboarding on the Messaging System

`ContinuousOnboardingFeature` (days 2, 3, 5 and 7) is re-expressed as Nimbus Messaging messages, and the **triggers do
the sequencing**. Changes outside the continuous onboarding code are kept to a minimum.

Installs that complete initial onboarding with `continuous-onboarding.messaging-system-enabled` set use the messages.
Everyone else, including users who are partly through continuous onboarding, stays on `ContinuousOnboardingFeature`,
see [Rollout](#rollout).

## Changes outside continuous onboarding

| Change | Where | Why |
|---|---|---|
| The `CO_*` triggers and the `CONTINUOUS_ONBOARDING` styles | `messaging-fenix.fml.yaml` | Where the shared triggers and styles are defined, as for the `VPN_*` triggers |
| `Message.action` is public instead of internal | a-c `Message` | So that `ContinuousOnboardingMessage` can tell what a message is for. The only other way to read the action, `getIntentForMessage`, also records a click |
| One surface, `continuous_onboarding` | `messaging-fenix.fml.yaml`, `FenixMessageSurfaceId` | Continuous onboarding is one flow that presents one message at a time, which is what a surface selects |
| Two actions: `OPEN_SETTINGS_IP_PROTECTION` and `CONTINUOUS_ONBOARDING_STAGE_COMPLETED` | `messaging-fenix.fml.yaml` | On one surface, a message's action says how it is presented. The first is an existing deep link. The second is not a deep link the app handles: it marks the messages that only end a stage |
| `MessagingAction.MessageDisplayed`, and `MessagingMiddleware.surfacesDisplayedOnPresentation` (only that surface) | `AppAction`, `MessagingMiddleware`, `Components` | For that surface, a message is recorded as displayed when it actually appears rather than when it is selected. Otherwise a message could be used up without being seen, or presented twice |
| A `://request_notification_permission` deep link | `HomeDeepLinkIntentProcessor` | Backs the `REQUEST_NOTIFICATION_PERMISSION` action on other surfaces. The notification card requests the permission directly |
| Messages are also restored for users on the Messaging System who turned off studies | `FenixApplication.restoreMessaging` | Continuous onboarding doesn't depend on studies today |

There are no changes to how the Messaging System records or selects messages.

## How the messages are presented

All the messages are on the `continuous_onboarding` surface, so at most one is selected at a time.
`ContinuousOnboardingMessage` wraps the selected message, in the way `VpnSettingsMessage` wraps the VPN settings
message, and says what it is from its action:

| Action | Presented as |
|---|---|
| `MAKE_DEFAULT_BROWSER` | The system's default browser role request |
| `REQUEST_NOTIFICATION_PERMISSION`, `TURN_ON_SYNC` | An onboarding card |
| `OPEN_SETTINGS_IP_PROTECTION` | The IP Protection onboarding prompt, whose content comes from the IP Protection feature |
| `CONTINUOUS_ONBOARDING_STAGE_COMPLETED` | Nothing: displaying the message records that a stage is over |

A message with any other action is dismissed without being shown.

## How the sequencing works

**History.** `ContinuousOnboardingMessagingBinding` records Nimbus events when it presents a message, when a card's
primary button is used, and when a message is dismissed: `onboarding.<message-id>.displayed`, `.clicked` and
`.dismissed`. Initial onboarding records `onboarding_completed`. The triggers
read these with plain JEXL (`eventSum`, `eventLastSeen`). The code doesn't know the stages.

**Each stage ends with an event:**
- **Days 2 and 3:** a `co-day-N-complete` message with the `CONTINUOUS_ONBOARDING_STAGE_COMPLETED` action, which
  shows nothing. It becomes eligible once the role request has been shown and the notification card has been answered, or
  they have nothing to ask for. It is evaluated right after a card's button is used or the role dialog returns, once
  the event that the button records has been written.
- **Day 5:** the sync card being dismissed, or `co-day-5-skipped` when the user is already signed in.
- **Day 7:** the VPN prompt, or `co-day-7-ineligible` / `co-day-7-satisfied`. While eligibility is unknown, none of
  them is eligible, so the stage waits, as today. `co-vpn` is only recorded once the prompt is actually handed to the
  IP Protection feature: if the user turns out to be ineligible, or the prompt stops being allowed, while it waits for
  the account state, the stage is left to the two stage-completed messages.

**What the triggers can't read from Nimbus** is passed in `triggerContext.continuous_onboarding`: `enabled`,
`default_browser_role_requestable`, `notification_permission_requestable`, `vpn_eligible`, `vpn_ineligible` and
`vpn_prompt_allowed`.

**Timing.** Each stage is due a number of days after the previous one ended, e.g. day 5 is due when
`'onboarding.co-day-3-complete.displayed'|eventLastSeen('Days') >= 2`. Only Nimbus's own event queries are used.

**The cards repeat** (`max-display-count: 10000`) until they are answered, as today: the notification card until one
of its buttons is used, the sync card until it is dismissed or the user signs in. The card surface never marks a
message as clicked, so "Start syncing" doesn't end the stage.

**Telemetry.** The stage-completed messages send the "already done" telemetry `ContinuousOnboardingFeature` sends for skipped
stages.

**The prompts that wait for continuous onboarding.** The review and set-as-default prompts ask
`isContinuousOnboardingInProgress`. For installs on the Messaging System, it evaluates the `CO_FINISHED` trigger, so
"finished" is defined in configuration.

**Messages that show nothing of their own**, the stage-completed messages, the role requests and `co-vpn`, have the text
`CONTINUOUS_ONBOARDING_MESSAGE_EMPTY_BODY`, because the Messaging System drops a message whose text is blank. It is
the counterpart of `VPN_MESSAGE_EMPTY_BODY`, and `ContinuousOnboardingMessage` refuses to make a card from it.

**When the surface is evaluated:** on every resume, once the messages have been restored, when IP Protection
eligibility changes, when the role request returns, and once a click or dismissal has been recorded. Messages are
restored at startup, and by the binding itself if there are none, for an install that started continuous onboarding
after startup with studies turned off.

**Displays are recorded asynchronously**, in both the message metadata and the event store, so a message can be
selected again before its triggers see the record. The binding guards against presenting twice: a stage-completed
message is displayed once per binding instance, and a shown VPN prompt keeps `vpn_prompt_allowed` true so that showing it doesn't
make `co-day-7-satisfied` eligible.

## Rollout

New installs only. The messages ship on every channel, and are inert until `startContinuousOnboarding` stores
`continuous-onboarding.messaging-system-enabled` for the install, once, when initial onboarding completes. The flag
defaults to true on the developer channel only, so rolling out is a default change or a Nimbus rollout. Switching an install back to `ContinuousOnboardingFeature` would restart its stages, because the
messaging version doesn't write that feature's `Settings` timestamps. Existing users finish on
`ContinuousOnboardingFeature`, which is removed once they have drained. Everything that can go then is marked "Remove
when ContinuousOnboardingFeature is retired".

## Known gaps

- **Days are UTC days, not local days.** Nimbus `Days` buckets roll over at UTC midnight (application-services
  `behavior.rs`), so "≥ 1 day" means "a later UTC date". `ContinuousOnboardingFeature` uses the device's local date,
  so away from UTC a stage can be due at the wrong time of day: at UTC+10 the day changes at 10:00 local, and a user
  who finishes onboarding at 09:00 is due day 2 an hour later. Pure JEXL can't count local days.
- **When the notification card comes back, the default browser role is not requested again.**
  `ContinuousOnboardingFeature` restarts the whole stage, role request included, e.g. after the device is rotated
  with the card open.
- **The configuration is brittle.** Its triggers name message ids in event names, a mistyped trigger silently disables
  a message, and nothing tests the configuration.
- **A card's image and secondary button are in code,** not in the message. `MessageData` has no fields for them, so
  `ContinuousOnboardingMessage.Card` adds them, chosen by the message's action.
- **`CONTINUOUS_ONBOARDING_STAGE_COMPLETED` is an action that does nothing.** A stage-completed message has no real
  action, but on one surface the action is the only thing that says what a message is.
- **Telemetry is keyed in code.** The telemetry shim recognises messages by id.
