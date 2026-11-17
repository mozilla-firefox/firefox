/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { LinkPreview } = ChromeUtils.importESModule(
  "moz-src:///browser/components/genai/LinkPreview.sys.mjs"
);
const { LinkPreviewModel } = ChromeUtils.importESModule(
  "moz-src:///browser/components/genai/LinkPreviewModel.sys.mjs"
);
const { Region } = ChromeUtils.importESModule(
  "resource://gre/modules/Region.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const TEST_LINK_URL = "https://example.com";

registerCleanupFunction(() => {
  Services.prefs.clearUserPref("browser.ml.linkPreview.onboardingTimes");
});
/**
 * Test that optin and collapsed preferences properly update existing cards.
 *
 * This test ensures that when preference values change, they properly
 * update the properties of already created link preview cards.
 */
add_task(async function test_pref_updates_existing_cards() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", true],
      ["browser.ml.linkPreview.collapsed", false],
    ],
  });

  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  const READABLE_PAGE_URL =
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html";

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(READABLE_PAGE_URL, {});

  const panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");

  const card = panel.querySelector("link-preview-card");
  ok(card, "card created for link preview");
  is(card.optin, true, "card has optin=true initially");
  is(card.collapsed, false, "card has collapsed=false initially");

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.optin", false]],
  });
  is(card.optin, false, "card optin updated to false");

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.collapsed", true]],
  });
  is(card.collapsed, true, "card collapsed updated to true");

  panel.remove();
  generateStub.restore();
  LinkPreview.keyboardComboActive = false;
});

/**
 * Test that link preview doesn't generate key points when optin is false.
 *
 * This test verifies that when the optin preference is set to false,
 * the link preview feature will not attempt to generate key points.
 */
add_task(async function test_no_keypoints_when_optin_false() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", false],
      ["browser.ml.linkPreview.collapsed", false],
    ],
  });
  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html",
    {}
  );

  let panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");

  is(
    generateStub.callCount,
    0,
    "generateTextAI should not be called when optin is false"
  );

  panel.remove();
  LinkPreview.keyboardComboActive = false;
  generateStub.restore();

  Services.prefs.clearUserPref("browser.ml.linkPreview.optin");
  Services.prefs.clearUserPref("browser.ml.linkPreview.collapsed");
});

/**
 * Test that link preview doesn't generate key points when collapsed is true.
 *
 * This test verifies that when the collapsed preference is set to true,
 * the link preview feature will not attempt to generate key points even if
 * optin is true.
 */
add_task(async function test_no_keypoints_when_collapsed_true() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", true],
      ["browser.ml.linkPreview.collapsed", true],
    ],
  });

  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  const READABLE_PAGE_URL =
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html";

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(READABLE_PAGE_URL, {});

  const panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");

  is(
    generateStub.callCount,
    0,
    "generateTextAI should not be called when collapsed is true"
  );

  const card = panel.querySelector("link-preview-card");
  ok(card, "card created for link preview");
  ok(!card.generating, "card should not be in generating state");
  is(card.optin, true, "card has optin=true");
  is(card.collapsed, true, "card has collapsed=true");

  panel.remove();
  generateStub.restore();
  LinkPreview.keyboardComboActive = false;
});

/**
 * Test that link preview does generate key points when optin is true and collapsed is false.
 *
 * This test verifies that when both the optin preference is true and collapsed
 * preference is false, the link preview feature will attempt to generate key points.
 */
add_task(async function test_generate_keypoints_when_opted_in() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", true],
      ["browser.ml.linkPreview.collapsed", false],
    ],
  });

  let onDownload, toResolve;
  const stub = sinon
    .stub(LinkPreviewModel, "generateTextAI")
    .callsFake(async (text, options) => {
      onDownload = options.onDownload;
      toResolve = Promise.withResolvers();
      return toResolve.promise;
    });

  const READABLE_PAGE_URL =
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html";

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(READABLE_PAGE_URL, {});

  const panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");

  if (!LinkPreview.canRunOnDevice) {
    const previewCard = panel.querySelector("link-preview-card");
    ok(previewCard, "card still renders the regular preview without a backend");
    await previewCard.updateComplete;
    ok(!previewCard.generating, "card is not generating without a backend");
    ok(
      !previewCard.shadowRoot.querySelector(".keypoints-header"),
      "no key points section without a backend"
    );
    is(stub.callCount, 0, "generateTextAI not called without a backend");
    panel.remove();
    stub.restore();
    LinkPreview.keyboardComboActive = false;
    return;
  }

  is(
    stub.callCount,
    1,
    "generateTextAI should be called when optin=true and collapsed=false"
  );

  const card = panel.querySelector("link-preview-card");

  onDownload(false);
  ok(card, "card created for link preview");
  ok(card.generating, "card should be in generating state");
  is(card.optin, true, "card has optin=true");
  is(card.collapsed, false, "card has collapsed=false");

  if (toResolve) {
    toResolve.resolve();
    await LinkPreview.lastRequest;
  }
  panel.remove();
  stub.restore();
  LinkPreview.keyboardComboActive = false;
});

/**
 * Test that when optin=true and collapsed=true, keypoints are not generated.
 *
 * This test represents a user who has opted in but doesn't want to see key points.
 * It verifies that even with optin=true, if collapsed=true then key points are not generated.
 */
add_task(async function test_no_keypoints_when_opted_in_but_collapsed() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", true], // Opted in
      ["browser.ml.linkPreview.collapsed", true], // But collapsed (don't show keypoints)
    ],
  });

  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  const READABLE_PAGE_URL =
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html";

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(READABLE_PAGE_URL, {});

  const panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");

  is(
    generateStub.callCount,
    0,
    "generateTextAI should not be called when collapsed is true"
  );

  const card = panel.querySelector("link-preview-card");
  ok(card, "card created for link preview");
  ok(!card.generating, "card should not be in generating state");
  is(card.optin, true, "card has optin=true");
  is(card.collapsed, true, "card has collapsed=true");

  panel.remove();
  generateStub.restore();
  LinkPreview.keyboardComboActive = false;
});

/**
 * Test that opt-out action in initial state works correctly.
 *
 * This test verifies that when a user denies the opt-in prompt,
 * the optin preference stays false and the collapsed preference is set to true.
 */
add_task(async function test_model_optin_deny_action() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", false], // Initial state - not opted in
      ["browser.ml.linkPreview.collapsed", false], // Not collapsed
    ],
  });

  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  const READABLE_PAGE_URL =
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html";

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(READABLE_PAGE_URL, {});

  const panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");

  const card = panel.querySelector("link-preview-card");
  ok(card, "card created for link preview");

  if (!LinkPreview.canRunOnDevice) {
    await card.updateComplete;
    ok(
      !card.shadowRoot.querySelector("model-optin"),
      "no opt-in prompt rendered without a backend"
    );
    is(
      generateStub.callCount,
      0,
      "generateTextAI not called without a backend"
    );
    panel.remove();
    generateStub.restore();
    LinkPreview.keyboardComboActive = false;
    Services.prefs.setBoolPref("browser.ml.linkPreview.optin", false);
    Services.prefs.setBoolPref("browser.ml.linkPreview.collapsed", false);
    return;
  }

  const modelOptinElement = await TestUtils.waitForCondition(() => {
    if (card.shadowRoot) {
      return card.shadowRoot.querySelector("model-optin");
    }
    return null;
  }, "Waiting for model-optin element");

  ok(modelOptinElement, "model-optin element is present");

  const optinDenyEvent = new CustomEvent("MlModelOptinDeny", {
    bubbles: true,
    composed: true,
  });
  modelOptinElement.dispatchEvent(optinDenyEvent);

  is(
    Services.prefs.getBoolPref("browser.ml.linkPreview.optin"),
    false,
    "optin preference should remain false after denying"
  );
  is(
    Services.prefs.getBoolPref("browser.ml.linkPreview.collapsed"),
    true,
    "collapsed preference should be true after denying"
  );

  is(
    generateStub.callCount,
    0,
    "generateTextAI should not be called after user denies opt-in"
  );

  panel.remove();
  generateStub.restore();
  LinkPreview.keyboardComboActive = false;
  Services.prefs.setBoolPref("browser.ml.linkPreview.optin", false);
  Services.prefs.setBoolPref("browser.ml.linkPreview.collapsed", false);
});

/**
 * Test that opt-in confirm action in initial state works correctly.
 *
 * This test verifies that when a user confirms the opt-in prompt,
 * the optin preference is set to true and key points generation is triggered.
 */
add_task(async function test_model_optin_confirm_action() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", false], // Initial state - not opted in
      ["browser.ml.linkPreview.collapsed", false], // Not collapsed
    ],
  });

  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  const READABLE_PAGE_URL =
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html";

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(READABLE_PAGE_URL, {});

  const panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");

  const card = panel.querySelector("link-preview-card");
  ok(card, "card created for link preview");

  if (!LinkPreview.canRunOnDevice) {
    await card.updateComplete;
    ok(
      !card.shadowRoot.querySelector("model-optin"),
      "no opt-in prompt rendered without a backend"
    );
    is(
      generateStub.callCount,
      0,
      "generateTextAI not called without a backend"
    );
    panel.remove();
    generateStub.restore();
    LinkPreview.keyboardComboActive = false;
    Services.prefs.setBoolPref("browser.ml.linkPreview.optin", false);
    Services.prefs.setBoolPref("browser.ml.linkPreview.collapsed", false);
    return;
  }

  const modelOptinElement = await TestUtils.waitForCondition(() => {
    if (card.shadowRoot) {
      return card.shadowRoot.querySelector("model-optin");
    }
    return null;
  }, "Waiting for model-optin element");

  ok(modelOptinElement, "model-optin element is present");

  const optinConfirmEvent = new CustomEvent("MlModelOptinConfirm", {
    bubbles: true,
    composed: true,
  });
  modelOptinElement.dispatchEvent(optinConfirmEvent);

  is(
    Services.prefs.getBoolPref("browser.ml.linkPreview.optin"),
    true,
    "optin preference should be set to true after confirming"
  );
  is(
    Services.prefs.getBoolPref("browser.ml.linkPreview.collapsed"),
    false,
    "collapsed preference should remain false after confirming"
  );

  await TestUtils.waitForCondition(
    () => generateStub.callCount > 0,
    "Waiting for generateTextAI to be called"
  );
  Assert.greater(
    generateStub.callCount,
    0,
    "generateTextAI should be called after user confirms opt-in"
  );

  panel.remove();
  generateStub.restore();
  LinkPreview.keyboardComboActive = false;
  Services.prefs.setBoolPref("browser.ml.linkPreview.optin", false);
  Services.prefs.setBoolPref("browser.ml.linkPreview.collapsed", false);
});

/**
 * Test that for a disallowed region, the opt-in card is not shown and
 * key points are hidden.
 *
 * This test verifies that if the current search region is in the list of
 * disallowed regions for key points, the opt-in prompt will not be shown
 * and no attempt will be made to generate key points.
 */
add_task(async function test_no_optin_or_keypoints_in_disallowed_region() {
  const currentRegion = Region.home;

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", false],
      ["browser.ml.linkPreview.collapsed", false],
      ["browser.ml.linkPreview.noKeyPointsRegions", currentRegion],
    ],
  });

  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html",
    {}
  );

  let panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");
  const card = panel.querySelector("link-preview-card");
  ok(card, "card created for link preview");

  ok(
    !LinkPreview.canShowKeyPoints,
    "LinkPreview should indicate key points cannot be shown"
  );

  // Verify that the opt-in element is NOT present
  const modelOptinElement = card.shadowRoot.querySelector("model-optin");
  ok(!modelOptinElement, "model-optin element should NOT be present");

  is(
    generateStub.callCount,
    0,
    "generateTextAI should not be called in a disallowed region"
  );

  panel.remove();
  generateStub.restore();
});

/**
 * Test when the preference is false and locked,  the opt-in prompt will not
 * be shown and no attempt will be made to generate key points.
 */
add_task(async function test_locked_preference() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", false],
      ["browser.ml.linkPreview.collapsed", false],
    ],
  });

  Services.prefs.lockPref("browser.ml.linkPreview.optin");

  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html",
    {}
  );

  let panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");
  const card = panel.querySelector("link-preview-card");
  ok(card, "card created for link preview");

  ok(
    !LinkPreview.canShowKeyPoints,
    "LinkPreview should indicate key points cannot be shown"
  );

  // Verify that the opt-in element is NOT present
  const modelOptinElement = card.shadowRoot.querySelector("model-optin");
  ok(!modelOptinElement, "model-optin element should NOT be present");

  is(
    generateStub.callCount,
    0,
    "generateTextAI should not be called in a disallowed region"
  );

  panel.remove();
  generateStub.restore();

  Services.prefs.unlockPref("browser.ml.linkPreview.optin");
});

/**
 * Test that for an unsupported locale, the opt-in card is not shown and
 * key points are hidden.
 *
 * This test verifies that if the current locale is not supported, the opt-in
 * prompt will not be shown and no attempt will be made to generate key points.
 */
add_task(async function test_no_optin_or_keypoints_in_unsupported_locale() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", false],
      ["browser.ml.linkPreview.collapsed", false],
    ],
  });

  const localeStub = sinon
    .stub(LinkPreview, "_isLocaleSupported")
    .returns(false);
  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html",
    {}
  );

  let panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");
  const card = panel.querySelector("link-preview-card");
  ok(card, "card created for link preview");

  ok(
    !LinkPreview.canShowKeyPoints,
    "LinkPreview should indicate key points cannot be shown"
  );

  // Verify that the opt-in element is NOT present
  const modelOptinElement = card.shadowRoot.querySelector("model-optin");
  ok(!modelOptinElement, "model-optin element should NOT be present");

  is(
    generateStub.callCount,
    0,
    "generateTextAI should not be called in an unsupported locale"
  );

  panel.remove();
  localeStub.restore();
  generateStub.restore();
});

/**
 * Test that when key points are blocked by the AI control preference, the
 * opt-in card is not shown and key points are hidden.
 */
add_task(async function test_blocked_control_hides_keypoints() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", true],
      ["browser.ml.linkPreview.collapsed", false],
      ["browser.ai.control.default", "available"],
      ["browser.ai.control.linkPreviewKeyPoints", "blocked"],
    ],
  });

  const canRunStub = sinon.stub(LinkPreview, "canRunOnDevice").get(() => true);
  const regionStub = sinon
    .stub(LinkPreview, "_isRegionSupported")
    .returns(true);
  const localeStub = sinon
    .stub(LinkPreview, "_isLocaleSupported")
    .returns(true);
  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html",
    {}
  );

  let panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");
  const card = panel.querySelector("link-preview-card");
  ok(card, "card created for link preview");
  await card.updateComplete;

  ok(
    !LinkPreview.canShowKeyPoints,
    "LinkPreview should indicate key points cannot be shown"
  );
  ok(
    !card.shadowRoot.querySelector(".keypoints-header"),
    "key points section is not rendered"
  );
  is(generateStub.callCount, 0, "key point generation is not invoked");
  const modelOptinElement = card.shadowRoot.querySelector("model-optin");
  ok(!modelOptinElement, "model-optin element should NOT be present");

  panel.remove();
  LinkPreview.keyboardComboActive = false;
  generateStub.restore();
  localeStub.restore();
  regionStub.restore();
  canRunStub.restore();
  await SpecialPowers.popPrefEnv();
});

/**
 * Test that the global AI control blocks key points when the feature-specific
 * control is set to "default".
 */
add_task(async function test_global_control_blocks_default_keypoints() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", true],
      ["browser.ml.linkPreview.collapsed", false],
      ["browser.ai.control.default", "blocked"],
      ["browser.ai.control.linkPreviewKeyPoints", "default"],
    ],
  });

  const canRunStub = sinon.stub(LinkPreview, "canRunOnDevice").get(() => true);
  const regionStub = sinon
    .stub(LinkPreview, "_isRegionSupported")
    .returns(true);
  const localeStub = sinon
    .stub(LinkPreview, "_isLocaleSupported")
    .returns(true);
  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html",
    {}
  );

  const panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");
  const card = panel.querySelector("link-preview-card");
  ok(card, "card created for link preview");
  await card.updateComplete;

  ok(
    !LinkPreview.canShowKeyPoints,
    "global blocked control disables key points"
  );
  ok(
    !card.shadowRoot.querySelector(".keypoints-header"),
    "key points section is not rendered"
  );
  is(generateStub.callCount, 0, "key point generation is not invoked");
  const modelOptinElement = card.shadowRoot.querySelector("model-optin");
  ok(!modelOptinElement, "model-optin element should NOT be present");

  panel.remove();
  LinkPreview.keyboardComboActive = false;
  generateStub.restore();
  localeStub.restore();
  regionStub.restore();
  canRunStub.restore();
  await SpecialPowers.popPrefEnv();
});

/**
 * Test that the link preview AI control preference properly updates existing cards.
 */
add_task(async function test_keypoints_control_updates_existing_card() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", true],
      ["browser.ml.linkPreview.collapsed", false],
      ["browser.ai.control.default", "available"],
      ["browser.ai.control.linkPreviewKeyPoints", "default"],
    ],
  });

  const canRunStub = sinon.stub(LinkPreview, "canRunOnDevice").get(() => true);
  const regionStub = sinon
    .stub(LinkPreview, "_isRegionSupported")
    .returns(true);
  const localeStub = sinon
    .stub(LinkPreview, "_isLocaleSupported")
    .returns(true);
  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html",
    {}
  );

  const panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");

  const card = panel.querySelector("link-preview-card");
  ok(card, "card created for link preview");
  await card.updateComplete;
  is(card.canShowKeyPoints, true, "key points initially available");

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ai.control.linkPreviewKeyPoints", "blocked"]],
  });
  await card.updateComplete;
  is(card.canShowKeyPoints, false, "blocked control hides key points");

  await SpecialPowers.popPrefEnv();
  await card.updateComplete;
  is(card.canShowKeyPoints, true, "default control shows key points again");

  panel.remove();
  LinkPreview.keyboardComboActive = false;
  generateStub.restore();
  localeStub.restore();
  regionStub.restore();
  canRunStub.restore();
  await SpecialPowers.popPrefEnv();
});

/**
 * Test that the global AI control preference properly updates existing cards.
 */
add_task(async function test_global_ai_control_updates_existing_card() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", true],
      ["browser.ml.linkPreview.collapsed", false],
      ["browser.ai.control.default", "available"],
      ["browser.ai.control.linkPreviewKeyPoints", "default"],
    ],
  });

  const canRunStub = sinon.stub(LinkPreview, "canRunOnDevice").get(() => true);
  const regionStub = sinon
    .stub(LinkPreview, "_isRegionSupported")
    .returns(true);
  const localeStub = sinon
    .stub(LinkPreview, "_isLocaleSupported")
    .returns(true);
  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html",
    {}
  );

  const panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");

  const card = panel.querySelector("link-preview-card");
  ok(card, "card created for link preview");
  await card.updateComplete;
  is(card.canShowKeyPoints, true, "global available default allows key points");

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ai.control.default", "blocked"]],
  });
  await card.updateComplete;
  is(card.canShowKeyPoints, false, "global blocked default hides key points");

  await SpecialPowers.popPrefEnv();
  await card.updateComplete;
  is(
    card.canShowKeyPoints,
    true,
    "global available default shows key points again"
  );

  panel.remove();
  LinkPreview.keyboardComboActive = false;
  generateStub.restore();
  localeStub.restore();
  regionStub.restore();
  canRunStub.restore();
  await SpecialPowers.popPrefEnv();
});
