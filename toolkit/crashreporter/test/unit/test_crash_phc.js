function check(extra, kind, size, hasFreeStack) {
  Assert.equal(extra.PHCKind, kind);

  // This is a string holding a decimal address.
  Assert.ok(/^\d+$/.test(extra.PHCBaseAddress));

  Assert.equal(extra.PHCUsableSize, size);

  // These are strings holding comma-separated lists of decimal addresses.
  // Sometimes on Mac they have a single entry.
  Assert.ok(/^(\d+,)*\d+$/.test(extra.PHCAllocStack));
  if (hasFreeStack) {
    Assert.ok(/^(\d+,)*\d+$/.test(extra.PHCFreeStack));
  } else {
    Assert.ok(!extra.hasOwnProperty("PHCFreeStack"));
  }
}

add_task(async function run_test() {
  if (!("@mozilla.org/toolkit/crash-reporter;1" in Cc)) {
    dump(
      "INFO | test_crash_phc.js | Can't test crashreporter in a non-libxul build.\n"
    );
    return;
  }

  await do_crash(
    function () {
      crashType = CrashTestUtils.CRASH_PHC_USE_AFTER_FREE;
    },
    function (mdump, extra) {
      // CRASH_PHC_USE_AFTER_FREE uses 32 for the size.
      check(extra, "FreedPage", 32, /* hasFreeStack */ true);
    },
    true
  );

  await do_crash(
    function () {
      crashType = CrashTestUtils.CRASH_PHC_DOUBLE_FREE;
    },
    function (mdump, extra) {
      // CRASH_PHC_DOUBLE_FREE uses 64 for the size.
      check(extra, "FreedPage", 64, /* hasFreeStack */ true);
    },
    true
  );

  await do_crash(
    function () {
      crashType = CrashTestUtils.CRASH_PHC_BOUNDS_VIOLATION;
    },
    function (mdump, extra) {
      // CRASH_PHC_BOUNDS_VIOLATION uses 96 for the size.
      check(extra, "GuardPage", 96, /* hasFreeStack */ false);
    },
    true
  );
});
