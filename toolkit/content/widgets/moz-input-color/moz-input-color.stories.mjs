/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "../vendor/lit.all.mjs";
import "./moz-input-color.mjs";

export default {
  title: "UI Widgets/Input Color",
  component: "moz-input-color",
  argTypes: {},
  parameters: {
    status: "stable",
    fluent: `
moz-input-color-label =
  .label = Background
    `,
  },
};

const Template = ({ value, label, l10nId }) => {
  return html`
    <moz-input-color
      value=${value}
      label=${label}
      data-l10n-id=${l10nId}
    ></moz-input-color>
  `;
};

export const Default = Template.bind({});
Default.args = {
  value: "#7293C9",
  l10nId: "moz-input-color-label",
};
