/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests that search terms don't persist with navigating to non-SERP URIs.
// Normally, when setURI is called, dependent properties for persisted search
// like originalURI are set to a different value and the userTypedValue is
// nullified. But, we should ensure that if setURI receives a URI with a
// different origin from the originalURI, search terms will not persist.

const SEARCH_STRING = "chocolate cake";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.showSearchTerms.featureGate", true]],
  });
  let cleanup = await installPersistTestEngines();
  registerCleanupFunction(async function () {
    await PlacesUtils.history.clear();
    cleanup();
  });
});

add_task(async function test_search_terms_cleared_on_non_serp_host() {
  let { tab } = await searchWithTab(SEARCH_STRING);

  let nonSerpUri = Services.io.newURI("https://www.foo.com/");
  let originalURI = tab.linkedBrowser.originalURI;

  Assert.equal(
    originalURI.scheme,
    nonSerpUri.scheme,
    "Test URIs should have the same scheme."
  );

  Assert.notEqual(
    originalURI.host,
    nonSerpUri.host,
    "Test URIs should have a different host."
  );

  gURLBar.setURI({ uri: nonSerpUri });

  Assert.ok(
    !gURLBar.hasAttribute("persistsearchterms"),
    "Should not persist when setURI is called with a different host."
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_search_terms_cleared_on_non_serp_scheme() {
  let { tab } = await searchWithTab(SEARCH_STRING);

  let nonSerpUri = Services.io.newURI("foo://www.example.com/");
  let originalURI = tab.linkedBrowser.originalURI;

  Assert.notEqual(
    originalURI.scheme,
    nonSerpUri.scheme,
    "Test URIs should have a different scheme."
  );

  Assert.equal(
    originalURI.host,
    nonSerpUri.host,
    "Test URIs should have the same host."
  );

  gURLBar.setURI({ uri: nonSerpUri });

  Assert.ok(
    !gURLBar.hasAttribute("persistsearchterms"),
    "Should not persist when setURI is called with a different scheme."
  );

  BrowserTestUtils.removeTab(tab);
});
