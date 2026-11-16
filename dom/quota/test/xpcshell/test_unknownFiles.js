/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * This test is mainly to verify that init, initTemporaryStorage,
 * getUsageForPrincipal and clearStoragesForPrincipal are able to ignore
 * unknown files and directories in the storage/default directory and its
 * subdirectories.
 */
async function testSteps() {
  const principal = getPrincipal("http://example.com");

  async function testFunctionality(testFunction) {
    const modes = [
      {
        initializedStorage: false,
        initializedPersistentStorage: false,
        initializedTemporaryStorage: false,
      },
      {
        initializedStorage: true,
        initializedPersistentStorage: false,
        initializedTemporaryStorage: false,
      },
      {
        initializedStorage: true,
        initializedPersistentStorage: false,
        initializedTemporaryStorage: true,
      },
    ];

    for (const mode of modes) {
      info("Clearing");

      let request = clear();
      await requestFinished(request);

      info("Installing package");

      // The profile contains unknown files and unknown directories placed
      // across the repositories, origin directories and client directories.
      // The file make_unknownFiles.js was run locally, specifically it was
      // temporarily enabled in xpcshell.toml and then executed:
      // mach test --interactive dom/quota/test/xpcshell/make_unknownFiles.js
      // Make sure to temporarly remove the `run-if = [ "false" ]` before
      // executing this command
      // Run _execute_test() and quit()
      // Tweak the generated profile. It lives in your temporary profile dir
      // used for tests, which is printed when the test starts ("profile dir is …")
      // For convenience below is the way to execute this from command line:
      /*
      # Run the generate script, passing _execute_test() and quit() so you don't
      # have to type anything, and parsing the log with grep and awk to retrieve
      # the profile directory
      PROFILE_DIR=$(printf '_execute_test();\nquit();\n' | ./mach test \
        --interactive dom/quota/test/xpcshell/make_unknownFiles.js | grep  \
        "profile dir is" | awk '{print $NF}')
      : Go to profile dir
      cd $PROFILE_DIR
      : Remove the transient SQLite files
      rm -f storage/default/http+++example.com/cache/caches.sqlite-wal \
        storage/default/http+++example.com/idb/2320029346mByDIdnedxe.sqlite-wal
      zip -r -X unknownFiles_profile.zip storage.sqlite storage
      cd -
      cp $PROFILE_DIR/unknownFiles_profile.zip dom/quota/test/xpcshell/
      */
      installPackage("unknownFiles_profile");

      if (mode.initializedStorage) {
        info("Initializing storage");

        request = init();
        await requestFinished(request);
      }

      if (mode.initializedTemporaryStorage) {
        info("Initializing temporary storage");

        request = initTemporaryStorage();
        await requestFinished(request);
      }

      info("Verifying initialization status");

      await verifyInitializationStatus(
        mode.initializedStorage,
        mode.initializedPersistentStorage,
        mode.initializedTemporaryStorage
      );

      await testFunction(
        mode.initializedStorage,
        mode.initializedTemporaryStorage
      );

      info("Clearing");

      request = clear();
      await requestFinished(request);
    }
  }

  // init and initTemporaryStorage functionality is tested in the
  // testFunctionality function as part of the multi mode testing

  info("Testing getUsageForPrincipal functionality");

  await testFunctionality(async function () {
    info("Getting origin usage");

    request = getOriginUsage(principal);
    await requestFinished(request);

    ok(
      request.result instanceof Ci.nsIQuotaOriginUsageResult,
      "The result is nsIQuotaOriginUsageResult instance"
    );
    is(request.result.usage, 147793, "Correct total usage");
    is(request.result.fileUsage, 200, "Correct file usage");
  });

  info("Testing clearStoragesForPrincipal functionality");

  await testFunctionality(async function () {
    info("Clearing origin");

    request = clearOrigin(principal, "default");
    await requestFinished(request);
  });
}
