/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import "chrome://browser/content/aiwindow/components/smartwindow-prompts.mjs";

window.MozXULElement.insertFTLIfNeeded("browser/aiWindow.ftl");

// The digest pill borrows smartwindow-prompts' styles, so each story renders
// it inside a prompts row.
export default {
  title: "Domain-specific UI Widgets/AI Window/Smartwindow Digest Prompt",
  component: "smartwindow-digest-prompt",
};

const Template = ({ prompts }) => html`
  <div style="min-height: 300px; padding: 20px;">
    <smartwindow-prompts
      mode="fullpage"
      .prompts=${prompts}
    ></smartwindow-prompts>
  </div>
`;

const digestPrompt = highlighted => ({
  title: "Morning News",
  type: "digest",
  id: "catch-up",
  // Keeps the highlight on while the story is open.
  highlightExpiresAt: highlighted ? Date.now() + 24 * 60 * 60 * 1000 : 0,
});

export const Highlighted = Template.bind({});
Highlighted.args = { prompts: [digestPrompt(true)] };

export const NotHighlighted = Template.bind({});
NotHighlighted.args = { prompts: [digestPrompt(false)] };
