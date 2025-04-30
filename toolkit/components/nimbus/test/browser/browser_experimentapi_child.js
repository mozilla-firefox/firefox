/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async function setup() {
  const cleanup = await setupTest();
  registerCleanupFunction(cleanup);
});

/**
 * Set up a listener for a SharedData update in the process corresponding to the
 * specified browser.
 *
 * You must await the promise returned by this function *before* triggering a
 * SharedData flush.
 *
 * After triggering the flush, you must await the promise inside the returned
 * object.
 *
 * Example:
 *
 * ```js
 * const childUpdated = await childSharedDataChanged(browser);
 * // Do something to modify SharedData
 * Services.ppmm.sharedData.flush();
 * await childUpdated.promise;
 * ```
 *
 * @returns {Promise<object>}
 *          A promise that resolves to an object containing a promise. The outer
 *          promise resolves when the event handler has been registered in the
 *          child. The inner promise resolves when the event has fired in the
 *          child.
 */
async function childSharedDataChanged(browser) {
  const MESSAGE = "browser_experimentapi_child:shared-data-changed";

  const deferred = Promise.withResolvers();
  const listener = () => {
    deferred.resolve();
    Services.ppmm.removeMessageListener(MESSAGE, listener);
  };

  Services.ppmm.addMessageListener(MESSAGE, listener);

  await SpecialPowers.spawn(browser, [MESSAGE], async MESSAGE => {
    Services.cpmm.sharedData.addEventListener(
      "change",
      async () => {
        await Services.cpmm.sendAsyncMessage(MESSAGE);
      },
      { once: true }
    );
  });

  // We can't return promise here because JavaScript will collapse it and
  // awaiting this function will await *that* promise, which we don't want to
  // do.
  return { promise: deferred.promise };
}

add_task(async function testGetFromChildNewEnrollment() {
  const browserWindow = Services.wm.getMostRecentWindow("navigator:browser");

  // Open a tab so we have a content process.
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser: browserWindow.gBrowser,
    url: "https://example.com",
    forceNewProcess: true,
  });
  const browser = tab.linkedBrowser;

  // Assert that the tab is in fact a content process and that we don't have any
  // experiments available yet.
  await SpecialPowers.spawn(browser, [], async () => {
    const { ExperimentAPI } = ChromeUtils.importESModule(
      "resource://nimbus/ExperimentAPI.sys.mjs"
    );

    Assert.equal(
      Services.appinfo.processType,
      Services.appinfo.PROCESS_TYPE_CONTENT,
      "This is running in a content process"
    );

    await ExperimentAPI.ready();

    Assert.equal(
      ExperimentAPI.getExperimentMetaData({ slug: "foo" }),
      null,
      "Experiment should not exist in child yet"
    );
    Assert.equal(
      ExperimentAPI.getExperimentMetaData({ featureId: "test-feature" }),
      null,
      "Experiment should not exist in child yet"
    );
  });

  let childUpdated = await childSharedDataChanged(browser);

  // Enroll in an experiment in the parent process.
  await ExperimentAPI._manager.enroll(
    ExperimentFakes.recipe("foo", {
      bucketConfig: {
        ...ExperimentFakes.recipe.bucketConfig,
        count: 1000,
      },
      branches: [
        {
          slug: "control",
          ratio: 1,
          features: [
            {
              featureId: "testFeature",
              value: {
                enabled: true,
                testInt: 123,
              },
            },
          ],
        },
      ],
    })
  );

  // Immediately serialize sharedData and broadcast changes to the child processes.
  //
  // In normal operation, this will happen during idle dispatch [1], but we want
  // to test that our IPC mechanisms work correctly, so we force it to happen so
  // that we can act immediately in the child.
  //
  // [1]: https://searchfox.org/mozilla-central/rev/5bea6ede57be43d450ecc24af7a535288c9a9f7d/dom/ipc/SharedMap.cpp#416-422
  Services.ppmm.sharedData.flush();
  await childUpdated.promise;

  // Check that the new state is reflected in the content process.
  await SpecialPowers.spawn(browser, [], async () => {
    const { ExperimentAPI, NimbusFeatures } = ChromeUtils.importESModule(
      "resource://nimbus/ExperimentAPI.sys.mjs"
    );
    const { TestUtils } = ChromeUtils.importESModule(
      "resource://testing-common/TestUtils.sys.mjs"
    );

    await TestUtils.waitForCondition(
      () => ExperimentAPI.getExperimentMetaData({ slug: "foo" }),
      "Wait for enrollment child to sync"
    );

    const bySlug = ExperimentAPI.getExperimentMetaData({ slug: "foo" });
    const byFeature = ExperimentAPI.getExperimentMetaData({
      featureId: "testFeature",
    });

    for (const [rv, field] of [
      [bySlug, "slug"],
      [byFeature, "featureId"],
    ]) {
      info(`when calling ExperimentAPI.getExperimentMetaData with ${field}:`);

      Assert.equal(rv.slug, "foo", "Experiment slug is correct");
      Assert.ok(rv.active, "Experiment is active");
      Assert.equal(
        rv.branch.slug,
        "control",
        "Experiment branch slug is correct"
      );
    }

    Assert.deepEqual(
      NimbusFeatures.testFeature.getAllVariables(),
      { enabled: true, testInt: 123 },
      "Experiment values are correct"
    );

    Assert.equal(
      NimbusFeatures.testFeature.getVariable("enabled"),
      true,
      "Experiment values are correct"
    );

    Assert.equal(
      NimbusFeatures.testFeature.getVariable("testInt"),
      123,
      "Experiment values are correct"
    );
  });

  childUpdated = await childSharedDataChanged(browser);
  // Unenroll from the experiment in the parent process.
  ExperimentAPI._manager.unenroll("foo");
  // Propagate the change to child processes.
  Services.ppmm.sharedData.flush();
  await childUpdated.promise;

  // Check that the new state is reflected in the content process.
  await SpecialPowers.spawn(browser, [], async () => {
    const { ExperimentAPI } = ChromeUtils.importESModule(
      "resource://nimbus/ExperimentAPI.sys.mjs"
    );
    const { TestUtils } = ChromeUtils.importESModule(
      "resource://testing-common/TestUtils.sys.mjs"
    );

    await TestUtils.waitForCondition(
      () => !ExperimentAPI.getExperimentMetaData({ slug: "foo" }).active,
      "Wait for unenrollment to sync"
    );

    const bySlug = ExperimentAPI.getExperimentMetaData({ slug: "foo" });
    const byFeature = ExperimentAPI.getExperimentMetaData({
      featureId: "testFeature",
    });

    info(
      "After unenrollment, when calling ExperimentAPI.getExperimentMetaData with slug:"
    );

    Assert.notEqual(bySlug, null, "Experiment is not null");
    Assert.ok(!bySlug.active, "Experiment is not active");
    Assert.equal(bySlug.branch.slug, "control", "Experiment branch is correct");

    info(
      "After unenrollment, when calling ExperimentAPI.getExperimentMetaData with featureId:"
    );

    Assert.equal(byFeature, null, "Experiment is null");
  });

  ExperimentAPI._manager.store._deleteForTests("foo");

  BrowserTestUtils.removeTab(tab);

  Services.ppmm.sharedData.flush();
});

add_task(async function testGetFromChildExistingEnrollment() {
  const browserWindow =
    Services.wm.getMostRecentBrowserWindow("navigator:browser");

  // We only want to test the new process case, so make sure to shut down any
  // existing processes.
  Services.ppmm.releaseCachedProcesses();

  await ExperimentAPI._manager.enroll(
    ExperimentFakes.recipe("qux", {
      bucketConfig: {
        ...ExperimentFakes.recipe.bucketConfig,
        count: 1000,
      },
      branches: [
        {
          slug: "treatment",
          ratio: 1,
          features: [
            {
              featureId: "testFeature",
              value: {
                enabled: false,
                testInt: 456,
              },
            },
          ],
        },
      ],
    })
  );

  // We don't have to wait for this to update in the client, but we *do* have to
  // flush to make it re-serialize the contents to make it available to new
  // processes.
  Services.ppmm.sharedData.flush();

  // Open a tab so that we have a content process.
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser: browserWindow.gBrowser,
    url: "https://example.com",
    forceNewProcess: true,
  });
  const browser = tab.linkedBrowser;

  // Check that the experiment is available in the child process.
  await SpecialPowers.spawn(browser, [], async () => {
    const { ExperimentAPI, NimbusFeatures } = ChromeUtils.importESModule(
      "resource://nimbus/ExperimentAPI.sys.mjs"
    );
    const { Assert } = ChromeUtils.importESModule(
      "resource://testing-common/Assert.sys.mjs"
    );

    await ExperimentAPI.ready();

    const bySlug = ExperimentAPI.getExperimentMetaData({ slug: "qux" });
    const byFeature = ExperimentAPI.getExperimentMetaData({
      featureId: "testFeature",
    });

    for (const [rv, field] of [
      [bySlug, "slug"],
      [byFeature, "featureId"],
    ]) {
      info(`when calling ExperimentAPI.getExperimentMetaData with ${field}:`);

      Assert.equal(rv.slug, "qux", "Experiment slug is correct");
      Assert.ok(rv.active, "Experiment is active");
      Assert.equal(
        rv.branch.slug,
        "treatment",
        "Experiment branch slug is correct"
      );
    }

    Assert.deepEqual(
      NimbusFeatures.testFeature.getAllVariables(),
      { enabled: false, testInt: 456 },
      "Experiment values are correct"
    );

    Assert.equal(
      NimbusFeatures.testFeature.getVariable("enabled"),
      false,
      "Experiment values are correct"
    );

    Assert.equal(
      NimbusFeatures.testFeature.getVariable("testInt"),
      456,
      "Experiment values are correct"
    );
  });

  ExperimentAPI._manager.unenroll("qux");
  ExperimentAPI._manager.store._deleteForTests("qux");
  BrowserTestUtils.removeTab(tab);

  Services.ppmm.sharedData.flush();
});
