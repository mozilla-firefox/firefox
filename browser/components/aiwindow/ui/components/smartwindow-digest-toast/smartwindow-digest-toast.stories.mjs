/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import "chrome://browser/content/aiwindow/components/smartwindow-digest-toast.mjs";

window.MozXULElement.insertFTLIfNeeded("browser/aiWindow.ftl");
window.MozXULElement.insertFTLIfNeeded("preview/aiWindow.ftl");

export default {
  title: "Domain-specific UI Widgets/AI Window/Smartwindow Digest Toast",
  component: "smartwindow-digest-toast",
  argTypes: {
    frequency: {
      options: ["daily", "weekly", "on-demand"],
      control: { type: "select" },
    },
    mode: {
      options: ["default", "edit"],
      control: { type: "select" },
    },
  },
};

const Template = ({ type, title, frequency, mode }) => html`
  <smartwindow-digest-toast
    type=${type}
    mode=${mode}
    .digest=${{ id: "story-digest", title, schedule: { type: frequency } }}
  ></smartwindow-digest-toast>
`;

export const Daily = Template.bind({});
Daily.args = {
  type: "success",
  title: "Nutrition research and food news",
  frequency: "daily",
  mode: "default",
};

export const Weekly = Template.bind({});
Weekly.args = {
  ...Daily.args,
  frequency: "weekly",
};

export const OnDemand = Template.bind({});
OnDemand.args = {
  ...Daily.args,
  frequency: "on-demand",
};

export const LongTitle = Template.bind({});
LongTitle.args = {
  ...Daily.args,
  title:
    "New nutrition research, dietary guidance and food news from around the world",
};

export const CreateFailed = Template.bind({});
CreateFailed.args = { ...Daily.args, type: "error", mode: "default" };

export const EditFailed = Template.bind({});
EditFailed.args = { ...Daily.args, type: "error", mode: "edit" };
