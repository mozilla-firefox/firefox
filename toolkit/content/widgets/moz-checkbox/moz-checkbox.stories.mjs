/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined, classMap } from "../vendor/lit.all.mjs";
import "./moz-checkbox.mjs";

export default {
  title: "UI Widgets/Checkbox",
  component: "moz-checkbox",
  argTypes: {
    l10nId: {
      options: [
        "moz-checkbox-label",
        "moz-checkbox-label-description",
        "moz-checkbox-long-label",
        "moz-checkbox-long-description",
      ],
      control: { type: "select" },
    },
  },
  parameters: {
    status: "stable",
    handles: ["click", "input", "change"],
    fluent: `
moz-checkbox-label =
  .label = The label of the checkbox
moz-checkbox-label-description =
  .label = The label of the checkbox
  .description = This is a description
moz-checkbox-long-label =
  .label = Lorem ipsum dolor sit amet, consectetur adipiscing elit. Vestibulum libero enim, luctus eu ante a, maximus imperdiet mi. Suspendisse sodales, nisi et commodo malesuada, lectus.
moz-checkbox-long-description =
  .label = Lorem ipsum dolor sit amet, consectetur adipiscing elit. Vestibulum libero enim, luctus eu ante a, maximus imperdiet mi. Suspendisse sodales, nisi et commodo malesuada, lectus.
  .description = Lorem ipsum dolor sit amet, consectetur adipiscing elit. Vestibulum libero enim, luctus eu ante a, maximus imperdiet mi. Suspendisse sodales, nisi et commodo malesuada, lectus.
    `,
  },
};

const Template = ({
  l10nId,
  checked,
  label,
  disabled,
  iconSrc,
  description,
  hasSlottedDescription,
  accesskey,
  supportPage,
  title,
  hasSlottedSupportLink,
  nestedFields,
  ellipsized,
  value,
}) => {
  let checkboxTemplate = html`
    <moz-checkbox
      ?checked=${checked}
      label=${ifDefined(label)}
      description=${ifDefined(description)}
      data-l10n-id=${ifDefined(l10nId)}
      .iconSrc=${iconSrc}
      ?disabled=${disabled}
      accesskey=${ifDefined(accesskey)}
      support-page=${ifDefined(supportPage)}
      title=${ifDefined(title || null)}
      value=${ifDefined(value)}
      class=${classMap({ "text-truncated-ellipsis": ellipsized })}
    >
      ${hasSlottedDescription
        ? html`<div slot="description">test slot text</div>`
        : ""}
      ${hasSlottedSupportLink
        ? html`<a slot="support-link" href="www.example.com">Click me!</a>`
        : ""}
      ${nestedFields
        ? html`<moz-checkbox slot="nested" data-l10n-id=${ifDefined(l10nId)}>
            </moz-checkbox>
            <moz-checkbox slot="nested" data-l10n-id=${ifDefined(l10nId)}>
              <moz-checkbox slot="nested" data-l10n-id=${ifDefined(l10nId)}>
              </moz-checkbox>
            </moz-checkbox> `
        : ""}
    </moz-checkbox>
  `;
  return nestedFields
    ? html`<moz-fieldset label="Checkbox with nested fields">
        ${checkboxTemplate}
      </moz-fieldset>`
    : checkboxTemplate;
};

export const Default = Template.bind({});
Default.args = {
  name: "example-moz-checkbox",
  value: "example-value",
  l10nId: "moz-checkbox-label",
  checked: false,
  disabled: false,
  iconSrc: "",
  description: "",
  title: "",
  label: "",
  accesskey: "",
  supportPage: "",
  hasSlottedSupportLink: false,
  nestedFields: false,
  ellipsized: false,
};

export const WithIcon = Template.bind({});
WithIcon.args = {
  ...Default.args,
  iconSrc: "chrome://global/skin/icons/highlights.svg",
};

export const CheckedByDefault = Template.bind({});
CheckedByDefault.args = {
  ...Default.args,
  checked: true,
};

export const Disabled = Template.bind({});
Disabled.args = {
  ...Default.args,
  disabled: true,
};

export const WithDescription = Template.bind({});
WithDescription.args = {
  ...Default.args,
  l10nId: "moz-checkbox-label-description",
};

export const WithSlottedDescription = Template.bind({});
WithSlottedDescription.args = {
  ...Default.args,
  hasSlottedDescription: true,
};

export const WithAccesskey = Template.bind({});
WithAccesskey.args = {
  ...Default.args,
  accesskey: "c",
};

export const WithSupportLink = Template.bind({});
WithSupportLink.args = {
  ...Default.args,
  supportPage: "test",
};

export const WithSlottedSupportLink = Template.bind({});
WithSlottedSupportLink.args = {
  ...Default.args,
  hasSlottedSupportLink: true,
};

export const WithNestedFields = Template.bind({});
WithNestedFields.args = {
  ...Default.args,
  nestedFields: true,
};

export const WithEllipsizedLabel = Template.bind({});
WithEllipsizedLabel.args = {
  ...Default.args,
  ellipsized: true,
  l10nId: "moz-checkbox-long-label",
};
