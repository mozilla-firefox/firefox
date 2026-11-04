"use strict";

const PAGE =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://example.com"
  ) + "file_trusted_types_library.sjs";

const PREF = "dom.security.trusted_types.readiness_telemetry.enabled";

function sumLabels(field) {
  const labels = Glean.security.trustedTypesReadiness.testGetValue() ?? {};
  return Object.entries(labels)
    .filter(([label]) => !field || label.includes(`_${field}_`))
    .reduce((sum, [, count]) => sum + count, 0);
}

async function libraryDetected(testCase) {
  await Services.fog.testFlushAllChildren();
  const before = { lib0: sumLabels("lib-0"), lib1: sumLabels("lib-1") };
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    `${PAGE}?case=${testCase}`
  );
  await BrowserTestUtils.removeTab(tab);
  let after;
  await TestUtils.waitForCondition(async () => {
    await Services.fog.testFlushAllChildren();
    after = { lib0: sumLabels("lib-0"), lib1: sumLabels("lib-1") };
    return after.lib0 + after.lib1 > before.lib0 + before.lib1;
  }, `Waiting for the ${testCase} document to be recorded`);
  Assert.equal(
    Glean.security.trustedTypesReadiness.__other__.testGetValue() ?? 0,
    0,
    "No invalid label recorded"
  );
  return after.lib1 > before.lib1;
}

add_task(async function test_libraries() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  for (const [testCase, expected] of [
    ["none", false],
    ["lit", true],
    ["react", true],
    ["react_disabled_hook", true],
    ["devtools_hook_without_react", false],
    ["htmx", true],
    ["angular", true],
  ]) {
    Assert.equal(
      await libraryDetected(testCase),
      expected,
      `${testCase} ${expected ? "is" : "is not"} detected as a library`
    );
  }
  await SpecialPowers.popPrefEnv();
});

// A document is detected once after load and reported once when destroyed,
// with a round trip through the bfcache in between changing neither.
add_task(async function test_bfcache() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await Services.fog.testFlushAllChildren();
  const before = { all: sumLabels(), lib1: sumLabels("lib-1") };
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    `${PAGE}?case=angular`
  );
  const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(
    tab.linkedBrowser,
    `${PAGE}?case=none`
  );
  await loaded;
  await Services.fog.testFlushAllChildren();
  Assert.equal(
    sumLabels(),
    before.all,
    "Nothing recorded on entering the bfcache"
  );
  const shown = BrowserTestUtils.waitForContentEvent(
    tab.linkedBrowser,
    "pageshow"
  );
  tab.linkedBrowser.goBack();
  await shown;
  await Services.fog.testFlushAllChildren();
  Assert.equal(
    sumLabels(),
    before.all,
    "Nothing recorded on leaving the bfcache"
  );
  await BrowserTestUtils.removeTab(tab);
  await TestUtils.waitForCondition(async () => {
    await Services.fog.testFlushAllChildren();
    return sumLabels() >= before.all + 2;
  }, "Waiting for both documents to be recorded");
  Assert.equal(sumLabels(), before.all + 2, "Each document is recorded once");
  Assert.equal(
    sumLabels("lib-1"),
    before.lib1 + 1,
    "The bfcached Angular document is detected at destruction"
  );
  await SpecialPowers.popPrefEnv();
});
