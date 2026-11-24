/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_smartformfill_form_review.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_smartformfill_form_review.js",
  this
);

async function openEditableFormReview(context) {
  const review = await openFormReview(context);

  await respondWithGeneratedFields(context.mockEngineManager, [
    {
      action: "generate",
      value: "generated@example.com",
      confidence: "high",
    },
  ]);
  await waitForFormReviewState(review.reviewBrowser, FORM_REVIEW_STATES.REVIEW);

  return review;
}

describe("Smart Form Fill form review editing", () => {
  let context;

  beforeEach(async () => {
    context = await setupFormReviewTest();
  });

  afterEach(async () => {
    await cleanupFormReviewTest(context);
    context = null;
  });

  it("renders generated values as editable fields", async () => {
    const { dialog, reviewBrowser } = await openEditableFormReview(context);

    Assert.deepEqual(
      await getFormReviewInputs(reviewBrowser),
      [
        {
          label: "Email",
          value: "generated@example.com",
        },
      ],
      "The detected label and generated value should be rendered"
    );

    const dialogClosed = waitForFormReviewClose(context.win, dialog);
    await activateFormReviewButton(
      reviewBrowser,
      "ai-smart-form-fill-cancel-review-button"
    );
    await dialogClosed;
  });

  it("fills an edited suggestion", async () => {
    const { dialog, reviewBrowser } = await openEditableFormReview(context);

    await editFormReviewInput(reviewBrowser, 0, "edited@example.com");
    await activateFormReviewButton(
      reviewBrowser,
      "ai-smart-form-fill-fill-form-button"
    );
    await waitForFormReviewState(reviewBrowser, FORM_REVIEW_STATES.FINAL);

    const snapshot = await getFormReviewSnapshot(reviewBrowser);
    Assert.equal(
      snapshot.errorType,
      null,
      "The fill operation should finish successfully"
    );
    Assert.equal(
      await getFormFieldValue(context.win.gBrowser.selectedBrowser, "#email"),
      "edited@example.com",
      "The edited suggestion should be filled into the form"
    );

    const dialogClosed = waitForFormReviewClose(context.win, dialog);
    await activateFormReviewButton(
      reviewBrowser,
      "ai-smart-form-fill-close-review-button"
    );
    await dialogClosed;
  });

  it("does not fill reviewed values after cancellation", async () => {
    const { dialog, reviewBrowser } = await openEditableFormReview(context);

    await editFormReviewInput(reviewBrowser, 0, "edited@example.com");

    const dialogClosed = waitForFormReviewClose(context.win, dialog);
    await activateFormReviewButton(
      reviewBrowser,
      "ai-smart-form-fill-cancel-review-button"
    );
    await dialogClosed;

    Assert.equal(
      await getFormFieldValue(context.win.gBrowser.selectedBrowser, "#email"),
      "",
      "Cancelling should leave the form field empty"
    );
  });
});
