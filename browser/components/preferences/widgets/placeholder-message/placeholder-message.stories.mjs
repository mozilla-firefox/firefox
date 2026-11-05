/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined } from "chrome://global/content/vendor/lit.all.mjs";
import "chrome://browser/content/preferences/widgets/placeholder-message.mjs";

export default {
  title: "Domain-specific UI Widgets/Settings/Placeholder Message",
  component: "placeholder-message",
  argTypes: {
    l10nId: {
      options: [
        "placeholder-message-label",
        "placeholder-message-label-description",
      ],
      control: { type: "select" },
    },
  },
  parameters: {
    status: "in-development",
    fluent: `
placeholder-message-label =
  .label = You’re not signed in
placeholder-message-label-description =
  .label = You’re not signed in
  .description = Sign in to keep your data private, encrypted, and instantly accessible everywhere you use Firefox.
    `,
  },
};

const Template = ({ imageSrc = "", l10nId = "", supportPage = "" }) => html`
  <div style="max-width: 500px">
    <placeholder-message
      imageSrc=${ifDefined(imageSrc)}
      data-l10n-id=${l10nId}
      support-page=${ifDefined(supportPage)}
    ></placeholder-message>
  </div>
`;

export const Default = Template.bind({});
Default.args = {
  imageSrc: "resource://content-accessible/illustrations/security-error.svg",
  l10nId: "placeholder-message-label-description",
  supportPage: "",
};

export const WithSupportPage = Template.bind({});
WithSupportPage.args = {
  ...Default.args,
  supportPage: "test",
};
