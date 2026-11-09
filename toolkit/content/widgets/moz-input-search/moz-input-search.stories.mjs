/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined, classMap } from "../vendor/lit.all.mjs";
import "./moz-input-search.mjs";

export default {
  title: "UI Widgets/Input Search",
  component: "moz-input-search",
  argTypes: {
    l10nId: {
      options: [
        "moz-input-search-label",
        "moz-input-search-placeholder",
        "moz-input-search-description",
        "moz-input-search-label-wrapped",
      ],
      control: { type: "select" },
    },
  },
  parameters: {
    status: "stable",
    handles: ["change", "input"],
    fluent: `
moz-input-search-label =
  .label = This is a search input
moz-input-search-placeholder =
  .label = This is a search input
  .placeholder = Placeholder text
moz-input-search-description =
  .label = This is a search input
  .description = Description for the search input
  .placeholder = Placeholder text
moz-input-search-label-wrapped =
  .label = Lorem ipsum dolor sit amet, consectetur adipiscing elit. Suspendisse tristique justo leo, ac pellentesque lacus gravida vitae. Nam pellentesque suscipit venenatis.
    `,
  },
};

const Template = ({
  name,
  value,
  iconSrc,
  disabled,
  required,
  l10nId,
  description,
  supportPage,
  accessKey,
  title,
  hasSlottedDescription,
  hasSlottedSupportLink,
  ellipsized,
}) => html`
  <moz-input-search
    name=${name}
    value=${ifDefined(value || null)}
    iconsrc=${ifDefined(iconSrc || null)}
    ?disabled=${disabled}
    ?required=${required}
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
  </moz-input-search>
`;

export const Default = Template.bind({});
Default.args = {
  name: "example-moz-input-search",
  value: "",
  iconSrc: "",
  disabled: false,
  required: false,
  l10nId: "moz-input-search-label",
  supportPage: "",
  accessKey: "",
  title: "",
  hasSlottedDescription: false,
  hasSlottedSupportLink: false,
};

export const WithPlaceholder = Template.bind({});
WithPlaceholder.args = {
  ...Default.args,
  l10nId: "moz-input-search-placeholder",
};

export const WithIcon = Template.bind({});
WithIcon.args = {
  ...Default.args,
  iconSrc: "chrome://global/skin/icons/highlights.svg",
};

export const withDescription = Template.bind({});
withDescription.args = {
  ...Default.args,
  l10nId: "moz-input-search-description",
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
  l10nId: "moz-input-search-description",
  disabled: true,
};

export const Required = Template.bind({});
Required.args = {
  ...Default.args,
  l10nId: "moz-input-search-description",
  required: true,
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
  l10nId: "moz-input-search-description",
};

export const WithSlottedSupportLink = Template.bind({});
WithSlottedSupportLink.args = {
  ...Default.args,
  hasSlottedSupportLink: true,
  l10nId: "moz-input-search-description",
};

export const WithEllipsizedLabel = Template.bind({});
WithEllipsizedLabel.args = {
  ...Default.args,
  ellipsized: true,
  l10nId: "moz-input-search-label-wrapped",
};
