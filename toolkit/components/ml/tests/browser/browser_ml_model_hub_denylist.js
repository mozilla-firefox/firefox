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

    /** @returns {Promise<boolean>} */
    async isListed(modelHub) {
      const models = await modelHub.listModels();
      return models.some(
        ({ name, revision }) =>
          name === `${hostname}/${MODEL}` && revision === REVISION
      );
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
 * The cache-hit path purges what it refuses. The delete has to qualify the
 * model name with the hub hostname, the way the cache stores it.
 */
add_task(async function test_denied_hub_purges_the_cached_file() {
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
    !(await hub.opfsHasFile(denied)),
    "The denied file was removed from disk by the read that refused it."
  );
  Assert.ok(
    !(await hub.isListed(denied)),
    "It is no longer listed as a cached model."
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
  Assert.ok(!(await hub.isListed(denied)), "The model is no longer listed.");

  await hub.cleanup();
});

/**
 * Every call can redirect to a different hub via modelHubRootUrl /
 * modelHubUrlTemplate, so the allow/deny check has to re-run per call rather
 * than trusting the ModelHub instance's own rootUrl.
 */
add_task(async function test_per_call_hub_override_is_allowed() {
  const defaultHub = MLTestUtils.createMockModelHub();
  const overrideHub = MLTestUtils.createMockModelHub();
  overrideHub.setFile({
    model: MODEL,
    revision: REVISION,
    file: FILE,
    content: "from the override hub",
  });

  const modelHub = new ModelHub({
    rootUrl: defaultHub.rootUrl,
    urlTemplate: "{model}/different_from_override/{revision}",
  });

  const [buffer] = await modelHub.getModelFileAsArrayBuffer({
    model: MODEL,
    revision: REVISION,
    file: FILE,
    taskName: TASK_NAME,
    modelHubRootUrl: overrideHub.rootUrl,
    modelHubUrlTemplate: overrideHub.urlTemplate,
  });

  Assert.equal(
    new TextDecoder().decode(buffer),
    "from the override hub",
    "The per-call override hub should be the one actually fetched."
  );
  Assert.equal(
    overrideHub.requestCount({ model: MODEL, revision: REVISION, file: FILE }),
    1,
    "The override hub was contacted."
  );
  Assert.equal(
    defaultHub.requestCount({ model: MODEL, revision: REVISION, file: FILE }),
    0,
    "The ModelHub's own default hub was never contacted."
  );

  await deleteCache(modelHub.cache);
  await defaultHub.cleanup();
  await overrideHub.cleanup();
});

/**
 * A per-call override to a hub outside the allow/deny list must be rejected
 * with a ForbiddenURLError, the same as the ModelHub's own rootUrl would be.
 */
add_task(async function test_per_call_hub_override_is_forbidden() {
  const hub = await withModelHub();
  const modelHub = hub.make(null);

  await Assert.rejects(
    modelHub.getModelFileAsArrayBuffer({
      ...hub.args,
      modelHubRootUrl: "https://forbidden.example.com",
      modelHubUrlTemplate: "{model}/resolve/{revision}",
    }),
    /Forbidden URL/,
    "A disallowed per-call override hub is rejected."
  );
  Assert.equal(
    hub.requests(),
    0,
    "The ModelHub's own default hub was never contacted as a fallback."
  );

  await hub.cleanup();
});
