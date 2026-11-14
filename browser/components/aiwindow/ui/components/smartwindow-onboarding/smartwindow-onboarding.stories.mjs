/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/smartwindow-onboarding.mjs";

export default {
  title: "Domain-specific UI Widgets/AI Window/Smartwindow Onboarding",
  component: "smartwindow-onboarding",
  parameters: {
    layout: "fullscreen",
    fluent: `
-brand-short-name = Firefox
-smart-window-brand-name = Smart Window
aiwindow-firstrun-title = Welcome to { -smart-window-brand-name }
aiwindow-firstrun-model-flexible-body = Ready for a variety of tasks

# The line break is intentional and is preserved when displayed.
smartwindow-onboarding-welcome-subtitle =
    It works alongside you to get stuff done.
    Plan, compare, explore, and more.
smartwindow-onboarding-title = Let’s make it yours
smartwindow-onboarding-subtitle = Set up { -smart-window-brand-name } the way you like. Make changes anytime in settings.
smartwindow-onboarding-model-label = Pick your AI model
smartwindow-onboarding-model-description = The model powers chats and actions in { -smart-window-brand-name }.
smartwindow-onboarding-model-auto = Auto
# Badge identifying the recommended model during onboarding: Mistral in the EEA, Auto elsewhere.
smartwindow-onboarding-model-recommended = Recommended
smartwindow-onboarding-model-auto-description = { -smart-window-brand-name } picks the best model for your task
smartwindow-onboarding-model-mistral-description = Good if you chat and browse in multiple languages
smartwindow-onboarding-model-gemini-description = Quick answers when you’re moving fast
smartwindow-onboarding-memories-label = Choose how { -smart-window-brand-name } learns about you
smartwindow-onboarding-memories-description = { -smart-window-brand-name } can learn from chats, browsing, or both to create memories. It uses them to personalize your experience over time.
smartwindow-onboarding-memories-both = Chats and browsing
smartwindow-onboarding-memories-browsing = Browsing only
smartwindow-onboarding-memories-chats = Chats only
smartwindow-onboarding-memories-none = Don’t learn from activity
smartwindow-onboarding-memories-both-description = Chats in { -smart-window-brand-name }, browsing across { -brand-short-name }
smartwindow-onboarding-memories-browsing-description = Browsing across { -brand-short-name }
smartwindow-onboarding-memories-chats-description = Chats in { -smart-window-brand-name }
smartwindow-onboarding-memories-none-description = You’ll see fewer tailored suggestions and may need to repeat details across chats.
smartwindow-onboarding-default-window =
    .label = Start in { -smart-window-brand-name }
    .description = Use it when { -brand-short-name } starts, restarts, or opens links from other apps.
smartwindow-onboarding-save =
    .label = Save and go
`,
  },
  argTypes: {
    modelChoiceId: { control: "select", options: ["4", "2", "3", "1"] },
    memoriesChoice: {
      control: "select",
      options: ["both", "browsing", "chats", "none"],
    },
    isDefaultWindow: { control: "boolean" },
    autoAdvanceMS: { control: "number" },
  },
};

const Template = ({
  modelChoiceId,
  recommendedModelChoiceId,
  memoriesChoice,
  isDefaultWindow,
  autoAdvanceMS,
}) => html`
  <smartwindow-onboarding
    .modelChoiceId=${modelChoiceId}
    .recommendedModelChoiceId=${recommendedModelChoiceId}
    .memoriesChoice=${memoriesChoice}
    .isDefaultWindow=${isDefaultWindow}
    .autoAdvanceMS=${autoAdvanceMS}
  ></smartwindow-onboarding>
`;

export const Default = Template.bind({});
Default.args = {
  modelChoiceId: "4",
  recommendedModelChoiceId: "4",
  memoriesChoice: "both",
  isDefaultWindow: true,
  autoAdvanceMS: 3000,
};

export const Setup = Template.bind({});
Setup.args = { ...Default.args, autoAdvanceMS: 0 };

export const ExistingChoices = Template.bind({});
ExistingChoices.args = {
  ...Setup.args,
  modelChoiceId: "3",
  memoriesChoice: "chats",
  isDefaultWindow: false,
};

export const EuropePromotion = Template.bind({});
EuropePromotion.args = {
  ...Setup.args,
  modelChoiceId: "3",
  recommendedModelChoiceId: "3",
};
