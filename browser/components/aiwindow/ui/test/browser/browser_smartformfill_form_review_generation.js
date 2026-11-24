/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_smartformfill_form_review.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_smartformfill_form_review.js",
  this
);

const { PageExtractorParent } = ChromeUtils.importESModule(
  "resource://gre/actors/PageExtractorParent.sys.mjs"
);

describe("Smart Form Fill form review generation", () => {
  let context;

  beforeEach(async () => {
    context = await setupFormReviewTest();
  });

  afterEach(async () => {
    await cleanupFormReviewTest(context);
    context = null;
  });

  it("shows progress while suggestions are generated", async () => {
    const { dialog, reviewBrowser } = await openFormReview(context);
    const snapshot = await getFormReviewSnapshot(reviewBrowser);

    Assert.equal(
      snapshot.state,
      FORM_REVIEW_STATES.PROGRESS,
      "The review should initially show progress"
    );
    Assert.ok(
      snapshot.l10nIds.includes("ai-smart-form-fill-finding-suggestions"),
      "The progress message should be rendered"
    );
    await tabToFormReviewElement(reviewBrowser, ".form-review-stop");

    await respondWithGeneratedFields(context.mockEngineManager, []);
    await waitForFormReviewState(reviewBrowser, FORM_REVIEW_STATES.FINAL);

    const dialogClosed = waitForFormReviewClose(context.win, dialog);
    await activateFormReviewButton(
      reviewBrowser,
      "ai-smart-form-fill-close-review-button"
    );
    await dialogClosed;
  });

  it("shows generated suggestions for review", async () => {
    const { dialog, reviewBrowser } = await openFormReview(context);

    const [generatedField] = await respondWithGeneratedFields(
      context.mockEngineManager,
      [
        {
          action: "generate",
          value: "generated@example.com",
          confidence: "high",
        },
      ]
    );
    await waitForFormReviewState(reviewBrowser, FORM_REVIEW_STATES.REVIEW);

    const snapshot = await getFormReviewSnapshot(reviewBrowser);
    Assert.deepEqual(
      snapshot.fields,
      [
        {
          id: generatedField.id,
          label: "Email",
          placeholder: "",
          name: "email",
          value: "generated@example.com",
        },
      ],
      "The review should receive generated values and detected field metadata"
    );
    Assert.ok(
      snapshot.l10nIds.includes("ai-smart-form-fill-review-heading"),
      "The review heading should be rendered"
    );
    await tabToFormReviewElement(reviewBrowser, "moz-input-text");

    const dialogClosed = waitForFormReviewClose(context.win, dialog);
    await BrowserTestUtils.synthesizeKey("KEY_Escape", {}, reviewBrowser);
    await dialogClosed;
  });

  it("enables filling after all suggestions have been reviewed", async () => {
    const { dialog, reviewBrowser } = await openFormReview(context);

    await makeFormReviewFieldsScrollable(reviewBrowser);
    await respondWithGeneratedFields(context.mockEngineManager, [
      {
        action: "generate",
        value: "generated@example.com",
        confidence: "high",
      },
    ]);
    await waitForFormReviewState(reviewBrowser, FORM_REVIEW_STATES.REVIEW);

    let snapshot = await getFormReviewSnapshot(reviewBrowser);
    Assert.ok(
      snapshot.fillButtonDisabled,
      "Fill should be disabled before the suggestions are fully reviewed"
    );

    await scrollFormReviewFieldsToBottom(reviewBrowser);

    snapshot = await getFormReviewSnapshot(reviewBrowser);
    Assert.ok(
      !snapshot.fillButtonDisabled,
      "Fill should be enabled after scrolling through all suggestions"
    );

    const dialogClosed = waitForFormReviewClose(context.win, dialog);
    await activateFormReviewButton(
      reviewBrowser,
      "ai-smart-form-fill-cancel-review-button"
    );
    await dialogClosed;
  });

  it("shows the no-suggestions result", async () => {
    const { dialog, reviewBrowser } = await openFormReview(context);

    await respondWithGeneratedFields(context.mockEngineManager, []);
    await waitForFormReviewState(reviewBrowser, FORM_REVIEW_STATES.FINAL);

    const snapshot = await getFormReviewSnapshot(reviewBrowser);
    Assert.equal(
      snapshot.errorType,
      FORM_REVIEW_ERRORS.NO_SUGGESTIONS,
      "The final state should identify that no suggestions were generated"
    );
    Assert.ok(
      snapshot.l10nIds.includes("ai-smart-form-fill-no-suggestions-heading"),
      "The no-suggestions heading should be rendered"
    );
    await tabToFormReviewElement(reviewBrowser, ".form-review-close");

    const dialogClosed = waitForFormReviewClose(context.win, dialog);
    await activateFormReviewButton(
      reviewBrowser,
      "ai-smart-form-fill-close-review-button"
    );
    await dialogClosed;
  });

  it("requests page extraction with boilerplate removed", async () => {
    const getTextStub = sinon
      .stub(PageExtractorParent.prototype, "getText")
      .resolves({ text: "extracted page text", links: [] });

    try {
      const { dialog, reviewBrowser } = await openFormReview(context);

      await respondWithGeneratedFields(context.mockEngineManager, []);
      await waitForFormReviewState(reviewBrowser, FORM_REVIEW_STATES.FINAL);

      Assert.ok(
        getTextStub.called,
        "Smart Form Fill should request page extraction"
      );
      Assert.ok(
        getTextStub
          .getCalls()
          .every(call => call.args[0]?.removeBoilerplate === true),
        "Every extraction call should request boilerplate removal"
      );

      const dialogClosed = waitForFormReviewClose(context.win, dialog);
      await activateFormReviewButton(
        reviewBrowser,
        "ai-smart-form-fill-close-review-button"
      );
      await dialogClosed;
    } finally {
      getTextStub.restore();
    }
  });

  it("shows the generation-failure result", async () => {
    const { dialog, reviewBrowser } = await openFormReview(context);

    await captureFormReviewRequest(
      context.mockEngineManager,
      FORM_VALUES_SCHEMA
    );
    await context.mockEngineManager.rejectAllRequests();
    await waitForFormReviewState(reviewBrowser, FORM_REVIEW_STATES.FINAL);

    const snapshot = await getFormReviewSnapshot(reviewBrowser);
    Assert.equal(
      snapshot.errorType,
      FORM_REVIEW_ERRORS.GENERATION_FAILED,
      "The final state should identify a generation failure"
    );
    Assert.ok(
      snapshot.l10nIds.includes("ai-smart-form-fill-error-heading"),
      "The general error heading should be rendered"
    );

    const dialogClosed = waitForFormReviewClose(context.win, dialog);
    await activateFormReviewButton(
      reviewBrowser,
      "ai-smart-form-fill-close-review-button"
    );
    await dialogClosed;
  });
});
