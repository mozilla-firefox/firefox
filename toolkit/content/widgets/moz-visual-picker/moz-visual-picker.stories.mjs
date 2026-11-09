/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined, classMap, nothing } from "../vendor/lit.all.mjs";
import "./moz-visual-picker.mjs";

export default {
  title: "UI Widgets/Visual Picker",
  component: "moz-visual-picker",
  argTypes: {
    value: {
      options: ["1", "2", "3"],
      control: { type: "select" },
    },
    slottedItem: {
      options: ["card", "avatar"],
      control: { type: "select" },
      if: { arg: "showItemLabels", truthy: false },
    },
    pickerL10nId: {
      options: ["moz-visual-picker", "moz-visual-picker-description"],
      control: { type: "select" },
    },
    type: {
      options: ["radio", "listbox"],
      control: { type: "select" },
    },
    labelPosition: {
      options: ["inside", "outside"],
      control: { type: "select" },
    },
  },
  parameters: {
    actions: {
      handles: ["click", "input", "change"],
    },
    status: "stable",
    fluent: `
moz-visual-picker =
  .label = Pick something
moz-visual-picker-description =
  .label = Pick something
  .description = Pick one of these cool things please
favicon-aria-label =
  .aria-label = Favicon avatar
experiments-aria-label =
  .aria-label = Experiments avatar
heart-aria-label =
  .aria-label = Heart avatar
`,
  },
};

const AVATAR_ICONS = [
  "chrome://global/skin/icons/defaultFavicon.svg",
  "chrome://global/skin/icons/experiments.svg",
  "chrome://global/skin/icons/heart.svg",
];

const AVATAR_L10N_IDS = [
  "favicon-aria-label",
  "experiments-aria-label",
  "heart-aria-label",
];

function getSlottedContent(type, index) {
  if (type == "card") {
    return html`<div class="slotted demo-card">
      <img
        src="chrome://browser/content/profiles/assets/system-theme-background.svg"
      />
      <span>I'm card number ${index + 1}</span>
    </div>`;
  }
  return html`<div class="slotted avatar">
    <img src=${AVATAR_ICONS[index]} role="presentation" />
  </div>`;
}

const Template = ({
  value,
  slottedItem,
  pickerL10nId,
  supportPage,
  type,
  orientation,
  showItemLabels,
  imageSrc,
  showItemDescriptions,
  labelPosition,
}) => {
  return html`
    <style>
      .slotted {
        display: flex;
        justify-content: center;
        align-items: center;
        height: 100%;
      }

      .demo-card {
        flex-direction: column;
        width: 120px;

        span {
          padding: var(--space-xsmall);
          text-align: center;
          flex: 1;
        }

        img {
          border-top-left-radius: inherit;
          border-top-right-radius: inherit;
          flex: 1;
        }
      }

      .avatar-item {
        --visual-picker-item-border-radius: var(--border-radius-circle);
      }

      .avatar {
        height: 50px;
        width: 50px;

        img {
          height: var(--icon-size);
          width: var(--icon-size);
          -moz-context-properties: fill;
          fill: var(--icon-color);
        }
      }

      moz-visual-picker-item {
        max-width: 150px;
      }

      /* Items are full-width rows in the vertical orientation, so the
         horizontal max-width would make them look cramped. */
      moz-visual-picker[orientation="vertical"] moz-visual-picker-item {
        max-width: none;
      }
    </style>
    <moz-visual-picker
      type=${type}
      orientation=${ifDefined(orientation)}
      data-l10n-id=${pickerL10nId}
      value=${ifDefined(value)}
      support-page=${supportPage}
    >
      ${[...Array.from({ length: 3 })].map(
        (_, i) =>
          html`<moz-visual-picker-item
            labelposition=${labelPosition}
            value=${i + 1}
            class=${classMap({ "avatar-item": slottedItem == "avatar" })}
            data-l10n-id=${slottedItem == "avatar"
              ? AVATAR_L10N_IDS[i]
              : nothing}
            label=${showItemLabels ? `Item number ${i + 1}` : nothing}
            description=${showItemDescriptions
              ? `Description for item number ${i + 1}`
              : nothing}
            imagesrc=${ifDefined(imageSrc)}
          >
            ${getSlottedContent(slottedItem, i)}
          </moz-visual-picker-item>`
      )}
    </moz-visual-picker>
  `;
};

export const Default = Template.bind({});
Default.args = {
  pickerL10nId: "moz-visual-picker",
  slottedItem: "card",
  value: "1",
  supportPage: "",
  type: "radio",
  showItemLabels: false,
  showItemDescriptions: false,
  labelPosition: "inside",
};

export const WithPickerDescription = Template.bind({});
WithPickerDescription.args = {
  ...Default.args,
  pickerL10nId: "moz-visual-picker-description",
};

export const WithPickerSupportLink = Template.bind({});
WithPickerSupportLink.args = {
  ...WithPickerDescription.args,
  supportPage: "foo",
};

export const AllUnselected = Template.bind({});
AllUnselected.args = {
  ...Default.args,
  value: "",
};

export const Listbox = Template.bind({});
Listbox.args = {
  ...Default.args,
  type: "listbox",
};

export const WithItemLabels = Template.bind({});
WithItemLabels.args = {
  ...Default.args,
  showItemLabels: true,
};

export const WithItemDescriptions = Template.bind({});
WithItemDescriptions.args = {
  ...WithItemLabels.args,
  showItemDescriptions: true,
};

export const WithImage = Template.bind({});
WithImage.args = {
  ...Default.args,
  imageSrc:
    "https://firefox-settings-attachments.cdn.mozilla.net/main-workspace/newtab-wallpapers-v2/e1108381-5c19-4cb4-a630-69f9e45503fb.avif",
};

export const WithImageAndLabel = Template.bind({});
WithImageAndLabel.args = {
  ...WithImage.args,
  showItemLabels: true,
};

const VerticalTemplate = args => html`
  <style>
    .vertical-container {
      max-width: 400px;
    }
  </style>
  <div class="vertical-container">${Template(args)}</div>
`;

export const Vertical = VerticalTemplate.bind({});
Vertical.args = {
  ...WithImage.args,
  orientation: "vertical",
  showItemLabels: true,
  showItemDescriptions: true,
};

export const LabelOutside = Template.bind({});
LabelOutside.args = {
  ...Default.args,
  showItemLabels: true,
  showItemDescriptions: true,
  labelPosition: "outside",
};
