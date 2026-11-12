"use strict";

// Verifies that with disk cache encryption enabled, an entry's metadata and
// data do not appear in plaintext on disk, yet read back correctly through the
// cache API (round-trip).

const MARKER = "C2ENCRYPTIONPLAINTEXTMARKER";
const META = "meta-" + MARKER;
const DATA = "data-" + MARKER + "-padding-so-it-spans-some-bytes-0123456789";

function run_test() {
  // browser.cache.disk.encryption.enabled is set to true via the xpcshell
  // manifest so it is in effect before the profile-do-change that loads the
  // encryption key (a runtime setBoolPref here would be too late).
  do_get_profile();

  let testingInterface = Services.cache2.QueryInterface(Ci.nsICacheTesting);

  // A truncated entry is handed to the consumer before its file has been
  // opened, so metadata and data are normally written into memory first and
  // the file catches up later. Block the I/O thread at the OPEN level to make
  // that ordering deterministic instead of leaving it to how busy the machine
  // is. Levels below OPEN still run, so the key load -- dispatched at
  // OPEN_PRIORITY -- completes before this takes hold.
  testingInterface.suspendCacheIOThread(3);

  asyncOpenCacheEntry(
    "http://encrypted/",
    "disk",
    Ci.nsICacheStorage.OPEN_TRUNCATE,
    null,
    new OpenCallback(NEW | WAITFORWRITE, META, DATA, function () {
      testingInterface.resumeCacheIOThread();
      // Force pending metadata and data to disk before inspecting the files.
      // A flush only waits for writes that are already queued, and the data is
      // only queued for writing once the file has been opened, which can
      // happen after this first flush is dispatched. The second flush is
      // dispatched after the file has been opened, so it waits for that write.
      testingInterface.flush(
        makeFlushObserver(() => {
          testingInterface.flush(makeFlushObserver(afterFlush));
        })
      );
    })
  );

  do_test_pending();
}

function afterFlush() {
  // Check first that the entry really reached the disk. If it had fallen back
  // to memory-only -- which is what happens when the cache is used before the
  // encryption key is loaded -- there would be nothing on disk to scan and the
  // marker assertion below would pass for the wrong reason.
  Assert.greater(
    diskCacheEntryFileCount(),
    0,
    "the entry must have been written to disk, not kept memory-only"
  );

  Assert.ok(
    !cacheDirContainsMarker(MARKER),
    "encrypted cache must not contain the plaintext marker on disk"
  );

  // Reading back through the API must restore the plaintext metadata and data.
  // OpenCallback asserts the metadata element and data match what was written.
  asyncOpenCacheEntry(
    "http://encrypted/",
    "disk",
    Ci.nsICacheStorage.OPEN_NORMALLY,
    null,
    new OpenCallback(NORMAL, META, DATA, function () {
      finish_cache2_test();
    })
  );
}

function makeFlushObserver(callback) {
  return {
    observe(subject, topic) {
      // Flush() never removes its observer, so remove it here, or it would also
      // be notified by the next flush.
      Services.obs.removeObserver(this, topic);
      executeSoon(callback);
    },
  };
}

function diskCacheEntryFileCount() {
  let dir = getDiskCacheDirectory();
  if (!dir.exists()) {
    return 0;
  }
  dir.append("entries");
  if (!dir.exists()) {
    return 0;
  }
  let count = 0;
  let entries = dir.directoryEntries;
  while (entries.hasMoreElements()) {
    if (entries.nextFile.fileSize > 0) {
      count++;
    }
  }
  return count;
}

function cacheDirContainsMarker(marker) {
  let dir = getDiskCacheDirectory();
  if (!dir.exists()) {
    return false;
  }
  return dirContainsMarker(dir, marker);
}

function dirContainsMarker(dir, marker) {
  let entries = dir.directoryEntries;
  while (entries.hasMoreElements()) {
    let file = entries.nextFile;
    if (file.isDirectory()) {
      if (dirContainsMarker(file, marker)) {
        return true;
      }
    } else if (fileContains(file, marker)) {
      return true;
    }
  }
  return false;
}

function fileContains(file, marker) {
  let fstream = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(
    Ci.nsIFileInputStream
  );
  fstream.init(file, -1, 0, 0);
  let bstream = Cc["@mozilla.org/binaryinputstream;1"].createInstance(
    Ci.nsIBinaryInputStream
  );
  bstream.setInputStream(fstream);
  let available = bstream.available();
  let bytes = available ? bstream.readBytes(available) : "";
  bstream.close();
  return bytes.includes(marker);
}
