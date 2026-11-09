/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "../vendor/lit.all.mjs";
import "./theme-picker.mjs";

export default {
  title: "UI Widgets/Theme Picker",
  component: "theme-picker",
  argTypes: {
    layout: {
      options: ["full", "compact"],
      control: { type: "select" },
    },
  },
  parameters: {
    status: "stable",
  },
};

const Template = ({ layout, showLabels, showNativeThemeOption }) => html`
  <theme-picker
    layout=${layout}
    .showLabels=${showLabels}
    .showNativeThemeOption=${showNativeThemeOption}
  ></theme-picker>
`;

export const Default = Template.bind({});
Default.args = {
  layout: "full",
  showLabels: true,
  showNativeThemeOption: true,
};

export const Compact = Template.bind({});
Compact.args = {
  ...Default.args,
  showLabels: false,
  layout: "compact",
};

export const WithoutVisibleLabels = Template.bind({});
WithoutVisibleLabels.args = {
  ...Default.args,
  showLabels: false,
};
