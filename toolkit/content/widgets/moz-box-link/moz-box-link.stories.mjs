/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined } from "../vendor/lit.all.mjs";
import "./moz-box-link.mjs";

export default {
  title: "UI Widgets/Box Link",
  component: "moz-box-link",
  argTypes: {
    l10nId: {
      options: ["moz-box-link-label", "moz-box-link-label-description"],
      control: { type: "select" },
    },
  },
  parameters: {
    status: "stable",
    fluent: `
moz-box-link-label =
  .label = Click me to navigate!
moz-box-link-label-description =
  .label = Click me to navigate!
  .description = Some description of the link
    `,
  },
};

const Template = ({ l10nId, href, supportPage, iconSrc }) => html`
  <div style="width: 300px">
    <moz-box-link
      data-l10n-id=${l10nId}
      href=${ifDefined(href)}
      iconSrc=${ifDefined(iconSrc)}
      support-page=${ifDefined(supportPage)}
    ></moz-box-link>
  </div>
`;

export const Default = Template.bind({});
Default.args = {
  l10nId: "moz-box-link-label",
  href: "https://example.com",
};

export const WithDescription = Template.bind({});
WithDescription.args = {
  ...Default.args,
  l10nId: "moz-box-link-label-description",
};

export const WithSupportLink = Template.bind({});
WithSupportLink.args = {
  ...Default.args,
  supportPage: "test",
};

export const WithIcon = Template.bind({});
WithIcon.args = {
  ...Default.args,
  iconSrc: "chrome://global/skin/icons/highlights.svg",
};
