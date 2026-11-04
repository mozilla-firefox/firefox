"use strict";

ChromeUtils.defineESModuleGetters(this, {
  CONTAINER_COLORS:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
  CONTAINER_COLOR_ALIASES:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
  ContextualIdentityService:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
  ExtensionPreferencesManager:
    "resource://gre/modules/ExtensionPreferencesManager.sys.mjs",
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
});

const CONTAINERS_PREF = "privacy.userContext.enabled";

AddonTestUtils.init(this);
AddonTestUtils.overrideCertDB();
AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "42",
  "42"
);

add_task(async function startup() {
  await AddonTestUtils.promiseStartupManager();
});

add_task(async function test_contextualIdentities_without_permissions() {
  function background() {
    browser.test.assertTrue(
      !browser.contextualIdentities,
      "contextualIdentities API is not available when the contextualIdentities permission is not required"
    );
    browser.test.notifyPass("contextualIdentities_without_permission");
  }

  let extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "temporary",
    background,
    manifest: {
      browser_specific_settings: {
        gecko: { id: "testing@thing.com" },
      },
      permissions: [],
    },
  });

  await extension.startup();
  await extension.awaitFinish("contextualIdentities_without_permission");
  await extension.unload();
});

add_task(async function test_contextualIdentities_supported_colors_and_icons() {
  // The API must expose exactly the palette the service considers supported,
  // in the same order, so it can't silently drift from the source of truth.
  const expectedColors = ContextualIdentityService.containerColors.map(
    name => ({
      color: name,
      colorCode: ContextualIdentityService.getContainerColorCode(name),
    })
  );
  const expectedIcons = ContextualIdentityService.containerIcons.map(name => ({
    icon: name,
    iconUrl: ContextualIdentityService.getContainerIconURL(name),
  }));

  async function background(expected) {
    let colors = await browser.contextualIdentities.getSupportedColors();
    browser.test.assertEq(
      JSON.stringify(expected.colors),
      JSON.stringify(colors),
      "getSupportedColors mirrors the colors supported by the browser"
    );

    let icons = await browser.contextualIdentities.getSupportedIcons();
    browser.test.assertEq(
      JSON.stringify(expected.icons),
      JSON.stringify(icons),
      "getSupportedIcons mirrors the icons supported by the browser"
    );

    // Shape checks that don't depend on the values computed in the parent, so a
    // service returning a well-formed object with garbage values is caught too.
    const hexMatch = /^#[0-9a-f]{6}$/;
    for (let entry of colors) {
      browser.test.assertTrue(
        hexMatch.test(entry.colorCode),
        `colorCode for ${entry.color} is a hex value (${entry.colorCode})`
      );
    }
    const iconMatch = /^resource:\/\/usercontext-content\/[a-z]+[.]svg$/;
    for (let entry of icons) {
      browser.test.assertTrue(
        iconMatch.test(entry.iconUrl),
        `iconUrl for ${entry.icon} has the expected shape (${entry.iconUrl})`
      );

      // An extension must actually be able to load the icon, so check the URL
      // resolves to a decodable image of the expected size.
      const img = new Image();
      img.src = entry.iconUrl;
      try {
        await img.decode();
      } catch (e) {
        browser.test.fail(`Failed to load image: ${img.src} - ${e}`);
      }
      browser.test.assertEq(
        32,
        img.naturalWidth,
        `Expected width for ${entry.icon}`
      );
      browser.test.assertEq(
        32,
        img.naturalHeight,
        `Expected height for ${entry.icon}`
      );
    }

    // Independent anchors with known values, so a totally broken source of
    // truth can't make the equality checks above trivially pass.
    browser.test.assertTrue(
      colors.some(c => c.color === "blue" && c.colorCode === "#455fe7"),
      "blue is exposed with its expected color code"
    );
    browser.test.assertTrue(
      icons.some(
        i =>
          i.icon === "fingerprint" &&
          i.iconUrl === "resource://usercontext-content/fingerprint.svg"
      ),
      "fingerprint is exposed with its expected icon url"
    );

    browser.test.notifyPass("contextualIdentities_supported");
  }

  let extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "temporary",
    background: `(${background})(${JSON.stringify({
      colors: expectedColors,
      icons: expectedIcons,
    })})`,
    manifest: {
      browser_specific_settings: {
        gecko: { id: "supported@thing.com" },
      },
      permissions: ["contextualIdentities"],
    },
  });

  await extension.startup();
  await extension.awaitFinish("contextualIdentities_supported");
  await extension.unload();
});

add_task(async function test_contextualIdentity_events() {
  async function background() {
    function createOneTimeListener(type) {
      return new Promise((resolve, reject) => {
        try {
          browser.test.assertTrue(
            type in browser.contextualIdentities,
            `Found API object browser.contextualIdentities.${type}`
          );
          const listener = change => {
            browser.test.assertTrue(
              "contextualIdentity" in change,
              `Found identity in change`
            );
            browser.contextualIdentities[type].removeListener(listener);
            resolve(change);
          };
          browser.contextualIdentities[type].addListener(listener);
        } catch (e) {
          reject(e);
        }
      });
    }

    function assertExpected(expected, container) {
      // Number of keys that are added by the APIs
      const createdCount = 2;
      for (let key of Object.keys(container)) {
        browser.test.assertTrue(key in expected, `found property ${key}`);
        browser.test.assertEq(
          expected[key],
          container[key],
          `property value for ${key} is correct`
        );
      }
      const hexMatch = /^#[0-9a-f]{6}$/;
      browser.test.assertTrue(
        hexMatch.test(expected.colorCode),
        "Color code property was expected Hex shape"
      );
      const iconMatch = /^resource:\/\/usercontext-content\/[a-z]+[.]svg$/;
      browser.test.assertTrue(
        iconMatch.test(expected.iconUrl),
        "Icon url property was expected shape"
      );
      browser.test.assertEq(
        Object.keys(expected).length,
        Object.keys(container).length + createdCount,
        "all expected properties found"
      );
    }

    let onCreatePromise = createOneTimeListener("onCreated");

    let containerObj = { name: "foobar", color: "red", icon: "circle" };
    let ci = await browser.contextualIdentities.create(containerObj);
    browser.test.assertTrue(!!ci, "We have an identity");
    const onCreateListenerResponse = await onCreatePromise;
    const cookieStoreId = ci.cookieStoreId;
    assertExpected(
      onCreateListenerResponse.contextualIdentity,
      Object.assign(containerObj, { cookieStoreId })
    );

    let onUpdatedPromise = createOneTimeListener("onUpdated");
    let updateContainerObj = { name: "testing", color: "blue", icon: "dollar" };
    ci = await browser.contextualIdentities.update(
      cookieStoreId,
      updateContainerObj
    );
    browser.test.assertTrue(!!ci, "We have an update identity");
    const onUpdatedListenerResponse = await onUpdatedPromise;
    assertExpected(
      onUpdatedListenerResponse.contextualIdentity,
      Object.assign(updateContainerObj, { cookieStoreId })
    );

    let onRemovePromise = createOneTimeListener("onRemoved");
    ci = await browser.contextualIdentities.remove(
      updateContainerObj.cookieStoreId
    );
    browser.test.assertTrue(!!ci, "We have an remove identity");
    const onRemoveListenerResponse = await onRemovePromise;
    assertExpected(
      onRemoveListenerResponse.contextualIdentity,
      Object.assign(updateContainerObj, { cookieStoreId })
    );

    browser.test.notifyPass("contextualIdentities_events");
  }

  let extension = ExtensionTestUtils.loadExtension({
    background,
    useAddonManager: "temporary",
    manifest: {
      browser_specific_settings: {
        gecko: { id: "testing@thing.com" },
      },
      permissions: ["contextualIdentities"],
    },
  });

  Services.prefs.setBoolPref(CONTAINERS_PREF, true);

  await extension.startup();
  await extension.awaitFinish("contextualIdentities_events");
  await extension.unload();

  Services.prefs.clearUserPref(CONTAINERS_PREF);
});

add_task(async function test_contextualIdentity_site_associations() {
  async function background() {
    function nextEvent() {
      return new Promise(resolve => {
        const listener = changeInfo => {
          browser.contextualIdentities.onSiteAssociationChanged.removeListener(
            listener
          );
          resolve(changeInfo);
        };
        browser.contextualIdentities.onSiteAssociationChanged.addListener(
          listener
        );
      });
    }

    browser.test.assertEq(
      null,
      await browser.contextualIdentities.getSiteAssociation({
        site: "example.com",
      }),
      "No association by default"
    );

    let addedPromise = nextEvent();
    await browser.contextualIdentities.setSiteAssociation({
      site: "example.com",
      cookieStoreId: "firefox-container-1",
    });
    let added = await addedPromise;
    browser.test.assertEq("example.com", added.site, "add event site");
    browser.test.assertEq(
      "firefox-container-1",
      added.cookieStoreId,
      "add event cookieStoreId"
    );

    browser.test.assertDeepEq(
      { site: "example.com", cookieStoreId: "firefox-container-1" },
      await browser.contextualIdentities.getSiteAssociation({
        site: "example.com",
      }),
      "Association was set"
    );

    browser.test.assertDeepEq(
      { site: "example.com", cookieStoreId: "firefox-container-1" },
      await browser.contextualIdentities.getSiteAssociation({
        site: "EXAMPLE.com",
      }),
      "The returned site is normalized"
    );

    let list = await browser.contextualIdentities.querySiteAssociations({});
    browser.test.assertEq(1, list.length, "One association");
    browser.test.assertEq("example.com", list[0].site, "queried site");
    browser.test.assertEq(
      "firefox-container-1",
      list[0].cookieStoreId,
      "queried cookieStoreId"
    );

    browser.test.assertEq(
      0,
      (
        await browser.contextualIdentities.querySiteAssociations({
          cookieStoreId: "firefox-container-2",
        })
      ).length,
      "No associations for another container"
    );

    await browser.test.assertRejects(
      browser.contextualIdentities.setSiteAssociation({
        site: "example.com",
        cookieStoreId: "firefox-default",
      }),
      "Invalid contextual identity: firefox-default",
      "Cannot associate a site with the default store"
    );

    await browser.test.assertRejects(
      browser.contextualIdentities.querySiteAssociations({
        cookieStoreId: "bogus",
      }),
      "Invalid contextual identity: bogus",
      "Filtering by an invalid store rejects"
    );

    for (let site of ["", "example.com/path", "*.example.com"]) {
      await browser.test.assertRejects(
        browser.contextualIdentities.setSiteAssociation({
          site,
          cookieStoreId: "firefox-container-1",
        }),
        `Invalid site: ${site}`,
        `${site} is rejected as a site`
      );

      await browser.test.assertRejects(
        browser.contextualIdentities.getSiteAssociation({ site }),
        `Invalid site: ${site}`,
        `${site} is rejected when getting an association`
      );
    }

    await browser.contextualIdentities.setSiteAssociation({
      site: "BÜCHER.example",
      cookieStoreId: "firefox-container-2",
    });
    browser.test.assertDeepEq(
      {
        site: "xn--bcher-kva.example",
        cookieStoreId: "firefox-container-2",
      },
      await browser.contextualIdentities.getSiteAssociation({
        site: "bücher.example",
      }),
      "An IDN site is returned encoded as ASCII"
    );
    await browser.contextualIdentities.removeSiteAssociation({
      site: "bücher.example",
    });

    let removedPromise = nextEvent();
    await browser.contextualIdentities.removeSiteAssociation({
      site: "example.com",
    });
    let removed = await removedPromise;
    browser.test.assertEq("example.com", removed.site, "remove event site");
    browser.test.assertFalse(
      "cookieStoreId" in removed,
      "remove event omits cookieStoreId"
    );

    browser.test.assertEq(
      null,
      await browser.contextualIdentities.getSiteAssociation({
        site: "example.com",
      }),
      "Association was removed"
    );

    browser.test.notifyPass("site_associations");
  }

  let extension = ExtensionTestUtils.loadExtension({
    background,
    useAddonManager: "temporary",
    manifest: {
      browser_specific_settings: {
        gecko: { id: "site-associations@thing.com" },
      },
      permissions: ["contextualIdentities"],
    },
  });

  Services.prefs.setBoolPref(CONTAINERS_PREF, true);

  await extension.startup();
  await extension.awaitFinish("site_associations");
  await extension.unload();

  Services.prefs.clearUserPref(CONTAINERS_PREF);
});

add_task(async function test_contextualIdentity_with_permissions() {
  async function background() {
    let ci;
    await browser.test.assertRejects(
      browser.contextualIdentities.get("foobar"),
      "Invalid contextual identity: foobar",
      "API should reject here"
    );
    await browser.test.assertRejects(
      browser.contextualIdentities.update("foobar", { name: "testing" }),
      "Invalid contextual identity: foobar",
      "API should reject for unknown updates"
    );
    await browser.test.assertRejects(
      browser.contextualIdentities.remove("foobar"),
      "Invalid contextual identity: foobar",
      "API should reject for removing unknown containers"
    );

    ci = await browser.contextualIdentities.get("firefox-container-1");
    browser.test.assertTrue(!!ci, "We have an identity");
    browser.test.assertTrue("name" in ci, "We have an identity.name");
    browser.test.assertTrue("color" in ci, "We have an identity.color");
    browser.test.assertTrue("icon" in ci, "We have an identity.icon");
    browser.test.assertEq("Personal", ci.name, "identity.name is correct");
    browser.test.assertEq(
      "firefox-container-1",
      ci.cookieStoreId,
      "identity.cookieStoreId is correct"
    );

    function listenForMessage(messageName, stateChangeBool) {
      return new Promise(resolve => {
        browser.test.onMessage.addListener(function listener(msg) {
          browser.test.log(`Got message from background: ${msg}`);
          if (msg === messageName + "-response") {
            browser.test.onMessage.removeListener(listener);
            resolve();
          }
        });
        browser.test.log(
          `Sending message to background: ${messageName} ${stateChangeBool}`
        );
        browser.test.sendMessage(messageName, stateChangeBool);
      });
    }

    await listenForMessage("containers-state-change", false);

    browser.test.assertRejects(
      browser.contextualIdentities.query({}),
      "Contextual identities are currently disabled",
      "Throws when containers are disabled"
    );

    await browser.test.assertRejects(
      browser.contextualIdentities.getSupportedColors(),
      "Contextual identities are currently disabled",
      "getSupportedColors throws when containers are disabled"
    );

    await browser.test.assertRejects(
      browser.contextualIdentities.getSupportedIcons(),
      "Contextual identities are currently disabled",
      "getSupportedIcons throws when containers are disabled"
    );

    await listenForMessage("containers-state-change", true);

    let cis = await browser.contextualIdentities.query({});
    browser.test.assertEq(
      4,
      cis.length,
      "by default we should have 4 containers"
    );

    cis = await browser.contextualIdentities.query({ name: "Personal" });
    browser.test.assertEq(
      1,
      cis.length,
      "by default we should have 1 container called Personal"
    );

    cis = await browser.contextualIdentities.query({ name: "foobar" });
    browser.test.assertEq(
      0,
      cis.length,
      "by default we should have 0 container called foobar"
    );

    ci = await browser.contextualIdentities.create({
      name: "foobar",
      color: "red",
      icon: "gift",
    });
    browser.test.assertTrue(!!ci, "We have an identity");
    browser.test.assertEq("foobar", ci.name, "identity.name is correct");
    browser.test.assertEq("red", ci.color, "identity.color is correct");
    browser.test.assertEq("gift", ci.icon, "identity.icon is correct");
    browser.test.assertTrue(
      !!ci.cookieStoreId,
      "identity.cookieStoreId is correct"
    );

    browser.test.assertRejects(
      browser.contextualIdentities.create({
        name: "foobar",
        color: "red",
        icon: "firefox",
      }),
      "Invalid icon firefox for container",
      "Create container called with an invalid icon"
    );

    browser.test.assertRejects(
      browser.contextualIdentities.create({
        name: "foobar",
        color: "firefox-orange",
        icon: "gift",
      }),
      "Invalid color name firefox-orange for container",
      "Create container called with an invalid color"
    );

    cis = await browser.contextualIdentities.query({});
    browser.test.assertEq(
      5,
      cis.length,
      "we should still have have 5 containers"
    );

    ci = await browser.contextualIdentities.get(ci.cookieStoreId);
    browser.test.assertTrue(!!ci, "We have an identity");
    browser.test.assertEq("foobar", ci.name, "identity.name is correct");
    browser.test.assertEq("red", ci.color, "identity.color is correct");
    browser.test.assertEq("gift", ci.icon, "identity.icon is correct");

    browser.test.assertRejects(
      browser.contextualIdentities.update(ci.cookieStoreId, {
        name: "foobar",
        color: "red",
        icon: "firefox",
      }),
      "Invalid icon firefox for container",
      "Create container called with an invalid icon"
    );

    browser.test.assertRejects(
      browser.contextualIdentities.update(ci.cookieStoreId, {
        name: "foobar",
        color: "firefox-orange",
        icon: "gift",
      }),
      "Invalid color name firefox-orange for container",
      "Create container called with an invalid color"
    );

    cis = await browser.contextualIdentities.query({});
    browser.test.assertEq(5, cis.length, "now we have 5 identities");

    ci = await browser.contextualIdentities.update(ci.cookieStoreId, {
      name: "barfoo",
      color: "blue",
      icon: "cart",
    });
    browser.test.assertTrue(!!ci, "We have an identity");
    browser.test.assertEq("barfoo", ci.name, "identity.name is correct");
    browser.test.assertEq("blue", ci.color, "identity.color is correct");
    browser.test.assertEq("cart", ci.icon, "identity.icon is correct");

    ci = await browser.contextualIdentities.get(ci.cookieStoreId);
    browser.test.assertTrue(!!ci, "We have an identity");
    browser.test.assertEq("barfoo", ci.name, "identity.name is correct");
    browser.test.assertEq("blue", ci.color, "identity.color is correct");
    browser.test.assertEq("cart", ci.icon, "identity.icon is correct");

    ci = await browser.contextualIdentities.remove(ci.cookieStoreId);
    browser.test.assertTrue(!!ci, "We have an identity");
    browser.test.assertEq("barfoo", ci.name, "identity.name is correct");
    browser.test.assertEq("blue", ci.color, "identity.color is correct");
    browser.test.assertEq("cart", ci.icon, "identity.icon is correct");

    cis = await browser.contextualIdentities.query({});
    browser.test.assertEq(4, cis.length, "we are back to 4 identities");

    browser.test.notifyPass("contextualIdentities");
  }

  function makeExtension(id) {
    return ExtensionTestUtils.loadExtension({
      useAddonManager: "temporary",
      background,
      manifest: {
        browser_specific_settings: {
          gecko: { id },
        },
        permissions: ["contextualIdentities"],
      },
    });
  }

  let extension = makeExtension("containers-test@mozilla.org");

  extension.onMessage("containers-state-change", stateBool => {
    Cu.reportError(`Got message "containers-state-change", ${stateBool}`);
    Services.prefs.setBoolPref(CONTAINERS_PREF, stateBool);
    Cu.reportError("Changed pref");
    extension.sendMessage("containers-state-change-response");
  });

  await extension.startup();
  await extension.awaitFinish("contextualIdentities");
  equal(
    Services.prefs.getBoolPref(CONTAINERS_PREF),
    true,
    "Pref should now be enabled, whatever it's initial state"
  );
  await extension.unload();
  equal(
    Services.prefs.getBoolPref(CONTAINERS_PREF),
    true,
    "Pref should remain enabled"
  );

  Services.prefs.clearUserPref(CONTAINERS_PREF);
});

add_task(async function test_contextualIdentity_extensions_enable_containers() {
  async function background() {
    let ci = await browser.contextualIdentities.get("firefox-container-1");
    browser.test.assertTrue(!!ci, "We have an identity");

    browser.test.notifyPass("contextualIdentities");
  }
  function makeExtension(id) {
    return ExtensionTestUtils.loadExtension({
      useAddonManager: "permanent",
      background,
      manifest: {
        browser_specific_settings: {
          gecko: { id },
        },
        permissions: ["contextualIdentities"],
      },
    });
  }
  async function testSetting(expect, message) {
    let setting =
      await ExtensionPreferencesManager.getSetting("privacy.containers");
    if (expect === null) {
      equal(setting, null, message);
    } else {
      equal(setting.value, expect, message);
    }
  }
  function testPref(expect, message) {
    equal(Services.prefs.getBoolPref(CONTAINERS_PREF), expect, message);
  }

  let extension = makeExtension("containers-test@mozilla.org");
  await extension.startup();
  await extension.awaitFinish("contextualIdentities");
  equal(
    Services.prefs.getBoolPref(CONTAINERS_PREF),
    true,
    "Pref should now be enabled, whatever it's initial state"
  );
  await extension.unload();
  await testSetting(null, "setting should be unset");
  equal(
    Services.prefs.getBoolPref(CONTAINERS_PREF),
    true,
    "Pref should remain enabled"
  );

  // Lets set containers explicitly to be off and test we keep it that way after removal
  Services.prefs.setBoolPref(CONTAINERS_PREF, false);

  let extension1 = makeExtension("containers-test-1@mozilla.org");
  await extension1.startup();
  await extension1.awaitFinish("contextualIdentities");
  await testSetting(extension1.id, "setting should be controlled");
  testPref(true, "Pref should now be enabled, whatever it's initial state");

  // Test that disabling leaves containers on, and that re-enabling with containers off
  // will re-enable containers.
  const addon = await AddonManager.getAddonByID(extension1.id);
  await addon.disable();
  await testSetting(undefined, "setting should not be an addon");
  testPref(true, "Pref should remain enabled, whatever it's initial state");

  Services.prefs.setBoolPref(CONTAINERS_PREF, false);

  await addon.enable();
  await testSetting(extension1.id, "setting should be controlled");
  testPref(true, "Pref should be enabled");

  await extension1.unload();
  await testSetting(null, "setting should be unset");
  equal(
    Services.prefs.getBoolPref(CONTAINERS_PREF),
    true,
    "Pref should remain enabled"
  );

  // Lets set containers explicitly to be on and test we keep it that way after removal.
  Services.prefs.setBoolPref(CONTAINERS_PREF, true);

  let extension2 = makeExtension("containers-test-2@mozilla.org");
  let extension3 = makeExtension("containers-test-3@mozilla.org");
  await extension2.startup();
  await extension2.awaitFinish("contextualIdentities");
  await extension3.startup();
  await extension3.awaitFinish("contextualIdentities");

  // Flip the ordering to check it's still enabled
  await testSetting(extension3.id, "setting should still be controlled by 3");
  testPref(true, "Pref should now be enabled 1");
  await extension3.unload();
  await testSetting(extension2.id, "setting should still be controlled by 2");
  testPref(true, "Pref should now be enabled 2");
  await extension2.unload();
  await testSetting(null, "setting should be unset");
  testPref(true, "Pref should now be enabled 3");

  Services.prefs.clearUserPref(CONTAINERS_PREF);
});

add_task(async function test_contextualIdentity_preference_change() {
  async function background() {
    let extensionInfo = await browser.management.getSelf();
    if (extensionInfo.version == "1.0.0") {
      const containers = await browser.contextualIdentities.query({});
      browser.test.assertEq(
        containers.length,
        4,
        "We still have the original containers"
      );
      await browser.contextualIdentities.create({
        name: "foobar",
        color: "red",
        icon: "circle",
      });
    }
    const containers = await browser.contextualIdentities.query({});
    browser.test.assertEq(containers.length, 5, "We have a new container");
    if (extensionInfo.version == "1.1.0") {
      await browser.contextualIdentities.remove(containers[4].cookieStoreId);
    }
    browser.test.notifyPass("contextualIdentities");
  }
  function makeExtension(id, version) {
    return ExtensionTestUtils.loadExtension({
      useAddonManager: "temporary",
      background,
      manifest: {
        version,
        browser_specific_settings: {
          gecko: { id },
        },
        permissions: ["contextualIdentities"],
      },
    });
  }

  Services.prefs.setBoolPref(CONTAINERS_PREF, false);
  let extension = makeExtension("containers-pref-test@mozilla.org", "1.0.0");
  await extension.startup();
  await extension.awaitFinish("contextualIdentities");
  equal(
    Services.prefs.getBoolPref(CONTAINERS_PREF),
    true,
    "Pref should now be enabled, whatever it's initial state"
  );

  let extension2 = makeExtension("containers-pref-test@mozilla.org", "1.1.0");
  await extension2.startup();
  await extension2.awaitFinish("contextualIdentities");

  await extension.unload();
  equal(
    Services.prefs.getBoolPref(CONTAINERS_PREF),
    true,
    "Pref should remain enabled"
  );

  Services.prefs.clearUserPref(CONTAINERS_PREF);
});

add_task(
  { pref_set: [["extensions.eventPages.enabled", true]] },
  async function test_contextualIdentity_event_page() {
    let extension = ExtensionTestUtils.loadExtension({
      useAddonManager: "permanent",
      manifest: {
        browser_specific_settings: {
          gecko: { id: "eventpage@mochitest" },
        },
        permissions: ["contextualIdentities"],
        background: { persistent: false },
      },
      background() {
        browser.contextualIdentities.onCreated.addListener(() => {
          browser.test.sendMessage("created");
        });
        browser.contextualIdentities.onUpdated.addListener(() => {});
        browser.contextualIdentities.onRemoved.addListener(() => {
          browser.test.sendMessage("removed");
        });
        browser.contextualIdentities.onSiteAssociationChanged.addListener(
          changeInfo => {
            browser.test.sendMessage("site-association-changed", changeInfo);
          }
        );
        browser.test.sendMessage("ready");
      },
    });

    const EVENTS = [
      "onCreated",
      "onUpdated",
      "onRemoved",
      "onSiteAssociationChanged",
    ];

    await extension.startup();
    await extension.awaitMessage("ready");
    for (let event of EVENTS) {
      assertPersistentListeners(extension, "contextualIdentities", event, {
        primed: false,
      });
    }

    await extension.terminateBackground();
    for (let event of EVENTS) {
      assertPersistentListeners(extension, "contextualIdentities", event, {
        primed: true,
      });
    }

    // test events waken background
    let identity = ContextualIdentityService.create("foobar", "circle", "red");

    await extension.awaitMessage("ready");
    await extension.awaitMessage("created");
    for (let event of EVENTS) {
      assertPersistentListeners(extension, "contextualIdentities", event, {
        primed: false,
      });
    }

    // onSiteAssociationChanged does not share its registrar with the events
    // above, so check that it primes and wakes the background on its own.
    // The onCreated call above is still tracked as a pending listener, which
    // would otherwise reset the idle timer instead of terminating.
    await extension.terminateBackground({ disableResetIdleForTest: true });
    for (let event of EVENTS) {
      assertPersistentListeners(extension, "contextualIdentities", event, {
        primed: true,
      });
    }

    ContextualIdentityService.setSiteAssociation(
      "example.com",
      identity.userContextId
    );

    await extension.awaitMessage("ready");
    Assert.deepEqual(
      await extension.awaitMessage("site-association-changed"),
      {
        site: "example.com",
        cookieStoreId: `firefox-container-${identity.userContextId}`,
      },
      "A new association woke the background and was delivered"
    );
    for (let event of EVENTS) {
      assertPersistentListeners(extension, "contextualIdentities", event, {
        primed: false,
      });
    }

    ContextualIdentityService.removeSiteAssociation("example.com");
    Assert.deepEqual(
      await extension.awaitMessage("site-association-changed"),
      { site: "example.com" },
      "A removed association is delivered without a cookieStoreId"
    );

    ContextualIdentityService.remove(identity.userContextId);
    await extension.awaitMessage("removed");

    // check primed listeners after startup
    await AddonTestUtils.promiseRestartManager();
    await extension.awaitStartup();

    for (let event of EVENTS) {
      assertPersistentListeners(extension, "contextualIdentities", event, {
        primed: true,
      });
    }

    await extension.unload();
  }
);

add_task(async function test_contextualIdentity_color_api_names() {
  // Every color name accepted by the API, see
  // https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/contextualIdentities/ContextualIdentity#color
  const inputColors = [
    // Colors since inception of contextualIdentities:
    "blue",
    "turquoise",
    "green",
    "yellow",
    "orange",
    "red",
    "pink",
    "purple",
    "toolbar",
    // New colors in version 153:
    "cyan",
    "violet",
    "gray",
  ];

  // Inputs that resolve to a different name. Inputs not listed here are
  // expected to be returned unchanged.
  const RENAMED_COLORS = {
    turquoise: "cyan",
    toolbar: "gray",
  };

  // Forward coverage: every internal color name and alias must be listed
  // above, so this test has to be updated whenever the set of names changes.
  for (let { name } of CONTAINER_COLORS) {
    ok(
      inputColors.includes(name),
      `internal color "${name}" is covered by the API test`
    );
  }
  for (let legacy of Object.keys(CONTAINER_COLOR_ALIASES)) {
    ok(
      inputColors.includes(legacy),
      `internal alias "${legacy}" is covered by the API test`
    );
  }

  let expectations = inputColors.map(input => [
    input,
    RENAMED_COLORS[input] ?? input,
  ]);

  async function background(colorExpectations) {
    let { cookieStoreId } = await browser.contextualIdentities.create({
      name: "color-api",
      color: "blue",
      icon: "circle",
    });

    for (let [input, expected] of colorExpectations) {
      let updated = await browser.contextualIdentities.update(cookieStoreId, {
        color: input,
      });
      browser.test.assertEq(
        expected,
        updated.color,
        `update() with "${input}" returns "${expected}"`
      );

      let fetched = await browser.contextualIdentities.get(cookieStoreId);
      browser.test.assertEq(
        expected,
        fetched.color,
        `get() after "${input}" returns "${expected}"`
      );
    }

    await browser.contextualIdentities.remove(cookieStoreId);
    browser.test.notifyPass("color-api-names");
  }

  let extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "temporary",
    background: `(${background})(${JSON.stringify(expectations)})`,
    manifest: {
      browser_specific_settings: { gecko: { id: "color-api@mozilla.org" } },
      permissions: ["contextualIdentities"],
    },
  });

  await extension.startup();
  await extension.awaitFinish("color-api-names");
  await extension.unload();
});

add_task(async function test_contextualIdentity_color_aliases_api() {
  async function background() {
    let cyan = await browser.contextualIdentities.create({
      name: "alias-cyan",
      color: "turquoise",
      icon: "circle",
    });
    browser.test.assertEq("cyan", cyan.color, "turquoise is returned as cyan");
    browser.test.assertEq(
      "#10a4ca",
      cyan.colorCode,
      "cyan returns the refreshed code"
    );

    let gray = await browser.contextualIdentities.create({
      name: "alias-gray",
      color: "red",
      icon: "circle",
    });
    gray = await browser.contextualIdentities.update(gray.cookieStoreId, {
      name: "alias-gray",
      color: "toolbar",
      icon: "circle",
    });
    browser.test.assertEq("gray", gray.color, "toolbar is returned as gray");

    await browser.contextualIdentities.remove(cyan.cookieStoreId);
    await browser.contextualIdentities.remove(gray.cookieStoreId);

    browser.test.notifyPass("color-aliases");
  }

  let extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "temporary",
    background,
    manifest: {
      browser_specific_settings: { gecko: { id: "color-aliases@mozilla.org" } },
      permissions: ["contextualIdentities"],
    },
  });

  await extension.startup();
  await extension.awaitFinish("color-aliases");
  await extension.unload();
});
