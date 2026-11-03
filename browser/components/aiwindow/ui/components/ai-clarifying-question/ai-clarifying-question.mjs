/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-textarea.mjs";

const ANSWER_EVENT = "ai-clarifying-question:answer";
const CLOSE_EVENT = "ai-clarifying-question:close";

const DEFAULT_MAX_CUSTOM_ANSWER_LENGTH = 500;

/**
 * @typedef {object} ClarifyingQuestionOption
 * @property {string} id - Identifier reported back in the answer event.
 * @property {string} label - Main text of the option.
 * @property {string} [description] - Secondary text under the label.
 * @property {boolean} [recommended] - Shows the "Recommended" badge and highlight.
 */

/**
 * @typedef {object} ClarifyingQuestion
 * @property {string} id - Identifier reported back in the answer event.
 * @property {string} question - The question text.
 * @property {ClarifyingQuestionOption[]} options - The listed answers.
 */

/**
 * Shows one clarifying question from an AI flow, with numbered options, a
 * free-text "Something else" answer and Skip. The component doesn't track
 * progress through the questions: the owner passes `questions` and
 * `currentIndex`, listens for the events below and moves to the next question.
 *
 * Events (bubbling, composed):
 *   - "ai-clarifying-question:answer"
 *       detail: { questionId, questionIndex, optionId, text }
 *       A listed option sets optionId and text to its label. A custom answer
 *       sets optionId to null and text to what was typed. Skip sets optionId
 *       to null and text to "".
 *   - "ai-clarifying-question:close"
 *       detail: { questionId, questionIndex }
 *
 * @property {ClarifyingQuestion[]} questions - All questions in the flow.
 * @property {number} currentIndex - Index of the question to show.
 * @property {number} maxCustomAnswerLength - Character limit for the custom answer.
 */
export class AIClarifyingQuestion extends MozLitElement {
  static properties = {
    questions: { type: Array },
    currentIndex: { type: Number },
    maxCustomAnswerLength: { type: Number },
    customAnswer: { type: String, state: true },
  };

  static queries = {
    customAnswerInput: "moz-textarea",
  };

  constructor() {
    super();
    this.questions = [];
    this.currentIndex = 0;
    this.maxCustomAnswerLength = DEFAULT_MAX_CUSTOM_ANSWER_LENGTH;
    this.customAnswer = "";
  }

  #shownQuestionKey = null;

  willUpdate() {
    const questionKey = `${this.currentIndex}:${this.currentQuestion?.id}`;
    if (questionKey != this.#shownQuestionKey) {
      this.#shownQuestionKey = questionKey;
      this.customAnswer = "";
    }
  }

  get currentQuestion() {
    return this.questions?.[this.currentIndex] ?? null;
  }

  get #trimmedCustomAnswer() {
    return this.customAnswer.trim();
  }

  #dispatchAnswer(optionId, text) {
    this.dispatchEvent(
      new CustomEvent(ANSWER_EVENT, {
        bubbles: true,
        composed: true,
        detail: {
          questionId: this.currentQuestion?.id ?? null,
          questionIndex: this.currentIndex,
          optionId,
          text,
        },
      })
    );
  }

  handleOptionClick(option) {
    this.#dispatchAnswer(option.id, option.label);
  }

  handleCustomAnswerInput(event) {
    this.customAnswer = event.target.value;
  }

  handleCustomAnswerKeydown(event) {
    if (event.key == "Enter" && !event.shiftKey && !event.isComposing) {
      event.preventDefault();
      if (this.#trimmedCustomAnswer) {
        this.handleSendOrSkip();
      }
    }
  }

  handleSendOrSkip() {
    this.#dispatchAnswer(null, this.#trimmedCustomAnswer);
  }

  handleCustomAnswerIconClick() {
    this.customAnswerInput.focus();
  }

  handleClose() {
    this.dispatchEvent(
      new CustomEvent(CLOSE_EVENT, {
        bubbles: true,
        composed: true,
        detail: {
          questionId: this.currentQuestion?.id ?? null,
          questionIndex: this.currentIndex,
        },
      })
    );
  }

  optionTemplate(option, index) {
    return html`
      <li>
        <button
          type="button"
          class="option"
          ?data-recommended=${option.recommended}
          @click=${() => this.handleOptionClick(option)}
        >
          <span class="option-number" aria-hidden="true">${index + 1}</span>
          <span class="option-text">
            ${option.recommended
              ? html`<span
                  class="recommended-badge"
                  data-l10n-id="ai-clarifying-question-recommended"
                ></span>`
              : nothing}
            <span class="option-label">${option.label}</span>
            ${option.description
              ? html`<span class="option-description"
                  >${option.description}</span
                >`
              : nothing}
          </span>
          ${option.recommended
            ? html`<img
                class="option-arrow"
                src="chrome://global/skin/icons/shaft-arrow-right.svg"
                alt=""
              />`
            : nothing}
        </button>
      </li>
    `;
  }

  render() {
    const question = this.currentQuestion;
    if (!question) {
      return nothing;
    }
    const hasCustomAnswer = !!this.#trimmedCustomAnswer;

    return html`
      <link
        rel="stylesheet"
        href="chrome://global/skin/design-system/text-and-typography.css"
      />
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/ai-clarifying-question.css"
      />
      <moz-button
        class="close-button"
        type="ghost icon"
        size="small"
        iconSrc="resource://content-accessible/close-12.svg"
        data-l10n-id="ai-clarifying-question-close"
        @click=${this.handleClose}
      ></moz-button>
      <div class="ai-clarifying-question-wrapper">
        <div class="question-header">
          <p
            class="question-progress text-deemphasized"
            data-l10n-id="ai-clarifying-question-progress"
            data-l10n-args=${JSON.stringify({
              current: this.currentIndex + 1,
              total: this.questions.length,
            })}
          ></p>
          <h2 class="question-text" id="question-text">${question.question}</h2>
        </div>
        <ol class="options" aria-labelledby="question-text">
          ${question.options?.map((option, index) =>
            this.optionTemplate(option, index)
          )}
        </ol>
        <div class="custom-answer">
          <img
            class="custom-answer-icon"
            src="chrome://global/skin/icons/edit-outline.svg"
            alt=""
            @click=${this.handleCustomAnswerIconClick}
          />
          <moz-textarea
            class="custom-answer-input"
            rows="1"
            data-l10n-id="ai-clarifying-question-custom-answer"
            .value=${this.customAnswer}
            .maxLength=${this.maxCustomAnswerLength}
            @input=${this.handleCustomAnswerInput}
            @keydown=${this.handleCustomAnswerKeydown}
          ></moz-textarea>
          <moz-button
            class="custom-answer-button"
            type="ghost"
            data-l10n-id=${hasCustomAnswer
              ? "ai-clarifying-question-send"
              : "ai-clarifying-question-skip"}
            @click=${this.handleSendOrSkip}
          ></moz-button>
        </div>
      </div>
    `;
  }
}

customElements.define("ai-clarifying-question", AIClarifyingQuestion);
