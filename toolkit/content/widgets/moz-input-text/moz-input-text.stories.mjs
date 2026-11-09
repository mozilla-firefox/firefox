/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined, classMap } from "../vendor/lit.all.mjs";
import "./moz-input-text.mjs";

export default {
  title: "UI Widgets/Input Text",
  component: "moz-input-text",
  argTypes: {
    l10nId: {
      options: [
        "moz-input-text-label",
        "moz-input-text-placeholder",
        "moz-input-text-description",
        "moz-input-text-label-wrapped",
      ],
      control: { type: "select" },
    },
  },
  parameters: {
    status: "stable",
    handles: ["change", "input"],
    fluent: `
moz-input-text-label =
  .label = This is a text input
moz-input-text-placeholder =
  .label = This is a text input
  .placeholder = Placeholder text here
moz-input-text-description =
  .label = This is a text input
  .description = This is a description for the text input
  .placeholder = Placeholder text here
moz-input-text-label-wrapped =
  .label = Lorem ipsum dolor sit amet, consectetur adipiscing elit. Suspendisse tristique justo leo, ac pellentesque lacus gravida vitae. Nam pellentesque suscipit venenatis.
    `,
  },
};

const Template = ({
  name,
  value,
  iconSrc,
  disabled,
  readonly,
  required,
  invalid,
  l10nId,
  description,
  supportPage,
  accessKey,
  title,
  hasSlottedDescription,
  hasSlottedSupportLink,
  ellipsized,
}) => html`
  <moz-input-text
    name=${name}
    value=${ifDefined(value || null)}
    iconsrc=${ifDefined(iconSrc || null)}
    ?disabled=${disabled}
    ?readonly=${readonly}
    ?required=${required}
    ?invalid=${invalid}
    data-l10n-id=${l10nId}
    support-page=${ifDefined(supportPage || null)}
    accesskey=${ifDefined(accessKey || null)}
    title=${ifDefined(title || null)}
    class=${classMap({ "text-truncated-ellipsis": ellipsized })}
  >
    ${hasSlottedDescription
      ? html`<div slot="description">${description}</div>`
      : ""}
    ${hasSlottedSupportLink
      ? html`<a slot="support-link" href="www.example.com">Click me!</a>`
      : ""}
  </moz-input-text>
`;

export const Default = Template.bind({});
Default.args = {
  name: "example-moz-input-text",
  value: "",
  iconSrc: "",
  disabled: false,
  readonly: false,
  required: false,
  invalid: false,
  l10nId: "moz-input-text-label",
  supportPage: "",
  accessKey: "",
  title: "",
  hasSlottedDescription: false,
  hasSlottedSupportLink: false,
};

export const WithPlaceholder = Template.bind({});
WithPlaceholder.args = {
  ...Default.args,
  l10nId: "moz-input-text-placeholder",
};

export const WithIcon = Template.bind({});
WithIcon.args = {
  ...Default.args,
  iconSrc: "chrome://global/skin/icons/highlights.svg",
};

export const WithDescription = Template.bind({});
WithDescription.args = {
  ...Default.args,
  l10nId: "moz-input-text-description",
};

export const WithSlottedDescription = Template.bind({});
WithSlottedDescription.args = {
  ...Default.args,
  description: "This is a custom slotted description.",
  hasSlottedDescription: true,
};

export const Disabled = Template.bind({});
Disabled.args = {
  ...Default.args,
  l10nId: "moz-input-text-description",
  disabled: true,
};

export const Readonly = Template.bind({});
Readonly.args = {
  ...Default.args,
  l10nId: "moz-input-text-description",
  readonly: true,
};

export const Required = Template.bind({});
Required.args = {
  ...Default.args,
  l10nId: "moz-input-text-description",
  required: true,
};

export const Invalid = Template.bind({});
Invalid.args = {
  ...Default.args,
  l10nId: "moz-input-text-description",
  value: "Invalid value",
  invalid: true,
};

export const WithAccesskey = Template.bind({});
WithAccesskey.args = {
  ...Default.args,
  accessKey: "s",
};

export const WithSupportLink = Template.bind({});
WithSupportLink.args = {
  ...Default.args,
  supportPage: "support-page",
  l10nId: "moz-input-text-description",
};

export const WithSlottedSupportLink = Template.bind({});
WithSlottedSupportLink.args = {
  ...Default.args,
  hasSlottedSupportLink: true,
  l10nId: "moz-input-text-description",
};

export const WithEllipsizedLabel = Template.bind({});
WithEllipsizedLabel.args = {
  ...Default.args,
  ellipsized: true,
  l10nId: "moz-input-text-label-wrapped",
};
