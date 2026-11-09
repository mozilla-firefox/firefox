/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined } from "../vendor/lit.all.mjs";
import "./moz-badge.mjs";

export default {
  title: "UI Widgets/Badge",
  component: "moz-badge",
  argTypes: {
    type: {
      control: "select",
      options: ["default", "beta", "new"],
    },
  },
  parameters: {
    status: "stable",
    fluent: `
moz-badge =
    .label = BADGE
    .title = Beta experiment
`,
  },
};

const Template = ({ label, iconSrc, title, l10nId, type }) => html`
  <moz-badge
    label=${label}
    iconSrc=${ifDefined(iconSrc)}
    title=${ifDefined(title)}
    data-l10n-id=${ifDefined(l10nId)}
    type=${ifDefined(type)}
  ></moz-badge>
`;

export const Default = Template.bind({});
Default.args = {
  l10nId: "moz-badge",
};

export const WithIcon = Template.bind({});
WithIcon.args = {
  iconSrc: "chrome://global/skin/icons/info.svg",
  l10nId: "moz-badge",
};

export const Beta = Template.bind({});
Beta.args = {
  type: "beta",
};

export const New = Template.bind({});
New.args = {
  type: "new",
};
