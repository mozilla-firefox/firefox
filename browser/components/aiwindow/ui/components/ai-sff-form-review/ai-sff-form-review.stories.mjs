/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import {
  FORM_REVIEW_ACTIONS,
  FORM_REVIEW_ERRORS,
  FORM_REVIEW_STATES,
} from "chrome://browser/content/aiwindow/modules/SmartFormFillConstants.mjs";
import "./ai-sff-form-review.mjs";

const FIELDS = [
  {
    id: "first-name",
    label: "First name",
    placeholder: "",
    name: "firstName",
    value: "Katherine",
  },
  {
    id: "last-name",
    label: "Last name",
    placeholder: "",
    name: "lastName",
    value: "Johnson",
  },
  {
    id: "email",
    label: "Email address",
    placeholder: "name@example.com",
    name: "email",
    value: "katherine@example.com",
  },
  {
    id: "phone",
    label: "",
    placeholder: "Phone number",
    name: "phone",
    value: "555-0100",
  },
  {
    id: "company",
    label: "",
    placeholder: "",
    name: "Company",
    value: "Mozilla",
  },
  {
    id: "unlabelled-field",
    label: "",
    placeholder: "",
    name: "",
    value: "Generated value",
  },
];

const SCROLLABLE_FIELDS = Array.from({ length: 20 }, (_, index) => ({
  id: `field-${index + 1}`,
  label: `Field ${index + 1}`,
  placeholder: "",
  name: `field-${index + 1}`,
  value: `Generated value ${index + 1}`,
}));

export default {
  title: "Domain-specific UI Widgets/AI Window/Smart Form Fill Form Review",
  component: "ai-sff-form-review",
  argTypes: {
    fields: {
      control: "object",
    },
    state: {
      control: "select",
      options: Object.values(FORM_REVIEW_STATES),
    },
    errorType: {
      control: "select",
      options: [null, ...Object.values(FORM_REVIEW_ERRORS)],
    },
    filledFieldCount: {
      control: "number",
    },
  },
  parameters: {
    status: "in-development",
    actions: {
      handles: Object.values(FORM_REVIEW_ACTIONS),
    },
    fluent: `
ai-smart-form-fill-fill-form-button =
    .label = Fill form
ai-smart-form-fill-review-heading = Review suggestions
ai-smart-form-fill-review-description = Edit or delete anything that’s not correct.
ai-smart-form-fill-field-fallback-label =
    .label = Field
ai-smart-form-fill-cancel-review-button =
    .label = Cancel
ai-smart-form-fill-finding-suggestions = Finding suggestions
ai-smart-form-fill-stop-finding-suggestions =
    .aria-label = Stop finding suggestions
ai-smart-form-fill-success-heading = Form filled with suggestions
ai-smart-form-fill-success-description = Check the form. Review the filled fields and update anything that looks off or missing before submitting.
ai-smart-form-fill-no-changes-heading = No changes applied
ai-smart-form-fill-no-changes-description = Smart Form Fill didn’t fill any fields. Check the form and try again.
ai-smart-form-fill-no-suggestions-heading = No suggestions found
ai-smart-form-fill-no-suggestions-description = Smart Form Fill wasn’t able to generate any suggestions for this form.
ai-smart-form-fill-error-heading = Something happened catch-all headline
ai-smart-form-fill-error-description = General explanation that maybe its the connection, or something else happened, and to try again
ai-smart-form-fill-close-review-button =
    .label = Close
    `,
  },
};

const Template = ({
  fields,
  state,
  errorType,
  filledFieldCount = null,
}) => html`
  <ai-sff-form-review
    .fields=${fields}
    .state=${state}
    .errorType=${errorType}
    .filledFieldCount=${filledFieldCount}
  ></ai-sff-form-review>
`;

export const Progress = Template.bind({});
Progress.args = {
  fields: FIELDS,
  state: FORM_REVIEW_STATES.PROGRESS,
  errorType: null,
};

export const Review = Template.bind({});
Review.args = {
  fields: FIELDS,
  state: FORM_REVIEW_STATES.REVIEW,
  errorType: null,
};

export const ScrollableReview = Template.bind({});
ScrollableReview.args = {
  fields: SCROLLABLE_FIELDS,
  state: FORM_REVIEW_STATES.REVIEW,
  errorType: null,
};

export const Success = Template.bind({});
Success.args = {
  fields: FIELDS,
  state: FORM_REVIEW_STATES.FINAL,
  errorType: null,
  filledFieldCount: 1,
};

export const NoChanges = Template.bind({});
NoChanges.args = {
  fields: FIELDS,
  state: FORM_REVIEW_STATES.FINAL,
  errorType: null,
  filledFieldCount: 0,
};

export const FromReviewError = Template.bind({});
FromReviewError.args = {
  fields: FIELDS,
  state: FORM_REVIEW_STATES.FINAL,
  errorType: FORM_REVIEW_ERRORS.GENERATION_FAILED,
};

export const NoSuggestions = Template.bind({});
NoSuggestions.args = {
  fields: [],
  state: FORM_REVIEW_STATES.FINAL,
  errorType: FORM_REVIEW_ERRORS.NO_SUGGESTIONS,
};
