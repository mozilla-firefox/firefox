/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined } from "../vendor/lit.all.mjs";
import "./moz-textarea.mjs";

export default {
  title: "UI Widgets/Textarea",
  component: "moz-textarea",
  argTypes: {
    l10nId: {
      options: [
        "moz-textarea-label",
        "moz-textarea-placeholder",
        "moz-textarea-description",
      ],
      control: { type: "select" },
    },
  },
  parameters: {
    status: "stable",
    handles: ["change", "input"],
    fluent: `
moz-textarea-label =
  .label = This is a textarea
moz-textarea-placeholder =
  .label = This is a textarea
  .placeholder = Placeholder text here
moz-textarea-description =
  .label = This is a textarea
  .description = This is a description for the textarea
  .placeholder = Placeholder text here
    `,
  },
};

const Template = ({
  name,
  value,
  rows,
  iconSrc,
  disabled,
  readonly,
  required,
  l10nId,
  description,
  supportPage,
  accessKey,
  title,
  hasSlottedDescription,
}) => html`
  <moz-textarea
    name=${name}
    value=${ifDefined(value || null)}
    rows=${rows}
    iconsrc=${ifDefined(iconSrc || null)}
    ?disabled=${disabled}
    ?readonly=${readonly}
    ?required=${required}
    data-l10n-id=${l10nId}
    support-page=${ifDefined(supportPage || null)}
    accesskey=${ifDefined(accessKey || null)}
    title=${ifDefined(title || null)}
  >
    ${hasSlottedDescription
      ? html`<div slot="description">${description}</div>`
      : ""}
  </moz-textarea>
`;

export const Default = Template.bind({});
Default.args = {
  name: "example-moz-textarea",
  value: "",
  rows: 3,
  iconSrc: "",
  disabled: false,
  readonly: false,
  required: false,
  l10nId: "moz-textarea-label",
  supportPage: "",
  accessKey: "",
  title: "",
  hasSlottedDescription: false,
};

export const WithIcon = Template.bind({});
WithIcon.args = {
  ...Default.args,
  iconSrc: "chrome://global/skin/icons/highlights.svg",
};

export const WithPlaceholder = Template.bind({});
WithPlaceholder.args = {
  ...Default.args,
  l10nId: "moz-textarea-placeholder",
};

export const WithDescription = Template.bind({});
WithDescription.args = {
  ...Default.args,
  l10nId: "moz-textarea-description",
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
  l10nId: "moz-textarea-description",
  disabled: true,
};

export const Readonly = Template.bind({});
Readonly.args = {
  ...Default.args,
  l10nId: "moz-textarea-description",
  readonly: true,
};

export const Required = Template.bind({});
Required.args = {
  ...Default.args,
  l10nId: "moz-textarea-description",
  required: true,
};

export const WithRows = Template.bind({});
WithRows.args = {
  ...Default.args,
  rows: 6,
};

export const Resizable = () => html`
  <moz-textarea
    style="--textarea-resize: vertical"
    data-l10n-id="moz-textarea-label"
  ></moz-textarea>
`;
