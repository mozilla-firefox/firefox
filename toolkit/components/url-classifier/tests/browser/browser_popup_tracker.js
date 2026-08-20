/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { UrlClassifierTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlClassifierTestUtils.sys.mjs"
);

const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);

const TRACKER_PATTERN = "*://tracking.example.org/*";
const TRACKER_PAGE = "https://tracking.example.org/" + TEST_PATH + "page.html";
const THIRD_PARTY_PAGE = "https://example.net/" + TEST_PATH + "page.html";
const FEATURE_TRACKING_NAME = "tracking-annotation";
const COLLECTION_NAME = "url-classifier-exceptions";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.trackingprotection.annotate_channels", true],
      ["urlclassifier.trackingAnnotationTable", "tracking-test-digest256"],
      ["privacy.trackingprotection.allow_list.baseline.enabled", true],
      ["privacy.trackingprotection.allow_list.convenience.enabled", true],
    ],
  });

  await UrlClassifierTestUtils.addTestTrackers();
});

function popupIsTracker(popupBrowser) {
  return SpecialPowers.spawn(popupBrowser, [], function () {
    const channel = SpecialPowers.wrap(content).docShell.currentDocumentChannel;
    if (!channel) {
      return false;
    }

    const flags = channel.QueryInterface(
      Ci.nsIClassifiedChannel
    ).firstPartyClassificationFlags;

    return Boolean(flags & Ci.nsIClassifiedChannel.CLASSIFIED_TRACKING);
  });
}

// Opens the popup by navigating straight to the tracker URL.
function openPopupDirectly(target) {
  return SpecialPowers.spawn(target, [TRACKER_PAGE], function (url) {
    content.window.open(url, "TrackerPopup");
  });
}

// Opens an about:blank popup first and only then navigates it to the tracker
// URL, so that the top-level document is still about:blank when the tracker
// load gets classified.
function openPopupViaAboutBlank(target) {
  return SpecialPowers.spawn(target, [TRACKER_PAGE], function (url) {
    let popup = content.window.open("about:blank", "TrackerPopup");
    popup.location.href = url;
  });
}

// The popup inherits the opaque origin of the sandboxed iframe.
function openPopupFromSandboxedIframe(browser) {
  return SpecialPowers.spawn(browser, [TRACKER_PAGE], function (url) {
    let iframe = content.document.createElement("iframe");
    iframe.sandbox = "allow-scripts allow-popups";
    iframe.srcdoc = `<script>
      let popup = window.open("about:blank", "TrackerPopup");
      popup.location.href = ${JSON.stringify(url)};
    <\/script>`;
    content.document.body.appendChild(iframe);
  });
}

// The popup inherits the origin of the third-party iframe, not the origin of
// the top-level opener.
async function openPopupFromThirdPartyIframe(browser) {
  await SpecialPowers.spawn(browser, [THIRD_PARTY_PAGE], async function (url) {
    let iframe = content.document.createElement("iframe");
    iframe.src = url;
    await new Promise(resolve => {
      iframe.addEventListener("load", resolve, { once: true });
      content.document.body.appendChild(iframe);
    });
  });

  await openPopupViaAboutBlank(browser.browsingContext.children[0]);
}

async function checkPopupClassification({ openPopup, expectTracker, message }) {
  await BrowserTestUtils.withNewTab(TEST_PAGE, async function (browser) {
    let popupPromise = BrowserTestUtils.waitForNewTab(
      gBrowser,
      TRACKER_PAGE,
      true
    );

    await openPopup(browser);

    let popupTab = await popupPromise;

    Assert.equal(
      popupTab.linkedBrowser.currentURI.spec,
      TRACKER_PAGE,
      "Popup tab should be navigated to the expected tracker URL"
    );

    Assert.equal(
      await popupIsTracker(popupTab.linkedBrowser),
      expectTracker,
      message
    );

    BrowserTestUtils.removeTab(popupTab);
  });
}

async function waitForExceptionListServiceSynced(urlPattern) {
  info(
    `Waiting for the exception list service to initialize for ${urlPattern}`
  );
  let classifier = Cc["@mozilla.org/url-classifier/dbservice;1"].getService(
    Ci.nsIURIClassifier
  );
  let feature = classifier.getFeatureByName(FEATURE_TRACKING_NAME);
  await TestUtils.waitForCondition(() => {
    let entries = feature.exceptionList.testGetEntries();
    if (urlPattern == null) {
      return entries.length === 0;
    }
    return entries.some(entry => entry.urlPattern === urlPattern);
  }, "Exception list service initialized");
}

// Installs a remote settings exception for the tracker that only applies below
// the given top-level URL pattern, runs aCallback and removes it again.
async function withSiteSpecificException(topLevelUrlPattern, aCallback) {
  let client = RemoteSettings(COLLECTION_NAME);
  let db = client.db;
  await db.clear();

  let entry = await db.create({
    category: "internal-pref",
    urlPattern: TRACKER_PATTERN,
    classifierFeatures: [FEATURE_TRACKING_NAME],
    topLevelUrlPattern,
  });

  await db.importChanges({}, Date.now());
  client.emit("sync", { data: { current: [entry] } });
  await waitForExceptionListServiceSynced(TRACKER_PATTERN);

  try {
    await aCallback();
  } finally {
    await db.clear();
    await db.importChanges({}, Date.now());
    client.emit("sync", { data: { current: [] } });
    await waitForExceptionListServiceSynced(null);
  }
}

add_task(async function test_popup_tracker_classification() {
  await checkPopupClassification({
    openPopup: openPopupDirectly,
    expectTracker: true,
    message: "Popup opened directly should be classified as a tracker",
  });
});

add_task(async function test_popup_tracker_classification_allowlisted() {
  await SpecialPowers.pushPrefEnv({
    set: [["urlclassifier.trackingAnnotationSkipURLs", TRACKER_PATTERN]],
  });

  await checkPopupClassification({
    openPopup: openPopupDirectly,
    expectTracker: false,
    message: "Globally skipped popup should not be classified as a tracker",
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_popup_tracker_href_change_classification() {
  await checkPopupClassification({
    openPopup: openPopupViaAboutBlank,
    expectTracker: true,
    message: "Popup navigated from about:blank should be a tracker",
  });
});

add_task(
  async function test_popup_tracker_href_change_classification_allowlisted() {
    await SpecialPowers.pushPrefEnv({
      set: [["urlclassifier.trackingAnnotationSkipURLs", TRACKER_PATTERN]],
    });

    await checkPopupClassification({
      openPopup: openPopupViaAboutBlank,
      expectTracker: false,
      message: "Globally skipped popup should not be classified as a tracker",
    });

    await SpecialPowers.popPrefEnv();
  }
);

// The about:blank popup inherits the principal of its opener, so the exception
// for the opener's site applies.
add_task(async function test_popup_tracker_site_specific_exception() {
  await withSiteSpecificException("*://example.com/*", async function () {
    await checkPopupClassification({
      openPopup: openPopupViaAboutBlank,
      expectTracker: false,
      message:
        "Popup opened by an exempted top-level site should not be a tracker",
    });
  });
});

// A sandboxed popup has an opaque origin, so no top-level site can be derived
// and the exception must not apply.
add_task(async function test_popup_tracker_sandboxed_iframe() {
  await withSiteSpecificException("*://example.com/*", async function () {
    await checkPopupClassification({
      openPopup: openPopupFromSandboxedIframe,
      expectTracker: true,
      message: "Popup with an opaque origin should still be a tracker",
    });
  });
});

// The popup inherits the third-party iframe's origin, so the exception has to
// be keyed on that origin rather than on the opener's top-level site.
add_task(async function test_popup_tracker_third_party_iframe() {
  await withSiteSpecificException("*://example.com/*", async function () {
    await checkPopupClassification({
      openPopup: openPopupFromThirdPartyIframe,
      expectTracker: true,
      message:
        "Popup opened by a third-party iframe should not use the opener's " +
        "top-level site for the exception",
    });
  });

  await withSiteSpecificException("*://example.net/*", async function () {
    await checkPopupClassification({
      openPopup: openPopupFromThirdPartyIframe,
      expectTracker: false,
      message:
        "Popup opened by an exempted third-party iframe should not be a tracker",
    });
  });
});
