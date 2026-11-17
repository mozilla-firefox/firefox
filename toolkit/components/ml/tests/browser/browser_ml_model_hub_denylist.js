/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/** @type {import("../MLTestUtils.sys.mjs")} */
const { MLTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/MLTestUtils.sys.mjs"
);

const MODEL = "acme/bert";
const REVISION = "v1";
const FILE = "config.json";
const TASK_NAME = "test_model_hub_denylist";

const DENY_LOCALHOST = [
  // eslint-disable-next-line sdl/no-insecure-url
  { filter: "DENY", urlPrefix: "http://localhost/" },
];

add_setup(function () {
  // Guard against an earlier test leaking this env var and silently
  // disabling the allow/deny list for every test below.
  Assert.ok(
    !Services.env.exists("MOZ_ALLOW_EXTERNAL_ML_HUB"),
    "MOZ_ALLOW_EXTERNAL_ML_HUB is unset, so the allow/deny list applies."
  );
});

async function withModelHub() {
  const hub = MLTestUtils.createMockModelHub();
  hub.setFile({ model: MODEL, revision: REVISION, file: FILE, content: "{}" });
  const hostname = new URL(hub.rootUrl).hostname;

  /** @type {Array<ModelHub>} */
  const created = [];

  const make = allowDenyList => {
    const modelHub = new ModelHub({
      rootUrl: hub.rootUrl,
      urlTemplate: hub.urlTemplate,
      allowDenyList,
    });
    created.push(modelHub);
    return modelHub;
  };

  return {
    hub,
    make,
    args: { model: MODEL, revision: REVISION, file: FILE, taskName: TASK_NAME },

    requests() {
      return hub.requestCount({ model: MODEL, revision: REVISION, file: FILE });
    },

    /** @returns {Promise<boolean>} */
    async opfsHasFile(modelHub) {
      const path = modelHub.cache.generateFilePathInOPFS({
        model: `${hostname}/${MODEL}`,
        revision: REVISION,
        file: FILE,
      });
      try {
        await SharedOPFS.getFileHandle(path);
        return true;
      } catch (error) {
        if (error.name === "NotFoundError") {
          return false;
        }
        throw error;
      }
    },

    async cleanup() {
      for (const modelHub of created) {
        if (modelHub.cache) {
          await deleteCache(modelHub.cache);
        }
      }
      await hub.cleanup();
    },
  };
}

add_task(async function test_denied_hub_is_not_contacted() {
  const hub = await withModelHub();
  const denied = hub.make(DENY_LOCALHOST);

  await Assert.rejects(
    denied.getModelFileAsArrayBuffer(hub.args),
    /Forbidden URL.*DENIED/,
    "Reading from a denied hub rejects."
  );
  Assert.equal(
    hub.requests(),
    0,
    "The hub was never contacted, not even to revalidate."
  );

  await hub.cleanup();
});

add_task(async function test_denied_hub_rejects_an_already_cached_file() {
  const hub = await withModelHub();
  const allowed = hub.make(null);

  await allowed.getModelFileAsArrayBuffer(hub.args);
  Assert.ok(
    await hub.opfsHasFile(allowed),
    "The file downloaded while the hub was permitted."
  );

  const denied = hub.make(DENY_LOCALHOST);

  await Assert.rejects(
    denied.getModelFileAsArrayBuffer(hub.args),
    /Forbidden URL.*DENIED/,
    "The cached copy is refused too."
  );

  await hub.cleanup();
});

/**
 * TODO: getModelDataAsFile deletes by bare model name, but the cache keys
 * entries by hostname (e.g. "localhost/acme/bert"), so the delete silently
 * fails to match and the denied file stays on disk. isModelAvailable deletes
 * correctly because it passes the hostname-qualified key.
 */
add_task(async function test_denied_hub_does_not_purge_the_cached_file() {
  const hub = await withModelHub();
  const allowed = hub.make(null);
  await allowed.getModelFileAsArrayBuffer(hub.args);

  const denied = hub.make(DENY_LOCALHOST);
  await Assert.rejects(
    denied.getModelFileAsArrayBuffer(hub.args),
    /Forbidden URL/,
    "The read is refused."
  );

  Assert.ok(
    await hub.opfsHasFile(denied),
    "The denied file is still on disk after the read that was supposed to purge it."
  );
  Assert.deepEqual(
    await denied.listModels(),
    [{ taskName: TASK_NAME, name: `localhost/${MODEL}`, revision: REVISION }],
    "It is also still listed as a cached model."
  );

  await hub.cleanup();
});

add_task(async function test_is_model_available_purges_a_denied_file() {
  const hub = await withModelHub();
  const allowed = hub.make(null);
  await allowed.getModelFileAsArrayBuffer(hub.args);

  const denied = hub.make(DENY_LOCALHOST);
  const available = await denied.isModelAvailable(MODEL, REVISION, {
    file: FILE,
  });

  Assert.ok(!available, "A denied model is reported unavailable.");
  Assert.ok(
    !(await hub.opfsHasFile(denied)),
    "The denied file was removed from disk."
  );
  Assert.deepEqual(
    await denied.listModels(),
    [],
    "The model is no longer listed."
  );

  await hub.cleanup();
});
