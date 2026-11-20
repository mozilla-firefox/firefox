"use strict";

const { FirstStartup } = ChromeUtils.importESModule(
  "resource://gre/modules/FirstStartup.sys.mjs"
);
const { updateAppInfo } = ChromeUtils.importESModule(
  "resource://testing-common/AppInfo.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const PREF_CATEGORY_TASKS = "first-startup.category-tasks-enabled";
const CATEGORY_NAME = "first-startup-new-profile";
const TEST_MODULE = "resource://test/FirstStartupCategoryModule.sys.mjs";
const CATEGORY_TASK_TOPIC = "first-startup-category-task-called";

add_setup(function test_setup() {
  do_get_profile();
  Services.fog.initializeFOG();
  // Delete any categories that have been registered statically so that we're
  // just running the one here under test.
  Services.catMan.deleteCategory(CATEGORY_NAME);
});

/**
 * Test that category tasks registered with the first-startup-new-profile
 * category are invoked during FirstStartup initialization, and that they run
 * after Nimbus initialization completes. Verifies that the
 * categoryTasksTime Glean metric is recorded.
 */
add_task(async function test_categoryTasks_called_after_nimbus() {
  if (AppConstants.MOZ_BUILD_APP !== "browser") {
    info("Skipping test - not a browser build");
    return;
  }

  updateAppInfo();
  Services.prefs.setBoolPref(PREF_CATEGORY_TASKS, true);
  FirstStartup.resetForTesting();

  let catManUpdated = TestUtils.topicObserved("xpcom-category-entry-added");
  Services.catMan.addCategoryEntry(
    CATEGORY_NAME,
    TEST_MODULE,
    "FirstStartupCategoryModule.firstStartupNewProfile",
    false,
    false
  );
  await catManUpdated;

  const { ExperimentAPI } = ChromeUtils.importESModule(
    "resource://nimbus/ExperimentAPI.sys.mjs"
  );
  let sandbox = sinon.createSandbox();
  let nimbusInitComplete = false;
  let originalInit = ExperimentAPI.init.bind(ExperimentAPI);

  sandbox.stub(ExperimentAPI, "init").callsFake(function (...args) {
    return originalInit(...args).finally(() => {
      nimbusInitComplete = true;
    });
  });

  let categoryTaskCalled = false;
  let categoryTaskPromise = TestUtils.topicObserved(CATEGORY_TASK_TOPIC).then(
    ([_subj, data]) => {
      Assert.ok(
        ExperimentAPI.init.calledOnce,
        "ExperimentAPI.init should have been called"
      );
      Assert.ok(
        nimbusInitComplete,
        "Nimbus init should be complete before category task starts"
      );
      categoryTaskCalled = true;
      return data;
    }
  );

  let submissionPromise = new Promise(resolve => {
    GleanPings.firstStartup.testBeforeNextSubmit(() => {
      Assert.equal(FirstStartup.state, FirstStartup.SUCCESS);
      Assert.ok(Glean.firstStartup.newProfile.testGetValue());
      Assert.equal(
        Glean.firstStartup.statusCode.testGetValue(),
        FirstStartup.SUCCESS
      );
      Assert.greater(Glean.firstStartup.nimbusInitTime.testGetValue(), 0);
      Assert.greater(
        Glean.firstStartup.categoryTasksTime.testGetValue(),
        0,
        "Category tasks time should be recorded"
      );
      resolve();
    });
  });

  FirstStartup.init(true);

  await submissionPromise;

  Assert.equal(
    await categoryTaskPromise,
    "executed",
    "Category task should have been called"
  );
  Assert.ok(categoryTaskCalled, "Category task was invoked");

  sandbox.restore();
  Services.catMan.deleteCategoryEntry(CATEGORY_NAME, TEST_MODULE, false);
  Services.prefs.clearUserPref(PREF_CATEGORY_TASKS);
});

/**
 * Test that category tasks are not invoked when the category tasks pref is
 * disabled, even when tasks are registered with the category.
 */
add_task(async function test_categoryTasks_disabled_by_pref() {
  if (AppConstants.MOZ_BUILD_APP !== "browser") {
    info("Skipping test - not a browser build");
    return;
  }

  updateAppInfo();
  Services.prefs.setBoolPref(PREF_CATEGORY_TASKS, false);
  FirstStartup.resetForTesting();

  let catManUpdated = TestUtils.topicObserved("xpcom-category-entry-added");
  Services.catMan.addCategoryEntry(
    CATEGORY_NAME,
    TEST_MODULE,
    "FirstStartupCategoryModule.firstStartupNewProfile",
    false,
    false
  );
  await catManUpdated;

  let categoryTaskCalled = false;
  Services.obs.addObserver(() => {
    categoryTaskCalled = true;
  }, CATEGORY_TASK_TOPIC);

  let submissionPromise = new Promise(resolve => {
    GleanPings.firstStartup.testBeforeNextSubmit(() => {
      resolve();
    });
  });

  FirstStartup.init(true);
  await submissionPromise;

  Assert.ok(
    !categoryTaskCalled,
    "Category task should not be called when pref is false"
  );

  Services.catMan.deleteCategoryEntry(CATEGORY_NAME, TEST_MODULE, false);
  Services.prefs.clearUserPref(PREF_CATEGORY_TASKS);
});
