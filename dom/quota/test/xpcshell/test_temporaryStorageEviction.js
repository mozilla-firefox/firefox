/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

const { PrefUtils } = ChromeUtils.importESModule(
  "resource://testing-common/dom/quota/test/modules/PrefUtils.sys.mjs"
);
const { PrincipalUtils } = ChromeUtils.importESModule(
  "resource://testing-common/dom/quota/test/modules/PrincipalUtils.sys.mjs"
);
const { QuotaUtils } = ChromeUtils.importESModule(
  "resource://testing-common/dom/quota/test/modules/QuotaUtils.sys.mjs"
);
const { SimpleDBUtils } = ChromeUtils.importESModule(
  "resource://testing-common/dom/simpledb/test/modules/SimpleDBUtils.sys.mjs"
);

// This value is used to set dom.quotaManager.temporaryStorage.fixedLimit
// for this test, and must match the needs of the writes we plan to do.
// The storage size must be a multiple of (number of origins - 1) to ensure
// `dataSize` is a whole number. This is enforced below with a check to
// guarantee predictable writes.
const storageSizeKB = 32;

/**
 * This test simulates origin usage across five related stages, exercising:
 * - Access time tracking on first and last access to each origin
 * - Eviction logic based on activity and last access time
 * - Usage reflection via QuotaManager's reporting APIs
 *
 * The test is data-driven: it defines a list of origins, each with a flag
 * array representing whether data on disk is expected to exist after each
 * stage. These flags are used to verify origin usage deterministically at each
 * point.
 *
 * The total storage size is evenly divided among all origins except the last
 * one. This ensures predictable write outcomes:
 * - All but the last origin succeed in writing initially
 * - The last origin exceeds quota and triggers eviction conditions
 *
 * Each stage simulates realistic temporary storage (AKA best-effort) behavior:
 * Stage 1 - Initializes all origin directories in reverse to test access time
 *           updates
 * Stage 2 - Opens connections and fills storage, leaving no room for the last
 *           origin
 * Stage 3 - Closes most connections to allow eviction of inactive origins
 * Stage 4 - Shrinks temporary storage by 50%, triggering additional evictions
 * Stage 5 - Writes again to the last origin to validate ongoing eviction
 *           behavior
 *
 * This test ensures correctness and robustness of temporary storage handling,
 * especially around eviction and access time policies.
 */
async function testTemporaryStorageEviction() {
  const storageSize = storageSizeKB * 1024;

  // flags: [stage1, stage2, stage3, stage4, stage5]
  // 1 = data on disk should exist, 0 = data on disk should not exist

  /* prettier-ignore */
  const originInfos = [
    { url: "https://www.alpha.com",   flags: [0, 1, 1, 1, 1] },
    { url: "https://www.beta.com",    flags: [0, 1, 0, 0, 0] },
    { url: "https://www.gamma.com",   flags: [0, 1, 1, 0, 0] },
    { url: "https://www.delta.com",   flags: [0, 1, 1, 0, 0] },
    { url: "https://www.epsilon.com", flags: [0, 1, 1, 0, 0] },
    { url: "https://www2.alpha.com",  flags: [0, 1, 1, 0, 0] },
    { url: "https://www2.beta.com",   flags: [0, 1, 1, 0, 0] },
    { url: "https://www2.gamma.com",  flags: [0, 1, 1, 0, 0] },
    { url: "https://www2.delta.com",  flags: [0, 1, 1, 0, 0] },
    { url: "https://www2.epsilon.com",flags: [0, 1, 1, 0, 0] },
    { url: "https://www3.alpha.com",  flags: [0, 1, 1, 0, 0] },
    { url: "https://www3.beta.com",   flags: [0, 1, 1, 0, 0] },
    { url: "https://www3.gamma.com",  flags: [0, 1, 1, 0, 0] },
    { url: "https://www3.delta.com",  flags: [0, 1, 1, 0, 0] },
    { url: "https://www3.epsilon.com",flags: [0, 1, 1, 0, 0] },
    { url: "https://www.alpha.org",   flags: [0, 1, 1, 0, 0] },
    { url: "https://www.beta.org",    flags: [0, 1, 1, 0, 0] },
    { url: "https://www.gamma.org",   flags: [0, 1, 1, 0, 0] },
    { url: "https://www.delta.org",   flags: [0, 1, 1, 1, 0] },
    { url: "https://www.epsilon.org", flags: [0, 1, 1, 1, 1] },
    { url: "https://www.zeta.org",    flags: [0, 1, 1, 1, 1] },
    { url: "https://www.eta.org",     flags: [0, 1, 1, 1, 1] },
    { url: "https://www.theta.org",   flags: [0, 1, 1, 1, 1] },
    { url: "https://www.iota.org",    flags: [0, 1, 1, 1, 1] },
    { url: "https://www.kappa.org",   flags: [0, 1, 1, 1, 1] },
    { url: "https://www.lambda.org",  flags: [0, 1, 1, 1, 1] },
    { url: "https://www.mu.org",      flags: [0, 1, 1, 1, 1] },
    { url: "https://www.nu.org",      flags: [0, 1, 1, 1, 1] },
    { url: "https://www.xi.org",      flags: [0, 1, 1, 1, 1] },
    { url: "https://www.omicron.org", flags: [0, 1, 1, 1, 1] },
    { url: "https://www.pi.org",      flags: [0, 1, 1, 1, 1] },
    { url: "https://www.rho.org",     flags: [0, 1, 1, 1, 1] },
    { url: "https://www.omega.org",   flags: [0, 0, 1, 1, 1] },
  ];
  Assert.equal(
    storageSize % (originInfos.length - 1),
    0,
    "Correct storage size"
  );

  const name = "test_temporaryStorageEviction";

  const dataSize = storageSize / (originInfos.length - 1);
  const dataBuffer = new ArrayBuffer(dataSize);

  async function checkUsage(stage) {
    for (const originInfo of originInfos) {
      const url = originInfo.url;

      info(`Checking usage for ${url}`);

      const principal = PrincipalUtils.createPrincipal(url);

      const request = Services.qms.getUsageForPrincipal(principal, {});
      const usageResult = await QuotaUtils.requestFinished(request);

      if (originInfo.flags[stage - 1]) {
        Assert.greater(usageResult.usage, 0, "Correct usage");
      } else {
        Assert.equal(usageResult.usage, 0, "Correct usage");
      }
    }
  }

  async function createAndOpenConnection(url) {
    const principal = PrincipalUtils.createPrincipal(url);

    const connection = SimpleDBUtils.createConnection(principal);

    const openRequest = connection.open(name);
    await SimpleDBUtils.requestFinished(openRequest);

    return connection;
  }

  info(
    "Stage 1: Reverse creation of origins to test first/last access time updates"
  );

  // Initializes storage and temporary storage and creates all origin
  // directories with metadata, in reverse order. This ensures that the
  // "first access" and "last access" logic for updating origin access time is
  // properly exercised in other stages.

  info("Initializing storage");

  {
    const request = Services.qms.init();
    await QuotaUtils.requestFinished(request);
  }

  info("Initializing temporary storage");

  {
    const request = Services.qms.initTemporaryStorage();
    await QuotaUtils.requestFinished(request);
  }

  info("Initializing temporary origins");

  for (const originInfo of originInfos.toReversed()) {
    const principal = PrincipalUtils.createPrincipal(originInfo.url);

    const request = Services.qms.initializeTemporaryOrigin(
      "default",
      principal,
      /* aCreateIfNonExistent */ true
    );
    await QuotaUtils.requestFinished(request);

    // Wait 40ms to ensure the next origin gets a different access time. Some
    // systems have low timer resolution, so this adds a safe buffer.
    await new Promise(function (resolve) {
      do_timeout(40, resolve);
    });
  }

  info("Checking usage");

  await checkUsage(/* stage */ 1);

  info(
    "Stage 2: All origins active; eviction not possible, last write should fail"
  );

  // Opens connections for all origins and writes data to each except the last
  // one. Since all origins remain active (open connections), none can be
  // evicted, even if storage runs out. This tests that eviction logic respects
  // activity status.

  const connections = await (async function () {
    let connections = [];
    // Stage 1
    for (const originInfo of originInfos) {
      const connection = await createAndOpenConnection(originInfo.url);

      connections.push(connection);
    }

    return connections;
  })();

  // Write to all but the last origin.
  for (const connection of connections.slice(0, -1)) {
    const writeRequest = connection.write(dataBuffer);
    await SimpleDBUtils.requestFinished(writeRequest);
  }

  // Try to write to the last origin.
  {
    const writeRequest = connections.at(-1).write(dataBuffer);
    try {
      await SimpleDBUtils.requestFinished(writeRequest);
      Assert.ok(false, "Should have thrown");
    } catch (e) {
      Assert.ok(true, "Should have thrown");
      Assert.strictEqual(
        e.resultCode,
        NS_ERROR_FILE_NO_DEVICE_SPACE,
        "Threw right result code"
      );
    }
  }

  await checkUsage(/* stage */ 2);

  info("Stage 3: Inactive origins can be evicted; last origin writes again");

  // Closes all connections except the first and last origin. This leaves most
  // origins inactive, making them eligible for eviction. The last origin
  // writes data again, which should now succeed because there is at least one
  // inactive origin that can be evicted to make space.

  // Close all connections except the first and the last
  for (const connection of connections.slice(1, -1)) {
    const closeRequest = connection.close();
    await SimpleDBUtils.requestFinished(closeRequest);

    // Wait 40ms to ensure the next origin gets a different access time. Some
    // systems have low timer resolution, so this adds a safe buffer.
    await new Promise(function (resolve) {
      do_timeout(40, resolve);
    });
  }

  // Write to the last origin.
  {
    const writeRequest = connections.at(-1).write(dataBuffer);
    await SimpleDBUtils.requestFinished(writeRequest);
  }

  await checkUsage(/* stage */ 3);

  info("Stage 4: Shrink quota by 50%; evict origins by last access time");

  // Shrinks the temporary storage quota by 50%. This triggers eviction of
  // approximately half of the origins based on their last access time. It
  // tests that quota reduction correctly respects access time ordering when
  // deciding which origins to evict.

  info("Shutting down storage");

  {
    const request = Services.qms.reset();
    await QuotaUtils.requestFinished(request);
  }

  info("Setting preferences");

  {
    const prefs = [
      ["dom.quotaManager.temporaryStorage.fixedLimit", storageSizeKB / 2],
    ];

    PrefUtils.setPrefs(prefs);
  }

  info("Initializing storage");

  {
    const request = Services.qms.init();
    await QuotaUtils.requestFinished(request);
  }

  info("Initializing temporary storage");

  {
    const request = Services.qms.initTemporaryStorage();
    await QuotaUtils.requestFinished(request);
  }

  await checkUsage(/* stage */ 4);

  info("Stage 5: Last origin writes more; one more origin should be evicted");

  // The last origin writes additional data, which should exceed the current
  // quota again. This triggers eviction of one more inactive origin,
  // validating that eviction continues to respect quota limits and frees up
  // space as needed.

  {
    const connection = await createAndOpenConnection(originInfos.at(-1).url);

    const seekRequest = connection.seek(dataSize);
    await SimpleDBUtils.requestFinished(seekRequest);

    const writeRequest = connection.write(dataBuffer);
    await SimpleDBUtils.requestFinished(writeRequest);
  }

  await checkUsage(/* stage */ 5);
}

async function testSteps() {
  add_task(
    {
      pref_set: [
        ["dom.quotaManager.loadQuotaFromCache", false],
        ["dom.quotaManager.temporaryStorage.fixedLimit", storageSizeKB],
        ["dom.quotaManager.temporaryStorage.updateOriginAccessTime", true],
      ],
    },
    testTemporaryStorageEviction
  );
}
