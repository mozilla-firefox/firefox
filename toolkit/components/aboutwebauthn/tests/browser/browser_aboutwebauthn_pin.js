/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const ABOUT_URL = "about:webauthn";

var doc, tab;

async function send_authinfo_and_open_pin_section(ops) {
  reset_about_page(doc);
  send_auth_info_and_check_categories(doc, ops);

  ["credentials-tab-button", "bio-enrollments-tab-button"].forEach(
    button_id => {
      let button = doc.getElementById(button_id);
      is(
        button.style.display,
        "none",
        button_id + " in the sidebar not hidden"
      );
    }
  );

  if (ops.clientPin !== null) {
    let pin_tab_button = doc.getElementById("pin-tab-button");
    // Check if PIN section is visible
    isnot(
      pin_tab_button.style.display,
      "none",
      "PIN button in the sidebar not visible"
    );

    // Click the section and wait for it to open
    let pin_section = doc.getElementById("set-change-pin-section");
    pin_tab_button.click();
    ok(BrowserTestUtils.isVisible(pin_section), "PIN section not visible");
    is(
      pin_tab_button.getAttribute("selected"),
      "true",
      "PIN section button not selected"
    );
  }
}

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

add_task(async function pin_not_supported() {
  // Not setting clientPIN at all should lead to not showing it in the sidebar
  send_authinfo_and_open_pin_section({ uv: true, clientPin: null });
  // Check if PIN section is invisible
  let pin_tab_button = doc.getElementById("pin-tab-button");
  is(pin_tab_button.style.display, "none", "PIN button in the sidebar visible");
});

add_task(async function pin_already_set() {
  send_authinfo_and_open_pin_section({ clientPin: true });

  let set_pin_button = doc.getElementById("set-pin-button");
  is(set_pin_button.style.display, "none", "Set PIN button visible");

  let change_pin_button = doc.getElementById("change-pin-button");
  isnot(
    change_pin_button.style.display,
    "none",
    "Change PIN button not visible"
  );

  let current_pin_div = doc.getElementById("current-pin-div");
  is(current_pin_div.hidden, false, "Current PIN field not visible");

  // Test that the button is only active if both inputs are set the same
  let new_pin = doc.getElementById("new-pin");
  let new_pin_repeat = doc.getElementById("new-pin-repeat");
  let current_pin = doc.getElementById("current-pin");

  set_text(new_pin, "abcdefg");
  is(change_pin_button.disabled, true, "Change PIN button not disabled");
  set_text(new_pin_repeat, "abcde");
  is(change_pin_button.disabled, true, "Change PIN button not disabled");
  set_text(new_pin_repeat, "abcdefg");
  is(change_pin_button.disabled, true, "Change PIN button not disabled");
  set_text(current_pin, "1234567");
  is(change_pin_button.disabled, false, "Change PIN button disabled");
});

add_task(async function pin_not_yet_set() {
  send_authinfo_and_open_pin_section({ clientPin: false });

  let set_pin_button = doc.getElementById("set-pin-button");
  isnot(set_pin_button.style.display, "none", "Set PIN button not visible");

  let change_pin_button = doc.getElementById("change-pin-button");
  is(change_pin_button.style.display, "none", "Change PIN button visible");

  let current_pin_div = doc.getElementById("current-pin-div");
  is(current_pin_div.hidden, true, "Current PIN field visible");

  // Test that the button is only active if both inputs are set the same
  let new_pin = doc.getElementById("new-pin");
  let new_pin_repeat = doc.getElementById("new-pin-repeat");

  set_text(new_pin, "abcdefg");
  is(set_pin_button.disabled, true, "Set PIN button not disabled");
  set_text(new_pin_repeat, "abcde");
  is(set_pin_button.disabled, true, "Set PIN button not disabled");
  set_text(new_pin_repeat, "abcdefg");
  is(set_pin_button.disabled, false, "Set PIN button disabled");
});

add_task(async function pin_switch_back_and_forth() {
  // This will click the PIN section button
  send_authinfo_and_open_pin_section({ clientPin: false });

  let pin_tab_button = doc.getElementById("pin-tab-button");
  let pin_section = doc.getElementById("set-change-pin-section");
  let info_tab_button = doc.getElementById("info-tab-button");
  let info_section = doc.getElementById("token-info-section");

  // a11y-tree is racy here, so we have to wait a tick for it to get up to date
  await TestUtils.waitForTick();
  // Now click the "info"-button and verify the correct buttons are highlighted
  info_tab_button.click();
  // await info_promise;
  ok(BrowserTestUtils.isVisible(info_section), "info section not visible");
  is(
    info_tab_button.getAttribute("selected"),
    "true",
    "Info tab button not selected"
  );
  isnot(
    pin_tab_button.getAttribute("selected"),
    "true",
    "PIN tab button selected"
  );

  // Click back to the PIN section
  pin_tab_button.click();
  ok(BrowserTestUtils.isVisible(pin_section), "PIN section not visible");
  is(
    pin_tab_button.getAttribute("selected"),
    "true",
    "PIN tab button not selected"
  );
  isnot(
    info_tab_button.getAttribute("selected"),
    "true",
    "Info button selected"
  );
});

add_task(async function invalid_pin() {
  send_authinfo_and_open_pin_section({ clientPin: true });
  let pin_tab_button = doc.getElementById("pin-tab-button");
  // Click the section and wait for it to open
  pin_tab_button.click();
  is(
    pin_tab_button.getAttribute("selected"),
    "true",
    "PIN section button not selected"
  );

  let change_pin_button = doc.getElementById("change-pin-button");

  // Test that the button is only active if both inputs are set the same
  let new_pin = doc.getElementById("new-pin");
  let new_pin_repeat = doc.getElementById("new-pin-repeat");
  let current_pin = doc.getElementById("current-pin");

  // Needed to activate change_pin_button
  set_text(new_pin, "abcdefg");
  set_text(new_pin_repeat, "abcdefg");
  set_text(current_pin, "1234567");

  // This should silently error out since we have no authenticator
  change_pin_button.click();

  // Now we fake a response from the authenticator, saying the PIN was invalid
  let pin_required = doc.getElementById("pin-required-section");
  let msg = JSON.stringify({ type: "pin-required" });
  Services.obs.notifyObservers(null, "about-webauthn-prompt", msg);
  ok(
    BrowserTestUtils.isVisible(pin_required),
    "PIN required dialog not visible"
  );

  let info_tab_button = doc.getElementById("info-tab-button");
  is(
    info_tab_button.classList.contains("disabled-category"),
    true,
    "Sidebar not disabled"
  );

  let pin_field = doc.getElementById("pin-required");
  let send_pin_button = doc.getElementById("send-pin-button");

  set_text(pin_field, "654321");
  send_pin_button.click();

  ok(
    BrowserTestUtils.isHidden(pin_required),
    "PIN required dialog did not disappear"
  );
  let pin_section = doc.getElementById("set-change-pin-section");
  ok(BrowserTestUtils.isVisible(pin_section), "PIN section did not reappear");
});
