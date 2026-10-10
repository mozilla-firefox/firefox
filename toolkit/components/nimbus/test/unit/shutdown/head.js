/* import-globals-from ../head.js */
/* import-globals-from ../../../../../../toolkit/profile/test/xpcshell/head.js */
/* import-globals-from ../../../../../../browser/components/profiles/tests/unit/head.js */

function promiseGetRecipesBlocks(loader) {
  const blocker = Promise.withResolvers();

  loader.remoteSettingsClients.experiments.get.callsFake(() => {
    blocker.resolve();

    // This promise will never resolve and get() will block.
    return new Promise(() => {});
  });

  return blocker.promise;
}

function promiseLoadUnenrolledSlugsBlocks(sandbox) {
  const blocker = Promise.withResolvers();

  sandbox
    .stub(NimbusEnrollments, "loadUnenrolledExperimentSlugsFromOtherProfiles")
    .callsFake(() => {
      blocker.resolve();

      // This promise will never resolve and
      // loadUnenrolledExperimentSlugsFromOtherProfiles() will block.
      return new Promise(() => {});
    });

  return blocker.promise;
}

function promiseWithUpdateLock(sandbox, loader) {
  const blocker = Promise.withResolvers();

  const withUpdateLock = loader.withUpdateLock;
  sandbox.stub(loader, "withUpdateLock").callsFake(async (...args) => {
    blocker.resolve();
    return withUpdateLock.call(loader, ...args);
  });

  return blocker.promise;
}

function promiseRecordTargetingContextBlocks(sandbox) {
  const blocker = Promise.withResolvers();

  const { ClientID } = ChromeUtils.importESModule(
    "resource://gre/modules/ClientID.sys.mjs"
  );

  // ClientID.getProfileGroupID() is the last call awaited by
  // recordTargetingContext().
  sandbox.stub(ClientID, "getProfileGroupID").callsFake(() => {
    blocker.resolve();

    // This promise will never resolve and recordTargetingContext() will block.
    return new Promise(() => {});
  });

  return blocker.promise;
}
