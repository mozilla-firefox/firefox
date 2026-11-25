/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/smartwindow-digest-form.mjs";

window.MozXULElement.insertFTLIfNeeded("browser/aiWindow.ftl");
window.MozXULElement.insertFTLIfNeeded("preview/aiWindow.ftl");

// Saving needs DigestManager, which Storybook can't load, so Create and Save
// changes fire digest-form:save-error here.
export default {
  title: "Domain-specific UI Widgets/AI Window/Smartwindow Digest Form",
  component: "smartwindow-digest-form",
  argTypes: {
    mode: { control: "select", options: ["default", "edit"] },
    digest: { control: "object" },
  },
};

const Template = ({ mode, digest }) => html`
  <link
    rel="stylesheet"
    href="chrome://browser/content/aiwindow/ai-window-shared.css"
  />
  <div style="max-width: 350px">
    <smartwindow-digest-form
      mode=${mode}
      .digest=${digest}
    ></smartwindow-digest-form>
  </div>
`;

const DIGEST = {
  id: "story",
  memory_id: "story",
  title: "Nutrition research and food news",
  description: "New nutrition research, dietary guidance and food news.",
  domains: ["bbcnews.com", "apnews.com"],
  schedule: { type: "daily", hour: 9, minute: 0 },
};

export const Create = Template.bind({});
Create.args = {
  mode: "default",
  digest: DIGEST,
};

export const Edit = Template.bind({});
Edit.args = {
  mode: "edit",
  digest: {
    ...DIGEST,
    schedule: { type: "weekly", weekday: 1, hour: 18, minute: 0 },
  },
};

export const OnDemand = Template.bind({});
OnDemand.args = {
  mode: "edit",
  digest: { ...DIGEST, schedule: { type: "on-demand" } },
};
