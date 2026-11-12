/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

var doc, tab;

add_setup(async function () {
  info("Starting about:webauthn");
  tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "about:webauthn",
    waitForLoad: true,
  });

  doc = tab.linkedBrowser.contentDocument;
});

registerCleanupFunction(async function () {
  // Close tab.
  await BrowserTestUtils.removeTab(tab);
});

add_task(async function verify_page_no_token() {
  let logging_section = doc.getElementById("logging-section");
  let logging_tab_button = doc.getElementById("logging-tab-button");
  is(
    logging_tab_button.getAttribute("selected"),
    "true",
    "token-info-section not selected by default"
  );
  ok(
    BrowserTestUtils.isVisible(logging_section),
    "logging-section not visible"
  );
  ok(
    BrowserTestUtils.isHidden(doc.getElementById("token-info-section")),
    "token-info-section visible by default"
  );

  let info_tab_button = doc.getElementById("info-tab-button");
  isnot(
    info_tab_button.getAttribute("selected"),
    "true",
    "token-info-section not selected by default"
  );

  // Switching to info-tab
  info_tab_button.click();

  let info_text = doc.getElementById("info-text-div");
  is(info_text.hidden, false, "info-text-div should be visible");
  let info_text_field = doc.getElementById("info-text-field");
  is(
    info_text_field.getAttribute("data-l10n-id"),
    "about-webauthn-text-connect-device",
    "info-text-div has wrong text"
  );
  let categories = doc.getElementById("categories");
  is(categories.hidden, false, "categories-sidebar should be visible");
  is(
    info_tab_button.getAttribute("selected"),
    "true",
    "token-info-section not selected by default"
  );
  let ctap2_info = doc.getElementById("ctap2-token-info");
  is(ctap2_info.style.display, "none", "ctap2-info-table is visible");
});

add_task(async function verify_no_auth_info() {
  let field = doc.getElementById("info-text-field");
  let promise = BrowserTestUtils.waitForMutationCondition(
    field,
    { attributes: true, attributeFilter: ["data-l10n-id"] },
    () =>
      field.getAttribute("data-l10n-id") ===
      "about-webauthn-text-non-ctap2-device"
  );
  Services.obs.notifyObservers(
    null,
    "about-webauthn-prompt",
    JSON.stringify({ type: "selected-device", auth_info: null })
  );
  await promise;

  let info_text = doc.getElementById("info-text-div");
  is(info_text.hidden, false);
});
