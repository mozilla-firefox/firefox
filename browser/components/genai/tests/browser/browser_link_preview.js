/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { LinkPreview } = ChromeUtils.importESModule(
  "moz-src:///browser/components/genai/LinkPreview.sys.mjs"
);
const { Region } = ChromeUtils.importESModule(
  "resource://gre/modules/Region.sys.mjs"
);
const { LinkPreviewModel } = ChromeUtils.importESModule(
  "moz-src:///browser/components/genai/LinkPreviewModel.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const TEST_LINK_URL = "https://example.com";

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

  let panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
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
  panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  await BrowserTestUtils.waitForEvent(panel, "popupshown");

  is(generateStub.callCount, 1, "generateTextAI for allowed language");

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
    set: [["browser.ml.linkPreview.enabled", true]],
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

  ok(!LinkPreview.keyboardComboActive, "just shift insufficient");

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
 * Test fetching errors.
 */
add_task(async function test_fetch_errors() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.enabled", true]],
  });
  const actor =
    window.browsingContext.currentWindowContext.getActor("LinkPreview");
  // eslint-disable-next-line @microsoft/sdl/no-insecure-url
  let result = await actor.fetchPageData("http://example.com/");

  ok(result.error, "got error from fetching http");
  is(
    result.error.result,
    Cr.NS_ERROR_UNKNOWN_PROTOCOL,
    "error result should be protocol"
  );

  result = await actor.fetchPageData(
    "https://itisatrap.org/firefox/its-a-trap.html"
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

  LinkPreview.keyboardComboActive = true;
  XULBrowserWindow.setOverLink(READABLE_PAGE_URL, {});

  const panel = await TestUtils.waitForCondition(() =>
    document.getElementById("link-preview-panel")
  );
  ok(panel, "Panel created for link preview");
  let events = Glean.genaiLinkpreview.start.testGetValue();
  is(events[0].extra.cached, "false", "not cached");
  is(events[0].extra.source, "shortcut", "source is shortcut");

  await BrowserTestUtils.waitForEvent(panel, "popupshown");

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

  panel.remove();
  stub.restore();
  LinkPreview.keyboardComboActive = false;
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

  panel.remove();
  LinkPreview.keyboardComboActive = false;
  generateStub.restore();

  Services.prefs.clearUserPref("browser.ml.linkPreview.noKeyPointsRegions");
});
