# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Verifies that flipping the at-rest disk cache encryption pref
# (browser.cache.disk.encryption.enabled) purges the disk cache on the next
# startup. The encryption state the cache was written with is stored in the
# index header (mIsEncrypted); when it no longer matches the pref at startup the
# whole cache is purged, so the cache never holds a mix of encrypted and
# plaintext entries.

import time
from pathlib import Path

from marionette_harness import MarionetteTestCase

URL = "http://cache-encryption-flip-test.example/resource"
DATA = "cache-encryption-flip-test-payload-0123456789"

# Writes a disk cache entry for the given url and resolves once it is stored.
WRITE_SCRIPT = """
const [url, data, resolve] = arguments;
const uri = Services.io.newURI(url);
const storage = Services.cache2.diskCacheStorage(Services.loadContextInfo.default);
storage.asyncOpenURI(uri, "", Ci.nsICacheStorage.OPEN_TRUNCATE, {
  QueryInterface: ChromeUtils.generateQI(["nsICacheEntryOpenCallback"]),
  onCacheEntryCheck() {
    return Ci.nsICacheEntryOpenCallback.ENTRY_WANTED;
  },
  onCacheEntryAvailable(entry, isNew, status) {
    try {
      entry.setMetaDataElement("test", "1");
      entry.metaDataReady();
      const os = entry.openOutputStream(0, -1);
      os.write(data, data.length);
      os.close();
      resolve(true);
    } catch (e) {
      resolve("error: " + e);
    }
  },
});
"""

# Resolves true iff a disk cache entry for the given url exists.
EXISTS_SCRIPT = """
const [url, resolve] = arguments;
const uri = Services.io.newURI(url);
const storage = Services.cache2.diskCacheStorage(Services.loadContextInfo.default);
storage.asyncOpenURI(uri, "", Ci.nsICacheStorage.OPEN_READONLY, {
  QueryInterface: ChromeUtils.generateQI(["nsICacheEntryOpenCallback"]),
  onCacheEntryCheck() {
    return Ci.nsICacheEntryOpenCallback.ENTRY_WANTED;
  },
  onCacheEntryAvailable(entry, isNew, status) {
    resolve(!!entry && Components.isSuccessCode(status));
  },
});
"""


class CacheEncryptionFlipPurgeTestCase(MarionetteTestCase):
    def setUp(self):
        super().setUp()
        self.marionette.enforce_gecko_prefs({
            "browser.cache.disk.enable": True,
            # Start unencrypted.
            "browser.cache.disk.encryption.enabled": False,
            # Force the index to be written back to disk eagerly so it
            # exists (carrying the encryption flag) before we restart: start the
            # index build immediately and write it after a single change.
            "browser.cache.disk.index.update_start_delay_ms": 0,
            "browser.cache.disk.index.min_unwritten_changes": 1,
            "browser.cache.disk.index.min_dump_interval_ms": 0,
            # Keep anything else from clearing the cache on shutdown.
            "privacy.sanitize.sanitizeOnShutdown": False,
            "privacy.clearOnShutdown.cache": False,
        })
        self.index_path = Path(self.marionette.profile_path).joinpath("cache2", "index")
        self.marionette.set_context("chrome")

    def tearDown(self):
        self.marionette.restart(in_app=False, clean=True)
        super().tearDown()

    def write_entry(self, url):
        result = self.marionette.execute_async_script(
            WRITE_SCRIPT, script_args=(url, DATA)
        )
        self.assertEqual(result, True, "writing the cache entry should succeed")

    def entry_exists(self, url):
        return self.marionette.execute_async_script(EXISTS_SCRIPT, script_args=(url,))

    def populate_and_persist_index(self):
        # The index is written back to disk only from the READY state (after the
        # initial build finishes), and only as a side effect of an entry
        # operation. The write thresholds are lowered to 1 change / 0ms in
        # setUp, so keep writing entries until the index file appears on disk.
        # Sample existence only once per iteration: each index rewrite removes
        # the old file before renaming index.tmp over it, so a second check
        # could hit that window.
        self.write_entry(URL)
        deadline = time.monotonic() + 30
        i = 0
        index_written = self.index_path.exists()
        while not index_written and time.monotonic() < deadline:
            self.write_entry(f"{URL}-{i}")
            i += 1
            time.sleep(0.2)
            index_written = self.index_path.exists()
        self.assertTrue(
            index_written,
            "the disk cache index file must be written to disk",
        )

    def test_purge_on_encryption_flip(self):
        # 1. Populate the (unencrypted) cache and persist the index.
        self.populate_and_persist_index()
        self.assertTrue(
            self.entry_exists(URL), "entry should exist after being written"
        )

        # 2. Control: restart without changing the pref. The index's stored
        #    encryption flag still matches the pref, so the entry must survive.
        self.marionette.restart(in_app=True, clean=False)
        self.marionette.set_context("chrome")
        self.assertTrue(
            self.entry_exists(URL),
            "entry must survive a restart when the encryption pref is unchanged",
        )

        # 3. Flip encryption on and restart. The index was written with the flag
        #    off, so at startup it no longer matches the pref and the whole cache
        #    must be purged.
        self.marionette.enforce_gecko_prefs({
            "browser.cache.disk.encryption.enabled": True
        })
        self.marionette.restart(in_app=True, clean=False)
        self.marionette.set_context("chrome")
        self.assertFalse(
            self.entry_exists(URL),
            "entry must be gone after flipping encryption purges the cache",
        )
