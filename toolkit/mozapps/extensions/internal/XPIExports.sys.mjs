/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This file wraps XPIDatabase, XPIInstall, and XPIProvider modules in order to
 * allow testing the shutdown+restart situation in AddonTestUtils.sys.mjs.
 */

// A shared `lazy` object for exports from XPIDatabase, XPIInternal, and
// XPIProvider modules.
//
// Consumers shouldn't store those property values to global variables, except
// for registering XPIProvider.
//
// The list of lazy getters should be in sync with resetXPIExports in
// AddonTestUtils.sys.mjs.
const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  // XPIDatabase.sys.mjs
  AddonInternal: "resource://gre/modules/addons/XPIDatabase.sys.mjs",
  XPIDatabase: "resource://gre/modules/addons/XPIDatabase.sys.mjs",
  XPIDatabaseReconcile: "resource://gre/modules/addons/XPIDatabase.sys.mjs",

  // XPIInstall.sys.mjs
  UpdateChecker: "resource://gre/modules/addons/XPIInstall.sys.mjs",
  XPIInstall: "resource://gre/modules/addons/XPIInstall.sys.mjs",
  verifyBundleSignedState: "resource://gre/modules/addons/XPIInstall.sys.mjs",

  // XPIProvider.sys.mjs
  XPIProvider: "resource://gre/modules/addons/XPIProvider.sys.mjs",
  XPIInternal: "resource://gre/modules/addons/XPIProvider.sys.mjs",
});

export { lazy as XPIExports };
