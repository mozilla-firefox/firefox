/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { TaskbarTabsRegistry, TaskbarTabsRegistryStorage } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/taskbartabs/TaskbarTabsRegistry.sys.mjs"
  );
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

function testFile() {
  let path = do_get_tempdir();
  let filename = Services.uuid.generateUUID().toString().slice(1, -1);
  path.append(filename + ".json");

  registerCleanupFunction(() => {
    if (path.exists()) {
      path.remove(false);
    }
  });

  return path;
}

add_task(async function test_create_taskbar_tab() {
  const url = Services.io.newURI("https://www.test.com/start");
  const userContextId = 0; // Default container.

  const registry = createInMemoryRegistry();

  Assert.ok(
    !registry.findTaskbarTab(url, userContextId),
    "Initially, no Taskbar Tab should exist for the given URL and container."
  );

  const taskbarTab = createTaskbarTab(registry, url, userContextId);

  Assert.ok(taskbarTab, "Taskbar Tab should be created.");
  Assert.deepEqual(
    registry.findTaskbarTab(url, userContextId),
    taskbarTab,
    "Found Taskbar Tab should match the one returned on creation."
  );

  const secondUrl = Services.io.newURI("https://www.another-test.com/start");
  const secondUserContextId = 1;
  const secondTaskbarTab = createTaskbarTab(
    registry,
    secondUrl,
    secondUserContextId
  );
  Assert.deepEqual(
    registry.findTaskbarTab(url, userContextId),
    taskbarTab,
    "First Taskbar Tab created should still be present."
  );
  Assert.deepEqual(
    registry.findTaskbarTab(secondUrl, secondUserContextId),
    secondTaskbarTab,
    "Second Taskbar Tab created should still be present."
  );

  const repeated = registry.findOrCreateTaskbarTab(
    secondUrl,
    secondUserContextId
  );
  Assert.equal(
    repeated.created,
    false,
    "The existing taskbar tab should have been found, not created"
  );
  Assert.deepEqual(
    repeated.taskbarTab,
    secondTaskbarTab,
    "Should have found the second created Taskbar Tab instead of creating a new Taskbar Tab."
  );
});

add_task(async function test_remove_taskbar_tab() {
  const url = Services.io.newURI("https://www.test.com/start");
  const userContextId = 0;

  const registry = createInMemoryRegistry();
  const taskbarTab = createTaskbarTab(registry, url, userContextId);

  Assert.deepEqual(
    registry.findTaskbarTab(url, userContextId),
    taskbarTab,
    "Taskbar Tab ID should match the ID returned on creation."
  );

  Assert.deepEqual(
    registry.removeTaskbarTab(taskbarTab.id),
    taskbarTab,
    "The removed Taskbar Tab was removed"
  );

  Assert.ok(
    !registry.findTaskbarTab(url, userContextId),
    "Taskbar Tab ID should be removed."
  );

  Assert.strictEqual(
    registry.getTaskbarTab(taskbarTab.id),
    null,
    "Null was returned since no Taskbar Tab with that ID exists"
  );

  Assert.strictEqual(
    registry.removeTaskbarTab(taskbarTab.id),
    null,
    "Null was returned since no Taskbar Tab with that ID exists"
  );
});

add_task(async function test_container_mismatch() {
  const url = Services.io.newURI("https://www.test.com/start");
  const userContextId = 0;
  const mismatchedUserContextId = 1;

  const registry = createInMemoryRegistry();
  const taskbarTab = createTaskbarTab(registry, url, userContextId);
  Assert.ok(taskbarTab, "Taskbar Tab ID should be created.");

  Assert.ok(
    !registry.findTaskbarTab(url, mismatchedUserContextId),
    `${mismatchedUserContextId} should not match a Taskbar Tab created with container ${userContextId}.`
  );
});

add_task(async function test_scope_navigable() {
  const url = Services.io.newURI("https://www.test.com/start");
  const validNavigationDomain = Services.io.newURI("https://www.test.com/test");
  const validNavigationSubdomain = Services.io.newURI(
    "https://www.subdomain.test.com"
  );
  const invalidNavigationDomain = Services.io.newURI(
    "https://www.anothertest.com"
  );
  const userContextId = 0;

  const registry = createInMemoryRegistry();
  const taskbarTab = createTaskbarTab(registry, url, userContextId);

  Assert.ok(
    taskbarTab.isScopeNavigable(validNavigationDomain),
    `${validNavigationDomain} should be a valid navigation target for ${taskbarTab.scopes[0].hostname}.`
  );

  Assert.ok(
    taskbarTab.isScopeNavigable(validNavigationSubdomain),
    `${validNavigationSubdomain} should be a valid navigation target for ${taskbarTab.scopes[0].hostname}.`
  );
  Assert.ok(
    !taskbarTab.isScopeNavigable(invalidNavigationDomain),
    `${invalidNavigationDomain} should be a valid navigation target for ${taskbarTab.scopes[0].hostname}.`
  );
});

add_task(async function test_psl_navigable() {
  const url = Services.io.newURI("https://www.bmoattachments.org/start");
  const invalidNavigationPublicSuffixList = Services.io.newURI(
    "https://www.invalid.bmoattachments.org"
  );
  const userContextId = 0;

  const registry = createInMemoryRegistry();
  const taskbarTab = createTaskbarTab(registry, url, userContextId);

  Assert.ok(
    !taskbarTab.isScopeNavigable(invalidNavigationPublicSuffixList),
    `bmoattachments.org is on the Public Suffix List, therefore ${invalidNavigationPublicSuffixList} should not be a valid navigation target for ${taskbarTab.scopes[0].hostname}.`
  );
});

add_task(async function test_save_and_load_consistency() {
  const url = Services.io.newURI("https://www.test.com/start");
  const userContextId = 0;

  let file = testFile();
  let saveStorage = new TaskbarTabsRegistryStorage(file);
  let saveRegistry = await saveStorage.load();
  const saveTaskbarTab = createTaskbarTab(saveRegistry, url, userContextId);

  await saveStorage.save(saveRegistry);

  let loadStorage = new TaskbarTabsRegistryStorage(file);
  let loadRegistry = await loadStorage.load();
  let loadTaskbarTab = loadRegistry.getTaskbarTab(saveTaskbarTab.id);

  Assert.deepEqual(
    saveTaskbarTab,
    loadTaskbarTab,
    "Taskbar Tab object should be identical after save and load."
  );
});

add_task(async function test_load_and_save_consistency() {
  const source = do_get_file("test_taskbarTabs.json");
  const file = testFile();
  await IOUtils.copy(source.path, file.path);

  // Test loading from the mock file
  const loadStorage = new TaskbarTabsRegistryStorage(file);
  const registry = await loadStorage.load();
  Assert.equal(
    registry.findTaskbarTab(Services.io.newURI("https://www.test.com"), 0).id,
    "4186657a-0fe5-492a-af64-dc628c232c4c",
    "Taskbar Tab ID should match the one in the test JSON file."
  );

  // Test saving to a new file
  const savedTo = testFile();
  const saveStorage = new TaskbarTabsRegistryStorage(savedTo);
  await saveStorage.save(registry);

  // Verify the output against the original file on disk.
  const originalData = await IOUtils.readJSON(source.path);
  const outputData = await IOUtils.readJSON(savedTo.path);

  Assert.deepEqual(
    outputData,
    originalData,
    "The in-memory mock file output should match the original file on disk."
  );
});

add_task(async function test_load_and_save_migrates_name() {
  const source = do_get_file("test_taskbarTabs_nonames.json");
  const file = testFile();
  await IOUtils.copy(source.path, file.path);

  const storage = new TaskbarTabsRegistryStorage(file);
  const registry = await storage.load();
  const tt = registry.findTaskbarTab(
    Services.io.newURI("https://www.test.com"),
    0
  );
  equal(
    tt.id,
    "4186657a-0fe5-492a-af64-dc628c232c4c",
    "Taskbar Tab ID should match the one in the test JSON file."
  );

  equal(typeof tt.name, "string", "A name should be present in-memory.");

  await storage.save(registry);

  const outputData = await IOUtils.readJSON(file.path);
  equal(
    typeof outputData.taskbarTabs[0].name,
    "string",
    "A name should be saved to storage."
  );
});

add_task(async function test_guards_against_commandline_strings() {
  const validUrl = Services.io.newURI("https://www.test.com/start");
  const invalidUrl = "https://www.test.com/start";

  const validUserContextId = 0;
  const invalidUserContextId = "0";

  const registry = createInMemoryRegistry();

  Assert.throws(
    () => registry.findTaskbarTab(invalidUrl, validUserContextId),
    /Invalid argument, `aUrl` should be instance of `nsIURL`/,
    "Should reject URLs provided as a string."
  );
  Assert.throws(
    () => registry.findTaskbarTab(validUrl, invalidUserContextId),
    /Invalid argument, `aUserContextId` should be type of `number`/,
    "Should reject userContextId provided as a string."
  );
});

add_task(async function test_guards_against_non_urls() {
  // about:blank is a URI, but not a URL.
  const url = Services.io.newURI("about:blank");
  const userContextId = 0;

  const registry = createInMemoryRegistry();

  throws(
    () => createTaskbarTab(registry, url, userContextId),
    /Invalid argument, `aUrl` should be instance of `nsIURL`/,
    "Should reject URIs that are not URLs."
  );
});

add_task(async function test_patch_becomes_visible() {
  const stub = sinon.stub();
  const registry = new TaskbarTabsRegistry(
    {
      save: stub,
    },
    []
  );
  const tt = createTaskbarTab(
    registry,
    Services.io.newURI("https://www.test.com/start"),
    0
  );

  Assert.equal(
    tt.shortcutRelativePath,
    null,
    "Should not start with a relative path"
  );
  registry.patchTaskbarTab(tt, {
    shortcutRelativePath: "some\\path\\string.lnk",
  });
  Assert.equal(
    tt.shortcutRelativePath,
    "some\\path\\string.lnk",
    "Should update to the new value"
  );
  // Once after 'createTaskbarTab', once after 'patchTaskbarTab'.
  Assert.equal(stub.callCount, 2, "Should save the database twice");
});

add_task(async function test_shortcutRelativePath_is_saved() {
  const file = testFile();
  const storage = new TaskbarTabsRegistryStorage(file);
  const registry = await storage.load();
  const tt = createTaskbarTab(
    registry,
    Services.io.newURI("https://www.test.com/start"),
    0
  );

  registry.patchTaskbarTab(tt, {
    shortcutRelativePath: "some\\path\\string.lnk",
  });

  await storage.save(registry);

  const data = await IOUtils.readJSON(file.path);
  Assert.equal(
    data.taskbarTabs[0].shortcutRelativePath,
    "some\\path\\string.lnk",
    "Shortcut relative path should be saved"
  );
});

add_task(async function test_multiple_match_longest_prefix() {
  const registry = createInMemoryRegistry();

  const uriWithPrefix = prefix =>
    Services.io.newURI("https://example.com" + prefix);

  const createWithScope = uri =>
    createTaskbarTab(registry, uri, 0, {
      manifest: {
        scope: uri.spec,
      },
    });
  const find = prefix => registry.findTaskbarTab(uriWithPrefix(prefix), 0);

  // Register them in an arbitrary order.
  const ttAB = createWithScope(uriWithPrefix("/ab"));
  const ttA = createWithScope(uriWithPrefix("/a"));
  const ttABC = createWithScope(uriWithPrefix("/abc"));
  const ttABCD = createWithScope(uriWithPrefix("/abc/d/"));

  equal(find("/q"), null, "/q does not exist");
  equal(find("/a").id, ttA.id, "/a matches /a");
  equal(find("/az").id, ttA.id, "/a matches /az");

  equal(find("/ab").id, ttAB.id, "/ab matches /ab");
  equal(find("/abq").id, ttAB.id, "/abq matches /ab");

  equal(find("/abc").id, ttABC.id, "/abc matches /abc");
  equal(find("/abc/").id, ttABC.id, "/abc/ matches /abc");
  equal(find("/abc/d").id, ttABC.id, "/abc/d matches /abc (not /abc/d/)");

  equal(find("/abc/d/").id, ttABCD.id, "/abc/d/ matches /abc/d/");
  equal(find("/abc/d/efgh").id, ttABCD.id, "/abc/d/efgh matches /abc/d/");
}).skip(); // TODO bug 2000948
