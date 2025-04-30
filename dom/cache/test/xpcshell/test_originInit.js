/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

async function testSteps() {
  // ToDo: Replace storage and default with a getter function once we expose the
  // filenames of them to a IDL file.
  const basePath = `${storageDirName}/${defaultPersistenceDirName}/`;
  const principal = getPrincipal("https://example.com");
  const originDirName = "https+++example.com";

  // ToDo: Replace caches.sqlite with a getter function once we expose the
  // filename of it to a IDL file.
  const cachesDatabase = getRelativeFile(
    `${basePath}/${originDirName}/${cacheClientDirName()}/caches.sqlite`
  );
  // ToDo: Replace .padding with a getter function once we expose the filename
  // of it to a IDL file.
  const paddingFile = getRelativeFile(
    `${basePath}/${originDirName}/${cacheClientDirName()}/.padding`
  );
  // ToDo: Replace .padding-tmp with a getter function once we expose the
  // filename of it to a IDL file.
  const paddingTempFile = getRelativeFile(
    `${basePath}/${originDirName}/${cacheClientDirName()}/.padding-tmp`
  );
  // ToDo: Replace morgue with a getter function once we expose the
  // filename of it to a IDL file.
  const morgueDir = getRelativeFile(
    `${basePath}/${originDirName}/${cacheClientDirName()}/morgue`
  );

  const persistentCacheDir = getRelativeFile(
    `${storageDirName}/${persistentPersistenceDirName}/${originDirName}/` +
      `${cacheClientDirName()}`
  );

  async function createNormalCacheOrigin() {
    async function sandboxScript() {
      const cache = await caches.open("myCache");
      const request = new Request("https://example.com/index.html");
      const response = new Response("hello world");
      await cache.put(request, response);
    }

    const sandbox = new Cu.Sandbox(principal, {
      wantGlobalProperties: ["caches", "fetch"],
    });

    const promise = new Promise(function (resolve, reject) {
      sandbox.resolve = resolve;
      sandbox.reject = reject;
    });

    Cu.evalInSandbox(
      sandboxScript.toSource() + " sandboxScript().then(resolve, reject);",
      sandbox
    );
    await promise;

    let request = reset();
    await requestFinished(request);
  }

  async function createPersistentTestOrigin() {
    let database = getSimpleDatabase(principal, "persistent");

    let request = database.open("data");
    await requestFinished(request);

    request = reset();
    await requestFinished(request);
  }

  function removeFile(file) {
    file.remove(false);
  }

  function removeDir(dir) {
    dir.remove(true);
  }

  function createEmptyFile(file) {
    file.create(Ci.nsIFile.NORMAL_FILE_TYPE, 0o644);
  }

  function createEmptyDirectory(dir) {
    dir.create(Ci.nsIFile.DIRECTORY_TYPE, 0o0755);
  }

  async function initTestOrigin() {
    let request = initStorage();
    await requestFinished(request);

    request = initTemporaryStorage();
    await requestFinished(request);

    request = initTemporaryOrigin(principal);
    await requestFinished(request);
  }

  async function initPersistentTestOrigin() {
    let request = initStorage();
    await requestFinished(request);

    request = initPersistentOrigin(principal);
    await requestFinished(request);
  }

  function checkFiles(
    expectCachesDatabase,
    expectPaddingFile,
    expectTempPaddingFile,
    expectMorgueDir
  ) {
    let exists = cachesDatabase.exists();
    if (expectCachesDatabase) {
      ok(exists, "caches.sqlite does exist");
    } else {
      ok(!exists, "caches.sqlite doesn't exist");
    }

    exists = paddingFile.exists();
    if (expectPaddingFile) {
      ok(exists, ".padding does exist");
    } else {
      ok(!exists, ".padding doesn't exist");
    }

    exists = paddingTempFile.exists();
    if (expectTempPaddingFile) {
      ok(exists, ".padding-tmp does exist");
    } else {
      ok(!exists, ".padding-tmp doesn't exist");
    }

    exists = morgueDir.exists();
    if (expectMorgueDir) {
      ok(exists, "morgue does exist");
    } else {
      ok(!exists, "moruge doesn't exist");
    }
  }

  async function clearTestOrigin() {
    let request = clearOrigin(principal, defaultPersistence);
    await requestFinished(request);
  }

  async function clearPersistentTestOrigin() {
    let request = clearOrigin(principal, persistentPersistence);
    await requestFinished(request);
  }

  async function testOriginInit(
    createCachesDatabase,
    createPaddingFile,
    createTempPaddingFile,
    createMorgueDir
  ) {
    info(
      `Testing initialization of cache directory when caches.sqlite ` +
        `${createCachesDatabase ? "exists" : "doesn't exist"}, .padding ` +
        `${createPaddingFile ? "exists" : "doesn't exist"}, .padding-tmp ` +
        `${createTempPaddingFile ? "exists" : "doesn't exist"}, morgue ` +
        `${createMorgueDir ? "exists" : "doesn't exist"}`
    );

    await createNormalCacheOrigin();

    checkFiles(true, true, false, true);

    if (!createCachesDatabase) {
      removeFile(cachesDatabase);
    }

    if (!createPaddingFile) {
      removeFile(paddingFile);
    }

    if (createTempPaddingFile) {
      createEmptyFile(paddingTempFile);
    }

    if (!createMorgueDir) {
      removeDir(morgueDir);
    }

    await initTestOrigin();

    checkFiles(
      createCachesDatabase,
      // After the origin is initialized, ".padding" should only exist when
      // "caches.sqlite" exists.
      createCachesDatabase,
      // After the origin is initialized, ".padding-tmp" should have always been
      // removed.
      false,
      createCachesDatabase && createMorgueDir
    );

    await clearTestOrigin();
  }

  // Test all possible combinations.
  for (let createCachesDatabase of [false, true]) {
    for (let createPaddingFile of [false, true]) {
      for (let createTempPaddingFile of [false, true]) {
        for (let createMorgueDir of [false, true]) {
          await testOriginInit(
            createCachesDatabase,
            createPaddingFile,
            createTempPaddingFile,
            createMorgueDir
          );
        }
      }
    }
  }

  // Verify that InitializeOrigin doesn't fail when a
  // storage/permanent/${origin}/cache exists.
  async function testPermanentCacheDir() {
    info(
      "Testing initialization of cache directory placed in permanent origin " +
        "directory"
    );

    await createPersistentTestOrigin();

    createEmptyDirectory(persistentCacheDir);

    try {
      await initPersistentTestOrigin();

      ok(true, "Should not have thrown");
    } catch (ex) {
      ok(false, "Should not have thrown");
    }

    let exists = persistentCacheDir.exists();
    ok(exists, "cache directory in permanent origin directory does exist");

    await clearPersistentTestOrigin();
  }

  await testPermanentCacheDir();
}

add_task(
  {
    pref_set: [["dom.quotaManager.loadQuotaFromCache", false]],
  },
  testSteps
);
