/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  NativeManifests: "resource://gre/modules/NativeManifests.sys.mjs",
  ctypes: "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  this,
  "pkcs11db",
  "@mozilla.org/security/pkcs11moduledb;1",
  Ci.nsIPKCS11ModuleDB
);

var { DefaultMap } = ExtensionUtils;

const findModuleByPath = async function (path) {
  for (let module of await pkcs11db.listModules()) {
    if (module && module.libName === path) {
      return module;
    }
  }
  return null;
};

this.pkcs11 = class extends ExtensionAPI {
  getAPI(context) {
    let manifestCache = new DefaultMap(async name => {
      let hostInfo = await NativeManifests.lookupManifest(
        "pkcs11",
        name,
        context
      );
      if (hostInfo) {
        // We don't normalize the absolute path below because
        // `Path.normalize` throws when the target file doesn't
        // exist, and that might be the case on non Windows
        // builds.
        let absolutePath = PathUtils.isAbsolute(hostInfo.manifest.path)
          ? hostInfo.manifest.path
          : PathUtils.joinRelative(
              PathUtils.parent(hostInfo.path),
              hostInfo.manifest.path
            );

        if (AppConstants.platform === "win") {
          // On Windows, `hostInfo.manifest.path` is expected to be a normalized
          // absolute path. On other platforms, this path may be relative but we
          // cannot use `PathUtils.normalize()` on non-absolute paths.
          absolutePath = PathUtils.normalize(absolutePath);
          hostInfo.manifest.path = absolutePath;
        }

        // PathUtils.filename throws if the path is not an absolute path.
        // The result is expected to be the basename of the file (without
        // the dir path and the extension) so it is fine to use an absolute
        // path that may not be normalized (non-Windows platforms).
        let manifestLib = PathUtils.filename(absolutePath);

        if (AppConstants.platform !== "linux") {
          manifestLib = manifestLib.toLowerCase(manifestLib);
        }
        if (
          manifestLib !== ctypes.libraryName("nssckbi") &&
          manifestLib !== ctypes.libraryName("osclientcerts")
        ) {
          return hostInfo.manifest;
        }
      }
      return Promise.reject({ message: `No such PKCS#11 module ${name}` });
    });
    return {
      pkcs11: {
        /**
         * Verify whether a given PKCS#11 module is installed.
         *
         * @param {string} name The name of the module, as specified in
         *                      the manifest file.
         * @returns {Promise} A Promise that resolves to true if the package
         *                    is installed, or false if it is not. May be
         *                    rejected if the module could not be found.
         */
        async isModuleInstalled(name) {
          let manifest = await manifestCache.get(name);
          return (await findModuleByPath(manifest.path)) !== null;
        },
        /**
         * Install a PKCS#11 module
         *
         * @param {string} name The name of the module, as specified in
         *                      the manifest file.
         * @param {integer} [flags = 0] Any flags to be passed on to the
         *                              nsIPKCS11ModuleDB.addModule method
         * @returns {Promise} When the Promise resolves, the module will have
         *                    been installed. When it is rejected, the module
         *                    either is already installed or could not be
         *                    installed for some reason.
         */
        async installModule(name, flags = 0) {
          let manifest = await manifestCache.get(name);
          if (!manifest.description) {
            return Promise.reject({
              message: `The description field in the manifest for PKCS#11 module ${name} must have a value`,
            });
          }
          await pkcs11db.addModule(
            manifest.description,
            manifest.path,
            flags,
            0
          );
        },
        /**
         * Uninstall a PKCS#11 module
         *
         * @param {string} name The name of the module, as specified in
         *                      the manifest file.
         * @returns {Promise}. When the Promise resolves, the module will have
         *                     been uninstalled. When it is rejected, the
         *                     module either was not installed or could not be
         *                     uninstalled for some reason.
         */
        async uninstallModule(name) {
          let manifest = await manifestCache.get(name);
          let module = await findModuleByPath(manifest.path);
          if (!module) {
            return Promise.reject({
              message: `The PKCS#11 module ${name} is not loaded`,
            });
          }
          await pkcs11db.deleteModule(module.name);
        },
        /**
         * Get a list of slots for a given PKCS#11 module, with
         * information on the token (if any) in the slot.
         *
         * The PKCS#11 standard defines slots as an abstract concept
         * that may or may not have at most one token. In practice, when
         * using PKCS#11 for smartcards (the most likely use case of
         * PKCS#11 for Firefox), a slot corresponds to a cardreader, and
         * a token corresponds to a card.
         *
         * @param {string} name The name of the PKCS#11 module, as
         *                 specified in the manifest file.
         * @returns {Promise} A promise that resolves to an array of objects
         *                    with two properties.  The `name` object contains
         *                    the name of the slot; the `token` object is null
         *                    if there is no token in the slot, or is an object
         *                    describing various properties of the token if
         *                    there is.
         */
        async getModuleSlots(name) {
          let manifest = await manifestCache.get(name);
          let module = await findModuleByPath(manifest.path);
          if (!module) {
            return Promise.reject({
              message: `The module ${name} is not installed`,
            });
          }
          let rv = [];
          for (let slot of module.slots) {
            let slotobj = {
              name: slot.name,
              token: null,
            };
            if (
              slot.status != Ci.nsIPKCS11Slot.SLOT_DISABLED &&
              slot.status != Ci.nsIPKCS11Slot.SLOT_NOT_PRESENT
            ) {
              let token = slot.getToken();
              slotobj.token = {
                name: token.tokenName,
                manufacturer: token.tokenManID,
                HWVersion: token.tokenHWVersion,
                FWVersion: token.tokenFWVersion,
                serial: token.tokenSerialNumber,
                isLoggedIn: token.isLoggedIn,
              };
            }
            rv.push(slotobj);
          }
          return rv;
        },
      },
    };
  }
};
