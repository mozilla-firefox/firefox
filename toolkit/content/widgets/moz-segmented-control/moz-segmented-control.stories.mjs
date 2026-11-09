/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined } from "../vendor/lit.all.mjs";
import "./moz-segmented-control.mjs";

let options = ["option1", "option2", "option3"];
let icons = [
  "chrome://global/skin/icons/highlights.svg",
  "chrome://global/skin/icons/delete.svg",
  "chrome://global/skin/icons/defaultFavicon.svg",
];
let itemL10nIds = {
  default: n => `control-option${n}`,
  long: n => `control-long-option${n}`,
  icon: n => `control-option${n}-icon-only`,
};

export default {
  title: "UI Widgets/Segmented Control",
  component: "moz-segmented-control",
  argTypes: {
    disabledItems: {
      options,
      control: { type: "check" },
    },
    size: {
      options: ["default", "small"],
      control: { type: "radio" },
    },
    labels: {
      options: Object.keys(itemL10nIds),
      control: { type: "radio" },
    },
  },
  parameters: {
    actions: {
      handles: ["change", "input"],
    },
    status: "stable",
    fluent: `
control-option1 = Option 1
control-option2 = Option 2
control-option3 = Option 3
control-option1-icon-only =
  .aria-label = Highlights view
control-option2-icon-only =
  .aria-label = Delete view
control-option3-icon-only =
  .aria-label = Favorites view
control-long-option1 = Option with long label 1
control-long-option2 = Option with long label 2
control-long-option3 = Option with long label 3
`,
  },
};

const Template = ({
  className = "",
  value = options[0],
  name = "segmented-control",
  showIcons = false,
  disabled = false,
  disabledItems = [],
  size = "default",
  withDeck = false,
  iconsOnly = false,
  labels = "default",
}) => html`
  ${withDeck
    ? html`
        <style>
          .deck-content {
            margin-top: 16px;
            padding: 16px;
            background: light-dark(#f0f0f0, #2a2a2e);
            border-radius: 8px;
          }
        </style>
      `
    : ""}
  <style>
    .wide {
      width: 100%;
    }
    .narrow {
      width: 500px;
    }
  </style>
  <moz-segmented-control
    class=${className}
    name=${name}
    value=${value}
    ?disabled=${disabled}
    deck=${ifDefined(withDeck ? "example-deck" : undefined)}
  >
    ${options.map(
      (option, i) => html`
        <moz-segmented-control-item
          data-l10n-id=${itemL10nIds[labels](i + 1)}
          data-l10n-attrs=${iconsOnly ? "aria-label" : "label"}
          value=${option}
          size=${size}
          ?disabled=${disabledItems.includes(option)}
          iconSrc=${ifDefined(showIcons || iconsOnly ? icons[i] : undefined)}
        ></moz-segmented-control-item>
      `
    )}
  </moz-segmented-control>
  ${withDeck
    ? html`
        <named-deck id="example-deck" selected-view=${value}>
          ${options.map(
            (option, i) => html`
              <div name=${option} class="deck-content">
                <h3>Option ${i + 1} Content</h3>
                <p>This is the content for option ${i + 1}.</p>
              </div>
            `
          )}
        </named-deck>
      `
    : ""}
`;

export const Default = Template.bind({});
Default.args = {
  value: options[0],
  name: "segmented-control",
  showIcons: false,
  disabled: false,
  disabledItems: [],
  size: "default",
  withDeck: false,
  iconsOnly: false,
  labels: "default",
};

export const WithIcons = Template.bind({});
WithIcons.args = {
  ...Default.args,
  showIcons: true,
};

export const IconsOnly = Template.bind({});
IconsOnly.args = {
  ...Default.args,
  iconsOnly: true,
  labels: "icon",
};

export const SmallSize = Template.bind({});
SmallSize.args = {
  ...Default.args,
  size: "small",
};

export const Disabled = Template.bind({});
Disabled.args = {
  ...Default.args,
  disabled: true,
};

export const DisabledItem = Template.bind({});
DisabledItem.args = {
  ...Default.args,
  disabledItems: ["option2"],
};

export const WithNamedDeck = Template.bind({});
WithNamedDeck.args = {
  ...Default.args,
  withDeck: true,
  showIcons: true,
};

export const Wide = Template.bind({});
Wide.args = {
  ...Default.args,
  className: "wide",
};

export const WithEllipsizedLabel = Template.bind({});
WithEllipsizedLabel.args = {
  ...Default.args,
  className: "narrow",
  labels: "long",
};
