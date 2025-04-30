/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// This test verifies that searching filters the features to just that subset that
// contains the search terms.
add_task(async function testFilterFeatures() {
  const recipes = [
    {
      ...DEFAULT_LABS_RECIPES[0],
      slug: "test-featureA",
    },
    {
      ...DEFAULT_LABS_RECIPES[1],
      slug: "test-featureB",
      firefoxLabsGroup: "experimental-features-group-customize-browsing",
    },
    {
      ...DEFAULT_LABS_RECIPES[2],
      slug: "test-featureC",
      targeting: "true",
      firefoxLabsGroup: "experimental-features-group-customize-browsing",
    },
    {
      ...DEFAULT_LABS_RECIPES[3],
      slug: "test-featureD",
      bucketConfig: NimbusTestUtils.factories.recipe.bucketConfig,
    },
  ];
  const cleanup = await setupLabsTest(recipes);

  await SpecialPowers.pushPrefEnv({
    set: [["browser.preferences.experimental", true]],
  });

  await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:preferences#paneExperimental"
  );
  const doc = gBrowser.contentDocument;

  await TestUtils.waitForCondition(
    () => doc.querySelector(".featureGate"),
    "wait for the first public feature to get added to the DOM"
  );

  const definitions = [
    {
      id: "test-featureA",
      title: "Experimental Feature 1",
      description: "This is a fun experimental feature you can enable",
      group: "experimental-features-group-customize-browsing",
      result: true,
    },
    {
      id: "test-featureB",
      title: "Experimental Thing 2",
      description: "This is a very boring experimental tool",
      group: "experimental-features-group-webpage-display",
      result: true,
    },
    {
      id: "test-featureC",
      title: "Experimental Thing 3",
      description: "This is a fun experimental feature for you can enable",
      group: "experimental-features-group-developer-tools",
      result: true,
    },
    {
      id: "test-featureD",
      title: "Experimental Thing 4",
      description: "This is not a checkbox that you should be enabling",
      group: "experimental-features-group-developer-tools",
      result: false,
    },
  ];

  // Manually modify the labels of the features that were just added, so that the test
  // can rely on consistent search terms.
  for (const definition of definitions) {
    const mainItem = doc.getElementById(definition.id);
    mainItem.label = definition.title;
    mainItem.removeAttribute("data-l10n-id");
    const descItem = doc.getElementById(`${definition.id}-description`);
    descItem.textContent = definition.description;
    descItem.removeAttribute("data-l10n-id");
  }

  // First, check that all of the items are visible by default.
  for (const definition of definitions) {
    checkVisibility(
      doc.getElementById(definition.id),
      true,
      `${definition.id} should be initially visible`
    );
  }

  // After searching, only a subset should be visible.
  await enterSearch(doc, "feature");

  for (const definition of definitions) {
    checkVisibility(
      doc.getElementById(definition.id),
      definition.result,
      `${definition.id} should be ${
        definition.result ? "visible" : "hidden"
      } after first search`
    );
    info(`Text for item was: ${doc.getElementById(definition.id).textContent}`);
  }

  // Reset the search entirely.
  const searchInput = doc.getElementById("searchInput");
  searchInput.value = "";
  searchInput.doCommand();

  // Clearing the search will go to the general pane so switch back to the experimental pane.
  EventUtils.synthesizeMouseAtCenter(
    doc.getElementById("category-experimental"),
    {},
    gBrowser.contentWindow
  );

  for (const definition of definitions) {
    checkVisibility(
      doc.getElementById(definition.id),
      true,
      `${definition.id} should be visible after search cleared`
    );
  }

  // Simulate entering a search and then clicking one of the category labels. The search
  // should reset each time.
  for (const category of ["category-search", "category-experimental"]) {
    await enterSearch(doc, "feature");

    for (const definition of definitions) {
      checkVisibility(
        doc.getElementById(definition.id),
        definition.result,
        `${definition.id} should be ${
          definition.result ? "visible" : "hidden"
        } after next search`
      );
    }

    // Check that switching to a non-find-in-page category changes item
    // visibility appropriately.
    EventUtils.synthesizeMouseAtCenter(
      doc.getElementById(category),
      {},
      gBrowser.contentWindow
    );

    // Ensure that async passes of localization and any code waiting for
    // those passes have finished running.
    await new Promise(r =>
      requestAnimationFrame(() => requestAnimationFrame(r))
    );
    const shouldShow = category == "category-experimental";
    for (const definition of definitions) {
      checkVisibility(
        doc.getElementById(definition.id),
        shouldShow,
        `${definition.id} should be ${
          shouldShow ? "visible" : "hidden"
        } after category change to ${category}`
      );
    }
  }

  BrowserTestUtils.removeTab(gBrowser.selectedTab);

  await cleanup();
});

function checkVisibility(element, expected, desc) {
  return expected
    ? is_element_visible(element, desc)
    : is_element_hidden(element, desc);
}

function enterSearch(doc, query) {
  let searchInput = doc.getElementById("searchInput");
  searchInput.focus();

  let searchCompletedPromise = BrowserTestUtils.waitForEvent(
    gBrowser.contentWindow,
    "PreferencesSearchCompleted",
    evt => evt.detail == query
  );

  EventUtils.sendString(query);

  return searchCompletedPromise;
}
