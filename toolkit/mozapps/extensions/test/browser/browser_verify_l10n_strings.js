/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Maps add-on descriptors to updated Fluent IDs. Keep it in sync
// with the list in XPIDatabase.sys.mjs.
const updatedAddonFluentIds = new Map([
  ["extension-default-theme-name", "extension-default-theme-name-auto"],
]);

add_task(async function test_ensure_bundled_addons_are_localized() {
  const l10n = new Localization(["browser/appExtensionFields.ftl"], true);
  let l10nReg = L10nRegistry.getInstance();
  let bundles = l10nReg.generateBundlesSync(
    ["en-US"],
    ["browser/appExtensionFields.ftl"]
  );
  let addons = await AddonManager.getAllAddons();
  let standardBuiltInThemes = addons.filter(
    addon => addon.isBuiltin && addon.type === "theme"
  );
  let bundle = bundles.next().value;

  ok(!!standardBuiltInThemes.length, "Standard built-in themes should exist");

  for (let standardTheme of standardBuiltInThemes) {
    let l10nId = standardTheme.id.replace("@mozilla.org", "");
    for (let prop of ["name", "description"]) {
      let defaultFluentId = `extension-${l10nId}-${prop}`;
      let fluentId =
        updatedAddonFluentIds.get(defaultFluentId) || defaultFluentId;
      ok(
        bundle.hasMessage(fluentId),
        `l10n id for ${standardTheme.id} \"${prop}\" attribute should exist`
      );
      const [expected] = l10n.formatMessagesSync([{ id: fluentId }]);
      Assert.equal(
        standardTheme[prop],
        expected.value,
        `Expect AddonWrapper ${prop} value to match the associated localized string`
      );
    }
  }
});
