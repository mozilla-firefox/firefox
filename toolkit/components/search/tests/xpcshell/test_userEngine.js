/* Any copyright is dedicated to the Public Domain.
 *    http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Tests that User Engines can be installed correctly.
 */

"use strict";

add_setup(async function () {
  Services.fog.initializeFOG();
  await Services.search.init();
});

add_task(async function test_user_engine() {
  let promiseEngineAdded = SearchTestUtils.promiseSearchNotification(
    SearchUtils.MODIFIED_TYPE.ADDED,
    SearchUtils.TOPIC_ENGINE_MODIFIED
  );
  await Services.search.addUserEngine({
    name: "user",
    url: "https://example.com/user?q={searchTerms}",
    suggestUrl: "https://example.com/suggest?q={searchTerms}",
    alias: "u",
  });
  await promiseEngineAdded;

  let engine = Services.search.getEngineByName("user");
  Assert.ok(engine, "Should have installed the engine.");

  Assert.equal(engine.name, "user", "Should have the correct name");
  Assert.equal(engine.description, null, "Should not have a description");
  Assert.deepEqual(engine.aliases, ["u"], "Should have the correct alias");

  let submission = engine.getSubmission("foo");
  Assert.equal(
    submission.uri.spec,
    "https://example.com/user?q=foo",
    "Should have the correct search url"
  );

  submission = engine.getSubmission("foo", SearchUtils.URL_TYPE.SUGGEST_JSON);
  Assert.equal(
    submission.uri.spec,
    "https://example.com/suggest?q=foo",
    "Should have the correct suggest url"
  );

  Services.search.defaultEngine = engine;

  await assertGleanDefaultEngine({
    normal: {
      providerId: "other",
      partnerCode: "",
      overriddenByThirdParty: false,
      engineId: "other-user",
      displayName: "user",
      loadPath: "[user]",
      submissionUrl: "blank:",
    },
  });
  await Services.search.removeEngine(engine);
});

add_task(async function test_user_engine_rename() {
  let engine = await Services.search.addUserEngine({
    name: "user",
    url: "https://example.com/user?q={searchTerms}",
  });

  let promiseEngineChanged = SearchTestUtils.promiseSearchNotification(
    SearchUtils.MODIFIED_TYPE.CHANGED,
    SearchUtils.TOPIC_ENGINE_MODIFIED
  );
  let success = engine.wrappedJSObject.rename("user2");
  await promiseEngineChanged;
  Assert.ok(true, "Received change notification.");

  Assert.ok(success, "Should have renamed the engine.");
  Assert.equal(engine.name, "user2", "Name was changed.");
  Assert.ok(
    !!Services.search.getEngineByName("user2"),
    "Should be found under the new name."
  );
  Assert.ok(
    !Services.search.getEngineByName("user"),
    "Should not be found under the old name."
  );
  await Services.search.removeEngine(engine);
});

add_task(async function test_user_engine_rename_duplicate() {
  let engine = await Services.search.addUserEngine({
    name: "user",
    url: "https://example.com/user?q={searchTerms}",
  });
  let engine2 = await Services.search.addUserEngine({
    name: "user2",
    url: "https://example.com/user?q={searchTerms}",
  });

  let success = engine.wrappedJSObject.rename("user2");
  Assert.ok(!success, "Engine was not renamed.");
  Assert.equal(engine.name, "user", "Should have kept the name.");

  Assert.notEqual(
    Services.search.getEngineByName("user").id,
    Services.search.getEngineByName("user2").id,
    "Should both be available."
  );

  await Services.search.removeEngine(engine);
  await Services.search.removeEngine(engine2);
});

add_task(async function test_user_engine_changeUrl() {
  let engine = await Services.search.addUserEngine({
    name: "user",
    url: "https://example.com/user?q={searchTerms}",
    alias: "u",
  });

  let submission = engine.getSubmission("foo");
  Assert.equal(
    submission.uri.spec,
    "https://example.com/user?q=foo",
    "Submission URL is correct initially."
  );
  Assert.ok(!submission.postData, "No post data.");

  let promiseEngineChanged = SearchTestUtils.promiseSearchNotification(
    SearchUtils.MODIFIED_TYPE.CHANGED,
    SearchUtils.TOPIC_ENGINE_MODIFIED
  );
  engine.wrappedJSObject.changeUrl(
    SearchUtils.URL_TYPE.SEARCH,
    "https://example.com/user?query={searchTerms}",
    null
  );
  await promiseEngineChanged;
  Assert.ok(true, "Received change notification.");

  submission = engine.getSubmission("foo");
  Assert.equal(
    submission.uri.spec,
    "https://example.com/user?query=foo",
    "Submission URL was changed."
  );
  Assert.ok(!submission.postData, "No post data.");

  engine.wrappedJSObject.changeUrl(
    SearchUtils.URL_TYPE.SEARCH,
    "https://example.com/user",
    "query={searchTerms}"
  );
  submission = engine.getSubmission("foo");
  Assert.equal(
    submission.uri.spec,
    "https://example.com/user",
    "Submission URL was changed."
  );
  Assert.ok(submission.postData, "Has post data.");

  submission = engine.getSubmission("foo", SearchUtils.URL_TYPE.SUGGEST_JSON);
  Assert.ok(!submission, "No suggest URL yet.");

  engine.wrappedJSObject.changeUrl(
    SearchUtils.URL_TYPE.SUGGEST_JSON,
    "https://example.com/suggest?query={searchTerms}",
    "GET"
  );
  submission = engine.getSubmission("foo", SearchUtils.URL_TYPE.SUGGEST_JSON);
  Assert.equal(
    submission.uri.spec,
    "https://example.com/suggest?query=foo",
    "Suggest URL was changed."
  );

  await Services.search.removeEngine(engine);
});
