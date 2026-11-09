/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined, classMap } from "../vendor/lit.all.mjs";
import "./moz-radio-group.mjs";

let greetings = ["hello", "howdy", "hola"];
let icons = [
  "chrome://global/skin/icons/highlights.svg",
  "chrome://global/skin/icons/delete.svg",
  "chrome://global/skin/icons/defaultFavicon.svg",
];
let accesskeyOptions = ["h", "w", "X"];

let defaultLabelIds = ["moz-radio-0", "moz-radio-1", "moz-radio-2"];
let wrappedLabelIds = [
  "moz-radio-long-0",
  "moz-radio-long-1",
  "moz-radio-long-2",
];

export default {
  title: "UI Widgets/Radio Group",
  component: "moz-radio-group",
  argTypes: {
    disabledButtons: {
      options: greetings,
      control: { type: "check" },
    },
    buttonLabels: {
      options: ["default", "wrapped"],
      mapping: {
        default: defaultLabelIds,
        wrapped: wrappedLabelIds,
      },
      control: { type: "radio" },
    },
    accesskeys: {
      if: { arg: "showAccesskeys", truthy: true },
    },
    headingLevel: {
      options: ["", "1", "2", "3", "4", "5", "6"],
      control: { type: "select" },
    },
  },
  parameters: {
    actions: {
      handles: ["click", "input", "change"],
    },
    status: "stable",
    fluent: `
moz-radio-group =
  .label = This is the group label
moz-radio-0 =
  .label = Hello
moz-radio-1 =
  .label = Howdy
moz-radio-2 =
  .label = Hola
moz-radio-long-0 =
  .label = Hello ipsum dolor sit amet, consectetur adipiscing elit. Cras tincidunt diam id ligula faucibus volutpat. Integer quis ultricies elit. In in dolor luctus velit sollicitudin efficitur vel id massa.
moz-radio-long-1 =
  .label = Howdy ipsum dolor sit amet, consectetur adipiscing elit. Cras tincidunt diam id ligula faucibus volutpat. Integer quis ultricies elit. In in dolor luctus velit sollicitudin efficitur vel id massa.
moz-radio-long-2 =
  .label = Hola ipsum dolor sit amet, consectetur adipiscing elit. Cras tincidunt diam id ligula faucibus volutpat. Integer quis ultricies elit. In in dolor luctus velit sollicitudin efficitur vel id massa.
moz-radio-described-0 =
  .label = Hello
  .description = This is the first option.
moz-radio-described-1 =
  .label = Howdy
  .description = This is the second option.
moz-radio-described-2 =
  .label = Hola
  .description = This is the third option.
moz-radio-described-long-0 =
  .label = Hello ipsum dolor sit amet, consectetur adipiscing elit. Cras tincidunt diam id ligula faucibus volutpat. Integer quis ultricies elit. In in dolor luctus velit sollicitudin efficitur vel id massa.
  .description = This is the first option.
moz-radio-described-long-1 =
  .label = Howdy ipsum dolor sit amet, consectetur adipiscing elit. Cras tincidunt diam id ligula faucibus volutpat. Integer quis ultricies elit. In in dolor luctus velit sollicitudin efficitur vel id massa.
  .description = This is the second option.
moz-radio-described-long-2 =
  .label = Hola ipsum dolor sit amet, consectetur adipiscing elit. Cras tincidunt diam id ligula faucibus volutpat. Integer quis ultricies elit. In in dolor luctus velit sollicitudin efficitur vel id massa.
  .description = This is the third option.
moz-radio-group-description =
  .label = This is the group label
  .description = This is the group description
    `,
  },
};

const Template = ({
  value = greetings[0],
  groupL10nId = "moz-radio-group",
  buttonLabels,
  groupName,
  showIcons,
  disabled,
  disabledButtons,
  showDescriptions,
  showAccesskeys,
  accesskeys,
  supportPage,
  groupSupportPage,
  title,
  hasSlottedSupportLinks,
  groupSlottedSupportLink,
  nestedFields,
  ellipsized,
  headingLevel,
}) => html`
  <moz-radio-group
    name=${groupName}
    data-l10n-id=${groupL10nId}
    support-page=${ifDefined(groupSupportPage)}
    ?disabled=${disabled}
    value=${value}
    .headingLevel=${headingLevel}
  >
    ${groupSlottedSupportLink
      ? html`<a href="/" slot="support-link">Slotted support link</a>`
      : ""}
    ${greetings.map(
      (greeting, i) => html`
        <moz-radio
          ?disabled=${disabledButtons.includes(greeting)}
          value=${greeting}
          data-l10n-id=${showDescriptions
            ? buttonLabels[i].replace("moz-radio", "moz-radio-described")
            : buttonLabels[i]}
          iconSrc=${ifDefined(showIcons ? icons[i] : "")}
          accesskey=${ifDefined(showAccesskeys ? accesskeys[i] : "")}
          support-page=${ifDefined(supportPage)}
          title=${ifDefined(title || null)}
          class=${classMap({ "text-truncated-ellipsis": ellipsized })}
        >
          ${hasSlottedSupportLinks
            ? html`<a slot="support-link" href="www.example.com">
                Click me!
              </a>`
            : ""}
          ${nestedFields
            ? html`<moz-checkbox
                slot="nested"
                data-l10n-id=${ifDefined(buttonLabels[i])}
              >
              </moz-checkbox> `
            : ""}
        </moz-radio>
      `
    )}
  </moz-radio-group>
`;

export const Default = Template.bind({});
Default.args = {
  label: "",
  buttonLabels: "default",
  groupName: "greeting",
  unchecked: false,
  showIcons: false,
  disabled: false,
  disabledButtons: [],
  showDescriptions: false,
  showAccesskeys: false,
  accesskeys: accesskeyOptions,
  supportPage: "",
  groupSupportPage: "",
  title: "",
  hasSlottedSupportLinks: false,
  groupSlottedSupportLink: false,
  headingLevel: "",
};

export const AllUnchecked = Template.bind({});
AllUnchecked.args = {
  ...Default.args,
  value: "",
};

export const WithIcon = Template.bind({});
WithIcon.args = {
  ...Default.args,
  showIcons: true,
};

export const DisabledRadioGroup = Template.bind({});
DisabledRadioGroup.args = {
  ...Default.args,
  disabled: true,
};

export const DisabledRadioButton = Template.bind({});
DisabledRadioButton.args = {
  ...Default.args,
  disabledButtons: ["hello"],
};

export const WithDescriptions = Template.bind({});
WithDescriptions.args = {
  ...Default.args,
  showDescriptions: true,
};

export const WithAccesskeys = Template.bind({});
WithAccesskeys.args = {
  ...Default.args,
  showAccesskeys: true,
};

export const WithSupportLinks = Template.bind({});
WithSupportLinks.args = {
  ...Default.args,
  supportPage: "test",
};

export const WithSlottedSupportLinks = Template.bind({});
WithSlottedSupportLinks.args = {
  ...Default.args,
  hasSlottedSupportLinks: true,
};

export const WithRadioGroupDescription = Template.bind({});
WithRadioGroupDescription.args = {
  ...Default.args,
  groupL10nId: "moz-radio-group-description",
};

export const WithRadioGroupSupportLink = Template.bind({});
WithRadioGroupSupportLink.args = {
  ...Default.args,
  groupL10nId: "moz-radio-group-description",
  groupSupportPage: "this is the group support page",
};

export const WithRadioGroupSlottedSupportLink = Template.bind({});
WithRadioGroupSlottedSupportLink.args = {
  ...Default.args,
  groupL10nId: "moz-radio-group-description",
  groupSlottedSupportLink: true,
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

export const WithHeadingLabel = Template.bind({});
WithHeadingLabel.args = {
  ...WithRadioGroupDescription.args,
  headingLevel: "2",
};
