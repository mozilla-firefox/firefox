/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ContentRelevancyManager:
    "moz-src:///toolkit/components/contentrelevancy/ContentRelevancyManager.sys.mjs",
  Interest:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustRelevancy.sys.mjs",
  InterestVector:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustRelevancy.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

const PREF_CONTENT_RELEVANCY_ENABLED = "toolkit.contentRelevancy.enabled";

let gSandbox;
let gFakeStore;
let gFakeRustRelevancyStore;

const TEST_INTEREST_VECTOR = new InterestVector({
  animals: 50,
  arts: 0,
  autos: 0,
  business: 0,
  career: 0,
  education: 50,
  fashion: 0,
  finance: 0,
  food: 0,
  government: 0,
  hobbies: 0,
  home: 0,
  news: 0,
  realEstate: 0,
  society: 0,
  sports: 0,
  tech: 0,
  travel: 0,
  inconclusive: 50,
});

const SINGLE_INTEREST_HIT = [Interest.ANIMALS];
const MULTI_INTEREST_HITS = [Interest.ANIMALS, Interest.EDUCATION];
const INTEREST_MISSES = [Interest.SPORTS, Interest.NEWS];
const INTEREST_INCONCLUSIVE = [Interest.INCONCLUSIVE];
// Original encoding for `Interest.EDUCATION`
const SINGLE_INTEREST_HIT_ORIGINAL_ENCODING = [6];
const INTEREST_INCONCLUSIVE_ORIGINAL_ENCODING = [0];
const INVALID_INTEREST = [-1];

add_setup(() => {
  gSandbox = sinon.createSandbox();
  Services.prefs.setBoolPref(PREF_CONTENT_RELEVANCY_ENABLED, true);
  gFakeStore = {
    close: gSandbox.fake(),
    userInterestVector: gSandbox.stub(),
  };
  gFakeRustRelevancyStore = {
    init: gSandbox.fake.returns(gFakeStore),
  };

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(PREF_CONTENT_RELEVANCY_ENABLED);
    ContentRelevancyManager.uninit();
    gSandbox.restore();
  });
});

add_task(async function test_getUserInterestVector() {
  gFakeStore.userInterestVector.resolves(TEST_INTEREST_VECTOR);

  ContentRelevancyManager.init(gFakeRustRelevancyStore);
  const vector = await ContentRelevancyManager.getUserInterestVector();

  Assert.deepEqual(
    TEST_INTEREST_VECTOR,
    vector,
    "Should return the vector from the store"
  );

  ContentRelevancyManager.uninit();
  gSandbox.restore();
});

add_task(async function test_getUserInterestVector_uninitialized() {
  const vector = await ContentRelevancyManager.getUserInterestVector();

  Assert.equal(
    null,
    vector,
    "Should return null when the manager is uninitialized"
  );
});

add_task(async function test_getUserInterestVector() {
  gFakeStore.userInterestVector.throws();

  ContentRelevancyManager.init(gFakeRustRelevancyStore);
  const vector = await ContentRelevancyManager.getUserInterestVector();

  Assert.equal(
    null,
    vector,
    "Should return null when it fails to fetch from the store."
  );

  ContentRelevancyManager.uninit();
  gSandbox.restore();
});

add_task(async function test_score() {
  gFakeStore.userInterestVector.resolves(TEST_INTEREST_VECTOR);

  ContentRelevancyManager.init(gFakeRustRelevancyStore);

  const scoreSingleHit =
    await ContentRelevancyManager.score(SINGLE_INTEREST_HIT);

  Assert.greater(
    scoreSingleHit,
    0,
    "Single interest hit should yield a positive score"
  );

  const scoreMultiHits =
    await ContentRelevancyManager.score(MULTI_INTEREST_HITS);

  Assert.greater(
    scoreMultiHits,
    scoreSingleHit,
    "Multiple interest hits should yield a higher relevance score"
  );

  const scoreMiss = await ContentRelevancyManager.score(INTEREST_MISSES);

  Assert.equal(0, scoreMiss, "Interest misses should yield relevance score 0");

  const scoreInconclusive = await ContentRelevancyManager.score(
    INTEREST_INCONCLUSIVE
  );

  Assert.equal(
    0,
    scoreInconclusive,
    "Interest INCONCLUSIVE should yield relevance score 0"
  );

  ContentRelevancyManager.uninit();
  gSandbox.restore();
});

add_task(async function test_score_uninitialized() {
  await Assert.rejects(
    ContentRelevancyManager.score(),
    /User interest vector not ready/,
    "Should throw if the manager is uninitialized"
  );
});

add_task(async function test_score_invalid_interests() {
  gFakeStore.userInterestVector.resolves(TEST_INTEREST_VECTOR);

  ContentRelevancyManager.init(gFakeRustRelevancyStore);

  await Assert.rejects(
    ContentRelevancyManager.score(INVALID_INTEREST),
    /Invalid interest value/,
    "Should throw for invalid interest"
  );

  ContentRelevancyManager.uninit();
  gSandbox.restore();
});

add_task(async function test_score_with_interest_adjustment() {
  gFakeStore.userInterestVector.resolves(TEST_INTEREST_VECTOR);

  ContentRelevancyManager.init(gFakeRustRelevancyStore);

  const score = await ContentRelevancyManager.score(
    SINGLE_INTEREST_HIT_ORIGINAL_ENCODING
  );

  Assert.greater(
    score,
    0,
    "Single interest hit w/ original encoding should yield a positive score"
  );

  const scoreInconclusive = await ContentRelevancyManager.score(
    INTEREST_INCONCLUSIVE_ORIGINAL_ENCODING
  );

  Assert.equal(
    0,
    scoreInconclusive,
    "Interest INCONCLUSIVE w/ original encoding should yield relevance score 0"
  );

  ContentRelevancyManager.uninit();
  gSandbox.restore();
});
