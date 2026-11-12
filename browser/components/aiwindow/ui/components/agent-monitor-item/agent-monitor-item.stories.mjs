/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import "chrome://browser/content/aiwindow/components/agent-monitor-item.mjs";

window.MozXULElement.insertFTLIfNeeded("preview/aiWindow.ftl");

export default {
  title: "Domain-specific UI Widgets/AI Window/Agent Monitor Item",
  component: "agent-monitor-item",
  argTypes: {
    agent: { control: "object" },
    mode: { control: "select", options: ["display", "create"] },
    expanded: { control: "boolean" },
    editing: { control: "boolean" },
  },
};

const AGENT = {
  id: "agent-sony",
  monitorName: "Nike Men's Vomero Plus Running Shoes",
  url: "soundnest.com/audio/sony-wh-1000xm5",
  faviconText: "S",
  faviconColor: "#e8663a",
  condition: "",
  conditionPresets: [],
  status: { label: "$278 ▼ −7%", kind: "triggered" },
  cadence: "Auto / on-device",
  history: [
    {
      when: "Today 2:14 PM",
      oldValue: "$299",
      newValue: "$278",
      note: "−7%",
    },
    {
      when: "Jun 24",
      flag: "possible change",
      note: "no notification",
      low: true,
    },
    {
      when: "Jun 17",
      oldValue: "$319",
      newValue: "$299",
      note: "first checked",
    },
  ],
};

const Template = ({ agent, mode, expanded, editing, showLastResult }) => html`
  <div style="max-width: 416px;">
    <agent-monitor-item
      .agent=${agent}
      mode=${mode}
      ?expanded=${expanded}
      ?editing=${editing}
      .showLastResult=${showLastResult}
    ></agent-monitor-item>
  </div>
`;

export const DisplayCollapsed = Template.bind({});
DisplayCollapsed.args = {
  agent: { ...AGENT, status: { label: "Watching", kind: "watching" } },
  mode: "display",
  expanded: false,
  editing: false,
};

export const DisplayExpanded = Template.bind({});
DisplayExpanded.args = {
  agent: AGENT,
  mode: "display",
  expanded: true,
  editing: false,
};

export const Editing = Template.bind({});
Editing.args = {
  agent: AGENT,
  mode: "display",
  expanded: true,
  editing: true,
};

export const Create = Template.bind({});
Create.args = {
  agent: {
    ...AGENT,
    monitorName: "",
    status: null,
    history: [],
  },
  mode: "create",
  expanded: false,
  editing: false,
};

export const HistoryWithFailedChecks = Template.bind({});
HistoryWithFailedChecks.args = {
  agent: {
    ...AGENT,
    history: [
      {
        id: "h-budget",
        checkedAt: "2026-08-14T09:00:00.000Z",
        status: "error",
        resultExplanation: "429 1",
        conditionMet: false,
        errorCode: "budget_exceeded",
      },
      {
        id: "h-rate-limit",
        checkedAt: "2026-08-13T09:00:00.000Z",
        status: "error",
        resultExplanation: "429 status code",
        conditionMet: false,
        errorCode: "rate_limit",
      },
      {
        id: "h-timeout",
        checkedAt: "2026-08-12T09:00:00.000Z",
        status: "error",
        resultExplanation: "The read timed out",
        conditionMet: false,
        errorCode: "timeout",
      },
      {
        id: "h-canceled",
        checkedAt: "2026-08-11T09:00:00.000Z",
        status: "error",
        resultExplanation: "Monitor check was canceled before it finished.",
        conditionMet: false,
        errorCode: "canceled",
      },
      {
        id: "h-met",
        checkedAt: "2026-08-10T09:00:00.000Z",
        status: "success",
        resultExplanation: "The price dropped to $265.",
        conditionMet: true,
      },
      {
        id: "h-not-met",
        checkedAt: "2026-08-09T09:00:00.000Z",
        status: "success",
        resultExplanation: "The price is still $299.",
        conditionMet: false,
      },
    ],
  },
  mode: "display",
  expanded: true,
  editing: false,
  showLastResult: true,
};

export const CheckNowHeldBackByBudget = Template.bind({});
CheckNowHeldBackByBudget.args = {
  ...DisplayExpanded.args,
  agent: {
    ...AGENT,
    checkLimit: {
      until: Date.now() + 3 * 24 * 60 * 60 * 1000,
      reason: "budget",
      errorCode: "budget_exceeded",
    },
  },
};
