/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_smartformfill_form_review.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_smartformfill_form_review.js",
  this
);

async function openGeneratedFormReview(context) {
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

// Filling only reports an error when setUserInput() throws, which a test
// cannot provoke through the page, so fail the parent's fill query instead.
// The returned handle lets a test arm further failures for its own retry.
function stubFillFailures(browser) {
  const actor =
    browser.browsingContext.currentWindowGlobal.getActor("SmartFormFill");
  const originalSendQuery = actor.sendQuery.bind(actor);
  let remaining = 0;

  const stub = sinon.stub(actor, "sendQuery").callsFake((name, data) => {
    if (name === "SmartFormFill:FillForm" && remaining > 0) {
      remaining--;
      return Promise.resolve({ hasErrors: true, cancelled: false });
    }

    return originalSendQuery(name, data);
  });

  return {
    failNextFills(count) {
      remaining = count;
    },
    restore: () => stub.restore(),
  };
}

// A retry resolves without leaving the final state, so its outcome has to be
// observed through the rendered strings rather than a state transition.
async function waitForFormReviewL10nId(reviewBrowser, l10nId) {
  await TestUtils.waitForCondition(async () => {
    const { l10nIds } = await getFormReviewSnapshot(reviewBrowser);
    return l10nIds.includes(l10nId);
  }, `Waiting for the review dialog to render "${l10nId}"`);
}

describe("Smart Form Fill form review filling", () => {
  let context;

  beforeEach(async () => {
    context = await setupFormReviewTest();
  });

  afterEach(async () => {
    await cleanupFormReviewTest(context);
    context = null;
  });

  it("fills a generated suggestion after review", async () => {
    const { dialog, reviewBrowser } = await openGeneratedFormReview(context);

    await activateFormReviewButton(
      reviewBrowser,
      "ai-smart-form-fill-fill-form-button"
    );
    await waitForFormReviewState(reviewBrowser, FORM_REVIEW_STATES.FINAL);

    Assert.equal(
      await getFormFieldValue(context.win.gBrowser.selectedBrowser, "#email"),
      "generated@example.com",
      "The generated suggestion should be filled into the form"
    );

    const snapshot = await getFormReviewSnapshot(reviewBrowser);
    Assert.equal(
      snapshot.filledFieldCount,
      1,
      "One field should have been filled"
    );
    Assert.ok(
      snapshot.l10nIds.includes("ai-smart-form-fill-success-heading"),
      "The success heading should be rendered"
    );

    const dialogClosed = waitForFormReviewClose(context.win, dialog);
    await activateFormReviewButton(
      reviewBrowser,
      "ai-smart-form-fill-close-review-button"
    );
    await dialogClosed;
  });

  it("preserves a value added to the page before filling", async () => {
    const { dialog, reviewBrowser } = await openGeneratedFormReview(context);

    await typeInFormField(
      context.win.gBrowser.selectedBrowser,
      "#email",
      "user@example.com"
    );
    await activateFormReviewButton(
      reviewBrowser,
      "ai-smart-form-fill-fill-form-button"
    );
    await waitForFormReviewState(reviewBrowser, FORM_REVIEW_STATES.FINAL);

    Assert.equal(
      await getFormFieldValue(context.win.gBrowser.selectedBrowser, "#email"),
      "user@example.com",
      "Filling should not replace the value already in the field"
    );

    const snapshot = await getFormReviewSnapshot(reviewBrowser);
    Assert.equal(
      snapshot.errorType,
      null,
      "Skipping a field that is no longer fillable should not be an error"
    );
    Assert.equal(
      snapshot.filledFieldCount,
      0,
      "No fields should have been filled"
    );
    Assert.ok(
      snapshot.l10nIds.includes("ai-smart-form-fill-no-changes-heading"),
      "The no-changes heading should be rendered"
    );

    const dialogClosed = waitForFormReviewClose(context.win, dialog);
    await activateFormReviewButton(
      reviewBrowser,
      "ai-smart-form-fill-close-review-button"
    );
    await dialogClosed;
  });

  describe("when filling the form fails", () => {
    let dialog, reviewBrowser, formBrowser, fillStub;

    beforeEach(async () => {
      ({ dialog, reviewBrowser } = await openGeneratedFormReview(context));
      formBrowser = context.win.gBrowser.selectedBrowser;
      fillStub = stubFillFailures(formBrowser);
      fillStub.failNextFills(1);

      await activateFormReviewButton(
        reviewBrowser,
        "ai-smart-form-fill-fill-form-button"
      );
      await waitForFormReviewState(reviewBrowser, FORM_REVIEW_STATES.FINAL);
    });

    afterEach(async () => {
      fillStub.restore();

      const dialogClosed = waitForFormReviewClose(context.win, dialog);
      await activateFormReviewButton(
        reviewBrowser,
        "ai-smart-form-fill-close-review-button"
      );
      await dialogClosed;
    });

    it("offers a retry that fills the form after a failed fill", async () => {
      const failed = await getFormReviewSnapshot(reviewBrowser);
      Assert.equal(
        failed.errorType,
        FORM_REVIEW_ERRORS.FILL_FAILED,
        "The final state should identify a fill failure"
      );
      Assert.ok(
        failed.l10nIds.includes("ai-smart-form-fill-error-try-again-heading"),
        "The retry heading should be rendered after the first failure"
      );
      Assert.ok(
        failed.l10nIds.includes("ai-smart-form-fill-try-again-button"),
        "The retry button should be offered after the first failure"
      );
      Assert.equal(
        await getFormFieldValue(formBrowser, "#email"),
        "",
        "A failed fill should leave the form field empty"
      );

      await activateFormReviewButton(
        reviewBrowser,
        "ai-smart-form-fill-try-again-button"
      );
      await waitForFormReviewL10nId(
        reviewBrowser,
        "ai-smart-form-fill-success-heading"
      );
      await waitForFormReviewFocus(reviewBrowser, ".form-review-dialog");

      const retried = await getFormReviewSnapshot(reviewBrowser);
      Assert.equal(
        retried.errorType,
        null,
        "A successful retry should clear the error"
      );
      Assert.equal(
        await getFormFieldValue(formBrowser, "#email"),
        "generated@example.com",
        "The retry should fill the generated suggestion into the form"
      );
    });

    it("stops offering a retry once the retry also fails", async () => {
      fillStub.failNextFills(1);

      await activateFormReviewButton(
        reviewBrowser,
        "ai-smart-form-fill-try-again-button"
      );
      await waitForFormReviewL10nId(
        reviewBrowser,
        "ai-smart-form-fill-error-heading"
      );
      await waitForFormReviewFocus(reviewBrowser, ".form-review-dialog");

      const snapshot = await getFormReviewSnapshot(reviewBrowser);
      Assert.equal(
        snapshot.errorType,
        FORM_REVIEW_ERRORS.FILL_FAILED,
        "The final state should still identify a fill failure"
      );
      Assert.ok(
        !snapshot.l10nIds.includes("ai-smart-form-fill-try-again-button"),
        "Only one retry should be offered per review"
      );
      Assert.equal(
        await getFormFieldValue(formBrowser, "#email"),
        "",
        "A failed retry should leave the form field empty"
      );
    });
  });

  it("leaves the form unchanged when generation is stopped", async () => {
    const { dialog, reviewBrowser } = await openFormReview(context);
    const dialogClosed = waitForFormReviewClose(context.win, dialog);

    await activateFormReviewButton(
      reviewBrowser,
      "ai-smart-form-fill-stop-finding-suggestions"
    );
    await dialogClosed;

    Assert.equal(
      await getFormFieldValue(context.win.gBrowser.selectedBrowser, "#email"),
      "",
      "Stopping generation should leave the form field empty"
    );
  });
});
