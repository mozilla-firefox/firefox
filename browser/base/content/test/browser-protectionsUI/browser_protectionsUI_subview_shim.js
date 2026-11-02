/* eslint-disable mozilla/no-arbitrary-setTimeout */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests the warning and list indicators that are shown in the protections panel
 * subview when a tracking channel is allowed via the
 * "urlclassifier-before-block-channel" event.
 */

// Choose origin so that all tracking origins used are third-parties.
const TRACKING_PAGE =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://example.net/browser/browser/base/content/test/browser-protectionsUI/trackingPage.html";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.trackingprotection.enabled", true],
      ["privacy.trackingprotection.annotate_channels", true],
      ["privacy.trackingprotection.cryptomining.enabled", true],
      ["privacy.trackingprotection.socialtracking.enabled", true],
      ["privacy.trackingprotection.fingerprinting.enabled", true],
      ["privacy.socialtracking.block_cookies.enabled", true],
      // Allowlist trackertest.org loaded by default in trackingPage.html
      ["urlclassifier.trackingSkipURLs", "*://trackertest.org/*"],
      ["urlclassifier.trackingAnnotationSkipURLs", "*://trackertest.org/*"],
      // Additional denylisted hosts.
      [
        "urlclassifier.trackingAnnotationTable.testEntries",
        "tracking.example.com",
      ],
      [
        "urlclassifier.features.cryptomining.blacklistHosts",
        "cryptomining.example.com",
      ],
      [
        "urlclassifier.features.cryptomining.annotate.blacklistHosts",
        "cryptomining.example.com",
      ],
      [
        "urlclassifier.features.fingerprinting.blacklistHosts",
        "fingerprinting.example.com",
      ],
      [
        "urlclassifier.features.fingerprinting.annotate.blacklistHosts",
        "fingerprinting.example.com",
      ],
    ],
  });

  await UrlClassifierTestUtils.addTestTrackers();
  await UrlClassifierTestUtils.addTestHostsFromPrefs();
  registerCleanupFunction(async () => {
    await UrlClassifierTestUtils.cleanupTestTrackers();
  });
});

async function assertSubViewState(category, expectedState) {
  await openProtectionsPanel();

  // Sort the expected state by origin and transform it into an array.
  let expectedStateSorted = Object.keys(expectedState)
    .sort()
    .reduce((stateArr, key) => {
      let obj = expectedState[key];
      obj.origin = key;
      stateArr.push(obj);
      return stateArr;
    }, []);

  if (!expectedStateSorted.length) {
    ok(
      BrowserTestUtils.isVisible(
        document.getElementById(
          "protections-popup-no-trackers-found-description"
        )
      ),
      "No Trackers detected should be shown"
    );
    return;
  }

  let categoryItem = document.getElementById(
    `protections-popup-category-${category}`
  );

  // Explicitly waiting for the category item becoming visible.
  await TestUtils.waitForCondition(() => {
    return BrowserTestUtils.isVisible(categoryItem);
  });

  ok(
    BrowserTestUtils.isVisible(categoryItem),
    `${category} category item is visible`
  );

  ok(!categoryItem.disabled, `${category} category item is enabled`);

  let subView = document.getElementById(`protections-popup-${category}View`);
  let viewShown = BrowserTestUtils.waitForEvent(subView, "ViewShown");
  categoryItem.click();
  await viewShown;

  ok(true, `${category} subView was shown`);

  info("Testing tracker list");

  // Get the listed trackers in the UI and sort them by origin.
  let items = Array.from(
    subView.querySelectorAll(
      `#protections-popup-${category}View-list .protections-popup-list-item`
    )
  ).sort((a, b) => {
    let originA = a.querySelector("label").value;
    let originB = b.querySelector("label").value;
    return originA.localeCompare(originB);
  });

  is(
    items.length,
    expectedStateSorted.length,
    "List has expected amount of entries"
  );

  for (let i = 0; i < expectedStateSorted.length; i += 1) {
    let expected = expectedStateSorted[i];
    let item = items[i];

    let label = item.querySelector(".protections-popup-list-host-label");
    ok(label, "Item has label.");
    is(label.tooltipText, expected.origin, "Label has correct tooltip.");
    is(label.value, expected.origin, "Label has correct text.");

    is(
      item.classList.contains("allowed"),
      !expected.block,
      "Item has allowed class if tracker is not blocked"
    );

    let shimAllowIndicator = item.querySelector(
      ".protections-popup-list-host-shim-allow-indicator"
    );

    if (expected.shimAllow) {
      is(item.childNodes.length, 2, "Item has two childNodes.");
      ok(shimAllowIndicator, "Item has shim allow indicator icon.");
      ok(
        shimAllowIndicator.tooltipText,
        "Shim allow indicator icon has tooltip text"
      );
    } else {
      is(item.childNodes.length, 1, "Item has one childNode.");
      ok(!shimAllowIndicator, "Item does not have shim allow indicator icon.");
    }
  }

  let shimAllowSection = document.getElementById(
    `protections-popup-${category}View-shim-allow-hint`
  );
  ok(shimAllowSection, `Category ${category} has shim-allow hint.`);

  if (Object.values(expectedState).some(entry => entry.shimAllow)) {
    BrowserTestUtils.isVisible(shimAllowSection, "Shim allow hint is visible.");
  } else {
    BrowserTestUtils.isHidden(shimAllowSection, "Shim allow hint is hidden.");
  }

  await closeProtectionsPanel();
}

async function runTestForCategoryAndState(category, action) {
  // Maps the protection categories to the test tracking origins defined in
  // ./trackingAPI.js and the UI class identifiers to look for in the
  // protections UI.
  let categoryToTestData = {
    tracking: {
      apiMessage: "more-tracking",
      origin: "https://itisatracker.org",
      elementId: "trackers",
    },
    socialtracking: {
      origin: "https://social-tracking.example.org",
      elementId: "socialblock",
    },
    cryptomining: {
      // eslint-disable-next-line sdl/no-insecure-url
      origin: "http://cryptomining.example.com",
      elementId: "cryptominers",
    },
    fingerprinting: {
      origin: "https://fingerprinting.example.com",
      elementId: "fingerprinters",
    },
  };

  let promise = BrowserTestUtils.openNewForegroundTab({
    url: TRACKING_PAGE,
    gBrowser,
  });
  // Wait for the tab to load and the initial blocking events from the
  // classifier.
  let [tab] = await Promise.all([promise, waitForContentBlockingEvent()]);

  let {
    origin: trackingOrigin,
    elementId: categoryElementId,
    apiMessage,
  } = categoryToTestData[category];
  if (!apiMessage) {
    apiMessage = category;
  }

  // For allow or replace actions we need to hook into before-block-channel.
  // If we don't hook into the event, the tracking channel will be blocked.
  let beforeBlockChannelPromise;
  if (action != "block") {
    beforeBlockChannelPromise = UrlClassifierTestUtils.handleBeforeBlockChannel(
      {
        filterOrigin: trackingOrigin,
        action,
      }
    );
  }
  // Load the test tracker matching the category.
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [{ apiMessage }],
    function (args) {
      content.postMessage(args.apiMessage, "*");
    }
  );
  await beforeBlockChannelPromise;

  // Next, test if the UI state is correct for the given category and action.
  let expectedState = {};
  expectedState[trackingOrigin] = {
    block: action == "block",
    shimAllow: action == "allow",
  };

  await assertSubViewState(categoryElementId, expectedState);

  BrowserTestUtils.removeTab(tab);
}

/**
 * Test mixed allow/block/replace states for the tracking protection category.
 *
 * @param {object} options - States to test.
 * @param {boolean} options.block - Test tracker block state.
 * @param {boolean} options.allow - Test tracker allow state.
 * @param {boolean} options.replace - Test tracker replace state.
 */
async function runTestMixed({ block, allow, replace }) {
  const ORIGIN_BLOCK = "https://trackertest.org";
  const ORIGIN_ALLOW = "https://itisatracker.org";
  const ORIGIN_REPLACE = "https://tracking.example.com";

  let promise = BrowserTestUtils.openNewForegroundTab({
    url: TRACKING_PAGE,
    gBrowser,
  });

  let [tab] = await Promise.all([promise, waitForContentBlockingEvent()]);

  if (block) {
    // Temporarily remove trackertest.org from the allowlist. The content
    // classifier learns about the skip prefs through the fixture, so republish
    // after each change.
    await SpecialPowers.pushPrefEnv({
      clear: [
        ["urlclassifier.trackingSkipURLs"],
        ["urlclassifier.trackingAnnotationSkipURLs"],
      ],
    });
    await UrlClassifierTestUtils.addTestHostsFromPrefs();
    let blockEventPromise = waitForContentBlockingEvent();
    await SpecialPowers.spawn(tab.linkedBrowser, [], function () {
      content.postMessage("tracking", "*");
    });
    await blockEventPromise;
    await SpecialPowers.popPrefEnv();
    await UrlClassifierTestUtils.addTestHostsFromPrefs();
  }

  if (allow) {
    let promiseEvent = waitForContentBlockingEvent();
    let promiseAllow = UrlClassifierTestUtils.handleBeforeBlockChannel({
      filterOrigin: ORIGIN_ALLOW,
      action: "allow",
    });

    await SpecialPowers.spawn(tab.linkedBrowser, [], function () {
      content.postMessage("more-tracking", "*");
    });

    await promiseAllow;
    await promiseEvent;
  }

  if (replace) {
    let promiseReplace = UrlClassifierTestUtils.handleBeforeBlockChannel({
      filterOrigin: ORIGIN_REPLACE,
      action: "replace",
    });

    await SpecialPowers.spawn(tab.linkedBrowser, [], function () {
      content.postMessage("more-tracking-2", "*");
    });

    await promiseReplace;
  }

  let expectedState = {};

  if (block) {
    expectedState[ORIGIN_BLOCK] = {
      shimAllow: false,
      block: true,
    };
  }

  if (replace) {
    expectedState[ORIGIN_REPLACE] = {
      shimAllow: false,
      block: false,
    };
  }

  if (allow) {
    expectedState[ORIGIN_ALLOW] = {
      shimAllow: true,
      block: false,
    };
  }

  // Check the protection categories subview with the block list.
  await assertSubViewState("trackers", expectedState);

  BrowserTestUtils.removeTab(tab);
}

add_task(async function testNoShim() {
  await runTestMixed({
    allow: false,
    replace: false,
    block: false,
  });
  await runTestMixed({
    allow: false,
    replace: false,
    block: true,
  });
});

add_task(async function testShimAllow() {
  await runTestMixed({
    allow: true,
    replace: false,
    block: false,
  });
  await runTestMixed({
    allow: true,
    replace: false,
    block: true,
  });
});

add_task(async function testShimReplace() {
  await runTestMixed({
    allow: false,
    replace: true,
    block: false,
  });
  await runTestMixed({
    allow: false,
    replace: true,
    block: true,
  });
});

add_task(async function testShimMixed() {
  await runTestMixed({
    allow: true,
    replace: true,
    block: true,
  });
});

add_task(async function testShimCategorySubviews() {
  let categories = [
    "tracking",
    "socialtracking",
    "cryptomining",
    "fingerprinting",
  ];
  for (let category of categories) {
    for (let action of ["block", "allow", "replace"]) {
      info(`Test category subview. category: ${category}, action: ${action}`);
      await runTestForCategoryAndState(category, action);
    }
  }
});
