/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

var doc, tab;

registerCleanupFunction(async function () {
  // Close tab.
  await BrowserTestUtils.removeTab(tab);
});

add_setup(async function () {
  info("Starting about:webauthn");
  tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "about:webauthn",
    waitForLoad: true,
  });
  doc = tab.linkedBrowser.contentDocument;
});

function send_log_data(direction, protocol, msg) {
  let data = JSON.stringify({
    direction,
    protocol,
    data: msg,
  });
  Services.obs.notifyObservers(null, "webauthn-logging", data);
}

add_task(async function test_logging_nothing() {
  let start_logging = doc.getElementById("start-logging-button");
  let stop_logging = doc.getElementById("stop-logging-button");

  ok(!start_logging.disabled, "Start enabled initially");
  ok(stop_logging.disabled, "Stop disabled initially");

  start_logging.click();

  ok(start_logging.disabled, "Start disabled while logging");
  ok(!stop_logging.disabled, "Stop enabled while logging");

  let info_tab_button = doc.getElementById("info-tab-button");
  is(
    info_tab_button.classList.contains("disabled-category"),
    true,
    "Sidebar not disabled"
  );
  stop_logging.click();

  ok(!start_logging.disabled, "Start re-enabled after stopping");
  ok(stop_logging.disabled, "Stop disabled after stopping");

  is(
    info_tab_button.classList.contains("disabled-category"),
    false,
    "Sidebar not re-enabled"
  );

  let logging_section = doc.getElementById("webauthn-log");
  is(
    logging_section.textContent,
    "",
    "Log contains data when it should be empty"
  );
});

add_task(async function test_logging_while_stopped() {
  is(
    doc.getElementById("webauthn-log").textContent,
    "",
    "Log contains data when it should be empty"
  );

  send_log_data("request", "CTAP2", "foo");
  send_log_data("response", "CTAP2", "bar");

  is(
    doc.getElementById("webauthn-log").textContent,
    "",
    "Log contains data when it should be empty"
  );
});

add_task(async function test_logging_start_stop() {
  let start_logging = doc.getElementById("start-logging-button");
  let stop_logging = doc.getElementById("stop-logging-button");

  start_logging.click();

  send_log_data("request", "CTAP2", "foo");
  send_log_data("response", "CTAP2", "bar");

  is(
    doc.getElementById("webauthn-log").textContent,
    "\n--> [CTAP2] foo\n\n<-- [CTAP2] bar\n",
    "Log contains wrong data"
  );

  stop_logging.click();

  // Should not be logged anymore
  send_log_data("request", "CTAP2", "baz");

  is(
    doc.getElementById("webauthn-log").textContent,
    "\n--> [CTAP2] foo\n\n<-- [CTAP2] bar\n",
    "Log contains wrong data"
  );
  // Resetting
  doc.getElementById("webauthn-log").textContent = "";
});

add_task(async function test_logging_clear_button() {
  let start_logging = doc.getElementById("start-logging-button");
  let stop_logging = doc.getElementById("stop-logging-button");
  let clear_logging = doc.getElementById("clear-logging-button");

  is(
    doc.getElementById("webauthn-log").textContent,
    "",
    "Log contains data when it should be empty"
  );

  clear_logging.click();

  is(
    doc.getElementById("webauthn-log").textContent,
    "",
    "Log contains data when it should be empty"
  );

  start_logging.click();

  send_log_data("request", "CTAP2", "foo");
  send_log_data("response", "CTAP2", "bar");

  is(
    doc.getElementById("webauthn-log").textContent,
    "\n--> [CTAP2] foo\n\n<-- [CTAP2] bar\n",
    "Log contains wrong data"
  );

  clear_logging.click();

  is(
    doc.getElementById("webauthn-log").textContent,
    "",
    "Log contains data when it should be empty"
  );

  send_log_data("request", "CTAP1", "foo");

  is(
    doc.getElementById("webauthn-log").textContent,
    "\n--> [CTAP1] foo\n",
    "Log contains wrong data"
  );

  stop_logging.click();

  clear_logging.click();

  is(
    doc.getElementById("webauthn-log").textContent,
    "",
    "Log contains data when it should be empty"
  );
});
