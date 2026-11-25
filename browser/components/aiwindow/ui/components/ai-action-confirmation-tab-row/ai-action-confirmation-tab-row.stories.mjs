/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import "chrome://browser/content/aiwindow/components/ai-action-confirmation-tab-row.mjs";

export default {
  title: "Domain-specific UI Widgets/AI Window/AI Action Confirmation Tab Row",
  component: "ai-action-confirmation-tab-row",
  argTypes: {
    tab: { control: "object" },
  },
};

const Template = ({ tab }) => html`
  <div style="max-width: 320px;">
    <ai-action-confirmation-tab-row
      .tab=${tab}
    ></ai-action-confirmation-tab-row>
  </div>
`;

export const Default = Template.bind({});
Default.args = {
  tab: {
    url: "https://example.com/quilting",
    title: "Quilting by Hand",
  },
};

export const WithFavicon = Template.bind({});
WithFavicon.args = {
  tab: {
    url: "https://www.mozilla.org/",
    title: "Internet for people, not profit",
    iconSrc: "chrome://global/skin/icons/info.svg",
  },
};

export const LongTitle = Template.bind({});
LongTitle.args = {
  tab: {
    url: "https://example.com/a-very-long-path",
    title:
      "A page with a title long enough that it has to be truncated with an ellipsis",
  },
};
