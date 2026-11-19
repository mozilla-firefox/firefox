/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/**
 * @typedef {object} FormReviewField
 * @property {string} id The stable ID of the detected form field.
 * @property {string} label The label detected for the form field.
 * @property {string} placeholder The form field’s placeholder text.
 * @property {string} name The form field’s name attribute.
 * @property {string} value The generated or user-edited field value.
 */

/** @typedef {"fill-form" | "cancel" | "stop" | "close"} FormReviewActionType */

/**
 * @typedef {object} FormReviewAction
 * @property {FormReviewActionType} type The action requested by the user.
 * @property {Array<{id: string, value: string}>} [fields]
 *   The reviewed field values included with a fill action.
 */

/**
 * @typedef {object} FormReviewFillResult
 * @property {boolean} hasErrors Whether the requested operation encountered an error.
 * @property {boolean} cancelled Whether the requested operation was cancelled.
 * @property {number} filledFieldCount Number of fields successfully filled.
 */

/** @typedef {"progress" | "review" | "final"} FormReviewState */

/** @typedef {"generation-failed" | "no-suggestions"} FormReviewGenerationErrorType */

/** @typedef {FormReviewGenerationErrorType | "fill-failed"} FormReviewErrorType */

/**
 * @typedef {
 *   {fields: Array<FormReviewField>} |
 *   {errorType: FormReviewGenerationErrorType}
 * } FormReviewGenerationResult
 */

/**
 * How many tabs are allowed to be selected by the user as context
 * to generate form field values.
 */
export const MAX_SELECTED_TABS = 5;

export const FORM_REVIEW_READY_EVENT = "SmartFormFillReview:Ready";

/**
 * Actions the user initiates in the form review dialog
 */
export const FORM_REVIEW_ACTIONS = Object.freeze({
  FILL_FORM: "fill-form",
  CANCEL: "cancel",
  STOP: "stop",
  CLOSE: "close",
});

/**
 * List of valid actions from the form review dialog
 */
export const VALID_FORM_REVIEW_ACTIONS = Object.freeze(
  Object.values(FORM_REVIEW_ACTIONS)
);

/**
 * The states the form review dialog can be in
 */
export const FORM_REVIEW_STATES = Object.freeze({
  PROGRESS: "progress",
  REVIEW: "review",
  FINAL: "final",
});

/**
 * Errors displayed by the form review dialog
 */
export const FORM_REVIEW_ERRORS = Object.freeze({
  GENERATION_FAILED: "generation-failed",
  NO_SUGGESTIONS: "no-suggestions",
  FILL_FAILED: "fill-failed",
});

/**
 * Errors that can occur while generating form suggestions
 */
export const VALID_FORM_REVIEW_GENERATION_ERRORS = Object.freeze([
  FORM_REVIEW_ERRORS.GENERATION_FAILED,
  FORM_REVIEW_ERRORS.NO_SUGGESTIONS,
]);

/**
 * Input types supported by Smart Form Fill
 */
export const SUPPORTED_INPUT_TYPES = [
  "text",
  "email",
  "tel",
  "number",
  "month",
];
