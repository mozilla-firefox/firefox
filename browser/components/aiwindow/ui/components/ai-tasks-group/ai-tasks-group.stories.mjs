/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import "chrome://browser/content/aiwindow/components/ai-tasks-group.mjs";
import "chrome://global/content/elements/moz-card.mjs";

export default {
  title: "Domain-specific UI Widgets/AI Window/Tasks group",
  component: "ai-tasks-group",
  argTypes: {
    itemCount: { control: { type: "number", min: 0 } },
  },
  parameters: {
    fluent: `
ai-tasks-group-monitors-active = Monitors · Active
ai-tasks-group-monitors-paused = Monitors · Paused
ai-tasks-group-count-of-limit = { $count } of { $limit }
ai-tasks-group-count = { $count }
ai-tasks-group-show-more = Show { $count } more
ai-tasks-group-show-less = Show less
    `,
  },
};

const Template = ({ headingL10nId, countL10nId, itemCount, limit }) => html`
  <ai-tasks-group
    .headingL10nId=${headingL10nId}
    .countL10nId=${countL10nId}
    .countL10nArgs=${{ count: itemCount, limit }}
  >
    ${Array.from(
      { length: itemCount },
      (_, i) => html`<moz-card>Task ${i + 1}</moz-card>`
    )}
  </ai-tasks-group>
`;

export const Active = Template.bind({});
Active.args = {
  headingL10nId: "ai-tasks-group-monitors-active",
  countL10nId: "ai-tasks-group-count-of-limit",
  itemCount: 3,
  limit: 15,
};

export const ActiveWithShowMore = Template.bind({});
ActiveWithShowMore.args = {
  ...Active.args,
  itemCount: 12,
};

export const Paused = Template.bind({});
Paused.args = {
  headingL10nId: "ai-tasks-group-monitors-paused",
  countL10nId: "ai-tasks-group-count",
  itemCount: 2,
};
