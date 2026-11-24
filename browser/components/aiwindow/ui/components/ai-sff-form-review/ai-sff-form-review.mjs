/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import {
  html,
  ifDefined,
  repeat,
} from "chrome://global/content/vendor/lit.all.mjs";
import {
  FORM_REVIEW_ACTIONS,
  FORM_REVIEW_ERRORS,
  FORM_REVIEW_READY_EVENT,
  FORM_REVIEW_STATES,
} from "chrome://browser/content/aiwindow/modules/SmartFormFillConstants.mjs";

// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button-group.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-input-text.mjs";

/** @typedef {import("chrome://global/content/vendor/lit.all.mjs").TemplateResult} TemplateResult */
/** @typedef {import("chrome://global/content/elements/moz-input-text.mjs").MozInputText} MozInputText */
/** @typedef {import("chrome://browser/content/aiwindow/modules/SmartFormFillConstants.mjs").FormReviewField} FormReviewField */
/** @typedef {import("chrome://browser/content/aiwindow/modules/SmartFormFillConstants.mjs").FormReviewActionType} FormReviewActionType */
/** @typedef {import("chrome://browser/content/aiwindow/modules/SmartFormFillConstants.mjs").FormReviewErrorType} FormReviewErrorType */
/** @typedef {import("chrome://browser/content/aiwindow/modules/SmartFormFillConstants.mjs").FormReviewState} FormReviewState */

/**
 * @typedef {object} FillFormActionDetail
 * @property {Array<{id: string, value: string}>} fields List of fields to fill
 */

/**
 * Smart Form Fill form fill review component
 */
export class AiSffFormReview extends MozLitElement {
  #reviewFieldsElement = null;
  #reviewFieldsObserver = null;
  #reviewedAllFields = false;

  static properties = {
    fields: { type: Array },
    state: { type: String },
    errorType: { type: String },
    filledFieldCount: { type: Number },
    filling: { type: Boolean },
  };

  static queries = {
    stateSection: ".form-review-dialog",
    reviewFields: ".form-review-fields",
    jumpButton: ".form-review-jump-to-bottom-button",
  };

  /**
   * Whether the single fill retry offered after a failure was already spent.
   *
   * @type {boolean}
   */
  #retryUsed = false;

  /** @type {ResizeObserver | null} */
  #overflowObserver = null;

  /** @type {(() => void) | null} */
  #scrollHandler = null;

  /** @type {(() => void) | null} */
  #jumpClickHandler = null;

  /** @type {number | null} */
  #scrollRafId = null;

  /**
   * Creates a form review component in progress state
   */
  constructor() {
    super();

    /** @type {Array<FormReviewField>} */
    this.fields = [];

    /** @type {FormReviewState} */
    this.state = FORM_REVIEW_STATES.PROGRESS;

    /** @type {FormReviewErrorType | null} */
    this.errorType = null;

    /** @type {number | null} */
    this.filledFieldCount = null;

    /** @type {boolean} */
    this.filling = false;
  }

  /**
   * Sends ready event so value generation can begin
   *
   * @returns {void}
   */
  async firstUpdated() {
    await this.#focusCurrentState();
    this.dispatchEvent(
      new CustomEvent(FORM_REVIEW_READY_EVENT, {
        bubbles: true,
      })
    );
  }

  /**
   * Moves focus to the first meaningful control after a state transition.
   *
   * @param {Map<string, unknown>} changedProperties
   *   Properties changed by the completed update.
   * @returns {void}
   */
  updated(changedProperties) {
    super.updated(changedProperties);

    const stateChanged = changedProperties.has("state");
    const retryCompleted =
      this.state === FORM_REVIEW_STATES.FINAL &&
      changedProperties.get("filling") === true &&
      !this.filling;

    if (!stateChanged && !retryCompleted) {
      return;
    }

    if (stateChanged && this.state === FORM_REVIEW_STATES.PROGRESS) {
      this.#retryUsed = false;
      this.#reviewedAllFields = false;
    }

    if (changedProperties.get("state") !== undefined || retryCompleted) {
      this.#focusCurrentState();
    }

    if (!stateChanged) {
      return;
    }

    this.#updateScrollListeners();
    this.#observeReviewFields();
  }

  #observeReviewFields() {
    this.#stopObservingReviewFields();

    if (
      this.#reviewedAllFields ||
      this.state !== FORM_REVIEW_STATES.REVIEW ||
      !this.reviewFields
    ) {
      return;
    }

    this.#reviewFieldsElement = this.reviewFields;
    this.#reviewFieldsElement.addEventListener(
      "scroll",
      this.#updateReviewCompletion
    );
    this.#reviewFieldsObserver = new ResizeObserver(
      this.#updateReviewCompletion
    );
    this.#reviewFieldsObserver.observe(this.#reviewFieldsElement);
  }

  #stopObservingReviewFields() {
    this.#reviewFieldsObserver?.disconnect();
    this.#reviewFieldsObserver = null;
    this.#reviewFieldsElement?.removeEventListener(
      "scroll",
      this.#updateReviewCompletion
    );
    this.#reviewFieldsElement = null;
  }

  #updateReviewCompletion = () => {
    if (this.#reviewedAllFields || !this.#reviewFieldsElement) {
      return;
    }

    const { scrollTop, scrollHeight, clientHeight } = this.#reviewFieldsElement;
    if (scrollHeight - scrollTop - clientHeight > 1) {
      return;
    }

    this.#reviewedAllFields = true;
    this.#stopObservingReviewFields();
    this.requestUpdate();
  };

  disconnectedCallback() {
    super.disconnectedCallback();
    this.#stopObservingReviewFields();
    this.#teardownScrollListeners();
  }

  /**
   * Focuses the current state after its accessible text is localized.
   *
   * @returns {Promise<void>}
   */
  async #focusCurrentState() {
    const section = this.stateSection;
    const state = this.state;
    if (!section) {
      return;
    }

    await this.ownerDocument.l10n?.translateFragment(section);

    if (
      section.isConnected &&
      this.state === state &&
      this.stateSection === section
    ) {
      section.focus();
    }
  }

  /**
   * Wires the jump-to-bottom button to the review list, which only exists
   * while the component is in the review state.
   *
   * @returns {void}
   */
  #updateScrollListeners() {
    this.#teardownScrollListeners();

    if (this.state !== FORM_REVIEW_STATES.REVIEW) {
      return;
    }

    const fields = this.reviewFields;
    const jumpButton = this.jumpButton;
    if (!fields || !jumpButton) {
      return;
    }

    this.#scrollHandler = () => {
      if (this.#scrollRafId) {
        return;
      }

      this.#scrollRafId = requestAnimationFrame(() => {
        this.#scrollRafId = null;
        this.#updateJumpButtonState();
      });
    };

    this.#jumpClickHandler = () => {
      fields.scrollTop = fields.scrollHeight;
    };

    // The fields render asynchronously and the list resizes with the dialog,
    // neither of which fires a scroll event, so recompute on resize too.
    this.#overflowObserver = new ResizeObserver(() =>
      this.#updateJumpButtonState()
    );

    fields.addEventListener("scroll", this.#scrollHandler);
    jumpButton.addEventListener("click", this.#jumpClickHandler);
    this.#overflowObserver.observe(fields);

    this.#updateJumpButtonState();
  }

  /**
   * Shows the jump-to-bottom button while the review list is scrolled well
   * away from its end.
   *
   * @returns {void}
   */
  #updateJumpButtonState() {
    const fields = this.reviewFields;
    const jumpButton = this.jumpButton;
    if (!fields || !jumpButton) {
      return;
    }

    const distanceFromBottom =
      fields.scrollHeight - fields.scrollTop - fields.clientHeight;
    const show = distanceFromBottom > 1;

    if (jumpButton.hasAttribute("visible") !== show) {
      jumpButton.toggleAttribute("visible", show);
      jumpButton.toggleAttribute("disabled", !show);
    }
  }

  /**
   * Releases the listeners and observer attached to the review list.
   *
   * @returns {void}
   */
  #teardownScrollListeners() {
    if (this.#scrollRafId) {
      cancelAnimationFrame(this.#scrollRafId);
      this.#scrollRafId = null;
    }

    if (this.#scrollHandler) {
      this.reviewFields?.removeEventListener("scroll", this.#scrollHandler);
      this.#scrollHandler = null;
    }

    if (this.#jumpClickHandler) {
      this.jumpButton?.removeEventListener("click", this.#jumpClickHandler);
      this.#jumpClickHandler = null;
    }

    this.#overflowObserver?.disconnect();
    this.#overflowObserver = null;
  }

  /**
   * Dispatches an action from the review component to its dialog host.
   *
   * @param {FormReviewActionType} type
   *   The action being requested.
   * @param {FillFormActionDetail} [detail]
   *   Data associated with the action.
   * @returns {void}
   */
  #dispatchAction(type, detail) {
    this.dispatchEvent(
      new CustomEvent(type, {
        bubbles: true,
        composed: true,
        detail,
      })
    );
  }

  /**
   * Saves an edited generated value for its corresponding form field.
   *
   * @param {InputEvent & {currentTarget: MozInputText}} event
   *   The input event containing the edited value.
   * @param {string} fieldId The identifier of the edited form field.
   *
   * @returns {void}
   */
  #handleInput(event, fieldId) {
    if (this.filling) {
      return;
    }

    this.fields = this.fields.map(field =>
      field.id === fieldId
        ? {
            ...field,
            value: event.currentTarget.value,
          }
        : field
    );
  }

  /**
   * Requests that the current reviewed values be filled into the form.
   *
   * @returns {void}
   */
  #handleFill() {
    if (this.filling || !this.#reviewedAllFields) {
      return;
    }

    this.filling = true;
    this.#dispatchAction(FORM_REVIEW_ACTIONS.FILL_FORM, {
      fields: this.fields.map(({ id, value }) => ({ id, value })),
    });
  }

  /**
   * Fills the reviewed values again after a failed fill, without giving the
   * user another chance to edit them. Only one retry is offered per review.
   *
   * @returns {void}
   */
  #handleRetry() {
    if (this.filling || this.#retryUsed) {
      return;
    }

    this.#retryUsed = true;
    this.#handleFill();
  }

  /**
   * Cancels review without requesting that any values be filled.
   *
   * @returns {void}
   */
  #handleCancel() {
    if (this.filling) {
      return;
    }

    this.#dispatchAction(FORM_REVIEW_ACTIONS.CANCEL);
  }

  /**
   * Requests cancellation of suggestion generation.
   *
   * @returns {void}
   */
  #handleStop() {
    this.#dispatchAction(FORM_REVIEW_ACTIONS.STOP);
  }

  /**
   * Requests that the completed dialog be closed.
   *
   * @returns {void}
   */
  #handleClose() {
    this.#dispatchAction(FORM_REVIEW_ACTIONS.CLOSE);
  }

  /**
   * Renders an editable generated value with the detected field label.
   *
   * @param {FormReviewField} field The generated field value to render.
   *
   * @returns {TemplateResult}
   */
  #renderReviewField(field) {
    const label = field.label || field.placeholder || field.name || "";
    const usesGenericLabel = !label;

    return html`
      <moz-input-text
        .value=${field.value}
        .label=${label}
        .disabled=${this.filling}
        data-l10n-id=${ifDefined(
          usesGenericLabel
            ? "ai-smart-form-fill-field-fallback-label"
            : undefined
        )}
        @input=${event => this.#handleInput(event, field.id)}
      ></moz-input-text>
    `;
  }

  /**
   * Renders the editable list of generated values and its fill and cancel
   * actions.
   *
   * @returns {TemplateResult}
   */
  #renderReview() {
    return html`
      <section
        class="form-review-dialog vertical-layout"
        tabindex="-1"
        aria-labelledby="form-review-heading"
        aria-describedby="form-review-description"
      >
        <h1
          id="form-review-heading"
          class="form-review-heading"
          data-l10n-id="ai-smart-form-fill-review-heading"
        ></h1>
        <p
          id="form-review-description"
          class="form-review-description"
          data-l10n-id="ai-smart-form-fill-review-description"
          data-l10n-args=${JSON.stringify({
            count: this.fields.length,
          })}
        ></p>
        <div class="form-review-fields-container">
          <div class="form-review-fields">
            ${repeat(
              this.fields,
              field => field.id,
              field => this.#renderReviewField(field)
            )}
          </div>
          <moz-button
            class="form-review-jump-to-bottom-button"
            data-l10n-id="ai-smart-form-fill-jump-to-bottom"
            data-l10n-attrs="aria-label,tooltiptext"
            iconsrc="chrome://global/skin/icons/shaft-arrow-down.svg"
            disabled
            type="ghost icon"
          ></moz-button>
        </div>
        <moz-button-group class="form-review-actions">
          <moz-button
            .disabled=${this.filling}
            size="large"
            data-l10n-id="ai-smart-form-fill-cancel-review-button"
            @click=${this.#handleCancel}
          ></moz-button>
          <moz-button
            type="primary"
            .disabled=${this.filling || !this.#reviewedAllFields}
            size="large"
            data-l10n-id="ai-smart-form-fill-fill-form-button"
            @click=${this.#handleFill}
          ></moz-button>
        </moz-button-group>
      </section>
    `;
  }

  /**
   * Renders the suggestion-generation progress indicator and stop action.
   *
   * @returns {TemplateResult}
   */
  #renderProgress() {
    return html`
      <section
        class="form-review-progress form-review-dialog"
        tabindex="-1"
        aria-labelledby="form-review-progress-label"
      >
        <div class="form-review-progress-group">
          <img
            class="form-review-icon form-review-progress-icon"
            src="chrome://browser/content/aiwindow/assets/loader.svg"
            alt=""
          />
          <span
            id="form-review-progress-label"
            class="form-review-progress-label"
            data-l10n-id="ai-smart-form-fill-finding-suggestions"
          ></span>
        </div>
        <moz-button
          type="ghost"
          class="form-review-stop"
          iconSrc="chrome://browser/content/aiwindow/assets/stop-generation.svg"
          data-l10n-id="ai-smart-form-fill-stop-finding-suggestions"
          @click=${this.#handleStop}
        ></moz-button>
      </section>
    `;
  }

  /**
   * Renders the success or failure result and its close action.
   *
   * @returns {TemplateResult}
   */
  #renderFinal() {
    const hasErrors = this.errorType !== null;
    // Only a failed fill can be retried, and only once. Each variant renders
    // its own button group because moz-button-group moves primary buttons to
    // the end of the light DOM, out of the template part that created them,
    // so a conditional primary button inside one group is never removed.
    const offerRetry =
      this.errorType === FORM_REVIEW_ERRORS.FILL_FAILED &&
      (!this.#retryUsed || this.filling);

    let headingId = "ai-smart-form-fill-success-heading";
    let descriptionId = "ai-smart-form-fill-success-description";
    let icon =
      hasErrors || this.filledFieldCount === 0
        ? "chrome://browser/content/aiwindow/assets/warning.svg"
        : "chrome://browser/content/aiwindow/assets/applied-policy.svg";

    if (this.errorType === FORM_REVIEW_ERRORS.NO_SUGGESTIONS) {
      headingId = "ai-smart-form-fill-no-suggestions-heading";
      descriptionId = "ai-smart-form-fill-no-suggestions-description";
    } else if (offerRetry) {
      headingId = "ai-smart-form-fill-error-try-again-heading";
      descriptionId = "ai-smart-form-fill-error-try-again-description";
    } else if (hasErrors) {
      headingId = "ai-smart-form-fill-error-heading";
      descriptionId = "ai-smart-form-fill-error-description";
    } else if (this.filledFieldCount === 0) {
      headingId = "ai-smart-form-fill-no-changes-heading";
      descriptionId = "ai-smart-form-fill-no-changes-description";
    }

    return html`
      <section
        class="form-review-dialog vertical-layout"
        tabindex="-1"
        aria-labelledby="form-review-final-heading"
        aria-describedby="form-review-final-description"
      >
        <h1
          id="form-review-final-heading"
          class="form-review-final-heading form-review-heading"
        >
          <img
            class="form-review-icon form-review-final-icon"
            src=${icon}
            alt=""
          />
          <span data-l10n-id=${headingId}></span>
        </h1>
        <p
          id="form-review-final-description"
          class="form-review-description"
          data-l10n-id=${descriptionId}
        ></p>
        ${offerRetry
          ? html`
              <moz-button-group class="form-review-actions">
                <moz-button
                  class="form-review-close"
                  size="large"
                  .disabled=${this.filling}
                  data-l10n-id="ai-smart-form-fill-close-review-button"
                  @click=${this.#handleClose}
                ></moz-button>
                <moz-button
                  class="form-review-retry"
                  type="primary"
                  size="large"
                  .disabled=${this.filling}
                  data-l10n-id="ai-smart-form-fill-try-again-button"
                  @click=${this.#handleRetry}
                ></moz-button>
              </moz-button-group>
            `
          : html`
              <moz-button-group class="form-review-actions">
                <moz-button
                  class="form-review-close"
                  type="primary"
                  size="large"
                  data-l10n-id="ai-smart-form-fill-close-review-button"
                  @click=${this.#handleClose}
                ></moz-button>
              </moz-button-group>
            `}
      </section>
    `;
  }

  /**
   * Renders the view associated with the component's current state.
   *
   * @returns {TemplateResult}
   */
  render() {
    let content;

    switch (this.state) {
      case FORM_REVIEW_STATES.PROGRESS:
        content = this.#renderProgress();
        break;
      case FORM_REVIEW_STATES.REVIEW:
        content = this.#renderReview();
        break;
      case FORM_REVIEW_STATES.FINAL:
        content = this.#renderFinal();
        break;
      default:
        content = this.#renderReview();
    }

    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/ai-sff-form-review.css"
      />
      ${content}
    `;
  }
}

customElements.define("ai-sff-form-review", AiSffFormReview);
