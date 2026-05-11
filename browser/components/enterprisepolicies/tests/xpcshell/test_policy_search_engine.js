/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const { SearchTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SearchTestUtils.sys.mjs"
);
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);
var { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

Services.prefs.setBoolPref("browser.search.log", true);
SearchTestUtils.init(this);

AddonTestUtils.init(this, false);
AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "48",
  "48"
);

add_setup(async () => {
  await AddonTestUtils.promiseStartupManager();
  await SearchService.init();
  console.log("done init");
});

add_task(async function test_install_and_set_default() {
  // Make sure we are starting in an expected state to avoid false positive
  // test results.
  Assert.notEqual(
    (await SearchService.getDefault()).name,
    "MozSearch",
    "Default search engine should not be MozSearch when test starts"
  );
  Assert.equal(
    SearchService.getEngineByName("Foo"),
    null,
    'Engine "Foo" should not be present when test starts'
  );

  await setupPolicyEngineWithJsonForSearch({
    policies: {
      SearchEngines: {
        Add: [
          {
            Name: "MozSearch",
            URLTemplate: "http://example.com/?q={searchTerms}",
          },
        ],
        Default: "MozSearch",
      },
    },
  });
  // Get in line, because the Search policy callbacks are async.
  await TestUtils.waitForTick();

  // If this passes, it means that the new search engine was properly installed
  // *and* was properly set as the default.
  Assert.equal(
    (await SearchService.getDefault()).name,
    "MozSearch",
    "Specified search engine should be the default"
  );

  // Clean up
  await setupPolicyEngineWithJsonForSearch({});
  EnterprisePolicyTesting.resetRunOnceState();
});

add_task(async function test_install_and_set_default_private() {
  // Make sure we are starting in an expected state to avoid false positive
  // test results.
  Assert.notEqual(
    (await SearchService.getDefaultPrivate()).name,
    "MozSearch",
    "Default search engine should not be MozSearch when test starts"
  );
  Assert.equal(
    SearchService.getEngineByName("Foo"),
    null,
    'Engine "Foo" should not be present when test starts'
  );

  await setupPolicyEngineWithJsonForSearch({
    policies: {
      SearchEngines: {
        Add: [
          {
            Name: "MozSearch",
            URLTemplate: "http://example.com/?q={searchTerms}",
          },
        ],
        DefaultPrivate: "MozSearch",
      },
    },
  });
  // Get in line, because the Search policy callbacks are async.
  await TestUtils.waitForTick();

  // If this passes, it means that the new search engine was properly installed
  // *and* was properly set as the default.
  Assert.equal(
    (await SearchService.getDefaultPrivate()).name,
    "MozSearch",
    "Specified search engine should be the default private engine"
  );

  // Clean up
  await setupPolicyEngineWithJsonForSearch({});
  EnterprisePolicyTesting.resetRunOnceState();
});

// Same as the last test, but with "PreventInstalls" set to true to make sure
// it does not prevent search engines from being installed properly
add_task(async function test_install_and_set_default_prevent_installs() {
  Assert.notEqual(
    (await SearchService.getDefault()).name,
    "MozSearch",
    "Default search engine should not be MozSearch when test starts"
  );
  Assert.equal(
    SearchService.getEngineByName("Foo"),
    null,
    'Engine "Foo" should not be present when test starts'
  );

  await setupPolicyEngineWithJsonForSearch({
    policies: {
      SearchEngines: {
        Add: [
          {
            Name: "MozSearch",
            URLTemplate: "http://example.com/?q={searchTerms}",
          },
        ],
        Default: "MozSearch",
        PreventInstalls: true,
      },
    },
  });
  // Get in line, because the Search policy callbacks are async.
  await TestUtils.waitForTick();

  Assert.equal(
    (await SearchService.getDefault()).name,
    "MozSearch",
    "Specified search engine should be the default"
  );

  // Clean up
  await setupPolicyEngineWithJsonForSearch({});
  EnterprisePolicyTesting.resetRunOnceState();
});

add_task(async function test_install_and_remove() {
  let iconURL =
    "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=";

  Assert.equal(
    SearchService.getEngineByName("Foo"),
    null,
    'Engine "Foo" should not be present when test starts'
  );

  await setupPolicyEngineWithJsonForSearch({
    policies: {
      SearchEngines: {
        Add: [
          {
            Name: "Foo",
            URLTemplate: "http://example.com/?q={searchTerms}",
            IconURL: iconURL,
          },
        ],
      },
    },
  });
  // Get in line, because the Search policy callbacks are async.
  await TestUtils.waitForTick();

  // If this passes, it means that the new search engine was properly installed

  let engine = SearchService.getEngineByName("Foo");
  Assert.notEqual(engine, null, "Specified search engine should be installed");

  Assert.equal(await engine.getIconURL(), iconURL, "Icon should be present");
  Assert.equal(engine.queryCharset, "UTF-8", "Should default to utf-8");

  await setupPolicyEngineWithJsonForSearch({
    policies: {
      SearchEngines: {
        Remove: ["Foo"],
      },
    },
  });
  // Get in line, because the Search policy callbacks are async.
  await TestUtils.waitForTick();

  // If this passes, it means that the specified engine was properly removed
  Assert.equal(
    SearchService.getEngineByName("Foo"),
    null,
    "Specified search engine should not be installed"
  );

  await setupPolicyEngineWithJsonForSearch({});
  EnterprisePolicyTesting.resetRunOnceState();
});

add_task(async function test_install_post_method_engine() {
  Assert.equal(
    SearchService.getEngineByName("Post"),
    null,
    'Engine "Post" should not be present when test starts'
  );

  await setupPolicyEngineWithJsonForSearch({
    policies: {
      SearchEngines: {
        Add: [
          {
            Name: "Post",
            Method: "POST",
            PostData: "q={searchTerms}&anotherParam=yes",
            URLTemplate: "http://example.com/",
          },
        ],
      },
    },
  });
  // Get in line, because the Search policy callbacks are async.
  await TestUtils.waitForTick();

  let engine = SearchService.getEngineByName("Post");
  Assert.notEqual(engine, null, "Specified search engine should be installed");

  Assert.equal(engine._urls[0].method, "POST", "Method should be POST");

  let submission = engine.getSubmission("term", "text/html");
  Assert.notEqual(submission.postData, null, "Post data should not be null");

  let scriptableInputStream = Cc[
    "@mozilla.org/scriptableinputstream;1"
  ].createInstance(Ci.nsIScriptableInputStream);
  scriptableInputStream.init(submission.postData);
  Assert.equal(
    scriptableInputStream.read(scriptableInputStream.available()),
    "q=term&anotherParam=yes",
    "Post data should be present"
  );

  await setupPolicyEngineWithJsonForSearch({});
  EnterprisePolicyTesting.resetRunOnceState();
});

add_task(async function test_install_with_encoding() {
  // Make sure we are starting in an expected state to avoid false positive
  // test results.
  Assert.equal(
    SearchService.getEngineByName("Encoding"),
    null,
    'Engine "Encoding" should not be present when test starts'
  );

  await setupPolicyEngineWithJsonForSearch({
    policies: {
      SearchEngines: {
        Add: [
          {
            Name: "Encoding",
            Encoding: "windows-1252",
            URLTemplate: "http://example.com/?q={searchTerms}",
          },
        ],
      },
    },
  });
  // Get in line, because the Search policy callbacks are async.
  await TestUtils.waitForTick();

  let engine = SearchService.getEngineByName("Encoding");
  Assert.equal(
    engine.queryCharset,
    "windows-1252",
    "Should have correct encoding"
  );

  // Clean up
  await setupPolicyEngineWithJsonForSearch({});
  EnterprisePolicyTesting.resetRunOnceState();
});

add_task(async function test_install_and_update() {
  await setupPolicyEngineWithJsonForSearch({
    policies: {
      SearchEngines: {
        Add: [
          {
            Name: "ToUpdate",
            URLTemplate: "http://initial.example.com/?q={searchTerms}",
          },
        ],
      },
    },
  });
  // Get in line, because the Search policy callbacks are async.
  await TestUtils.waitForTick();

  let engine = SearchService.getEngineByName("ToUpdate");
  Assert.notEqual(engine, null, "Specified search engine should be installed");

  Assert.equal(
    engine.getSubmission("test").uri.spec,
    "http://initial.example.com/?q=test",
    "Initial submission URL should be correct."
  );

  await setupPolicyEngineWithJsonForSearch({
    policies: {
      SearchEngines: {
        Add: [
          {
            Name: "ToUpdate",
            URLTemplate: "http://update.example.com/?q={searchTerms}",
          },
        ],
      },
    },
  });
  // Get in line, because the Search policy callbacks are async.
  await TestUtils.waitForTick();

  engine = SearchService.getEngineByName("ToUpdate");
  Assert.notEqual(engine, null, "Specified search engine should be installed");

  Assert.equal(
    engine.getSubmission("test").uri.spec,
    "http://update.example.com/?q=test",
    "Updated Submission URL should be correct."
  );

  // Clean up
  await setupPolicyEngineWithJsonForSearch({});
  EnterprisePolicyTesting.resetRunOnceState();
});

add_task(async function test_install_with_suggest() {
  // Make sure we are starting in an expected state to avoid false positive
  // test results.
  Assert.equal(
    SearchService.getEngineByName("Suggest"),
    null,
    'Engine "Suggest" should not be present when test starts'
  );

  await setupPolicyEngineWithJsonForSearch({
    policies: {
      SearchEngines: {
        Add: [
          {
            Name: "Suggest",
            URLTemplate: "http://example.com/?q={searchTerms}",
            SuggestURLTemplate: "http://suggest.example.com/?q={searchTerms}",
          },
        ],
      },
    },
  });
  // Get in line, because the Search policy callbacks are async.
  await TestUtils.waitForTick();

  let engine = SearchService.getEngineByName("Suggest");

  Assert.equal(
    engine.getSubmission("test", "application/x-suggestions+json").uri.spec,
    "http://suggest.example.com/?q=test",
    "Updated Submission URL should be correct."
  );

  // Clean up
  await setupPolicyEngineWithJsonForSearch({});
  EnterprisePolicyTesting.resetRunOnceState();
});

add_task(async function test_install_and_restart_keeps_settings() {
  // Make sure we are starting in an expected state to avoid false positive
  // test results.
  Assert.equal(
    SearchService.getEngineByName("Settings"),
    null,
    'Engine "Settings" should not be present when test starts'
  );

  await setupPolicyEngineWithJsonForSearch({
    policies: {
      SearchEngines: {
        Add: [
          {
            Name: "Settings",
            URLTemplate: "http://example.com/?q={searchTerms}",
          },
        ],
      },
    },
  });
  // Get in line, because the Search policy callbacks are async.
  await TestUtils.waitForTick();

  let settingsWritten = SearchTestUtils.promiseSearchNotification(
    "write-settings-to-disk-complete"
  );
  let engine = SearchService.getEngineByName("Settings");
  engine.hidden = true;
  engine.alias = "settings";
  await settingsWritten;

  await setupPolicyEngineWithJsonForSearch({
    policies: {
      SearchEngines: {
        Add: [
          {
            Name: "Settings",
            URLTemplate: "http://example.com/?q={searchTerms}",
          },
        ],
      },
    },
  });

  engine = SearchService.getEngineByName("Settings");

  Assert.ok(engine.hidden, "Should have kept the engine hidden after restart");
  Assert.equal(
    engine.alias,
    "settings",
    "Should have kept the engine alias after restart"
  );

  // Clean up
  await setupPolicyEngineWithJsonForSearch({});
  EnterprisePolicyTesting.resetRunOnceState();
});

add_task(async function test_reset_default() {
  await setupPolicyEngineWithJsonForSearch({
    policies: {
      SearchEngines: {
        Remove: ["DuckDuckGo"],
      },
    },
  });
  // Get in line, because the Search policy callbacks are async.
  await TestUtils.waitForTick();

  let engine = SearchService.getEngineByName("DuckDuckGo");

  Assert.equal(
    engine.hidden,
    true,
    "Application specified engine should be hidden."
  );

  await SearchService.restoreDefaultEngines();

  engine = SearchService.getEngineByName("DuckDuckGo");
  Assert.equal(
    engine.hidden,
    false,
    "Application specified engine should not be hidden"
  );

  EnterprisePolicyTesting.resetRunOnceState();
});
