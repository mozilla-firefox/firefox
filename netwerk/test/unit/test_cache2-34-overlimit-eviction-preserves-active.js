"use strict";

// Test that over-limit eviction does not doom entries currently being written.
// Bug 2031577: a newly-created entry has the lowest frecency and was
// incorrectly selected as the eviction candidate before the entry finished
// writing, causing it to be doomed.  After the fix, entries with active
// file handles are skipped during eviction.
//
// Strategy: fill the cache, then shrink the capacity so the cache is over
// the soft limit but well under the hard limit (105% of capacity).  When
// the new entry is written, over-limit eviction is triggered.  Without the
// fix the new entry (lowest frecency) would be doomed; with the fix
// eviction skips it and removes fill entries instead.
//
// Sizing notes: shrinking capacity by (1 - f) and adding a new entry of
// size s requires s/U <= 0.05 - 1.05*(1 - f) to stay below the 105% hard
// limit.  Hitting the hard limit makes WriteInternal fail with
// NS_ERROR_FILE_NO_DEVICE_SPACE, which would in turn make the read-back
// throw and hang the test.  We pick a large fill and a small shrink so
// the new entry leaves ~10 KB of margin under the hard limit.

const kEntryDataSize = 4000;
const kNumFillEntries = 100;
const kInitialCapacityKB = 1024;

function makeData(size, char) {
  return char.repeat(size);
}

function touchEntry(url, meta, data, cb) {
  asyncOpenCacheEntry(
    url,
    "disk",
    Ci.nsICacheStorage.OPEN_NORMALLY,
    null,
    new OpenCallback(NEW, meta, data, function () {
      asyncOpenCacheEntry(
        url,
        "disk",
        Ci.nsICacheStorage.OPEN_NORMALLY,
        null,
        new OpenCallback(NORMAL, meta, data, cb)
      );
    })
  );
}

function run_test() {
  do_get_profile();

  Services.prefs.setBoolPref("browser.cache.disk.smart_size.enabled", false);
  Services.prefs.setIntPref("browser.cache.disk.capacity", kInitialCapacityKB);
  Services.prefs.setIntPref("browser.cache.disk.max_entry_size", -1);

  let data = makeData(kEntryDataSize, "x");

  let urls = [];
  for (let i = 0; i < kNumFillEntries; i++) {
    urls.push("http://old" + i + "/");
  }

  function fillNext(idx) {
    if (idx >= urls.length) {
      Services.cache2
        .QueryInterface(Ci.nsICacheTesting)
        .flush(makeFlushObserver(afterFill));
      return;
    }
    touchEntry(urls[idx], "m", data, function () {
      fillNext(idx + 1);
    });
  }

  fillNext(0);

  function afterFill() {
    let totalBytes = kNumFillEntries * kEntryDataSize;
    let totalKB = Math.ceil(totalBytes / 1024);
    // Shrink capacity just below current usage so writing the new entry
    // triggers over-limit eviction.  Keep the shrink small so the new
    // entry doesn't push total usage over the 105% hard limit.
    let newCapacity = totalKB - 4;
    Services.prefs.setIntPref("browser.cache.disk.capacity", newCapacity);

    asyncOpenCacheEntry(
      "http://new-entry/",
      "disk",
      Ci.nsICacheStorage.OPEN_TRUNCATE,
      null,
      new OpenCallback(NEW | WAITFORWRITE, "newm", data, function () {
        // Write completed without the entry being doomed (otherwise the
        // OutputStream operations inside OpenCallback would have failed).
        // Restore a large capacity before the read-back so any in-flight
        // over-limit eviction stops, preventing it from picking the new
        // entry (now the lowest-frecency entry without an active handle).
        Services.prefs.setIntPref(
          "browser.cache.disk.capacity",
          kInitialCapacityKB
        );

        asyncOpenCacheEntry(
          "http://new-entry/",
          "disk",
          Ci.nsICacheStorage.OPEN_NORMALLY,
          null,
          new OpenCallback(NORMAL, "newm", data, function () {
            finish_cache2_test();
          })
        );
      })
    );
  }

  do_test_pending();
}

function makeFlushObserver(callback) {
  return {
    observe() {
      executeSoon(callback);
    },
  };
}
