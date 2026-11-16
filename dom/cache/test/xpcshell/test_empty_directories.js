/**
 *  This test is mainly to verify cache won't leave emptry directory.
 */

function resetStorage() {
  return new Promise(function (resolve) {
    var qms = Services.qms;
    var request = qms.reset();
    request.callback = resolve;
  });
}

async function setUpEnv() {
  Services.prefs.setBoolPref("dom.quotaManager.testing", true);

  // We need this for generating the basic profile path
  create_test_profile("schema_25_profile.zip");

  // Trigger storage upgrade
  await caches.open("test");
  const cacheDir = getCacheDir();
  let morgueDir = cacheDir.clone();
  morgueDir.append("morgue");

  // clean the cache directoy
  for (let dir of morgueDir.directoryEntries) {
    dir.remove(true);
  }

  await resetStorage();
}

// This function ensure the directory with file shouldn't have been deleted and
// ensure the directory without file should've been deleted.
function verifyResult() {
  const cacheDir = getCacheDir();
  let morgueDir = cacheDir.clone();
  morgueDir.append("morgue");

  let foundEmpty = false;
  for (let dir of morgueDir.directoryEntries) {
    let empty = true;
    // eslint-disable-next-line no-unused-vars
    for (let file of dir.directoryEntries) {
      empty = false;
    }

    foundEmpty = foundEmpty || empty;
  }
  return !foundEmpty;
}

add_task(async function testSteps() {
  const url = "https://www.mozilla.org";

  info("Setting up environment");

  await setUpEnv();

  info("Test 0 - Upgrade from 25 to 26");

  let cache = await caches.open("test");
  let response = await cache.match(url);
  ok(!!response, "Upgrade from 25 to 26 do succeed");

  info("Test 1 - BodyDeleteFiles shouldn't leave an empty directory");

  await cache.put(url, response.clone());
  // eslint-disable-next-line no-unused-vars
  let r = await cache.match(url);
  await cache.delete(url);
  await resetStorage();

  cache = await caches.open("test");

  // Extra operation to ensure the deletion is completed
  await cache.match(url);

  ok(verifyResult(), "Empty directory should be removed");

  await caches.delete("test");
});
