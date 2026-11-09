/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined } from "../vendor/lit.all.mjs";
import "./moz-box-button.mjs";

export default {
  title: "UI Widgets/Box Button",
  component: "moz-box-button",
  argTypes: {
    l10nId: {
      options: ["moz-box-button-label", "moz-box-button-label-description"],
      control: { type: "select" },
    },
  },
  parameters: {
    status: "stable",
    fluent: `
moz-box-button-label =
  .label = Click me to navigate!
moz-box-button-label-description =
  .label = Click me to navigate!
  .description = Some description of the button
    `,
  },
};

const Template = ({
  l10nId,
  disabled,
  iconSrc,
  accesskey,
  clickHandler,
}) => html`
  <div style="width: 300px">
    <moz-box-button
      @click=${clickHandler}
      data-l10n-id=${l10nId}
      ?disabled=${disabled}
      iconsrc=${iconSrc}
      accesskey=${ifDefined(accesskey)}
    ></moz-box-button>
  </div>
`;

export const Default = Template.bind({});
Default.args = {
  l10nId: "moz-box-button-label",
  disabled: false,
  iconSrc: "",
  accesskey: "",
};

export const Disabled = Template.bind({});
Disabled.args = {
  ...Default.args,
  disabled: true,
};

export const WithDescription = Template.bind({});
WithDescription.args = {
  l10nId: "moz-box-button-label-description",
};

export const WithIcon = Template.bind({});
WithIcon.args = {
  ...WithDescription.args,
  iconSrc: "chrome://global/skin/icons/highlights.svg",
};

export const WithAccesskey = Template.bind({});
WithAccesskey.args = {
  ...WithDescription.args,
  iconSrc: "chrome://global/skin/icons/highlights.svg",
  accesskey: "o",
  clickHandler: () => alert("Activating the accesskey clicks the button"),
};
