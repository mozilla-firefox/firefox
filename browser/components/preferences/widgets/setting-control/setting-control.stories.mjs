/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import "chrome://browser/content/preferences/widgets/setting-control.mjs";

export default {
  title: "Domain-specific UI Widgets/Settings/Setting Control",
  component: "setting-control",
  parameters: {
    status: "in-development",
    handles: ["click", "input", "change"],
    fluent: `
checkbox-example-input =
  .label = Checkbox example of setting-control
  .description = Could have a description like moz-checkbox.
select-example-input =
  .label = Select example of setting-control
  .description = Could have a description like moz-select.
select-option-0 =
  .label = Option 0
select-option-1 =
  .label = Option 1
select-option-2 =
  .label = Option 2
radio-example-input =
  .label = Radio example of setting-control
  .description = Could have a description like moz-radio-group.
radio-option-0 =
  .label = Option 0
radio-option-1 =
  .label = Option 1
  .description = It's a full moz-radio
radio-option-2 =
  .label = Option 2
extension-controlled-input =
  .label = Setting controlled by extension
extension-controlled-message = <strong>My Extension</strong> requires Controlled Setting.
disable-extension =
  .label = Disable extension
  .tooltiptext = Disable extension
extension-controlled-enable-2 = Storybook Only: Refresh the page to enable the extension. To re-enable this extension visit <a data-l10n-name="addons-link">Extensions and themes</a>.`,
  },
};

const Template = ({ config, setting }) => html`
  <link
    rel="stylesheet"
    href="chrome://browser/content/preferences/widgets/setting-control.css"
  /><setting-control .config=${config} .setting=${setting}></setting-control>
`;

const DEFAULT_SETTING = {
  value: 1,
  on() {},
  off() {},
  userChange() {},
  getControlConfig: c => c,
  controllingExtensionInfo: {},
  visible: true,
};

export const Checkbox = Template.bind({});
Checkbox.args = {
  config: {
    id: "checkbox-example",
    l10nId: "checkbox-example-input",
  },
  setting: DEFAULT_SETTING,
};

export const Select = Template.bind({});
Select.args = {
  config: {
    id: "select-example",
    l10nId: "select-example-input",
    control: "moz-select",
    supportPage: "example-support",
    options: [
      {
        value: 0,
        l10nId: "select-option-0",
      },
      {
        value: 1,
        l10nId: "select-option-1",
      },
      {
        value: 2,
        l10nId: "select-option-2",
      },
    ],
  },
  setting: DEFAULT_SETTING,
};

export const Radio = Template.bind({});
Radio.args = {
  config: {
    id: "radio-example",
    l10nId: "radio-example-input",
    control: "moz-radio-group",
    supportPage: "example-support",
    options: [
      {
        value: 0,
        l10nId: "radio-option-0",
      },
      {
        value: 1,
        l10nId: "radio-option-1",
        supportPage: "support-page",
      },
      {
        value: 2,
        l10nId: "radio-option-2",
      },
    ],
  },
  setting: DEFAULT_SETTING,
};

export const ExtensionControlled = Template.bind({});
ExtensionControlled.args = {
  config: {
    id: "extension-controlled-example",
    l10nId: "extension-controlled-input",
    pref: "privacy.userContext.enabled",
  },
  setting: {
    ...DEFAULT_SETTING,
    disableControllingExtension() {
      delete this.controllingExtensionInfo.id;
      delete this.controllingExtensionInfo.name;
      document
        .querySelector("with-common-styles")
        .shadowRoot.querySelector("setting-control")
        .requestUpdate();
    },
    controllingExtensionInfo: {
      id: "extension-controlled-example",
      l10nId: "extension-controlled-message",
      name: "My Extension",
      supportPage: "preferences",
      mayDisable: true,
      // NOTE: allowControl defaults to false, but it can be set to true
      allowControl: false,
    },
  },
};
