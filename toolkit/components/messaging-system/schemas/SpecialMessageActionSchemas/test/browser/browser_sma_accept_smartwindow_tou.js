/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_accept_smartwindow_tou() {
  const notified = TestUtils.topicObserved("smartwindow-tou-accepted");
  await SMATestUtils.executeAndValidateAction({
    type: "ACCEPT_SMARTWINDOW_TOU",
  });
  await notified;
  Assert.ok(true, "smartwindow-tou-accepted is notified");
});
