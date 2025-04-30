/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { _ExperimentFeature: ExperimentFeature, ExperimentAPI } =
  ChromeUtils.importESModule("resource://nimbus/ExperimentAPI.sys.mjs");
const { ExperimentManager } = ChromeUtils.importESModule(
  "resource://nimbus/lib/ExperimentManager.sys.mjs"
);
const { ExperimentFakes, ExperimentTestUtils, NimbusTestUtils } =
  ChromeUtils.importESModule(
    "resource://testing-common/NimbusTestUtils.sys.mjs"
  );
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

NimbusTestUtils.init(this);

add_setup(async function () {
  await ExperimentAPI.ready();

  const sandbox = sinon.createSandbox();

  /* We stub the functions that operate with enrollments and remote rollouts
   * so that any access to store something is implicitly validated against
   * the schema and no records have missing (or extra) properties while in tests
   */

  const origAddExperiment = ExperimentManager.store.addEnrollment.bind(
    ExperimentManager.store
  );
  sandbox
    .stub(ExperimentManager.store, "addEnrollment")
    .callsFake(enrollment => {
      ExperimentTestUtils.validateEnrollment(enrollment);
      return origAddExperiment(enrollment);
    });

  // Ensure the inner callback runs after all other registered cleanup
  // functions. This lets tests use registerCleanupFunction to clean up any
  // stray enrollments.
  registerCleanupFunction(() => {
    registerCleanupFunction(() => {
      NimbusTestUtils.assert.storeIsEmpty(ExperimentManager.store);
      sandbox.restore();
    });
  });
});

async function setupTest() {
  await ExperimentAPI.ready();
  await ExperimentAPI._rsLoader.finishedUpdating();

  await ExperimentAPI._rsLoader.remoteSettingsClients.experiments.db.importChanges(
    {},
    Date.now(),
    [],
    { clear: true }
  );

  await ExperimentAPI._rsLoader.updateRecipes("test");

  return async function cleanup() {
    await NimbusTestUtils.removeStore(ExperimentAPI._manager.store);
  };
}

async function assertEmptyStore(store) {
  await NimbusTestUtils.removeStore(store);
}
