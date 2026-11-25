/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import "chrome://browser/content/aiwindow/components/ai-action-confirmation.mjs";
import "chrome://browser/content/aiwindow/components/ai-action-confirmation-tab-row.mjs";

export default {
  title: "Domain-specific UI Widgets/AI Window/AI Action Confirmation",
  component: "ai-action-confirmation",
  parameters: {
    fluent: `
smartwindow-nl-undo-button =
    .label = Undo
smart-window-closed-tabs-label =
    { $count ->
        [one] Closed { $count } tab
       *[other] Closed { $count } tabs
    }
smart-window-closed-and-restored-label = Closed and restored tabs
  `,
  },
  argTypes: {
    labelL10nId: { control: "text" },
    labelL10nArgs: { control: "object" },
    canUndo: { control: "boolean" },
    isExpanded: { control: "boolean" },
    items: { control: "object" },
  },
};

const EXAMPLE_TABS = Array.from({ length: 20 }, (_, index) => ({
  url: `https://example.com/${index + 1}`,
  title: `Example tab ${index + 1}`,
}));

const Template = ({
  labelL10nId,
  labelL10nArgs,
  canUndo,
  isExpanded,
  items,
}) => html`
  <div
    style="max-width: 320px;
           --ai-action-confirmation-icon-image: url('chrome://browser/skin/closed-tabs.svg');"
  >
    <ai-action-confirmation
      .labelL10nId=${labelL10nId}
      .labelL10nArgs=${labelL10nArgs}
      .canUndo=${canUndo}
      .isExpanded=${isExpanded}
      .items=${items}
      .rowTemplate=${tab =>
        html`<ai-action-confirmation-tab-row
          .tab=${tab}
        ></ai-action-confirmation-tab-row>`}
    ></ai-action-confirmation>
  </div>
`;

export const Collapsed = Template.bind({});
Collapsed.args = {
  labelL10nId: "smart-window-closed-tabs-label",
  labelL10nArgs: { count: 1 },
  canUndo: true,
  isExpanded: false,
  items: [EXAMPLE_TABS[2]],
};

export const Expanded = Template.bind({});
Expanded.args = {
  labelL10nId: "smart-window-closed-tabs-label",
  labelL10nArgs: { count: 3 },
  canUndo: true,
  isExpanded: true,
  items: EXAMPLE_TABS.slice(0, 3),
};

export const ExpandedOverflow = Template.bind({});
ExpandedOverflow.args = {
  labelL10nId: "smart-window-closed-tabs-label",
  labelL10nArgs: { count: EXAMPLE_TABS.length },
  canUndo: true,
  isExpanded: true,
  items: EXAMPLE_TABS,
};

export const AfterUndo = Template.bind({});
AfterUndo.args = {
  labelL10nId: "smart-window-closed-and-restored-label",
  canUndo: false,
  isExpanded: false,
  items: EXAMPLE_TABS.slice(0, 3),
};
