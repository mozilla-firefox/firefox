/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { getModelIconSrc } from "chrome://browser/content/aiwindow/modules/ModelIcons.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-toggle.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/smartwindow-option-select.mjs";

const {
  AUTO_MODEL_CHOICE_ID,
  getModelDisplayOrder,
  getAllModelsData,
  getCachedModelsData,
} = window.IS_STORYBOOK
  ? {
      AUTO_MODEL_CHOICE_ID: "4",
      getModelDisplayOrder: () => [AUTO_MODEL_CHOICE_ID, "2", "3", "1"],
      getCachedModelsData: () => ({
        1: { shortName: "Gemini 3.1 Flash Lite", brandName: "Gemini" },
        2: { shortName: "Qwen 3 235B", brandName: "Qwen" },
        3: { shortName: "Mistral Small 4", brandName: "Mistral" },
      }),
      getAllModelsData: async () => getCachedModelsData(),
    }
  : ChromeUtils.importESModule(
      "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
    );

const MODEL_OPTIONS_BY_ID = {
  [AUTO_MODEL_CHOICE_ID]: {
    value: AUTO_MODEL_CHOICE_ID,
    l10nId: "smartwindow-onboarding-model-auto",
    descriptionL10nId: "smartwindow-onboarding-model-auto-description",
  },
  2: {
    value: "2",
    descriptionL10nId: "aiwindow-firstrun-model-flexible-body",
  },
  3: {
    value: "3",
    descriptionL10nId: "smartwindow-onboarding-model-mistral-description",
  },
  1: {
    value: "1",
    descriptionL10nId: "smartwindow-onboarding-model-gemini-description",
  },
};

function getModelOptions(models) {
  return [...new Set([AUTO_MODEL_CHOICE_ID, ...getModelDisplayOrder()])]
    .filter(id => MODEL_OPTIONS_BY_ID[id])
    .map(id => ({
      ...MODEL_OPTIONS_BY_ID[id],
      iconSrc: getModelIconSrc(id),
      label: models[id]?.shortName,
      buttonLabel: models[id]?.brandName,
    }));
}

const MEMORIES_OPTIONS = [
  {
    value: "both",
    l10nId: "smartwindow-onboarding-memories-both",
    descriptionL10nId: "smartwindow-onboarding-memories-both-description",
  },
  {
    value: "browsing",
    l10nId: "smartwindow-onboarding-memories-browsing",
    descriptionL10nId: "smartwindow-onboarding-memories-browsing-description",
  },
  {
    value: "chats",
    l10nId: "smartwindow-onboarding-memories-chats",
    descriptionL10nId: "smartwindow-onboarding-memories-chats-description",
  },
  {
    value: "none",
    l10nId: "smartwindow-onboarding-memories-none",
    descriptionL10nId: "smartwindow-onboarding-memories-none-description",
  },
];

/**
 * First-run UI. Emits smartwindow-onboarding:save with the three choices
 * in event.detail, without writing preferences.
 *
 * @property {string} modelChoiceId - Model choice: "4", "2", "3", or "1".
 * @property {string} recommendedModelChoiceId - Model that receives the recommendation badge.
 * @property {string} memoriesChoice - "both", "browsing", "chats", or "none".
 * @property {boolean} isDefaultWindow - Whether to start in Smart Window.
 * @property {number} autoAdvanceMS - Intro duration in milliseconds; 0 skips it.
 */
export class SmartwindowOnboarding extends MozLitElement {
  static properties = {
    modelChoiceId: { type: String },
    recommendedModelChoiceId: { type: String },
    memoriesChoice: { type: String },
    isDefaultWindow: { type: Boolean },
    autoAdvanceMS: { type: Number },
    modelOptions: { state: true },
    showingSetup: { state: true },
    leavingWelcome: { state: true },
  };

  constructor() {
    super();
    this.modelOptions = getModelOptions(getCachedModelsData());
    this.modelChoiceId = AUTO_MODEL_CHOICE_ID;
    this.recommendedModelChoiceId = AUTO_MODEL_CHOICE_ID;
    this.memoriesChoice = "both";
    this.isDefaultWindow = true;
    this.autoAdvanceMS = 3000;
    this.showingSetup = false;
    this.leavingWelcome = false;
  }

  async firstUpdated() {
    this.modelOptions = getModelOptions(await getAllModelsData());
  }

  #advanceTimer;

  connectedCallback() {
    super.connectedCallback();
    if (this.hasUpdated) {
      this.#scheduleAdvance();
    }
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    clearTimeout(this.#advanceTimer);
  }

  updated(changed) {
    super.updated(changed);
    if (changed.has("autoAdvanceMS")) {
      this.#scheduleAdvance();
    }
  }

  #scheduleAdvance() {
    clearTimeout(this.#advanceTimer);
    this.leavingWelcome = false;
    if (!this.isConnected || this.showingSetup) {
      return;
    }
    this.#advanceTimer = setTimeout(() => {
      this.leavingWelcome = true;
      const transitionMS =
        this.autoAdvanceMS > 0 &&
        !window.matchMedia("(prefers-reduced-motion: reduce)").matches
          ? 1000
          : 0;
      this.#advanceTimer = setTimeout(async () => {
        this.showingSetup = true;
        await this.updateComplete;
        if (this.isConnected) {
          this.shadowRoot.querySelector("h1").focus();
        }
      }, transitionMS);
    }, this.autoAdvanceMS);
  }

  #save() {
    this.dispatchEvent(
      new CustomEvent("smartwindow-onboarding:save", {
        detail: {
          modelChoiceId: this.modelChoiceId,
          memoriesChoice: this.memoriesChoice,
          isDefaultWindow: this.isDefaultWindow,
        },
        bubbles: true,
        composed: true,
      })
    );
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/smartwindow-onboarding.css"
      />
      <main>
        ${this.showingSetup
          ? this.#setupTemplate()
          : html`
              <section
                class="welcome ${this.leavingWelcome ? "leaving" : ""}"
                aria-labelledby="welcome-title"
              >
                <img
                  class="welcome-logo"
                  src="chrome://browser/content/aiwindow/assets/smart-window.svg"
                  alt=""
                />
                <h1
                  class="onboarding-title welcome-title"
                  id="welcome-title"
                  data-l10n-id="aiwindow-firstrun-title"
                ></h1>
                <p
                  class="welcome-subtitle"
                  data-l10n-id="smartwindow-onboarding-welcome-subtitle"
                ></p>
              </section>
            `}
      </main>
    `;
  }

  #setupTemplate() {
    return html`
      <section class="setup" aria-labelledby="setup-title">
        <header class="setup-header">
          <h1
            class="onboarding-title"
            id="setup-title"
            tabindex="-1"
            data-l10n-id="smartwindow-onboarding-title"
          ></h1>
          <p
            class="setup-subtitle"
            data-l10n-id="smartwindow-onboarding-subtitle"
          ></p>
        </header>
        <div class="choices">
          <div
            class="choice"
            role="group"
            aria-labelledby="model-label"
            aria-describedby="model-description"
          >
            <span class="choice-icon-container">
              <img
                class="choice-icon"
                src="chrome://browser/skin/lightning-bolt.svg"
                alt=""
              />
            </span>
            <div class="choice-text">
              <h2
                class="choice-label"
                id="model-label"
                data-l10n-id="smartwindow-onboarding-model-label"
              ></h2>
              <p
                class="choice-description"
                id="model-description"
                data-l10n-id="smartwindow-onboarding-model-description"
              ></p>
            </div>
            <smartwindow-option-select
              class="choice-select"
              id="model"
              .options=${this.modelOptions.map(option => ({
                ...option,
                secondaryLabelL10nId:
                  option.value === this.recommendedModelChoiceId
                    ? "smartwindow-onboarding-model-recommended"
                    : undefined,
              }))}
              .value=${this.modelChoiceId}
              @smartwindow-option-select:change=${event => {
                this.modelChoiceId = event.detail.value;
              }}
            ></smartwindow-option-select>
          </div>
          <div
            class="choice"
            role="group"
            aria-labelledby="memories-label"
            aria-describedby="memories-description"
          >
            <span class="choice-icon-container">
              <img
                class="choice-icon"
                src="chrome://browser/content/aiwindow/assets/memories-on.svg"
                alt=""
              />
            </span>
            <div class="choice-text">
              <h2
                class="choice-label"
                id="memories-label"
                data-l10n-id="smartwindow-onboarding-memories-label"
              ></h2>
              <p
                class="choice-description"
                id="memories-description"
                data-l10n-id="smartwindow-onboarding-memories-description"
              ></p>
            </div>
            <smartwindow-option-select
              class="choice-select"
              id="memories"
              .options=${MEMORIES_OPTIONS}
              .value=${this.memoriesChoice}
              @smartwindow-option-select:change=${event => {
                this.memoriesChoice = event.detail.value;
              }}
            ></smartwindow-option-select>
          </div>
          <div class="choice">
            <span class="choice-icon-container">
              <img
                class="choice-icon"
                src="chrome://browser/content/aiwindow/assets/smart-window.svg"
                alt=""
              />
            </span>
            <moz-toggle
              class="default-window-toggle"
              id="default-window"
              inputlayout="inline-end"
              data-l10n-id="smartwindow-onboarding-default-window"
              .pressed=${this.isDefaultWindow}
              @toggle=${event => {
                this.isDefaultWindow = event.target.pressed;
              }}
            ></moz-toggle>
          </div>
        </div>
        <moz-button
          class="save-button"
          id="save"
          type="primary"
          data-l10n-id="smartwindow-onboarding-save"
          @click=${this.#save}
        ></moz-button>
      </section>
    `;
  }
}

customElements.define("smartwindow-onboarding", SmartwindowOnboarding);
