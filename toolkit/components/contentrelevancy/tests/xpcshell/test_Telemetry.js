/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ContentRelevancyManager } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/contentrelevancy/ContentRelevancyManager.sys.mjs"
);

const PREF_CONTENT_RELEVANCY_ENABLED = "toolkit.contentRelevancy.enabled";

add_setup(async function setup() {
  // FOG needs a profile directory to put its data in.
  do_get_profile();

  // FOG needs to be initialized in order for data to flow.
  Services.fog.initializeFOG();

  Services.prefs.setBoolPref(PREF_CONTENT_RELEVANCY_ENABLED, true);
  ContentRelevancyManager.init();

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(PREF_CONTENT_RELEVANCY_ENABLED);
  });
});

/**
 * Test classification metrics - succeed.
 */
add_task(async function test_classify_succeed() {
  Services.fog.testResetFOG();

  Assert.equal(null, Glean.relevancyClassify.succeed.testGetValue());
  Assert.equal(null, Glean.relevancyClassify.duration.testGetValue());

  await ContentRelevancyManager._test_doClassification();

  Assert.deepEqual(
    {
      input_size: 0,
      input_classified_size: 0,
      input_inconclusive_size: 0,
      output_interest_size: 0,
      interest_top_1_hits: 0,
      interest_top_2_hits: 0,
      interest_top_3_hits: 0,
    },
    Glean.relevancyClassify.succeed.testGetValue()[0].extra,
    "Should record the succeed event"
  );
  Assert.greater(
    Glean.relevancyClassify.duration.testGetValue().sum,
    0,
    "Should record the duration"
  );
});

/**
 * Test classification metrics - fail - insufficient-input.
 */
add_task(async function test_classify_fail_case1() {
  Services.fog.testResetFOG();

  Assert.equal(null, Glean.relevancyClassify.fail.testGetValue());
  Assert.equal(null, Glean.relevancyClassify.duration.testGetValue());

  // Require at least 1 input to trigger the failure.
  await ContentRelevancyManager._test_doClassification({ minUrlsForTest: 1 });

  Assert.deepEqual(
    {
      reason: "insufficient-input",
    },
    Glean.relevancyClassify.fail.testGetValue()[0].extra,
    "Should record the fail event"
  );
  Assert.equal(
    null,
    Glean.relevancyClassify.duration.testGetValue(),
    "Should not record the duration"
  );
});
