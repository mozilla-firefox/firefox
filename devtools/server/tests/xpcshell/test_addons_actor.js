/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

async function connect() {
  DevToolsServer.init();
  DevToolsServer.registerAllActors();

  const client = new DevToolsClient(DevToolsServer.connectPipe());
  await client.connect();

  const addons = await client.mainRoot.getFront("addons");
  return [client, addons];
}

function getPlatformFilePath(path) {
  const allowMissing = false;
  const usePlatformSeparator = true;
  return getFilePath(path, allowMissing, usePlatformSeparator);
}

// The AddonsManager test helper can only be called once per test script.
// This `setup` task will run first.
add_setup(async () => {
  await startupAddonsManager();
});

add_task(async function testSuccessfulInstall() {
  const [client, addons] = await connect();

  const addonPath = getPlatformFilePath("addons/web-extension");
  const installedAddon = await addons.installTemporaryAddon(addonPath, false);
  equal(installedAddon.id, "test-addons-actor@mozilla.org");
  // The returned object is currently not a proper actor.
  equal(installedAddon.actor, false);

  const addonList = await client.mainRoot.listAddons();
  ok(addonList && addonList.map(a => a.name), "Received list of add-ons");
  const addon = addonList.find(a => a.id === installedAddon.id);
  ok(addon, "Test add-on appeared in root install list");

  await close(client);
});

add_task(async function testNonExistantPath() {
  const [client, addons] = await connect();

  await Assert.rejects(
    addons.installTemporaryAddon("some-non-existant-path", false),
    /Could not install add-on.*Component returned failure/
  );

  await close(client);
});

add_task(async function testInvalidExtensionMissingManifestJson() {
  const [client, addons] = await connect();

  await Assert.rejects(
    addons.installTemporaryAddon(getPlatformFilePath("addons"), false),
    /Could not install add-on.*does not contain a valid manifest/
  );

  await close(client);
});

add_task(async function testInvalidExtensionManifestJsonIsNotJson() {
  const [client, addons] = await connect();

  await Assert.rejects(
    addons.installTemporaryAddon(
      getPlatformFilePath("addons/invalid-extension-manifest-badjson"),
      false
    ),
    // It would be ideal if the error message contained manifest.json, but at
    // least having some description is better than nothing.
    /Could not install add-on.*SyntaxError: JSON.parse: unexpected character at line 2 column 1 of the JSON data/
  );

  await close(client);
});

add_task(async function testInvalidExtensionManifestJsonMissingRequiredKey() {
  const [client, addons] = await connect();

  await Assert.rejects(
    addons.installTemporaryAddon(
      getPlatformFilePath("addons/invalid-extension-manifest"),
      false
    ),
    /Could not install add-on.*Error: Extension is invalid\nReading manifest: Property "name" is required/
  );

  await close(client);
});

add_task(async function testInvalidExtensionMissingLocales() {
  const [client, addons] = await connect();

  await Assert.rejects(
    addons.installTemporaryAddon(
      getPlatformFilePath("addons/invalid-extension-missing-locales"),
      false
    ),
    // It would be ideal if the error message contained manifest.json, but at
    // least having some description is better than nothing.
    /Could not install add-on.*Error: Extension is invalid\nLoading locale file _locales\/en\/messages.json: .*NS_ERROR_FILE_NOT_FOUND/
  );

  await close(client);
});
