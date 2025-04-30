/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  BuiltInThemeConfig: "resource:///modules/BuiltInThemeConfig.sys.mjs",
});

const kActiveThemePref = "extensions.activeThemeID";

class _BuiltInThemes {
  /**
   * The list of themes to be installed. This is exposed on the class so tests
   * can set custom config files.
   */
  builtInThemeMap = lazy.BuiltInThemeConfig;

  /**
   * @param {string} id An addon's id string.
   * @returns {string}
   *   If `id` refers to a built-in theme, returns a path pointing to the
   *   theme's preview image. Null otherwise.
   */
  previewForBuiltInThemeId(id) {
    let theme = this.builtInThemeMap.get(id);
    if (theme) {
      return `${theme.path}preview.svg`;
    }

    return null;
  }

  /**
   * If the active theme is built-in, this function calls
   * AddonManager.maybeInstallBuiltinAddon for that theme.
   */
  maybeInstallActiveBuiltInTheme() {
    const activeThemeID = Services.prefs.getStringPref(
      kActiveThemePref,
      "default-theme@mozilla.org"
    );
    let activeBuiltInTheme = this.builtInThemeMap.get(activeThemeID);

    if (activeBuiltInTheme) {
      lazy.AddonManager.maybeInstallBuiltinAddon(
        activeThemeID,
        activeBuiltInTheme.version,
        activeBuiltInTheme.path
      );
    }
  }

  /**
   * Ensures that all built-in themes are installed and expired themes are
   * uninstalled.
   */
  async ensureBuiltInThemes() {
    let installPromises = [];

    for (let [id, themeInfo] of this.builtInThemeMap.entries()) {
      installPromises.push(
        lazy.AddonManager.maybeInstallBuiltinAddon(
          id,
          themeInfo.version,
          themeInfo.path
        )
      );
    }

    await Promise.all(installPromises);
  }
}

export var BuiltInThemes = new _BuiltInThemes();
