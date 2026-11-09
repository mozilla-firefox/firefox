/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined, classMap } from "../vendor/lit.all.mjs";
import "./moz-input-folder.mjs";

export default {
  title: "UI Widgets/Input Folder",
  component: "moz-input-folder",
  argTypes: {
    l10nId: {
      options: [
        "moz-input-folder-label",
        "moz-input-folder-placeholder",
        "moz-input-folder-description",
        "moz-input-folder-long-label",
      ],
      control: { type: "select" },
    },
  },
  parameters: {
    status: "stable",
    handles: ["change", "click"],
    fluent: `
moz-input-folder-label =
  .label = Save files to
moz-input-folder-placeholder =
  .label = Save files to
  .placeholder = Select folder
moz-input-folder-description =
  .label = Save files to
  .description = Description text
  .placeholder = Select folder
moz-input-folder-long-label =
  .label = Lorem ipsum dolor sit amet, consectetur adipiscing elit. Vestibulum libero enim, luctus eu ante a, maximus imperdiet mi. Suspendisse sodales, nisi et commodo malesuada, lectus.
    `,
  },
};

const Template = ({
  name,
  value,
  iconSrc,
  disabled,
  l10nId,
  description,
  supportPage,
  title,
  hasSlottedDescription,
  hasSlottedSupportLink,
  ellipsized,
  hasAdditionalAction,
}) => html`
  <div style="width: 450px;">
    <moz-input-folder
      name=${name}
      value=${ifDefined(value || null)}
      iconsrc=${ifDefined(iconSrc || null)}
      ?disabled=${disabled}
      data-l10n-id=${l10nId}
      support-page=${ifDefined(supportPage || null)}
      title=${ifDefined(title || null)}
      @click=${{
        handleEvent: e => {
          if (e.composedPath().some(el => el.id == "choose-folder-button")) {
            e.stopPropagation();
            alert("This would open the file picker");
          }
        },
        capture: true,
      }}
      class=${classMap({ "text-truncated-ellipsis": ellipsized })}
    >
      ${hasSlottedDescription
        ? html`<div slot="description">${description}</div>`
        : ""}
      ${hasSlottedSupportLink
        ? html`<a slot="support-link" href="www.example.com">Click me!</a>`
        : ""}
      ${hasAdditionalAction
        ? html`<moz-button
            slot="actions"
            @click=${() => alert("Use the 'actions' slot for more actions.")}
            >Click me!</moz-button
          >`
        : ""}
    </moz-input-folder>
  </div>
`;

export const Default = Template.bind({});
Default.args = {
  name: "example-moz-input-folder",
  value: "",
  iconSrc: "",
  disabled: false,
  l10nId: "moz-input-folder-label",
  supportPage: "",
  title: "",
  hasSlottedDescription: false,
  hasSlottedSupportLink: false,
};

export const WithValue = Template.bind({});
WithValue.args = {
  ...Default.args,
  value: "/User/Downloads",
};

export const WithPlaceholder = Template.bind({});
WithPlaceholder.args = {
  ...Default.args,
  l10nId: "moz-input-folder-placeholder",
};

export const WithIcon = Template.bind({});
WithIcon.args = {
  ...Default.args,
  iconSrc: "chrome://global/skin/icons/highlights.svg",
};

export const withDescription = Template.bind({});
withDescription.args = {
  ...Default.args,
  l10nId: "moz-input-folder-description",
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
  l10nId: "moz-input-folder-description",
  disabled: true,
};

export const WithSupportLink = Template.bind({});
WithSupportLink.args = {
  ...Default.args,
  supportPage: "support-page",
  l10nId: "moz-input-folder-description",
};

export const WithSlottedSupportLink = Template.bind({});
WithSlottedSupportLink.args = {
  ...Default.args,
  hasSlottedSupportLink: true,
  l10nId: "moz-input-folder-description",
};

export const WithEllipsizedLabel = Template.bind({});
WithEllipsizedLabel.args = {
  ...Default.args,
  ellipsized: true,
  l10nId: "moz-input-folder-long-label",
};

export const WithAdditionalActions = Template.bind({});
WithAdditionalActions.args = {
  ...Default.args,
  hasAdditionalAction: true,
};
