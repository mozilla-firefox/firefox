/* Any copyright is dedicated to the Public Domain.
 *    http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Tests that OpenSearch engines are installed and set up correctly.
 *
 * Note: simple.xml, post.xml, suggestion.xml and suggestion-alternate.xml
 * all use different namespaces to reflect the possibitities that may be
 * installed.
 * mozilla-ns.xml uses the mozilla namespace.
 */

"use strict";

const tests = [
  {
    file: "simple.xml",
    name: "simple",
    description: "A small test engine",
    searchUrl: "https://example.com/search?q=foo",
  },
  {
    file: "post.xml",
    name: "Post",
    description: "",
    searchUrl: "https://example.com/post",
    searchPostData: "searchterms=foo",
  },
  {
    file: "suggestion.xml",
    name: "suggestion",
    description: "A small engine with suggestions",
    queryCharset: "windows-1252",
    searchUrl: "https://example.com/search?q=foo",
    suggestUrl: "https://example.com/suggest?suggestion=foo",
  },
  {
    file: "suggestion-alternate.xml",
    name: "suggestion-alternate",
    description: "A small engine with suggestions",
    searchUrl: "https://example.com/search?q=foo",
    suggestUrl: "https://example.com/suggest?suggestion=foo",
  },
  {
    file: "mozilla-ns.xml",
    name: "mozilla-ns",
    description: "An engine using mozilla namespace",
    // mozilla-ns.xml also specifies a MozParam. However, they were only
    // valid for app-provided engines, and hence the param should not show
    // here.
    searchUrl: "https://example.com/search?q=foo",
  },
];

add_setup(async function () {
  Services.fog.initializeFOG();
  useHttpServer();
  await Services.search.init();
});

for (const test of tests) {
  add_task(async () => {
    info(`Testing ${test.file}`);
    let promiseEngineAdded = SearchTestUtils.promiseSearchNotification(
      SearchUtils.MODIFIED_TYPE.ADDED,
      SearchUtils.TOPIC_ENGINE_MODIFIED
    );
    let engine = await SearchTestUtils.installOpenSearchEngine({
      url: `${gHttpURL}/opensearch/${test.file}`,
    });
    await promiseEngineAdded;
    Assert.ok(engine, "Should have installed the engine.");

    Assert.equal(engine.name, test.name, "Should have the correct name");

    Assert.equal(
      engine.wrappedJSObject._loadPath,
      `[http]localhost/${test.file}`
    );

    Assert.equal(
      engine.queryCharset,
      test.queryCharset ?? SearchUtils.DEFAULT_QUERY_CHARSET,
      "Should have the expected query charset"
    );

    let submission = engine.getSubmission("foo");
    Assert.equal(
      submission.uri.spec,
      test.searchUrl,
      "Should have the correct search url"
    );

    if (test.searchPostData) {
      let sis = Cc["@mozilla.org/scriptableinputstream;1"].createInstance(
        Ci.nsIScriptableInputStream
      );
      sis.init(submission.postData);
      let data = sis.read(submission.postData.available());
      Assert.equal(
        decodeURIComponent(data),
        test.searchPostData,
        "Should have received the correct POST data"
      );
    } else {
      Assert.equal(
        submission.postData,
        null,
        "Should have not received any POST data"
      );
    }

    submission = engine.getSubmission("foo", SearchUtils.URL_TYPE.SUGGEST_JSON);
    if (test.suggestUrl) {
      Assert.equal(
        submission.uri.spec,
        test.suggestUrl,
        "Should have the correct suggest url"
      );
    } else {
      Assert.equal(submission, null, "Should not have a suggestion url");
    }
  });
}

add_task(async function test_telemetry_reporting() {
  // Use an engine from the previous tests.
  let engine = Services.search.getEngineByName("simple");
  Services.search.defaultEngine = engine;

  await assertGleanDefaultEngine({
    normal: {
      providerId: "other",
      partnerCode: "",
      overriddenByThirdParty: false,
      engineId: "other-simple",
      displayName: "simple",
      loadPath: "[http]localhost/simple.xml",
      submissionUrl: "blank:",
    },
  });
});
