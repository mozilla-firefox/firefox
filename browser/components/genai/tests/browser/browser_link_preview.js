/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { LinkPreview } = ChromeUtils.importESModule(
  "moz-src:///browser/components/genai/LinkPreview.sys.mjs"
);
const { MLUninstallService } = ChromeUtils.importESModule(
  "chrome://global/content/ml/Utils.sys.mjs"
);
const { Region } = ChromeUtils.importESModule(
  "resource://gre/modules/Region.sys.mjs"
);
const { LinkPreviewModel } = ChromeUtils.importESModule(
  "moz-src:///browser/components/genai/LinkPreviewModel.sys.mjs"
);

const { LinkPreviewChild } = ChromeUtils.importESModule(
  "moz-src:///browser/components/genai/LinkPreviewChild.sys.mjs"
);

const { Readerable } = ChromeUtils.importESModule(
  "resource://gre/modules/Readerable.sys.mjs"
);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const TEST_LINK_URL = "https://example.com";

registerCleanupFunction(() => {
  Services.prefs.clearUserPref("browser.ml.linkPreview.onboardingTimes");
  Services.prefs.clearUserPref("browser.ml.linkPreview.supportedLocales");
});

/**
 * Test that link preview doesn't generate key points for non-English content.
 *
 * This test performs the following steps:
 * 1. Enables the Link Preview feature via the preference `"browser.ml.linkPreview.enabled"`.
 * 2. Stubs the `generateTextAI` method to track if it's called.
 * 3. Simulates pressing the Alt key and hovering over a French-language link.
 * 4. Verifies that the link preview panel is shown but no AI generation is attempted.
 * 5. Checks that the card is properly configured to not show generating/waiting states.
 */
add_task(async function test_skip_generate_if_non_eng() {
  const TEST_LINK_URL_FR =
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableFr.html";
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.enabled", true]],
  });

  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  window.dispatchEvent(
    new KeyboardEvent("keydown", {
      bubbles: true,
      cancelable: true,
      altKey: true,
      shiftKey: true,
    })
  );
  XULBrowserWindow.setOverLink(TEST_LINK_URL_FR);

  let panel = await TestUtils.waitForCondition(
    () => document.getElementById("link-preview-panel"),
    "On first attempt, timed out waiting for link-preview-panel to be created for French link"
  );
  ok(panel, "Panel created for link preview");

  await BrowserTestUtils.waitForEvent(panel, "popupshown");

  is(
    generateStub.callCount,
    0,
    "generateTextAI should not be called when article isn't English"
  );

  const card = panel.querySelector("link-preview-card");
  ok(card, "card created for link preview");
  ok(!card.generating, "card should not be in generating state");

  // Test again with pref allowing French
  panel.remove();

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.allowedLanguages", ""]],
  });
  XULBrowserWindow.setOverLink(TEST_LINK_URL_FR);
  panel = await TestUtils.waitForCondition(
    () => document.getElementById("link-preview-panel"),
    "On second attempt, timed out waiting for link-preview-panel to be created with French allowed"
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");

  is(
    generateStub.callCount,
    LinkPreview.canRunOnDevice ? 1 : 0,
    "generateTextAI called for allowed language only when backend is available"
  );

  panel.remove();
  generateStub.restore();
  LinkPreview.keyboardComboActive = false;
});

/**
 * Tests that the Link Preview feature is correctly triggered when the Shift+Alt
 * key is pressed.
 *
 * This test performs the following steps:
 * 1. Enables the Link Preview feature via the preference `"browser.ml.linkPreview.enabled"`.
 * 2. Creates and dispatches a `keydown` event with the `altKey` property set to `true`.
 * 3. Sets an over link using `XULBrowserWindow.setOverLink`.
 * 4. Verifies that the `_maybeLinkPreview` method of `LinkPreview` is called with the correct window.
 */
add_task(async function test_link_preview_with_shift_alt_key_event() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.shift", true],
    ],
  });

  let stub = sinon.stub(LinkPreview, "_maybeLinkPreview");

  ok(!LinkPreview.keyboardComboActive, "not yet active");

  window.dispatchEvent(
    new KeyboardEvent("keydown", {
      bubbles: true,
      cancelable: true,
      altKey: true,
    })
  );

  ok(!LinkPreview.keyboardComboActive, "just alt insufficient");

  window.dispatchEvent(
    new KeyboardEvent("keydown", {
      bubbles: true,
      cancelable: true,
      shiftKey: true,
    })
  );

  ok(LinkPreview.keyboardComboActive, "just shift is sufficient");

  window.dispatchEvent(
    new KeyboardEvent("keydown", {
      bubbles: true,
      cancelable: true,
      altKey: true,
      ctrlKey: true,
      shiftKey: true,
    })
  );

  ok(!LinkPreview.keyboardComboActive, "extra control also not active");

  let keydownEvent = new KeyboardEvent("keydown", {
    bubbles: true,
    cancelable: true,
    altKey: true,
    shiftKey: true,
  });
  window.dispatchEvent(keydownEvent);

  ok(LinkPreview.keyboardComboActive, "shift+alt active");

  XULBrowserWindow.setOverLink(TEST_LINK_URL, {});

  ok(
    stub.calledWith(window),
    "_maybeLinkPreview was called with the correct window"
  );

  stub.restore();
  Services.prefs.clearUserPref("browser.ml.linkPreview.enabled");
  LinkPreview.keyboardComboActive = false;
});

/**
 * Tests long press of mouse to trigger link preview.
 */
add_task(async function test_link_preview_with_long_press() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.longPressMs", 0],
    ],
  });

  const stub = sinon.stub(LinkPreview, "renderLinkPreviewPanel");

  XULBrowserWindow.setOverLink(TEST_LINK_URL, {});

  is(LinkPreview.cancelLongPress, null, "long press not started");

  window.dispatchEvent(new MouseEvent("mousedown", { button: 1, buttons: 4 }));

  is(LinkPreview.cancelLongPress, null, "long press ignore non-primary button");

  window.dispatchEvent(
    new MouseEvent("mousedown", { buttons: 1, ctrlKey: true })
  );

  is(LinkPreview.cancelLongPress, null, "long press ignore modifier keys");

  window.dispatchEvent(new MouseEvent("mousedown"));

  is(
    LinkPreview.cancelLongPress,
    null,
    "long press ignore activation without a held button"
  );

  window.dispatchEvent(new MouseEvent("mousedown", { buttons: 1 }));

  ok(LinkPreview.cancelLongPress, "long press timer started");

  window.dispatchEvent(new MouseEvent("mouseup"));

  is(LinkPreview.cancelLongPress, null, "long press cancelled");
  is(stub.callCount, 0, "no link preview shown");

  window.dispatchEvent(new MouseEvent("mousedown", { buttons: 1 }));

  await TestUtils.waitForCondition(
    () => stub.callCount,
    "waiting for long press timer"
  );

  is(LinkPreview.cancelLongPress, null, "long press completed");
  is(stub.callCount, 1, "preview shown");
  is(stub.firstCall.args[0], window, "link preview shown for correct window");
  is(stub.firstCall.args[1], TEST_LINK_URL, "preview test link");
  is(stub.firstCall.args[2], "long_press", "source set for long press");

  window.dispatchEvent(new MouseEvent("mouseup"));

  stub.restore();
});

/**
 * Tests that regular typing prevents link preview.
 */
add_task(async function test_link_preview_with_typing() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.shift", true]],
  });

  const stub = sinon.stub(LinkPreview, "renderLinkPreviewPanel");

  XULBrowserWindow.setOverLink(TEST_LINK_URL, {});

  is(LinkPreview.recentTyping, 0, "recent typing unset");

  window.dispatchEvent(
    new KeyboardEvent("keydown", {
      bubbles: true,
      cancelable: true,
      key: ":",
    })
  );

  ok(LinkPreview.recentTyping, "recent typing set");
  ok(!LinkPreview.keyboardComboActive, "typing isn't combo");
  is(stub.callCount, 0, "no link preview shown");

  window.dispatchEvent(
    new KeyboardEvent("keydown", {
      bubbles: true,
      cancelable: true,
      shiftKey: true,
    })
  );

  ok(LinkPreview.keyboardComboActive, "shift is combo");
  is(stub.callCount, 0, "no link preview shown");

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.recentTypingMs", 0]],
  });

  window.dispatchEvent(
    new KeyboardEvent("keydown", {
      bubbles: true,
      cancelable: true,
      shiftKey: true,
    })
  );

  is(stub.callCount, 1, "preview shown without typing delay");

  window.dispatchEvent(
    new KeyboardEvent("keydown", {
      bubbles: true,
      cancelable: true,
      key: "Tab",
      shiftKey: true,
    })
  );

  ok(LinkPreview.recentTyping, "recent typing set for shift-tab");

  stub.restore();
  LinkPreview.recentTyping = 0;
});

/**
 * Tests that certain behaviors do not trigger unexpectedly.
 */
add_task(async function test_link_preview_no_trigger() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.shift", true]],
  });

  const stub = sinon.stub(LinkPreview, "renderLinkPreviewPanel");

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(TEST_LINK_URL, {});

  ok(LinkPreview.overLinkTime, "have some time");
  is(stub.callCount, 1, "preview shown");

  LinkPreview.overLinkTime -= 10000;

  window.dispatchEvent(
    new KeyboardEvent("keydown", {
      bubbles: true,
      cancelable: true,
      shiftKey: true,
    })
  );

  is(stub.callCount, 1, "ignored for stale link");

  XULBrowserWindow.setOverLink(TEST_LINK_URL, {});

  is(stub.callCount, 2, "shown again");

  XULBrowserWindow.setOverLink(TEST_LINK_URL, {});

  is(stub.callCount, 3, "and again");

  XULBrowserWindow.setOverLink(TEST_LINK_URL + "#", {});

  is(stub.callCount, 3, "ignored single page #");

  XULBrowserWindow.setOverLink("javascript:void(0)", {});

  is(stub.callCount, 3, "ignored single page javascript:");

  stub.restore();
});

/**
 * Tests that no event is dispatched when the Link Preview feature is disabled, even if the Alt key is pressed.
 *
 * This test performs the following steps:
 * 1. Disables the Link Preview feature via the preference `"browser.ml.linkPreview.enabled"`.
 * 2. Creates and dispatches a `keydown` event with the `altKey` property set to `true`.
 * 3. Sets an over link using `XULBrowserWindow.setOverLink`.
 * 4. Verifies that the `_maybeLinkPreview` method of `LinkPreview` is not called.
 */
add_task(async function test_no_event_triggered_when_disabled_with_alt_key() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.enabled", false]],
  });

  let stub = sinon.stub(LinkPreview, "_maybeLinkPreview");

  let keydownEvent = new KeyboardEvent("keydown", {
    bubbles: true,
    cancelable: true,
    altKey: true,
    shiftKey: true,
  });
  window.dispatchEvent(keydownEvent);

  XULBrowserWindow.setOverLink(TEST_LINK_URL, {});

  ok(
    !stub.called,
    "_maybeLinkPreview should not be called when the feature is disabled"
  );

  stub.restore();
  Services.prefs.clearUserPref("browser.ml.linkPreview.enabled");
  LinkPreview.keyboardComboActive = false;
});

/**
 * Test that page data is fetched.
 */
add_task(async function test_fetch_page_data() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.enabled", true]],
  });
  const actor =
    window.browsingContext.currentWindowContext.getActor("LinkPreview");
  const result = await actor.fetchPageData(
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html"
  );

  ok(result.article, "article should be populated");
  is(result.article.byline, "by Jane Doe", "byline should be correct");
  is(result.article.title, "Article title", "title should be correct");
  ok(result.rawMetaInfo, "rawMetaInfo should be populated");
  ok(result.meta.imageUrl, "imageUrl should be populated");
  ok(result.meta, "meta should be populated");
  ok(result.urlComponents, "urlComponents should be populated");
  is(
    result.meta.description,
    "This is the article description.",
    "description should be correct"
  );
  is(
    result.rawMetaInfo["html:title"],
    "Article title",
    "title from raw metainfo should be correct"
  );
  is(result.meta.title, "Article title", "title should be correct");
  is(
    result.meta.imageUrl,
    "https://example.com/article-image.jpg",
    "imageUrl should be correct"
  );
  is(
    result.urlComponents.domain,
    "example.com",
    "url domain should be correct"
  );
  is(
    result.urlComponents.filename,
    "readableEn.html",
    "url filename should be correct"
  );
});

/**
 * Test that Shift-JIS encoding is handled correctly.
 */
add_task(async function test_fetch_shift_jis() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.enabled", true]],
  });
  const actor =
    window.browsingContext.currentWindowContext.getActor("LinkPreview");
  const result = await actor.fetchPageData(
    "https://example.com/browser/browser/components/genai/tests/browser/data/encodingWithShiftJIS.html"
  );

  ok(!result.error, "should not have an error");
  is(
    result.rawMetaInfo["html:title"],
    "Shift-JIS テスト",
    "title should be correct"
  );
});

/**
 * Test fetching errors.
 */
add_task(async function test_fetch_errors() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.enabled", true]],
  });
  const actor =
    window.browsingContext.currentWindowContext.getActor("LinkPreview");
  // eslint-disable-next-line sdl/no-insecure-url
  let result = await actor.fetchPageData("http://example.com/");

  ok(result.error, "got error from fetching http");
  is(
    result.error.result,
    Cr.NS_ERROR_UNKNOWN_PROTOCOL,
    "error result should be protocol"
  );

  result = await actor.fetchPageData(
    "https://www.itisatrap.org/firefox/its-a-trap.html"
  );
  ok(result.error, "got error from fetching trap");
  is(
    result.error.result,
    Cr.NS_ERROR_PHISHING_URI,
    "error result should be phishing"
  );

  result = await actor.fetchPageData("https://unknown.host.example.com");
  ok(result.error, "got error from fetching host");
  is(
    result.error.result,
    Cr.NS_ERROR_UNKNOWN_HOST,
    "error result should be host"
  );
});

/**
 * Test that link preview panel is shown.
 */
add_task(async function test_link_preview_panel_shown() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.enabled", true]],
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

  LinkPreview.keyboardComboActive = "shift";
  XULBrowserWindow.setOverLink(READABLE_PAGE_URL, {});

  const panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  ok(panel, "Panel created for link preview");
  let events = Glean.genaiLinkpreview.start.testGetValue();
  is(events[0].extra.cached, "false", "not cached");
  is(events[0].extra.source, "shift", "source is keyboard combo");
  is(events[0].extra.tab, "blank", "tab context is blank");

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

  is(stub.callCount, 1, "would have generated key points");
  events = Glean.genaiLinkpreview.fetch.testGetValue();
  is(events[0].extra.description, "true", "got description");
  is(events[0].extra.image, "true", "no image");
  is(events[0].extra.length, "7200", "got length");
  is(events[0].extra.outcome, "success", "got outcome");
  is(events[0].extra.sitename, "false", "no site name");
  is(events[0].extra.skipped, "false", "not skipped");
  ok(events[0].extra.time, "got time");
  is(events[0].extra.title, "true", "got title");
  is(events[0].extra.tab, "blank", "tab context is blank");

  const card = panel.querySelector("link-preview-card");
  ok(card, "card created for link preview");
  ok(card.generating, "initially marked as generating");
  is(card.progress, -1, "initially assume not waiting");
  is(LinkPreview.progress, -1, "initially assume not downloading");

  onDownload(true);

  is(card.progress, 0, "switched to waiting when download initiates");
  is(LinkPreview.progress, 0, "shared waiting for download");

  onDownload(true, 42);

  is(card.progress, 42, "percentage reflected in card");
  is(LinkPreview.progress, 42, "shared progress updated");

  onDownload(false);

  is(card.progress, -1, "no longer waiting after download complete");
  is(LinkPreview.progress, -1, "downloading updated");
  ok(card.generating, "still generating");

  toResolve.resolve();
  await LinkPreview.lastRequest;

  ok(!card.generating, "done generating");
  events = Glean.genaiLinkpreview.generate.testGetValue();
  ok(events[0].extra.delay, "got delay");
  ok(events[0].extra.download, "got download");
  is(events[0].extra.outcome, "success", "got outcome");
  is(events[0].extra.sentences, "0", "got sentences");
  ok(events[0].extra.time, "got time");

  panel.hidePopup();

  events = Glean.genaiLinkpreview.cardClose.testGetValue();
  ok(events[0].extra.duration, "got duration");
  is(events[0].extra.tab, "blank", "tab context is blank");

  panel.remove();
  stub.restore();
  LinkPreview.keyboardComboActive = false;
});

/**
 * Test that LinkPreview blocks pages on domains that don't support Reader Mode
 */
add_task(async function test_reader_mode_blocked_domains() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.enabled", true]],
  });

  const fetchHTML = async url => {
    const response = await fetch(url, { method: "GET" });
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    return await response.text(); // returns raw HTML as string
  };

  const stub = sinon
    .stub(LinkPreviewChild.prototype, "fetchHTML")
    .callsFake(async _ => {
      return fetchHTML(
        "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html"
      );
    });

  const actor =
    window.browsingContext.currentWindowContext.getActor("LinkPreview");

  let result;

  Assert.greaterOrEqual(
    Readerable._blockedHosts.length,
    2,
    "we have enough in blockedHosts"
  );

  for (const url of Readerable._blockedHosts) {
    if (url === "github.com") {
      continue;
    }
    result = await actor.fetchPageData(url);
    Assert.deepEqual(
      result.article,
      {},
      `article should be empty for url ${url}`
    );
    ok(result.meta, "meta should be populated");

    is(
      result.rawMetaInfo["html:title"],
      "Article title",
      "title from raw metainfo should be correct"
    );
  }

  stub.restore();
  Services.prefs.clearUserPref("browser.ml.linkPreview.enabled");
});

/**
 * Test that link preview panel doesn't generate key points when URL is not readable.
 */
add_task(async function test_skip_keypoints_generation_if_url_not_readable() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.enabled", true]],
  });

  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  window.dispatchEvent(
    new KeyboardEvent("keydown", {
      bubbles: true,
      cancelable: true,
      altKey: true,
      shiftKey: true,
    })
  );
  XULBrowserWindow.setOverLink(TEST_LINK_URL, {});

  const panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  ok(panel, "Panel created for link preview");

  await BrowserTestUtils.waitForEvent(panel, "popupshown");

  is(
    generateStub.callCount,
    0,
    "generateTextAI should not be called when article has no text content"
  );

  const card = panel.querySelector("link-preview-card");
  ok(card, "card created for link preview");
  ok(!card.generating, "card should not be in generating state");
  is(card.progress, -1, "card should not be in downloading state");
  is(LinkPreview.progress, -1, "not downloading model");

  panel.remove();
  generateStub.restore();
  LinkPreview.keyboardComboActive = false;
});

/**
 * Test that the Link Preview feature does not generate key points in disallowed regions.
 *
 * This test performs the following steps:
 * 1. Sets the current region as a disallowed region for key point generation via the preference `"browser.ml.linkPreview.noKeyPointsRegions"`.
 * 2. Enables the Link Preview feature via the preference `"browser.ml.linkPreview.enabled"`.
 * 3. Stubs the `generateTextAI` method to track if it's called.
 * 4. Activates the keyboard combination for link preview and sets an over link.
 * 5. Verifies that the link preview panel is shown but no AI generation is attempted.
 * 6. Ensures that the `generateTextAI` method is not called when the region is disallowed.
 * 7. Cleans up by removing the panel, restoring the stub, and clearing the user preference
 *    `"browser.ml.linkPreview.noKeyPointsRegions"` to avoid affecting other tests.
 */
add_task(async function test_no_key_points_in_disallowed_region() {
  const currentRegion = Region.home;

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
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

  is(
    generateStub.callCount,
    0,
    "generateTextAI should not be called when region is disallowed"
  );
  ok(!LinkPreview.canShowKeyPoints, "should not show key points");

  panel.remove();
  LinkPreview.keyboardComboActive = false;
  generateStub.restore();

  Services.prefs.clearUserPref("browser.ml.linkPreview.noKeyPointsRegions");

  is(
    LinkPreview.canShowKeyPoints,
    LinkPreview.canRunOnDevice,
    "key points track hardware support once the region is allowed"
  );
});

/**
 * Test that no key points are generated and no key points UI is rendered when
 * the device hardware cannot run the llama.cpp backend, while the rest of the
 * preview card still renders.
 */
add_task(async function test_no_key_points_on_unsupported_hardware() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.enabled", true]],
  });

  const canRunStub = sinon.stub(LinkPreview, "canRunOnDevice").get(() => false);
  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  ok(
    !LinkPreview.canShowKeyPoints,
    "should not show key points on unsupported hardware"
  );

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
    "generateTextAI should not be called on unsupported hardware"
  );

  const card = panel.querySelector("link-preview-card");
  ok(card, "card still renders the regular preview");
  await card.updateComplete;
  ok(
    !card.shadowRoot.querySelector(".keypoints-header"),
    "key points section should not be rendered"
  );

  panel.remove();
  LinkPreview.keyboardComboActive = false;
  generateStub.restore();
  canRunStub.restore();

  is(
    LinkPreview.canShowKeyPoints,
    LinkPreview.canRunOnDevice,
    "key points track hardware support once the stub is restored"
  );
});

/**
 * Test that .og-error-message element is rendered with correct error messages
 * given different props set on the card
 */
add_task(async function test_link_preview_error_rendered() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.enabled", true]],
  });

  const READABLE_PAGE_URL =
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html";

  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(READABLE_PAGE_URL, {});

  const panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");

  const card = panel.querySelector("link-preview-card");
  ok(card, "Found link-preview-card in panel");

  if (!LinkPreview.canRunOnDevice) {
    await card.updateComplete;
    ok(
      !card.shadowRoot.querySelector(".keypoints-header"),
      "no key points section without a backend"
    );
    ok(
      !card.shadowRoot.querySelector(".og-error-message"),
      "no key points error message rendered without a backend"
    );
    panel.remove();
    generateStub.restore();
    LinkPreview.keyboardComboActive = false;
    return;
  }

  // Check that errors are off by default.
  ok(
    !card.isMissingDataErrorState,
    "Should not be missing data error initially"
  );

  ok(!card.generationError, "Should not have generation error initially");

  // Force a "missing data" error and confirm the card updates.
  card.isMissingDataErrorState = true;
  await TestUtils.waitForCondition(() =>
    card.shadowRoot.querySelector(".og-error-message")
  );
  let ogErrorEl1 = card.shadowRoot.querySelector(".og-error-message");
  ok(ogErrorEl1, "og-error-message shown with isMissingDataErrorState = true");
  is(
    ogErrorEl1.getAttribute("data-l10n-id"),
    "link-preview-generation-error-missing-data-v2",
    "Correct fluent ID for missing data error"
  );

  // Switch to a "generation error"
  card.isMissingDataErrorState = false;
  card.generationError = { name: "UnexpectedError" };
  await TestUtils.waitForCondition(() =>
    card.shadowRoot.querySelector(".og-error-message")
  );
  let ogErrorEl2 = card.shadowRoot.querySelector(".og-error-message");
  ok(ogErrorEl2, "og-error-message shown with generationError set");

  is(
    ogErrorEl2.getAttribute("data-l10n-id"),
    "link-preview-generation-error-unexpected",
    "Correct fluent ID for generation error"
  );
  is(
    ogErrorEl2.textContent.trim(),
    "Something went wrong.",
    "Correct localized message for generation error"
  );

  card.generationError = { name: "NotEnoughMemoryError" };
  await TestUtils.waitForCondition(() =>
    card.shadowRoot.querySelector(".og-error-message")
  );
  ok(
    !card.shadowRoot.querySelector(".retry-link"),
    "Retry link should not show with NotEnoughMemoryError"
  );
  // Cleanup
  panel.remove();
  generateStub.restore();
  LinkPreview.keyboardComboActive = false;
});

/**
 * Test that settings icon is correctly rendered in the link preview card.
 */
add_task(async function test_link_preview_settings_icon_rendered() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.enabled", true]],
  });

  const generateStub = sinon.stub(LinkPreviewModel, "generateTextAI");
  const openPreferencesStub = sinon.stub(window, "openPreferences");

  const READABLE_PAGE_URL =
    "https://example.com/browser/browser/components/genai/tests/browser/data/readableEn.html";

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(READABLE_PAGE_URL, {});

  // Wait for panel to appear
  const panel = await TestUtils.waitForCondition(
    () => document.getElementById("link-preview-panel"),
    "Timed out waiting for link-preview-panel"
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");

  const card = panel.querySelector("link-preview-card");
  ok(card, "Card created for link preview");

  await TestUtils.waitForCondition(
    () =>
      card.shadowRoot.querySelector(
        "moz-button[data-l10n-id='link-preview-settings-button']"
      ),
    "Timed out waiting for settings button"
  );

  const settingsButton = card.shadowRoot.querySelector(
    "moz-button[data-l10n-id='link-preview-settings-button']"
  );

  ok(settingsButton, "Settings button is rendered");

  is(
    settingsButton.getAttribute("type"),
    "icon ghost",
    "Settings button has correct type"
  );

  settingsButton.click();

  ok(
    openPreferencesStub.calledWith("general-link-preview"),
    "openPreferences called with correct argument when settings button clicked"
  );

  // Clean up
  panel.remove();
  generateStub.restore();
  openPreferencesStub.restore();
  LinkPreview.keyboardComboActive = false;
});

/**
 * Test that clicking on the keypoints header properly toggles the expanded/collapsed state
 * and updates the preference.
 */
add_task(async function test_toggle_expand_collapse() {
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
  ok(card, "Card created for link preview");

  if (!LinkPreview.canRunOnDevice) {
    await card.updateComplete;
    ok(
      !card.shadowRoot.querySelector(".keypoints-header"),
      "no key points header to toggle without a backend"
    );
    is(
      generateStub.callCount,
      0,
      "generateTextAI not called without a backend"
    );
    panel.remove();
    generateStub.restore();
    LinkPreview.keyboardComboActive = false;
    return;
  }

  is(card.collapsed, false, "Card should start expanded");
  is(
    generateStub.callCount,
    1,
    "generateTextAI should be called initially when collapsed is false"
  );

  const keypointsHeader = card.shadowRoot.querySelector(".keypoints-header");
  ok(keypointsHeader, "Found keypoints header");

  keypointsHeader.click();

  is(
    card.collapsed,
    true,
    "Card should now be collapsed after clicking header"
  );

  is(
    Services.prefs.getBoolPref("browser.ml.linkPreview.collapsed"),
    true,
    "Preference should be updated to collapsed=true"
  );

  keypointsHeader.click();

  is(
    card.collapsed,
    false,
    "Card should now be expanded after clicking header again"
  );

  is(
    Services.prefs.getBoolPref("browser.ml.linkPreview.collapsed"),
    false,
    "Preference should be updated to collapsed=false"
  );

  // Clean up
  panel.remove();
  generateStub.restore();
  LinkPreview.keyboardComboActive = false;
});

/**
 * Test that the Link Preview feature does not generate key points in unsupported locales.
 */
add_task(async function test_no_key_points_in_unsupported_locale() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", true],
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
  ok(card, "Card created for link preview");

  is(
    generateStub.callCount,
    0,
    "generateTextAI should not be called when locale is not supported"
  );
  ok(!LinkPreview.canShowKeyPoints, "should not show key points");

  panel.remove();
  LinkPreview.keyboardComboActive = false;
  localeStub.restore();
  generateStub.restore();
});

/**
 * Test that the Link Preview feature does generate key points in supported locales.
 */
add_task(async function test_key_points_in_supported_locale() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", true],
    ],
  });

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
  ok(card, "Card created for link preview");

  is(
    generateStub.callCount,
    LinkPreview.canRunOnDevice ? 1 : 0,
    "generateTextAI called for a supported locale only when backend is available"
  );
  is(
    LinkPreview.canShowKeyPoints,
    LinkPreview.canRunOnDevice,
    "key points track hardware support in a supported locale"
  );

  panel.remove();
  LinkPreview.keyboardComboActive = false;
  localeStub.restore();
  generateStub.restore();
});
