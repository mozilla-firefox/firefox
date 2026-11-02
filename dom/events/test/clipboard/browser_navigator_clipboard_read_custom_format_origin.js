/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const kCustomFormat = "web text/x-custom-format-origin-test";
const kSameOriginUrl = "https://example.com/";
const kCrossOriginUrl = "https://example.org/";

// Run the clipboard calls in the page realm so that they use the page
// principal rather than the SpecialPowers sandbox one.
function writeCustomFormat(aUrl, aData) {
  return BrowserTestUtils.withNewTab(aUrl, async browser => {
    await SpecialPowers.spawn(
      browser,
      [kCustomFormat, aData],
      async (aFormat, aText) => {
        content.wrappedJSObject.customFormat = aFormat;
        content.wrappedJSObject.customData = aText;
        await content.wrappedJSObject.eval(`
          navigator.clipboard.write([
            new ClipboardItem({
              [customFormat]: new Blob([customData], { type: customFormat }),
              "text/plain": new Blob([customData], { type: "text/plain" }),
            }),
          ])
        `);
      }
    );
  });
}

function readClipboardTypes(aUrl) {
  return BrowserTestUtils.withNewTab(aUrl, async browser => {
    return SpecialPowers.spawn(browser, [], async () => {
      const types = await content.wrappedJSObject.eval(`
        navigator.clipboard
          .read()
          .then(items => (items.length ? Array.from(items[0].types) : []))
      `);
      return Array.from(types);
    });
  });
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.events.testing.asyncClipboard", true],
      ["dom.clipboard.customFormatSupport.enabled", true],
      ["clipboard.readCustomFormatsFromClipboard.enabled", true],
    ],
  });
});

add_task(async function test_same_origin_only() {
  await SpecialPowers.pushPrefEnv({
    set: [["clipboard.readCrossOriginCustomFormats.enabled", false]],
  });

  await writeCustomFormat(kSameOriginUrl, "same-origin-only " + Math.random());

  let types = await readClipboardTypes(kSameOriginUrl);
  ok(types.includes(kCustomFormat), "Same-origin read sees custom format");
  ok(types.includes("text/plain"), "Same-origin read sees text/plain");

  types = await readClipboardTypes(kCrossOriginUrl);
  ok(!types.includes(kCustomFormat), "Cross-origin read hides custom format");
  ok(types.includes("text/plain"), "Cross-origin read still sees text/plain");

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_data_without_principal() {
  await SpecialPowers.pushPrefEnv({
    set: [["clipboard.readCrossOriginCustomFormats.enabled", false]],
  });

  const trans = Cc["@mozilla.org/widget/transferable;1"].createInstance(
    Ci.nsITransferable
  );
  trans.init(null);
  trans.addDataFlavor(kCustomFormat);
  const data = Cc["@mozilla.org/supports-cstring;1"].createInstance(
    Ci.nsISupportsCString
  );
  data.data = "no principal " + Math.random();
  trans.setTransferData(kCustomFormat, data);
  Services.clipboard.setData(trans, null, Ci.nsIClipboard.kGlobalClipboard);

  const types = await readClipboardTypes(kSameOriginUrl);
  ok(
    !types.includes(kCustomFormat),
    "Custom format without a data principal is hidden"
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_cross_origin_pref_enabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["clipboard.readCrossOriginCustomFormats.enabled", true]],
  });

  await writeCustomFormat(
    kSameOriginUrl,
    "cross-origin pref enabled " + Math.random()
  );

  const types = await readClipboardTypes(kCrossOriginUrl);
  ok(
    types.includes(kCustomFormat),
    "Cross-origin read sees custom format when the pref is enabled"
  );

  await SpecialPowers.popPrefEnv();
});

async function readClipboardTypesFromExtension(aPermissions) {
  const extension = ExtensionTestUtils.loadExtension({
    manifest: { permissions: aPermissions },
    background() {
      navigator.clipboard
        .read()
        .then(items => (items.length ? Array.from(items[0].types) : []))
        .then(types => browser.test.sendMessage("types", types))
        .catch(e => browser.test.sendMessage("types", [e.name]));
    },
  });
  await extension.startup();
  const types = await extension.awaitMessage("types");
  await extension.unload();
  return types;
}

add_task(async function test_extension_with_clipboard_read_permission() {
  await SpecialPowers.pushPrefEnv({
    set: [["clipboard.readCrossOriginCustomFormats.enabled", false]],
  });

  await writeCustomFormat(kSameOriginUrl, "extension " + Math.random());

  let types = await readClipboardTypesFromExtension(["clipboardRead"]);
  ok(
    types.includes(kCustomFormat),
    "Extension with clipboardRead sees cross-origin custom format"
  );

  types = await readClipboardTypesFromExtension([]);
  ok(
    !types.includes(kCustomFormat),
    "Extension without clipboardRead does not see cross-origin custom format"
  );
  ok(
    types.includes("text/plain"),
    "Extension without clipboardRead still sees text/plain"
  );

  await SpecialPowers.popPrefEnv();
});
