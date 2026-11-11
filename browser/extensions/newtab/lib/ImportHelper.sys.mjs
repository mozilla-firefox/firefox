/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
});

/**
 * Checks whether a module file exists without importing it. Importing a
 * missing chrome, resource or moz-src URL crashes Firefox under test
 * automation; loads typed as fetches are exempt from that check.
 *
 * @param {string} uri
 * @returns {boolean}
 */
function moduleExists(uri) {
  try {
    lazy.NetUtil.newChannel({
      uri,
      loadUsingSystemPrincipal: true,
      contentPolicyType: Ci.nsIContentPolicy.TYPE_FETCH,
    })
      .open()
      .close();
    return true;
  } catch {
    return false;
  }
}

export let ImportHelper = {
  /**
   * Helper so the new tab extension can easily trainhop while we convert
   * modules from resource:// to moz-src:// URIs.
   *
   * @param {string} module
   *   The full moz-src URI to import.
   * @param {string} [fallbackResourcePath ="resource://gre/modules/"]
   *   The resource url prefix to use for fallback import if the moz-src
   *   module does not exist.
   *   The helper will suffix _only_ the module filename to this path.
   */
  import(module, fallbackResourcePath = "resource://gre/modules/") {
    if (moduleExists(module)) {
      return ChromeUtils.importESModule(module);
    }
    let baseName = module.split("/").pop();
    return ChromeUtils.importESModule(fallbackResourcePath + baseName);
  },
};
