/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

const { FileUtils } = ChromeUtils.importESModule(
  "resource://testing-common/dom/quota/test/modules/FileUtils.sys.mjs"
);
const { PrincipalUtils } = ChromeUtils.importESModule(
  "resource://testing-common/dom/quota/test/modules/PrincipalUtils.sys.mjs"
);
const { QuotaUtils } = ChromeUtils.importESModule(
  "resource://testing-common/dom/quota/test/modules/QuotaUtils.sys.mjs"
);

/**
 * This test is mainly to verify that L2 related cached usage information is
 * stable across a forced metadata restoration and consists of these steps:
 *  - Install a packaged profile
 *  - Initialize storage and temporary storage
 *  - Get full origin metadata (cached in memory)
 *  - Shutdown storage
 *  - Remove the origin metadata file on disk (forces restoration)
 *  - Initialize storage and temporary storage
 *  - Get full origin metadata (cached in memory)
 *  - Compare cached usage information
 *
 * This test is intended to catch changes that would require bumping
 * kCurrentQuotaVersion.
 *
 * If the comparison of cached usage information fails, it is very likely that
 * kCurrentQuotaVersion needs to be bumped.
 *
 * See also the documentation for kCurrentQuotaVersion in Constants.h.
 *
 * Note: This test currently contains only the essential coverage for
 * detecting L2 cache validity issues. In the future, the packaged profile
 * could be generated using more comprehensive data creation methods, and a
 * separate test could be added to leverage the conditioned profiles
 * infrastructure. However, a custom test with its own pre-packaged profile
 * will still be needed, since we do not have control over the contents of
 * conditioned profiles.
 */
async function testSecondaryCacheValidity() {
  const principal = PrincipalUtils.createPrincipal("http://example42.com");
  const metadata = FileUtils.getFile(
    "storage/default/http+++example42.com/.metadata-v2"
  );

  info("Clearing storage");

  {
    const request = Services.qms.clear();
    await QuotaUtils.requestFinished(request);
  }

  info("Installing package");

  // The profile contains a single origin directory in the default repository
  // populated with basic data for all quota clients. It was created locally
  // using the make_secondaryCacheValidity.js script, which can be
  // executed like this:
  // mach xpcshell-test --interactive dom/quota/test/xpcshell/make_secondaryCacheValidity.js
  // Make sure to temporarly remove the `run-if = [ "false" ]` before executing
  // this command
  // Run _execute_test() and quit()
  // Tweak the generated profile. It lives in your temporary profile dir used
  // for tests, which is printed when the test starts ("profile dir is …")
  // 1. Remove the folder "storage/temporary".
  // 2. Remove the file "storage/ls-archive.sqlite".
  // 3. Remove the file "storage/default/http+++example42.com/cache/caches.sqlite-wal".
  // 4. Remove the file "storage/default/http+++example42.com/idb/2320029346mByDIdnedxe.sqlite-wal".
  // For convenience below is the way to execute this from command line:
  /*
  # Run the generate script, passing _execute_test() and quit() so you don't
  # have to type anything, and parsing the log with grep and awk to retrieve
  # the profile directory
  PROFILE_DIR=$(printf '_execute_test();\nquit();\n' | ./mach test \
    --interactive dom/quota/test/xpcshell/make_secondaryCacheValidity.js | \
    grep "profile dir is" | awk '{print $NF}')
  : Go to profile dir
  cd $PROFILE_DIR
  : Remove the unneeded files
  rm -r storage/temporary storage/ls-archive.sqlite \
    storage/default/http+++example42.com/cache/caches.sqlite-wal \
    storage/default/http+++example42.com/idb/2320029346mByDIdnedxe.sqlite-wal
  : Package only storage.sqlite and storage/
  zip -r -X secondaryCacheValidity_profile.zip storage.sqlite storage
  : Copy the new profile
  cd -
  cp $PROFILE_DIR/secondaryCacheValidity_profile.zip dom/quota/test/xpcshell/
  */
  installPackage("secondaryCacheValidity_profile");

  info("Initializing storage");

  {
    const request = Services.qms.init();
    await QuotaUtils.requestFinished(request);
  }

  info("Initializing temporary storage");

  {
    const request = Services.qms.initTemporaryStorage();
    await QuotaUtils.requestFinished(request);
  }

  info("Getting full origin metadata");

  {
    const request = Services.qms.getFullOriginMetadata("default", principal);
    await QuotaUtils.requestFinished(request);
  }

  info("Getting full origin metadata");

  const fullOriginMetadataBefore = await (async function () {
    const request = Services.qms.getFullOriginMetadata("default", principal);
    await QuotaUtils.requestFinished(request);
    return request.result;
  })();

  info("Shutting down storage");

  {
    const request = Services.qms.reset();
    await QuotaUtils.requestFinished(request);
  }

  info("Removing origin metadata");

  metadata.remove(false);

  info("Initializing storage");

  {
    const request = Services.qms.init();
    await QuotaUtils.requestFinished(request);
  }

  info("Initializing temporary storage");

  {
    const request = Services.qms.initTemporaryStorage();
    await QuotaUtils.requestFinished(request);
  }

  info("Getting full origin metadata");

  const fullOriginMetadataAfter = await (async function () {
    const request = Services.qms.getFullOriginMetadata("default", principal);
    await QuotaUtils.requestFinished(request);
    return request.result;
  })();

  info("Verifying full origin metadata");

  Assert.notEqual(
    BigInt(fullOriginMetadataBefore.lastAccessTime),
    BigInt(fullOriginMetadataAfter.lastAccessTime),
    "Last access times are not the same"
  );

  info("Client usages before: " + fullOriginMetadataBefore.clientUsages);
  info("Client usages after: " + fullOriginMetadataAfter.clientUsages);

  Assert.equal(
    fullOriginMetadataBefore.clientUsages,
    fullOriginMetadataAfter.clientUsages,
    "Client usages are the same"
  );

  info("Origin usgae before: " + fullOriginMetadataBefore.originUsage);
  info("Origin usage after: " + fullOriginMetadataAfter.originUsage);

  Assert.equal(
    fullOriginMetadataBefore.originUsage,
    fullOriginMetadataAfter.originUsage,
    "Origin usage is the same"
  );
}

/* exported testSteps */
async function testSteps() {
  add_task(
    {
      pref_set: [["dom.quotaManager.loadQuotaFromCache", false]],
    },
    testSecondaryCacheValidity
  );
}
