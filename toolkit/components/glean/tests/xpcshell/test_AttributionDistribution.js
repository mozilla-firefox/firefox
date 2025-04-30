/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

add_setup(
  /* on Android FOG is set up through head.js */
  { skip_if: () => AppConstants.platform == "android" },
  function test_setup() {
    // FOG needs a profile directory to put its data in.
    do_get_profile();

    // We need to initialize it once, otherwise operations will be stuck in the pre-init queue.
    Services.fog.initializeFOG();
  }
);

add_task(function test_attribution_works() {
  // Ensure we aren't racing Glean init.
  // (Remove upon vendoring of a fix to bug 1959515).
  Glean.testOnly.balloons.testGetValue();

  let attr = Services.fog.testGetAttribution();
  Assert.deepEqual(
    attr,
    {
      source: null,
      medium: null,
      campaign: null,
      term: null,
      content: null,
    },
    "Initial attribution should be empty."
  );

  Services.fog.updateAttribution("source", null, "campaign", null, "content");

  let expected = {
    source: "source",
    medium: null,
    campaign: "campaign",
    term: null,
    content: "content",
  };
  attr = Services.fog.testGetAttribution();
  Assert.deepEqual(attr, expected, "Must give what it got.");
});

add_task(function test_distribution_works() {
  // Ensure we aren't racing Glean init.
  // (Remove upon vendoring of a fix to bug 1959515).
  Glean.testOnly.balloons.testGetValue();

  let dist = Services.fog.testGetDistribution();
  Assert.deepEqual(
    dist,
    { name: null },
    "Initial distribution should be empty."
  );

  Services.fog.updateDistribution("name");

  dist = Services.fog.testGetDistribution();
  Assert.deepEqual(dist, { name: "name" }, "Must give what it got.");
});
