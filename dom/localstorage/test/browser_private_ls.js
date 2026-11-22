/**
 * This test verifies that:
 * 1. localStorage works in private browsing with full CRUD operations
 *    and data persists within a session (across tab navigations).
 * 2. On-disk storage under storage/private/ uses UUID-based directory names
 *    (not readable origin strings) and database files are encrypted.
 * 3. Cleanup removes storage/private/ when the last private browsing context
 *    exits.
 * 4. A new private browsing session starts with empty localStorage.
 */

/**
 * Thin wrapper around `tab.linkedBrowser`'s `content.localStorage`. Exposes
 * the localStorage API surface (setItem/getItem/removeItem/clear/length) so
 * call sites read like the real Web API instead of using ad-hoc helper
 * function names.
 */
class TabLocalStorage {
  constructor(tab) {
    this.browser = tab.linkedBrowser;
  }

  setItem(key, value) {
    return SpecialPowers.spawn(this.browser, [key, value], (k, v) => {
      content.localStorage.setItem(k, v);
    });
  }

  getItem(key) {
    return SpecialPowers.spawn(this.browser, [key], k => {
      return content.localStorage.getItem(k);
    });
  }

  removeItem(key) {
    return SpecialPowers.spawn(this.browser, [key], k => {
      content.localStorage.removeItem(k);
    });
  }

  clear() {
    return SpecialPowers.spawn(this.browser, [], () => {
      content.localStorage.clear();
    });
  }

  get length() {
    return SpecialPowers.spawn(this.browser, [], () => {
      return content.localStorage.length;
    });
  }
}

// LocalStorage only flushes to disk on a 5s timer (kFlushTimeoutMs in
// dom/localstorage/ActorsParent.cpp) and exposes no API to force an earlier
// flush, so on-disk state appears (and, on cleanup, disappears) asynchronously.
// The default waitForCondition (50 tries x 100ms = 5s) races that timer and
// intermittently times out exactly when the flush lands; poll well past it with
// margin for slow CI instead.
const DISK_POLL_INTERVAL_MS = 250;
const DISK_POLL_MAX_TRIES = 120; // ~30s

function privateStorageDir() {
  let profileDir = Services.dirsvc.get("ProfD", Ci.nsIFile);
  profileDir.append("storage");
  profileDir.append("private");
  return profileDir;
}

// The SQLite file format (https://sqlite.org/fileformat.html) fully specifies
// the layout of the first 100 bytes of every SQLite database. We use those
// invariants to detect that the on-disk file is not a plaintext SQLite
// database, which is sufficient to conclude it has been encrypted.
//
// Even though the LS encryption layer leaves the first 32 bytes (the SQLite
// header magic + page-size fields) in cleartext, the bytes from offset 32
// onward are encrypted. Within that encrypted range, the SQLite spec fixes
// two strong invariants that cannot survive encryption:
//
//   * Bytes 72..91 are documented as "Reserved for expansion. Must be zero."
//     20 contiguous zero bytes in a fixed location is virtually impossible
//     for encrypted output to match by chance.
//   * Bytes 56..59 encode the database text encoding as a big-endian 4-byte
//     integer that must be exactly 1, 2, or 3.
//
// If either invariant is broken, the bytes beyond the cleartext header are
// not plaintext SQLite content, so the file is encrypted.
function looksEncrypted(bytes) {
  if (bytes.length < 100) {
    return false;
  }

  // bytes 72..91 must be all zeros in an unencrypted SQLite file.
  for (let i = 72; i < 92; i++) {
    if (bytes[i] !== 0) {
      return true;
    }
  }

  // bytes 56..59 must encode text encoding (1, 2 or 3) big-endian.
  const textEncoding =
    (bytes[56] << 24) | (bytes[57] << 16) | (bytes[58] << 8) | bytes[59];
  if (textEncoding < 1 || textEncoding > 3) {
    return true;
  }

  return false;
}

// Check whether a directory name matches QuotaManager's PB origin on-disk format:
// `uuid+++<UUID>`.
function isPrivateOriginDir(name) {
  return /^uuid\+\+\+[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(
    name
  );
}

// Find all .sqlite files recursively under a directory.
function findSQLiteFiles(dir) {
  let results = [];
  if (!dir.exists()) {
    return results;
  }
  let entries = dir.directoryEntries;
  while (entries.hasMoreElements()) {
    let entry = entries.nextFile;
    if (entry.isDirectory()) {
      results = results.concat(findSQLiteFiles(entry));
    } else if (entry.leafName.endsWith(".sqlite")) {
      results.push(entry);
    }
  }
  return results;
}

async function waitForPrivateStorageCleanup() {
  let storageDir = privateStorageDir();
  if (storageDir.exists()) {
    await TestUtils.waitForCondition(
      () => !storageDir.exists(),
      "Waiting for storage/private/ directory to be removed",
      DISK_POLL_INTERVAL_MS,
      DISK_POLL_MAX_TRIES
    );
  }
}

/**
 * Test 1: Full CRUD operations and cross-tab persistence within a session.
 */
add_task(async function test_crud_and_cross_tab() {
  const pageUrl =
    "http://example.com/browser/dom/localstorage/test/page_private_ls.html";

  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  let tab1 = await BrowserTestUtils.openNewForegroundTab(
    privateWin.gBrowser,
    pageUrl
  );

  let storage1 = new TabLocalStorage(tab1);

  is(await storage1.length, 0, "Storage starts empty in new PB session");

  // setItem
  await storage1.setItem("foo", "bar");
  await storage1.setItem("key2", "úžasné");
  await storage1.setItem("toRemove", "temporary");

  is(await storage1.getItem("foo"), "bar", "Can read back written value");
  is(await storage1.length, 3, "Length is correct after writes");

  // removeItem
  await storage1.removeItem("toRemove");
  is(await storage1.getItem("toRemove"), null, "Removed item returns null");
  is(await storage1.length, 2, "Length decremented after removeItem");

  // Overwrite existing key
  await storage1.setItem("foo", "baz");
  is(await storage1.getItem("foo"), "baz", "Overwritten value is updated");
  is(await storage1.length, 2, "Length unchanged after overwrite");

  // Cross-tab visibility
  let tab2 = await BrowserTestUtils.openNewForegroundTab(
    privateWin.gBrowser,
    pageUrl
  );
  let storage2 = new TabLocalStorage(tab2);

  is(await storage2.getItem("foo"), "baz", "Updated value is visible in tab2");
  is(
    await storage2.getItem("key2"),
    "úžasné",
    "Non-ASCII data is visible in tab2"
  );
  is(await storage2.length, 2, "Length matches in tab2");

  // clear()
  await storage1.clear();
  is(await storage1.length, 0, "Length is 0 after clear()");
  is(await storage1.getItem("foo"), null, "Data gone after clear()");

  // Verify clear is visible in other tab.
  is(await storage2.length, 0, "clear() visible in tab2");

  BrowserTestUtils.removeTab(tab2);
  await BrowserTestUtils.closeWindow(privateWin);

  await waitForPrivateStorageCleanup();
});

/**
 * Test 2: On-disk storage uses UUID directories and encrypted database files.
 *
 * This test verifies the on-disk storage structure for PB localStorage.
 * PB localStorage data is stored on disk under storage/private/ with
 * UUID-based directory names and encrypted SQLite database files.
 */
add_task(async function test_disk_structure_and_encryption() {
  const pageUrl =
    "http://example.com/browser/dom/localstorage/test/page_private_ls.html";

  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  let tab = await BrowserTestUtils.openNewForegroundTab(
    privateWin.gBrowser,
    pageUrl
  );
  let storage = new TabLocalStorage(tab);

  // Write data to trigger on-disk storage creation.
  await storage.setItem("testKey", "testValue");

  let storageDir = privateStorageDir();

  // PB data is written to disk asynchronously. Wait for the directory to
  // exist; if it never appears, treat that as a real test failure (it would
  // mean the PBM-persistent-LS code path is broken).
  await TestUtils.waitForCondition(
    () => storageDir.exists(),
    "Waiting for storage/private/ directory to be created",
    DISK_POLL_INTERVAL_MS,
    DISK_POLL_MAX_TRIES
  );

  // Check directory structure: immediate children should be UUID-named.
  let originDirs = [];
  let entries = storageDir.directoryEntries;
  while (entries.hasMoreElements()) {
    let entry = entries.nextFile;
    if (entry.isDirectory()) {
      originDirs.push(entry);
    }
  }

  Assert.greater(originDirs.length, 0, "At least one origin directory exists");

  for (let dir of originDirs) {
    ok(
      isPrivateOriginDir(dir.leafName),
      `Origin directory '${dir.leafName}' matches uuid+++<UUID> format`
    );
  }

  // Find .sqlite files and verify they are encrypted by checking that the
  // first 100 bytes violate the SQLite file format's documented invariants
  // (see comment on looksEncrypted above). LS creates data.sqlite empty and
  // writes its first page only after syncing the rollback journal, so wait
  // for the file to hold the bytes we check rather than just for it to exist.
  await TestUtils.waitForCondition(
    () => {
      let files = findSQLiteFiles(storageDir);
      return files.length && files.every(file => file.fileSize >= 100);
    },
    "Waiting for the LS .sqlite file to be flushed to disk",
    DISK_POLL_INTERVAL_MS,
    DISK_POLL_MAX_TRIES
  );
  let sqliteFiles = findSQLiteFiles(storageDir);

  for (let file of sqliteFiles) {
    // IOUtils.read returns a Uint8Array and handles file open/close itself,
    // avoiding the manual nsIInputStream/nsIBinaryInputStream lifecycle that
    // could leak handles on Windows.
    let headBytes = await IOUtils.read(file.path, { maxBytes: 100 });
    ok(
      looksEncrypted(headBytes),
      `Database file '${file.leafName}' has encrypted content`
    );
  }

  await BrowserTestUtils.closeWindow(privateWin);

  await waitForPrivateStorageCleanup();
});

/**
 * Test 3: Session isolation - new PB session starts with empty localStorage.
 */
add_task(async function test_session_isolation() {
  const pageUrl =
    "http://example.com/browser/dom/localstorage/test/page_private_ls.html";

  // Session 1: Write data.
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  let tab = await BrowserTestUtils.openNewForegroundTab(
    privateWin.gBrowser,
    pageUrl
  );
  let storage = new TabLocalStorage(tab);

  await storage.setItem("sessionData", "secret");
  is(
    await storage.getItem("sessionData"),
    "secret",
    "Data written in session 1"
  );

  await BrowserTestUtils.closeWindow(privateWin);

  await waitForPrivateStorageCleanup();

  // Session 2: Verify empty.
  privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  tab = await BrowserTestUtils.openNewForegroundTab(
    privateWin.gBrowser,
    pageUrl
  );
  storage = new TabLocalStorage(tab);

  is(await storage.length, 0, "Storage is empty in new PB session");
  is(await storage.getItem("sessionData"), null, "Data from session 1 is gone");

  await BrowserTestUtils.closeWindow(privateWin);
});
