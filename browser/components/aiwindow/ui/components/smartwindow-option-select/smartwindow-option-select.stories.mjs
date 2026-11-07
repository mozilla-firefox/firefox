/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined } from "chrome://global/content/vendor/lit.all.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/smartwindow-option-select.mjs";

const ASSETS = "chrome://browser/content/aiwindow/assets";

const MODEL_OPTIONS = [
  {
    value: "auto",
    l10nId: "smartwindow-option-select-story-auto",
    secondaryLabelL10nId: "smartwindow-option-select-story-recommended",
    buttonL10nId: "smartwindow-option-select-story-auto-button",
    descriptionL10nId: "smartwindow-option-select-story-auto-description",
    iconSrc: `${ASSETS}/model-choice-auto.svg`,
  },
  {
    value: "qwen",
    l10nId: "smartwindow-option-select-story-qwen",
    buttonL10nId: "smartwindow-option-select-story-qwen-button",
    descriptionL10nId: "smartwindow-option-select-story-qwen-description",
    iconSrc: `${ASSETS}/model-choice-qwen.svg`,
  },
  {
    value: "mistral",
    l10nId: "smartwindow-option-select-story-mistral",
    buttonL10nId: "smartwindow-option-select-story-mistral-button",
    descriptionL10nId: "smartwindow-option-select-story-mistral-description",
    iconSrc: `${ASSETS}/model-choice-mistral.svg`,
  },
  {
    value: "gemini",
    l10nId: "smartwindow-option-select-story-gemini",
    buttonL10nId: "smartwindow-option-select-story-gemini-button",
    descriptionL10nId: "smartwindow-option-select-story-gemini-description",
    iconSrc: `${ASSETS}/model-choice-gemini.svg`,
  },
];

const MEMORIES_OPTIONS = [
  {
    value: "all",
    l10nId: "smartwindow-option-select-story-memories-all",
    descriptionL10nId:
      "smartwindow-option-select-story-memories-all-description",
  },
  {
    value: "browsing",
    l10nId: "smartwindow-option-select-story-memories-browsing",
    descriptionL10nId:
      "smartwindow-option-select-story-memories-browsing-description",
  },
  {
    value: "chats",
    l10nId: "smartwindow-option-select-story-memories-chats",
    descriptionL10nId:
      "smartwindow-option-select-story-memories-chats-description",
  },
  {
    value: "off",
    l10nId: "smartwindow-option-select-story-memories-off",
    descriptionL10nId:
      "smartwindow-option-select-story-memories-off-description",
  },
];

export default {
  title: "Domain-specific UI Widgets/AI Window/Smartwindow Option Select",
  component: "smartwindow-option-select",
  parameters: {
    fluent: `
smartwindow-option-select-story-recommended = Recommended
smartwindow-option-select-story-auto = Auto
smartwindow-option-select-story-auto-button = Auto
smartwindow-option-select-story-auto-description = Smart Window picks the best model for your task
smartwindow-option-select-story-qwen = Qwen 3 235B
smartwindow-option-select-story-qwen-button = Qwen
smartwindow-option-select-story-qwen-description = Ready for a variety of tasks
smartwindow-option-select-story-mistral = Mistral Small 4
smartwindow-option-select-story-mistral-button = Mistral
smartwindow-option-select-story-mistral-description = Good if you chat and browse in multiple languages
smartwindow-option-select-story-gemini = Gemini Flash Lite 3.1
smartwindow-option-select-story-gemini-button = Gemini
smartwindow-option-select-story-gemini-description = Quick answers when you’re moving fast
smartwindow-option-select-story-memories-all = Chats and browsing
smartwindow-option-select-story-memories-all-description = Chats in Smart Window, browsing across Firefox
smartwindow-option-select-story-memories-browsing = Browsing only
smartwindow-option-select-story-memories-browsing-description = Browsing across Firefox
smartwindow-option-select-story-memories-chats = Chats only
smartwindow-option-select-story-memories-chats-description = Chats in Smart Window
smartwindow-option-select-story-memories-off = Don’t learn from activity
smartwindow-option-select-story-memories-off-description = You’ll see fewer tailored suggestions and may need to repeat details across chats.
    `,
  },
  argTypes: {
    maxWidth: { control: { type: "text" } },
    value: {
      options: MODEL_OPTIONS.map(option => option.value),
      control: { type: "select" },
    },
  },
};

const Template = ({ options, value, maxWidth }) => html`
  <smartwindow-option-select
    style=${ifDefined(
      maxWidth
        ? `--smartwindow-option-select-max-width: ${maxWidth}`
        : undefined
    )}
    .options=${options}
    .value=${value}
  ></smartwindow-option-select>
`;

export const WithIcons = Template.bind({});
WithIcons.args = {
  options: MODEL_OPTIONS,
  value: "auto",
};

export const WithoutIcons = Template.bind({});
WithoutIcons.args = {
  options: MEMORIES_OPTIONS,
  value: "all",
  maxWidth: "370px",
};
