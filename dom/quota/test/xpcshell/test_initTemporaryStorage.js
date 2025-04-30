/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * This test is mainly to verify initTemporaryStorage() does call
 * QuotaManager::EnsureTemporaryStorageIsInitializedInternal() which does
 * various things, for example, it restores the directory metadata if it's
 * broken or missing.
 */

async function testSteps() {
  const originDirPath = "storage/default/https+++foo.example.com";
  const metadataFileName = ".metadata-v2";

  info("Setting prefs");

  Services.prefs.setBoolPref("dom.quotaManager.loadQuotaFromCache", false);

  info("Initializing");

  let request = init();
  await requestFinished(request);

  info("Verifying initialization status");

  await verifyInitializationStatus(true, false, false);

  info("Creating an empty directory");

  let originDir = getRelativeFile(originDirPath);
  originDir.create(Ci.nsIFile.DIRECTORY_TYPE, parseInt("0755", 8));

  info("Initializing the temporary storage");

  request = initTemporaryStorage();
  await requestFinished(request);

  info(
    "Verifying directory metadata was restored after calling " +
      "initTemporaryStorage()"
  );

  let metadataFile = originDir.clone();
  metadataFile.append(metadataFileName);

  ok(metadataFile.exists(), "Directory metadata file does exist");

  info("Verifying initialization status");

  await verifyInitializationStatus(true, false, true);
}
