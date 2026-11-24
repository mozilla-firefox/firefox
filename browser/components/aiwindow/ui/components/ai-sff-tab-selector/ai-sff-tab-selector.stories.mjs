/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import "./ai-sff-tab-selector.mjs";

const DEFAULT_FAVICON = "chrome://global/skin/icons/defaultFavicon.svg";

export default {
  title: "Domain-specific UI Widgets/AI Window/Smart Form Fill Tab Selector",
  component: "ai-sff-tab-selector",
  argTypes: {
    suggestedTabs: {
      control: "object",
    },
    otherTabs: {
      control: "object",
    },
  },
  parameters: {
    status: "in-development",
    actions: {
      handles: ["toggle"],
    },
    fluent: `
ai-smart-form-fill-edit-sources-title = Edit sources
ai-smart-form-fill-suggested-tabs-heading = Suggested tabs
ai-smart-form-fill-other-tabs-heading = Other tabs
ai-smart-form-fill-tab-select-toggle =
    .aria-label = Toggle tab selection for { $tabTitle }
ai-smart-form-fill-cancel-tab-select-button =
    .label = Cancel
ai-smart-form-fill-accept-tab-select-button =
    .label = Done
    `,
  },
};

const Template = ({ suggestedTabs, otherTabs }) => html`
  <ai-sff-tab-selector
    .suggestedTabs=${suggestedTabs}
    .otherTabs=${otherTabs}
  ></ai-sff-tab-selector>
`;

export const Default = Template.bind({});
Default.args = {
  suggestedTabs: [
    {
      id: "linkedin",
      title: "Firstname Lastname - LinkedIn",
      url: "linkedin.com/in/firstnamelastname",
      favicon: DEFAULT_FAVICON,
      pressed: true,
    },
    {
      id: "resume",
      title: "My Resume - Firstname Lastname",
      url: "PDF document",
      favicon: DEFAULT_FAVICON,
      pressed: true,
    },
  ],
  otherTabs: [
    {
      id: "airbnb",
      title: "Cabins in Colorado - Airbnb",
      url: "airbnb.com/colorado-cabins",
      favicon: DEFAULT_FAVICON,
      pressed: false,
    },
    {
      id: "gmail",
      title: "Inbox - name@gmail.com",
      url: "mail.google.com/inbox",
      favicon: DEFAULT_FAVICON,
      pressed: false,
    },
    {
      id: "mozilla",
      title: "Mozilla",
      url: "mozilla.org",
      favicon: DEFAULT_FAVICON,
      pressed: false,
    },
    {
      id: "github",
      title: "GitHub",
      url: "github.com",
      favicon: DEFAULT_FAVICON,
      pressed: false,
    },
    {
      id: "wikipedia",
      title: "Wikipedia",
      url: "wikipedia.org",
      favicon: DEFAULT_FAVICON,
      pressed: false,
    },
    {
      id: "reddit",
      title: "Reddit",
      url: "reddit.com",
      favicon: DEFAULT_FAVICON,
      pressed: false,
    },
    {
      id: "youtube",
      title: "YouTube",
      url: "youtube.com",
      favicon: DEFAULT_FAVICON,
      pressed: false,
    },
    {
      id: "mdn",
      title: "MDN Web Docs",
      url: "developer.mozilla.org",
      favicon: DEFAULT_FAVICON,
      pressed: false,
    },
  ],
};
