/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_blocker_is_removed_synchronously() {
  do_get_profile();

  // sEnabled is sampled in the constructor, so the pref has to be set before
  // the service is first instantiated.
  Services.prefs.setBoolPref(
    "privacy.trackingprotection.content.protection.enabled",
    true
  );

  const service = Cc["@mozilla.org/content-classifier-service;1"].getService(
    Ci.nsIContentClassifierService
  );
  Assert.ok(service, "the service is available");

  const blocker = service.QueryInterface(Ci.nsIAsyncShutdownBlocker);
  Assert.equal(
    blocker.name,
    "ContentClassifierService: Shutting down",
    "the blocker reports the name that shows up in crash signatures"
  );
  Assert.equal(
    blocker.state.getProperty("phase"),
    "init-succeeded",
    "the service initialized"
  );

  blocker.blockShutdown(null);

  // Deliberately no event loop spin: the blocker must be gone by the time
  // blockShutdown() returns. Anything else makes shutdown depend on thread
  // scheduling we do not control.
  Assert.equal(
    blocker.state.getProperty("phase"),
    "shutdown-ended",
    "the blocker was removed before blockShutdown() returned"
  );
});
