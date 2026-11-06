/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */
/* eslint-disable mozilla/no-arbitrary-setTimeout */

/**
 * This test verifies that writes of a failed flush are not lost and are
 * persisted by the next flush or when the connection is closed. See bug
 * 2058495.
 */

const { QuotaUtils } = ChromeUtils.importESModule(
  "resource://testing-common/dom/quota/test/modules/QuotaUtils.sys.mjs"
);

const flushSleepTimeSec = 6;

function waitForFlush() {
  info("Sleeping for " + flushSleepTimeSec + " seconds to let the flush run");

  return new Promise(function (resolve) {
    setTimeout(resolve, flushSleepTimeSec * 1000);
  });
}

// The usage journal is written before the commit and only removed once the
// commit succeeds, so it is left behind by a failed flush.
function getUsageJournalFile() {
  return getRelativeFile("storage/default/http+++example.com/ls/usage-journal");
}

async function addItemAndFlush(storage, item) {
  info("Adding item");
  storage.setItem(item.key, item.value);

  await returnToEventLoop();
  await waitForFlush();
}

async function addItemWithFailingFlush(storage, item) {
  info("Making the next flush fail");

  await QuotaUtils.withArtificialFailures(
    Ci.nsIQuotaArtificialFailure.CATEGORY_LOCAL_STORAGE_FLUSH,
    /* probability */ 100,
    Cr.NS_ERROR_FAILURE,
    addItemAndFlush.bind(null, storage, item)
  );

  ok(getUsageJournalFile().exists(), "Flush failed");
}

async function checkPersistedItems(principal, items) {
  info("Resetting");

  let request = reset();
  await requestFinished(request);

  ok(!getUsageJournalFile().exists(), "No pending flush failure");

  info("Getting usage");

  request = getOriginUsage(principal);
  await requestFinished(request);

  const expectedUsage = items.reduce(
    (usage, item) => usage + item.key.length + item.value.length,
    0
  );

  is(request.result.usage, expectedUsage, "Correct usage");

  info("Getting storage");

  const storage = getLocalStorage(principal);

  for (const item of items) {
    is(storage.getItem(item.key), item.value, "Correct value");
  }

  info("Clearing");

  request = clear();
  await requestFinished(request);
}

add_task(async function testRetryOnNextFlush() {
  const items = [
    { key: "key1", value: "value1" },
    { key: "key2", value: "value2" },
  ];

  const principal = getDefaultPrincipal();

  info("Getting storage");

  const storage = getLocalStorage(principal);

  await addItemWithFailingFlush(storage, items[0]);

  info("Adding second item");

  await addItemAndFlush(storage, items[1]);

  ok(!getUsageJournalFile().exists(), "Retry flush succeeded");

  await checkPersistedItems(principal, items);
});

add_task(async function testRetryOnClose() {
  const items = [{ key: "key1", value: "value1" }];

  const principal = getDefaultPrincipal();

  info("Getting storage");

  const storage = getLocalStorage(principal);

  await addItemWithFailingFlush(storage, items[0]);

  // No other write, so the failed writes are only flushed when the reset
  // closes the connection.
  await checkPersistedItems(principal, items);
});
