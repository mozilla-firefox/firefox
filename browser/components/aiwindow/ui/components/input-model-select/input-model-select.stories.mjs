/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/panel-list.mjs";
import "chrome://browser/content/aiwindow/components/input-model-select.mjs";

const AVAILABLE_MODELS = {
  1: {
    model: "gemini-3.1-flash-lite",
    ownerName: "Google",
    labelId: "fast",
    shortName: "Gemini 3.1 Flash Lite",
    brandName: "Gemini",
  },
  2: {
    model: "qwen3-235b-a22b-instruct-2507-maas",
    ownerName: "Alibaba",
    labelId: "allpurpose",
    shortName: "Qwen 3 235B",
    brandName: "Qwen",
  },
  3: {
    model: "mistral-small-2603",
    ownerName: "Mistral",
    labelId: "personal",
    shortName: "Mistral Small 4",
    brandName: "Mistral",
  },
};

const AVAILABLE_MODELS_WITH_CUSTOM = {
  0: {
    model: "custom-model",
    ownerName: "",
    labelId: "custom",
  },
  ...AVAILABLE_MODELS,
};

export default {
  title: "Domain-specific UI Widgets/AI Window/Input Model Select",
  component: "input-model-select",
  parameters: {
    fluent: `
aiwindow-input-model-select-button-label-fast = Fast
aiwindow-input-model-select-button-label-allpurpose = Flexible
aiwindow-input-model-select-button-label-personal = Personal
aiwindow-input-model-select-button-label-custom = Custom
aiwindow-input-model-select-settings-link = Model settings
aiwindow-input-model-select-default-badge =
    .label = Default
    .title = The selected default model
    `,
  },
  argTypes: {
    selectedModelId: {
      options: Object.values(AVAILABLE_MODELS_WITH_CUSTOM).map(m => m.model),
      control: { type: "select" },
    },
  },
};

const Template = ({ availableModels, selectedModelId }) => html`
  <input-model-select
    .availableModels=${availableModels}
    .selectedModelId=${selectedModelId}
  ></input-model-select>
`;

export const Default = Template.bind({});
Default.args = {
  availableModels: AVAILABLE_MODELS,
  selectedModelId: "gemini-3.1-flash-lite",
};

export const WithCustomModel = Template.bind({});
WithCustomModel.args = {
  availableModels: AVAILABLE_MODELS_WITH_CUSTOM,
  selectedModelId: "custom-model",
};
