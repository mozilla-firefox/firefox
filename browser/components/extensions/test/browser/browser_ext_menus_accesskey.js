"use strict";

const PAGE =
  "http://mochi.test:8888/browser/browser/components/extensions/test/browser/context.html";

loadTestSubscript("head_unified_extensions.js");

add_task(async function accesskeys() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, PAGE);
  gBrowser.selectedTab = tab;

  async function background() {
    // description is informative.
    // title is passed to menus.create.
    // label and key are compared with the actual values. autoKey
    // replaces key for an access key generated from the label.
    const TESTCASES = [
      {
        description: "Amp at start",
        title: "&accesskey",
        label: "accesskey",
        key: "a",
      },
      {
        description: "amp in between",
        title: "A& b",
        label: "A b",
        key: " ",
      },
      {
        description: "lonely amp",
        title: "&",
        label: "",
        autoKey: null,
      },
      {
        description: "amp at end",
        title: "End &",
        label: "End ",
        autoKey: "E",
      },
      {
        description: "escaped amp",
        title: "A && B",
        label: "A & B",
        autoKey: "A",
      },
      {
        description: "amp before escaped amp",
        title: "A &T&& before",
        label: "A T& before",
        key: "T",
      },
      {
        description: "amp after escaped amp",
        title: "A &&&T after",
        label: "A &T after",
        key: "T",
      },
      {
        // Only the first amp should be used as the access key.
        description: "amp, escaped amp, amp to ignore",
        title: "First &1 comes && first &2 serves",
        label: "First 1 comes & first 2 serves",
        key: "1",
      },
      {
        description: "no amp, leading punctuation",
        title: '"Quoted" title',
        label: '"Quoted" title',
        autoKey: "Q",
      },
      {
        description: "no amp, leading emoji",
        title: "🦊 Fox",
        label: "🦊 Fox",
        autoKey: "F",
      },
      {
        description: "no amp, leading digit",
        title: "2 columns",
        label: "2 columns",
        autoKey: "2",
      },
      {
        description: "no amp, Cyrillic",
        title: "Перевести",
        label: "Перевести",
        autoKey: "П",
      },
      {
        description: "no amp, letter outside the BMP",
        title: "𞤀𞤣𞤤𞤢𞤥",
        label: "𞤀𞤣𞤤𞤢𞤥",
        autoKey: "𞤀",
      },
      {
        description: "no amp, Chinese followed by Latin",
        title: "翻译 English",
        label: "翻译 English",
        autoKey: "E",
      },
      {
        description: "created with amp, updated without amp",
        title: "temp with &X", // will be updated below.
        label: "remove amp",
        autoKey: "r",
      },
      {
        description: "created without amp, update with amp",
        title: "temp without access key", // will be updated below.
        label: "add ampY",
        key: "Y",
      },
    ];

    let menuIds = TESTCASES.map(({ title }) => browser.menus.create({ title }));

    // Should clear the access key:
    await browser.menus.update(menuIds[menuIds.length - 2], {
      title: "remove amp",
    });

    // Should add an access key:
    await browser.menus.update(menuIds[menuIds.length - 1], {
      title: "add amp&Y",
    });
    // Should not clear the access key because title is not set:
    await browser.menus.update(menuIds[menuIds.length - 1], { enabled: true });

    browser.test.sendMessage("testCases", TESTCASES);
  }
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["menus"],
    },
    background,
  });

  await extension.startup();

  const TESTCASES = await extension.awaitMessage("testCases");
  let menu = await openExtensionContextMenu();
  let items = menu.getElementsByTagName("menuitem");
  is(items.length, TESTCASES.length, "Expected menu items for page");
  TESTCASES.forEach(({ description, label, key, autoKey }, i) => {
    is(items[i].label, label, `Label for item ${i} (${description})`);
    is(
      items[i].accessKey,
      key ?? autoKey,
      `Accesskey for item ${i} (${description})`
    );
    is(
      items[i].hasAttribute("auto-accesskey"),
      autoKey !== undefined,
      `Accesskey source for item ${i} (${description})`
    );
    ok(
      items[i].classList.contains("webextension-menuitem"),
      `Item ${i} may share its accesskey (${description})`
    );
  });

  await closeExtensionContextMenu();
  await extension.unload();
  BrowserTestUtils.removeTab(tab);
});

add_task(async function accesskeys_selection() {
  const PAGE_WITH_AMPS = "data:text/plain;charset=utf-8,PageSelection&Amp";
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    PAGE_WITH_AMPS
  );
  gBrowser.selectedTab = tab;

  async function background() {
    const TESTCASES = [
      {
        description: "Selection without amp",
        title: "percent-s: %s.",
        label: "percent-s: PageSelection&Amp.",
        autoKey: "p",
      },
      {
        description: "Selection with amp after %s",
        title: "percent-s: %s &A.",
        label: "percent-s: PageSelection&Amp A.",
        key: "A",
      },
      {
        description: "Selection with amp before %s",
        title: "percent-s: &B %s.",
        label: "percent-s: B PageSelection&Amp.",
        key: "B",
      },
      {
        description: "Amp-percent",
        title: "Amp-percent: &%.",
        label: "Amp-percent: %.",
        key: "%",
      },
      {
        // "&%s" should be treated as "%s", and "ignore this" with amps should be ignored.
        description: "Selection with amp-percent-s",
        title: "Amp-percent-s: &%s.&i&g&n&o&r&e& &t&h&i&s",
        label: "Amp-percent-s: PageSelection&Amp.ignore this",
        // Chrome uses the first character of the selection as access key.
        autoKey: "A",
      },
      {
        description: "Selection with amp before amp-percent-s",
        title: "Amp-percent-s: &_ &%s.",
        label: "Amp-percent-s: _ PageSelection&Amp.",
        key: "_",
      },
    ];

    let lastMenuId;
    for (let { title } of TESTCASES) {
      lastMenuId = browser.menus.create({ contexts: ["selection"], title });
    }
    // Round-trip to ensure that the menus have been registered.
    await browser.menus.update(lastMenuId, {});
    browser.test.sendMessage("testCases", TESTCASES);
  }
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["menus"],
    },
    background,
  });

  await extension.startup();

  // Select all
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], function () {
    let doc = content.document;
    let range = doc.createRange();
    let selection = content.getSelection();
    selection.removeAllRanges();
    range.selectNodeContents(doc.body);
    selection.addRange(range);
  });

  const TESTCASES = await extension.awaitMessage("testCases");
  let menu = await openExtensionContextMenu();
  let items = menu.getElementsByTagName("menuitem");
  is(items.length, TESTCASES.length, "Expected menu items for page");
  TESTCASES.forEach(({ description, label, key, autoKey }, i) => {
    is(items[i].label, label, `Label for item ${i} (${description})`);
    is(
      items[i].accessKey,
      key ?? autoKey,
      `Accesskey for item ${i} (${description})`
    );
    is(
      items[i].hasAttribute("auto-accesskey"),
      autoKey !== undefined,
      `Accesskey source for item ${i} (${description})`
    );
    ok(
      items[i].classList.contains("webextension-menuitem"),
      `Item ${i} may share its accesskey (${description})`
    );
  });

  await closeExtensionContextMenu();
  await extension.unload();
  BrowserTestUtils.removeTab(tab);
});

async function checkAccessKeysInExtensionMenu(testCases) {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, PAGE);

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["menus"],
    },
    async background() {
      browser.test.onMessage.addListener(async titles => {
        let lastMenuId;
        for (let title of titles) {
          lastMenuId = browser.menus.create({ title });
        }
        // Round-trip to ensure that the menus have been registered.
        await browser.menus.update(lastMenuId, {});
        browser.test.sendMessage("created");
      });
    },
  });

  await extension.startup();
  extension.sendMessage(testCases.map(({ title }) => title));
  await extension.awaitMessage("created");

  let menu = await openExtensionContextMenu();
  let items = menu.getElementsByTagName("menuitem");
  is(items.length, testCases.length, "Expected menu items for page");
  testCases.forEach(({ title, key, autoKey }, i) => {
    is(items[i].accessKey, key ?? autoKey, `Accesskey for "${title}"`);
    is(
      items[i].hasAttribute("auto-accesskey"),
      autoKey !== undefined,
      `Accesskey source for "${title}"`
    );
  });

  await closeExtensionContextMenu();
  await extension.unload();
  BrowserTestUtils.removeTab(tab);
}

// A letter typed through an IME can't be an access key, so these items get
// digits instead.
add_task(async function accesskeys_ime_scripts() {
  await checkAccessKeysInExtensionMenu([
    { title: "翻译", autoKey: "1" },
    { title: "ひらがなカタカナ", autoKey: "2" },
    { title: "번역", autoKey: "3" },
    { title: "ＰＤＦで保存", autoKey: "4" },
    { title: "ページを２倍", autoKey: "5" },
  ]);
});

add_task(async function accesskeys_ime_scripts_mixed() {
  await checkAccessKeysInExtensionMenu([
    { title: "&Copy", key: "C" },
    { title: "翻译", autoKey: "1" },
    { title: "2 columns", autoKey: "2" },
    { title: "🦊", autoKey: "3" },
    { title: "Paste", autoKey: "P" },
  ]);
});

add_task(async function accesskeys_top_level_menu() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, PAGE);

  async function background() {
    browser.menus.create({ id: "parent", title: "Parent", contexts: ["all"] });
    browser.menus.create({ parentId: "parent", title: "Child" });
    // Round-trip to ensure that the menus have been registered.
    await browser.menus.update("parent", {});
    browser.test.sendMessage("ready");
  }
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["menus"],
    },
    background,
  });

  await extension.startup();
  await extension.awaitMessage("ready");

  let contextMenu = await openContextMenu("#img1");
  let menu = contextMenu.querySelector("menu.webextension-menuitem");
  is(menu.label, "Parent", "The extension's item is a submenu");
  is(menu.accessKey, "P", "A submenu gets an accesskey");

  await closeContextMenu(contextMenu);
  await extension.unload();
  BrowserTestUtils.removeTab(tab);
});

add_task(async function accesskeys_refresh_while_open() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, PAGE);

  async function background() {
    browser.menus.onShown.addListener(async () => {
      await browser.menus.update("item", { title: "Changed" });
      await browser.menus.refresh();
      browser.test.sendMessage("refreshed");
    });
    browser.menus.create({ id: "item", title: "Before", contexts: ["all"] });
    // Round-trip to ensure that the menus have been registered.
    await browser.menus.update("item", {});
    browser.test.sendMessage("ready");
  }
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["menus"],
    },
    background,
  });

  await extension.startup();
  await extension.awaitMessage("ready");

  let contextMenu = await openContextMenu("#img1");
  await extension.awaitMessage("refreshed");
  let item = contextMenu.querySelector(".webextension-menuitem");
  is(item.label, "Changed", "The item was rebuilt while the menu is open");
  is(item.accessKey, "C", "The rebuilt item gets an accesskey");

  await closeContextMenu(contextMenu);
  await extension.unload();
  BrowserTestUtils.removeTab(tab);
});

// The unified extensions context menu gives its own items no accesskeys, so an
// extension's item gets none either and doesn't take the first letter of
// "Manage Extension".
add_task(async function accesskeys_menu_without_accesskeys() {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["menus"],
      browser_action: { default_area: "menupanel" },
    },
    async background() {
      browser.menus.create({
        id: "item",
        title: "Manage tabs",
        contexts: ["browser_action"],
      });
      // Round-trip to ensure that the menus have been registered.
      await browser.menus.update("item", {});
      browser.test.sendMessage("ready");
    },
    useAddonManager: "temporary",
  });

  await extension.startup();
  await extension.awaitMessage("ready");

  await openExtensionsPanel();
  let menu = await openUnifiedExtensionsContextMenu(extension.id);
  let item = menu.querySelector(".webextension-menuitem");
  is(item.label, "Manage tabs", "The extension's item is in the menu");
  ok(!item.hasAttribute("accesskey"), "The extension's item gets no accesskey");

  let hidden = BrowserTestUtils.waitForEvent(menu, "popuphidden");
  menu.hidePopup();
  await hidden;
  await closeExtensionsPanel();
  await extension.unload();
});
