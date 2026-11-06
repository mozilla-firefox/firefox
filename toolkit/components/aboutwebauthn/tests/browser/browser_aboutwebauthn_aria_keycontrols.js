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
  let ops = {
    credMgmt: true,
    clientPin: true,
    bioEnroll: true,
  };
  send_auth_info_and_check_categories(doc, ops);
});

registerCleanupFunction(async function () {
  // Close tab.
  await BrowserTestUtils.removeTab(tab);
});

add_task(async function moving_up_and_down() {
  doc.getElementById("info-tab-button").focus();
  [
    ["UP", "DOWN"],
    ["LEFT", "RIGHT"],
  ].forEach(dirs => {
    let up = dirs[0];
    let down = dirs[1];
    EventUtils.sendKey(down);
    is(
      doc.activeElement,
      doc.getElementById("pin-tab-button"),
      "Wrong element active"
    );
    EventUtils.sendKey(down);
    is(
      doc.activeElement,
      doc.getElementById("credentials-tab-button"),
      "Wrong element active"
    );
    EventUtils.sendKey(down);
    is(
      doc.activeElement,
      doc.getElementById("bio-enrollments-tab-button"),
      "Wrong element active"
    );
    // Trying to go down further should do nothing
    EventUtils.sendKey(down);
    is(
      doc.activeElement,
      doc.getElementById("bio-enrollments-tab-button"),
      "Wrong element active"
    );
    EventUtils.sendKey(down);
    is(
      doc.activeElement,
      doc.getElementById("bio-enrollments-tab-button"),
      "Wrong element active"
    );

    // Going back up again
    EventUtils.sendKey(up);
    is(
      doc.activeElement,
      doc.getElementById("credentials-tab-button"),
      "Wrong element active"
    );
    EventUtils.sendKey(up);
    is(
      doc.activeElement,
      doc.getElementById("pin-tab-button"),
      "Wrong element active"
    );
    EventUtils.sendKey(up);
    is(
      doc.activeElement,
      doc.getElementById("info-tab-button"),
      "Wrong element active"
    );
    // Trying to go up further should do nothing
    EventUtils.sendKey(up);
    is(
      doc.activeElement,
      doc.getElementById("info-tab-button"),
      "Wrong element active"
    );
    EventUtils.sendKey(up);
    is(
      doc.activeElement,
      doc.getElementById("info-tab-button"),
      "Wrong element active"
    );
  });
});

add_task(async function switching_sections() {
  doc.getElementById("info-tab-button").focus();
  EventUtils.sendKey("DOWN"); // Pin section
  EventUtils.sendKey("DOWN"); // Credentials section
  EventUtils.sendKey("RETURN");

  let credentials_section = doc.getElementById("credential-management-section");
  ok(
    BrowserTestUtils.isVisible(credentials_section),
    "credentials section not visible"
  );

  EventUtils.sendKey("UP"); // PIN section
  EventUtils.sendKey("RETURN");

  let pin_section = doc.getElementById("set-change-pin-section");
  ok(BrowserTestUtils.isVisible(pin_section), "pin section not visible");

  EventUtils.sendKey("DOWN"); // Credentials section
  EventUtils.sendChar(" "); // Try using Space instead of Return

  ok(
    BrowserTestUtils.isVisible(credentials_section),
    "credentials section not visible"
  );
});

add_task(async function jumping_sections() {
  doc.getElementById("info-tab-button").focus();
  EventUtils.sendKey("DOWN"); // Pin section
  EventUtils.sendKey("DOWN"); // Credentials section
  is(
    doc.activeElement,
    doc.getElementById("credentials-tab-button"),
    "Wrong element active"
  );

  EventUtils.sendKey("HOME");
  is(
    doc.activeElement,
    doc.getElementById("info-tab-button"),
    "Wrong element active"
  );

  // Another hit of the Home-key should change nothing
  EventUtils.sendKey("HOME");
  is(
    doc.activeElement,
    doc.getElementById("info-tab-button"),
    "Wrong element active"
  );

  EventUtils.sendKey("END");
  is(
    doc.activeElement,
    doc.getElementById("bio-enrollments-tab-button"),
    "Wrong element active"
  );

  EventUtils.sendKey("HOME");
  is(
    doc.activeElement,
    doc.getElementById("info-tab-button"),
    "Wrong element active"
  );

  EventUtils.sendKey("DOWN"); // Pin section
  EventUtils.sendKey("DOWN"); // Credentials section
  is(
    doc.activeElement,
    doc.getElementById("credentials-tab-button"),
    "Wrong element active"
  );

  EventUtils.sendKey("END");
  is(
    doc.activeElement,
    doc.getElementById("bio-enrollments-tab-button"),
    "Wrong element active"
  );

  // Another hit of the "End"-key should change nothing
  EventUtils.sendKey("END");
  is(
    doc.activeElement,
    doc.getElementById("bio-enrollments-tab-button"),
    "Wrong element active"
  );
});
