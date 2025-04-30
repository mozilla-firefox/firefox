/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_URI = "data:text/html;charset=utf-8,<h1>Test";

let created = false;

add_task(async function test_data_channel_observer() {
  setupObserver();
  let tab = await BrowserTestUtils.addTab(gBrowser, TEST_URI);
  await BrowserTestUtils.waitForCondition(() => created);
  ok(created, "We received observer notification");
  await BrowserTestUtils.removeTab(tab);
});

function setupObserver() {
  const observer = {
    QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),

    observe: function observe(subject, topic) {
      switch (topic) {
        case "data-channel-opened": {
          ok(
            subject instanceof Ci.nsIDataChannel,
            "Channel should be a nsIDataChannel instance"
          );
          let channelURI = subject.QueryInterface(Ci.nsIChannel).URI.spec;
          if (channelURI === TEST_URI) {
            Services.obs.removeObserver(observer, "data-channel-opened");
            created = true;
          }
          break;
        }
      }
    },
  };
  Services.obs.addObserver(observer, "data-channel-opened");
}
