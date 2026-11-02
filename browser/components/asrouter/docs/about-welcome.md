# About Welcome

## Overview
The About Welcome page is a full-screen, multi-step onboarding experience that appears for most new users after successfully installing Firefox. Exceptions include users on Enterprise builds or custom configurations where this experience is disabled on first startup.

The onboarding experience on `about:welcome` displays screens in the following precedence order:
- Has AMO attribution
  - [Return to AMO](/browser/components/asrouter/docs/first-run.md#return-to-amo-rtamo) custom onboarding screens
- Experiments
- `MR_ABOUT_WELCOME_DEFAULT` [screens](https://searchfox.org/mozilla-central/rev/3b707c8fd7e978eebf24279ee51ccf07895cfbcb/browser/components/newtab/aboutwelcome/lib/AboutWelcomeDefaults.jsm#523) selected by dynamic rules inside the [prepareContentForReact](https://searchfox.org/mozilla-central/rev/3b707c8fd7e978eebf24279ee51ccf07895cfbcb/browser/components/newtab/aboutwelcome/lib/AboutWelcomeDefaults.jsm#577) method. Each screen's visibility can be dynamically configured through the screen-level targeting ([see example](https://searchfox.org/mozilla-central/rev/3b707c8fd7e978eebf24279ee51ccf07895cfbcb/browser/components/newtab/aboutwelcome/lib/AboutWelcomeDefaults.jsm#90)).

The {doc}`Spotlight messaging surface </browser/components/asrouter/docs/spotlight>` can be configured to show `about:welcome` screens in a modal format by using [exposed window.AW* interfaces](https://searchfox.org/firefox-main/source/browser/base/content/spotlight.js#28-47). These interfaces expose top level functions expected by the About Welcome bundle and allow for custom handling as needed. This [Unified Onboarding](https://docs.google.com/document/d/1o8W-gEkgw2GC7KFSfQRkKfWzNJg1-6fpkVPrYmmot4Y/edit) approach enables reusing `about:welcome` as a rendering engine for multiple messaging surfaces such as Spotlight and {doc}`Feature Callout </browser/components/asrouter/docs/feature-callout>`.

## Testing about:welcome Screens

### Testing with the Screens Pref
1. Go to `about:config`, search for the `browser.aboutwelcome.screens` pref and set it to the array of screen values to be used in a JSON recipe.
2. Go to about:welcome and you should see the newly configured screen(s)

**Note:** If you are enrolled in an About Welcome nimbus experiment, the screens you set in the pref will not appear. You can see your current enrollments at `about:studies`.

### Testing with Experiments
You can test custom `about:welcome` UI by creating an experiment. [Messaging Journey](https://experimenter.info/messaging/desktop-messaging-journey) captures creating and testing experiments via Nimbus. Note that test experiments should be created in the Nimbus [staging environment](https://stage.experimenter.nonprod.webservices.mozgcp.net/nimbus/), not in production.

### Example JSON for Screens Property

You can see JSON for all current default screens in about:welcome [here](https://searchfox.org/mozilla-central/rev/3b707c8fd7e978eebf24279ee51ccf07895cfbcb/browser/components/newtab/aboutwelcome/lib/AboutWelcomeDefaults.jsm#38-454).

### Example about:welcome Screen

#### Browser View
![About Welcome 1](./aboutwelcome-1.png)

#### Responsive View
![About Welcome Responsive 1](./aboutwelcome-res-1.png)

### Content Tiles
Screens in about:welcome support a variety of content tiles. Typically, only one tile is displayed per screen. When necessary, multiple tiles can be included on one screen. Each tile can be configured with a header that describes the tile and reveals it when clicked (see example below).

![Content Tiles](./content-tiles.png)

#### Addons Picker
Displays a curated list of recommended add-ons that users can choose to install directly from the tile.

![About Welcome Addons Picker](./aboutwelcome-addons-picker.png)

#### Migration Wizard
Guides users through the process of migrating their data, such as bookmarks, passwords, and browsing history, from another browser to Firefox.

![About Welcome Migration Wizard](./aboutwelcome-migration-wizard.png)

#### Mobile Downloads
Encourages users to download Firefox on their mobile devices by providing QR codes and/or a link to a page where users can email themselves a download link.

![About Welcome Mobile Downloads](./aboutwelcome-mobile-downloads.png)

#### Multiselect Checklist
Offers a checklist of actions a user can opt-in to.

![About Welcome Multiselect](./aboutwelcome-multiselect.png)

Individual checkboxes can opt in to conditional rendering with a `targeting`
property containing a JEXL expression, the same way screen-level `targeting`
works. A checkbox is rendered only when its expression evaluates to a truthy
value; checkboxes without a `targeting` property are always shown.
```js
tiles: {
  type: "multiselect",
  data: [
    {
      id: "checkbox-1",
      targeting: "doesAppNeedPin",
      label: "Pin Firefox to Taskbar",
      action: { type: "PIN_FIREFOX_TO_TASKBAR" },
    },
    {
      id: "checkbox-2",
      label: "Import from Previous Browser",
      action: { type: "SHOW_MIGRATION_WIZARD" },
    },
  ],
}
```

#### Single Select Picker
Provides users with set of mutually exclusive options.

![About Welcome Multiselect](./aboutwelcome-single-select-picker.png)

Individual items can opt in to conditional rendering with a `targeting`
property containing a JEXL expression, the same way screen-level `targeting`
works. An item is rendered only when its expression evaluates to a truthy
value; items without a `targeting` property are always shown.
```js
tiles: {
  type: "single-select",
  data: [
    {
      id: "split-view",
      targeting: "firefoxVersion >= 150",
      type: "carousel-card",
      label: { raw: "Split View" },
      body: {
        raw: "Two tabs side by side, right where you need them. Finally.",
      },
    },
    {
      id: "tab-groups",
      type: "carousel-card",
      label: { raw: "Tab Groups" },
      body: { raw: "Keep related tabs together in named groups." },
    },
  ],
}
```

##### Carousel Card

The `carousel-card` type displays a single select tile inside a card with a full bleed image, label, and an optional button. These cards, along with navigation pills, are consumed by the `carousel` layout and supported with horizontal scrolling navigation.

The row renders at one of two sizes. The large size applies when the Spotlight
is at least 800px wide and the window is over 650px tall. It enlarges the
selected card, grows its label and button to match, and leaves the cards on
either side peeking in from the edges of the row. The small size applies at
every other window size. It gives each card the same dimensions, marks the
selected one with a thicker border, and fits one line of label text under the
image.

Set `screen_style.width` on the screen to request the large size. A window
narrower than 800px holds the Spotlight at its default width.

Including `body` is optional. The card does not display the text, but it stays
in the page so a screen reader reads it out after the label.

![Carousel Card](./carousel-card.png)

##### Carousel Navigation Pills

A `carousel-card` item may define a `pill`, which renders it into a
`moz-segmented-control` row above the cards. `pill.icon` must be a `chrome://`
SVG authored with `context-fill` so it can recolour on the selected pill and in
High Contrast Mode. Selecting a pill scrolls its card
to the centre and marks it selected. The pill row requires `tiles.layout` to be
`"carousel"`, and only renders when at least two items define a `pill`. It
respects per-item `targeting` because the pills and the cards come from the same
`data` array. Set `tiles.pill_nav_label` to override the group's accessible name.

```js
tiles: {
  type: "single-select",
  layout: "carousel",
  pill_nav_label: { raw: "Feature highlights" },
  data: [
    {
      id: "tab-groups",
      type: "carousel-card",
      pill: {
        label: { raw: "Tab Groups" },
        icon: "chrome://browser/skin/tabs.svg",
      },
      label: { raw: "Tab Groups" },
      body: { raw: "Keep related tabs together in named groups." },
    },
  ],
}
```

#### Theme Picker
Allows users to select from a set of themes to personalize the browser’s appearance.

![About Welcome Themes](./aboutwelcome-themes.png)

## Impression Actions

A screen can fire a special message action automatically when it is first shown. This is configured by adding an `impression_action` to the screen's `content`:

```javascript
{
  id: "AW_PIN_SCREEN",
  content: {
    title: { string_id: "..." },
    impression_action: {
      type: "PIN_FIREFOX_TO_TASKBAR",
      data: { privatePin: false },
      once: true,
    },
  },
}
```

There is an [allowlist of action types](https://searchfox.org/firefox-main/search?q=ALLOWED_IMPRESSION_ACTIONS&path=browser%2Fcomponents%2Fasrouter%2Fmodules%2FASRouterScreenUtils.sys.mjs&case=false&regexp=false) that are allowed to fire on impression. The allowlist is enforced in `ASRouterScreenUtils.handleImpressionAction`. A `MULTI_ACTION` may fire on impression only when every nested action is itself allowlisted. If any nested action is not allowlisted, the whole action is rejected. When `once` is `true`, the action fires only on the screen's first view, determined from recorded screen impressions.

For example, to pin to both the taskbar and the Start Menu on impression:

```javascript
impression_action: {
  type: "MULTI_ACTION",
  once: true,
  data: {
    actions: [
      { type: "PIN_FIREFOX_TO_TASKBAR" },
      { type: "PIN_FIREFOX_TO_START_MENU" },
    ],
  },
}
```

Impression actions are currently only supported on `about:welcome`. The trigger is scoped by exporting `AWSendImpressionAction` from the about:welcome actor, so on other surfaces it is undefined and the action silently does nothing. Spotlights and feature callouts route through the same `AboutWelcomeParent` message handler and could take advantage of this feature in the future by exporting that `AWSendImpressionAction`, but currently do not.
