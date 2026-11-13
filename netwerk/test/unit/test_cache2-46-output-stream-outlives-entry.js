"use strict";

// A cache file kept alive by a closed output stream past the release of the
// last CacheEntry at XPCOM shutdown must not trip the "Network cache reported
// memory consumption is not at 0" assertion (bug 1994039). Both objects are
// parked in the shared system global so they are only released by the final
// shutdown cycle collection, after the service manager dropped the service.
// The entry is listed first so that it is finalized before the stream.

add_task(async function test_output_stream_outlives_entry() {
  do_get_profile();

  let storage = Services.cache2.diskCacheStorage(
    Services.loadContextInfo.default
  );
  let entry = await new Promise(resolve => {
    storage.asyncOpenURI(
      Services.io.newURI("http://outlives-entry/"),
      "",
      Ci.nsICacheStorage.OPEN_NORMALLY,
      {
        onCacheEntryCheck() {
          return Ci.nsICacheEntryOpenCallback.ENTRY_WANTED;
        },
        onCacheEntryAvailable(aEntry, aNew, aStatus) {
          Assert.equal(aStatus, Cr.NS_OK);
          Assert.ok(aNew);
          resolve(aEntry);
        },
      }
    );
  });

  entry.setMetaDataElement("meta", "x".repeat(1024));
  let outputStream = entry.openOutputStream(0, -1);
  let data = "y".repeat(1024);
  outputStream.write(data, data.length);
  outputStream.close();

  let sharedGlobal = Cu.getGlobalForObject(Services);
  sharedGlobal.gCacheTestHolder = new sharedGlobal.Array(entry, outputStream);
});
