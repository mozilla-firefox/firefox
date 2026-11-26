/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async function setup() {
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  let originals = providersManager.providers;
  providersManager.providers = [];
  registerCleanupFunction(() => {
    providersManager.providers = originals;
  });
});

add_task(async function overflow() {
  let row = await openAndGetTargetRow({ title: "0123457890".repeat(50) });
  await assertOverflow(row.querySelector(".urlbarView-title"), true);
  await UrlbarTestUtils.promisePopupClose(window);
});

add_task(async function reuse() {
  let firstRow = await openAndGetTargetRow({ title: "short" });
  await assertOverflow(firstRow.querySelector(".urlbarView-title"), false);

  let secondRow = await openAndGetTargetRow({ title: "0123457890".repeat(50) });
  Assert.equal(firstRow, secondRow, "The row should be reused");
  await assertOverflow(secondRow.querySelector(".urlbarView-title"), true);

  let thirdRow = await openAndGetTargetRow({ title: "short" });
  Assert.equal(firstRow, thirdRow, "The row should be reused");
  await assertOverflow(thirdRow.querySelector(".urlbarView-title"), false);

  await UrlbarTestUtils.promisePopupClose(window);
});

add_task(async function hideAndShow() {
  let row = await openAndGetTargetRow({ title: "0123457890".repeat(50) });
  let title = row.querySelector(".urlbarView-title");
  await assertOverflow(title, true);

  // Hide the row.
  row.toggleAttribute("hidden", true);
  await TestUtils.waitForCondition(
    () => !title.hasAttribute("overflow"),
    "Waiting for the overflow attribute to be dropped while hidden"
  );

  // Show it again.
  row.toggleAttribute("hidden", false);
  await TestUtils.waitForCondition(
    () => title.hasAttribute("overflow"),
    "Waiting for the overflow attribute to be added again"
  );
  await assertOverflow(title, true);

  await UrlbarTestUtils.promisePopupClose(window);
});

async function openAndGetTargetRow({
  url = "https://example.com/test",
  title,
}) {
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  let provider = new UrlbarTestUtils.TestProvider({
    name: "OverflowTestProvider",
    results: [
      new UrlbarResult({
        type: UrlbarShared.RESULT_TYPE.URL,
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        suggestedIndex: 0,
        payload: { url, title },
      }),
    ],
  });
  providersManager.registerProvider(provider);

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "any",
  });
  providersManager.unregisterProvider(provider);

  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    let details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    if (details.url == url) {
      return details.element.row;
    }
  }

  throw new Error("Result not found unexpectedly");
}

async function assertOverflow(element, expected) {
  await TestUtils.waitForCondition(
    () => element.hasAttribute("overflow") == expected,
    `Waiting for the overflow attribute to be ${expected}`
  );
  if (expected) {
    Assert.greater(element.scrollWidth, element.clientWidth);
  } else {
    Assert.lessOrEqual(element.scrollWidth, element.clientWidth);
  }
}
