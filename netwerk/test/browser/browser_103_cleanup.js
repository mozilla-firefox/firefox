/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const BASE_URL = "https://example.com/browser/netwerk/test/browser/";

add_task(async function test_103_cancel_parent_connect() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["network.early-hints.enabled", true],
      ["network.early-hints.parent-connect-timeout", 1],
    ],
  });

  // The preload must be cancelled by the parent-connect timeout, never adopted
  // by the content process. Without this the test would silently go back to
  // racing the timeout if early hints ever start being processed here.
  let connectedBack = false;
  let observer = () => {
    connectedBack = true;
  };
  Services.obs.addObserver(observer, "earlyhints-connectback");
  registerCleanupFunction(() => {
    Services.obs.removeObserver(observer, "earlyhints-connectback");
  });

  let observed = TestUtils.topicObserved("http-on-stop-request", subject => {
    subject = subject.QueryInterface(Ci.nsIChannel);
    return subject.URI.spec == BASE_URL + "square2.png";
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: BASE_URL + "103_cleanup.sjs", waitForLoad: true },
    () => {}
  );

  let [subject] = await observed;
  Assert.equal(
    subject.QueryInterface(Ci.nsIChannel).canceledReason,
    "parent-connect-timeout"
  );
  Assert.ok(!connectedBack, "the content process must not connect back");
});
