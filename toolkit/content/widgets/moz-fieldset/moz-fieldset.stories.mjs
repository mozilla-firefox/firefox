/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined } from "../vendor/lit.all.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "./moz-fieldset.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "../moz-badge/moz-badge.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "../moz-toggle/moz-toggle.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "../moz-button/moz-button.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "../moz-radio-group/moz-radio-group.mjs";

export default {
  title: "UI Widgets/Fieldset",
  component: "moz-fieldset",
  argTypes: {
    headingLevel: {
      options: ["", "1", "2", "3", "4", "5", "6"],
      control: { type: "select" },
    },
    l10nId: {
      options: ["moz-fieldset-label", "moz-fieldset-description"],
      control: { type: "select" },
    },
    iconSrc: {
      options: [
        "",
        "chrome://global/skin/icons/info.svg",
        "chrome://global/skin/icons/highlights.svg",
        "chrome://global/skin/icons/warning.svg",
        "chrome://global/skin/icons/heart.svg",
        "chrome://global/skin/icons/edit.svg",
      ],
      control: { type: "select" },
    },
    badge: {
      options: ["", "beta", "new"],
      control: { type: "select" },
    },
  },
  parameters: {
    status: "stable",
    fluent: `
moz-fieldset-label =
  .label = Related Settings
moz-fieldset-description =
  .label = Some Settings
  .description = Perhaps you want to have a longer description of what these settings do. Width is set explicitly for emphasis.
moz-badge-beta = Beta
moz-badge-new = New
  `,
  },
};

const Template = ({
  label,
  description,
  l10nId,
  supportPage,
  hasSlottedSupportLinks,
  headingLevel,
  disabled,
  iconSrc,
  badge,
}) => html`
  <moz-fieldset
    data-l10n-id=${l10nId}
    .label=${label}
    .description=${description}
    .headingLevel=${headingLevel}
    .disabled=${disabled}
    support-page=${supportPage}
    style="width: 400px;"
    iconsrc=${ifDefined(iconSrc)}
    badge=${ifDefined(badge || undefined)}
  >
    <moz-toggle
      pressed
      label="First setting"
      description="These could have descriptions too."
    ></moz-toggle>
    <moz-checkbox label="Second setting"></moz-checkbox>
    <moz-checkbox
      label="Third setting"
      description="Checkbox with a description."
      support-page="foo"
    ></moz-checkbox>
    <moz-select label="Make a choice">
      <moz-option label="Option One" value="1"></moz-option>
      <moz-option label="Option A" value="a"></moz-option>
    </moz-select>
    <moz-radio-group label="Radio group setting">
      <moz-radio label="Option 1" value="1"></moz-radio>
      <moz-radio label="Option 2" value="2"></moz-radio>
      <moz-radio label="Option 3" value="3"></moz-radio>
    </moz-radio-group>
    <moz-button label="Button"></moz-button>
    ${hasSlottedSupportLinks
      ? html`<a slot="support-link" href="www.example.com"> Click me! </a>`
      : ""}
  </moz-fieldset>
`;

export const Default = Template.bind({});
Default.args = {
  label: "",
  description: "",
  supportPage: "",
  l10nId: "moz-fieldset-label",
  hasSlottedSupportLinks: false,
  disabled: false,
  iconSrc: "",
  badge: "",
};

export const WithDescription = Template.bind({});
WithDescription.args = {
  ...Default.args,
  l10nId: "moz-fieldset-description",
};

export const WithSupportLink = Template.bind({});
WithSupportLink.args = {
  ...Default.args,
  supportPage: "test",
};

export const WithDescriptionAndSupportLink = Template.bind({});
WithDescriptionAndSupportLink.args = {
  ...WithSupportLink.args,
  l10nId: "moz-fieldset-description",
};

export const WithSlottedSupportLink = Template.bind({});
WithSlottedSupportLink.args = {
  ...Default.args,
  hasSlottedSupportLinks: true,
};

export const WithDescriptionAndSlottedSupportLink = Template.bind({});
WithDescriptionAndSlottedSupportLink.args = {
  ...WithDescription.args,
  hasSlottedSupportLinks: true,
};

export const WithHeadingLegend = Template.bind({});
WithHeadingLegend.args = {
  ...WithDescription.args,
  headingLevel: "2",
};

export const Disabled = Template.bind({});
Disabled.args = {
  ...WithDescription.args,
  disabled: true,
};

export const WithIcon = Template.bind({});
WithIcon.args = {
  ...WithSupportLink.args,
  iconSrc: "chrome://global/skin/icons/info.svg",
};

export const WithBadge = Template.bind({});
WithBadge.args = {
  ...Default.args,
  badge: "new",
};
