# Activation Window Feature

## Overview

The Activation Window feature allows the browser to experiment with the first hours of a new profile's lifetime by temporarily setting different defaults during a configurable time period (typically 48 hours). This enables:

- Hiding or showing specific content sections (top sites, top stories) during the activation window
- Displaying messaging when entering or exiting the activation window
- Reverting to normal defaults after the activation window expires
- Preserving user preference changes made during the activation window

The feature is controlled via Nimbus experiments using the `newtabTrainhop` feature with `type: "activationWindowBehavior"`.

## How It Works

### High-level Architecture

1. **Profile Creation Time Tracking** (`AboutNewTab.sys.mjs`)
   - The browser computes the profile creation instant on startup using `ProfileAge.sys.mjs`
   - The `createdInstant` is cached on the ActivityStream instance for the session

2. **Activation Window Evaluation** (`PrefsFeed.sys.mjs`)
   - `checkForActivationWindow()` runs on:
     - PrefsFeed initialization (startup)
     - Each NEW_TAB_STATE_REQUEST action (when opening a new tab)
   - It compares the current time against the profile age to determine if we're within the activation window
   - Enters or exits activation window state as needed

3. **Default Pref Manipulation**
   - When entering the activation window: Sets default branch prefs for top sites/stories to experiment values
   - When exiting the activation window: Restores default branch prefs to original values
   - User pref values _always_ override defaults, even after enabling and then re-disabling.

4. **User Preference Tracking**
   - Tracks user preference changes during the activation window
   - On exit, ensures that any user changes are persisted

5. **State Broadcasting**
   - Pref changes are broadcast to all content processes
   - StartupCacheInit queues changes for the cached about:home page if it exists

6. **Messaging Integration**
   - PrefsFeed sets message ID prefs on enter/exit: `activationWindow.enterMessageID` and `activationWindow.exitMessageID`
   - ASRouter messages can target these prefs using JEXL expressions
   - The `ActivationWindowMessage` component renders messages with bespoke UI (card layout with image, heading, message, and buttons)

## Configuration

### Nimbus Configuration Schema

The activation window is configured via Nimbus using the `newtabTrainhop` feature:

```javascript
{
  featureId: "newtabTrainhop",
  value: {
    type: "activationWindowBehavior",
    payload: {
      enabled: true,
      maxProfileAgeInHours: 48,
      disableTopSites: true,
      disableTopStories: true,
      variant: "a",
      enterActivationWindowMessageID: "ACTIVATION_WINDOW_WELCOME_V1",
      exitActivationWindowMessageID: "ACTIVATION_WINDOW_EXIT_V1"
    }
  }
}
```

#### Configuration Fields

- **`enabled`** (boolean, default: false): Whether the activation window feature is active
- **`maxProfileAgeInHours`** (number, default: 48): Duration of the activation window in hours
- **`disableTopSites`** (boolean, default: false): Hide top sites section during activation window
- **`disableTopStories`** (boolean, default: false): Hide top stories section during activation window
- **`variant`** (string, default: ""): Experiment variant identifier
- **`enterActivationWindowMessageID`** (string, default: ""): Message ID to show when entering the window
- **`exitActivationWindowMessageID`** (string, default: ""): Message ID to show when exiting the window

### Message Structure

Messages for the activation window use the `ActivationWindowMessage` component and follow this schema:

```javascript
{
  id: "MESSAGE_ID",
  template: "newtab_message",
  content: {
    messageType: "ActivationWindowMessage",

    // Heading: plain string or Fluent ID
    heading: "Welcome to Your New Tab!",
    // OR
    heading: { string_id: "activation-window-welcome-heading-fluent-id" },

    // Message: plain string or Fluent ID
    message: "We've personalized your experience...",
    // OR
    message: { string_id: "activation-window-welcome-message-fluent-id" },

    // Image (optional, defaults to kit-in-circle.svg if not provided)
    imageSrc: "chrome://newtab/content/data/content/assets/kit.png",

    primaryButton: {
      // Plain text label (for tests)
      label: "Learn More",
      // OR Fluent ID label (for production)
      label: { string_id: "activation-window-primary-button-fluent-id" },

      action: {
        type: "SHOW_PERSONALIZE"
      }
    },

    secondaryButton: {
      label: "Dismiss",
      // OR
      label: { string_id: "activation-window-secondary-button-fluent-id" },

      action: { dismiss: true }
    }
  },
  trigger: { id: "newtabMessageCheck" },
  targeting: `'browser.newtabpage.activity-stream.activationWindow.enterMessageID' | preferenceValue == 'MESSAGE_ID'`,
  groups: []
}
```

#### Message Content Fields

- **`messageType`** (string, required): Must be `"ActivationWindowMessage"`
- **`heading`** (string or object, optional): Heading text
  - Plain string: `"Welcome to Firefox"`
  - Fluent ID: `{ string_id: "activation-window-heading-fluent-id" }`
- **`message`** (string or object, optional): Message text
  - Plain string: `"We've personalized your experience"`
  - Fluent ID: `{ string_id: "activation-window-message-fluent-id" }`
- **`imageSrc`** (string, optional): Chrome URL to image displayed in the message. If not provided, defaults to `"chrome://newtab/content/data/content/assets/kit-in-circle.svg"`
- **`primaryButton`** (object, optional): Configuration for primary button.
- **`secondaryButton`** (object, optional): Configuration for secondary button

### Image variants

`imageSrc` may be accompanied by optional variants that let the image adapt to color scheme and viewport width. Supplying any of the narrow/responsive variants opts the message into a "flush" image treatment that bleeds to the card edges, with three layout tiers:

- **Below 866px** (vertical layout): a full-width banner, using `imageSrcResponsive` (and `imageSrcDarkResponsive` in dark mode).
- **866px–1072px** (horizontal layout): a full-height flush column, using `imageSrcNarrow` (and `imageSrcDarkNarrow` in dark mode). The 1072px breakpoint matches the newtab discovery-stream section switch.
- **At/above 1072px** (horizontal layout): a full-height flush column, using `imageSrc` (and `imageSrcDark` in dark mode).

Any variant that is not provided falls back to `imageSrc`. Dark-mode variants are selected via `prefers-color-scheme`. Each tier's art is chosen to match that tier's column proportions to minimize cropping.

#### Button Configuration

Each button object has:

- **`label`** (string or object, required):
  - Plain string for test messages: `"Click Me"`
  - Fluent object for production: `{ string_id: "button-label-id-fluent-id" }`
- **`action`** (object, required): ASRouter action specification
  - `{ dismiss: true }` - Dismiss and block the message
  - `{ type: "SHOW_PERSONALIZE" }` - Open personalization panel
  - More actions may be added in the future.

## Testing Locally

### Using PanelTestProvider

Test messages are available in `browser/components/asrouter/modules/PanelTestProvider.sys.mjs`:

- `TEST_ACTIVATION_WINDOW_ENTER_MESSAGE`
- `TEST_ACTIVATION_WINDOW_EXIT_MESSAGE`
- `TEST_ACTIVATION_WINDOW_FULL_BLEED_IMAGE`

To test these messages:

1. Open `about:newtab#asrouter` in Firefox
2. Find either the enter or exit message in the message list
3. Modify any of the parameters in the message as you'd like
4. Click "Show" or "Modify" to display the message
5. Open a new tab
