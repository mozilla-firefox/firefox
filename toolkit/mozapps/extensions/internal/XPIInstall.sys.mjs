/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This file contains most of the logic required to install extensions.
 * In general, we try to avoid loading it until extension installation
 * or update is required. Please keep that in mind when deciding whether
 * to add code here or elsewhere.
 */

/**
 * @typedef {number} integer
 */

/* eslint "valid-jsdoc": [2, {requireReturn: false, requireReturnDescription: false, prefer: {return: "returns"}}] */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { XPIExports } from "resource://gre/modules/addons/XPIExports.sys.mjs";
import {
  computeSha256HashAsString,
  getHashStringForCrypto,
  hasStrongSignature,
} from "resource://gre/modules/addons/crypto-utils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import {
  AddonManager,
  AddonManagerPrivate,
} from "resource://gre/modules/AddonManager.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonRepository: "resource://gre/modules/addons/AddonRepository.sys.mjs",
  AddonSettings: "resource://gre/modules/addons/AddonSettings.sys.mjs",
  CertUtils: "resource://gre/modules/CertUtils.sys.mjs",
  ExtensionData: "resource://gre/modules/Extension.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  ProductAddonChecker:
    "resource://gre/modules/addons/ProductAddonChecker.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  UpdateUtils: "resource://gre/modules/UpdateUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "IconDetails", () => {
  return ChromeUtils.importESModule(
    "resource://gre/modules/ExtensionParent.sys.mjs"
  ).ExtensionParent.IconDetails;
});

const { nsIBlocklistService } = Ci;

const nsIFile = Components.Constructor(
  "@mozilla.org/file/local;1",
  "nsIFile",
  "initWithPath"
);

const BinaryOutputStream = Components.Constructor(
  "@mozilla.org/binaryoutputstream;1",
  "nsIBinaryOutputStream",
  "setOutputStream"
);
const CryptoHash = Components.Constructor(
  "@mozilla.org/security/hash;1",
  "nsICryptoHash",
  "initWithString"
);
const FileInputStream = Components.Constructor(
  "@mozilla.org/network/file-input-stream;1",
  "nsIFileInputStream",
  "init"
);
const FileOutputStream = Components.Constructor(
  "@mozilla.org/network/file-output-stream;1",
  "nsIFileOutputStream",
  "init"
);
const ZipReader = Components.Constructor(
  "@mozilla.org/libjar/zip-reader;1",
  "nsIZipReader",
  "open"
);

XPCOMUtils.defineLazyServiceGetters(lazy, {
  gCertDB: ["@mozilla.org/security/x509certdb;1", "nsIX509CertDB"],
});

const PREF_INSTALL_REQUIRESECUREORIGIN =
  "extensions.install.requireSecureOrigin";
const PREF_PENDING_OPERATIONS = "extensions.pendingOperations";
const PREF_SYSTEM_ADDON_UPDATE_URL = "extensions.systemAddon.update.url";
const PREF_XPI_ENABLED = "xpinstall.enabled";
const PREF_XPI_DIRECT_WHITELISTED = "xpinstall.whitelist.directRequest";
const PREF_XPI_FILE_WHITELISTED = "xpinstall.whitelist.fileRequest";
const PREF_XPI_WHITELIST_REQUIRED = "xpinstall.whitelist.required";
const PREF_XPI_WEAK_SIGNATURES_ALLOWED =
  "xpinstall.signatures.weakSignaturesTemporarilyAllowed";

const PREF_SELECTED_THEME = "extensions.activeThemeID";

const TOOLKIT_ID = "toolkit@mozilla.org";

ChromeUtils.defineLazyGetter(lazy, "MOZ_UNSIGNED_SCOPES", () => {
  let result = 0;
  if (AppConstants.MOZ_UNSIGNED_APP_SCOPE) {
    result |= AddonManager.SCOPE_APPLICATION;
  }
  if (AppConstants.MOZ_UNSIGNED_SYSTEM_SCOPE) {
    result |= AddonManager.SCOPE_SYSTEM;
  }
  return result;
});

/**
 * Returns a nsIFile instance for the given path, relative to the given
 * base file, if provided.
 *
 * @param {string} path
 *        The (possibly relative) path of the file.
 * @param {nsIFile} [base]
 *        An optional file to use as a base path if `path` is relative.
 * @returns {nsIFile}
 */
function getFile(path, base = null) {
  // First try for an absolute path, as we get in the case of proxy
  // files. Ideally we would try a relative path first, but on Windows,
  // paths which begin with a drive letter are valid as relative paths,
  // and treated as such.
  try {
    return new nsIFile(path);
  } catch (e) {
    // Ignore invalid relative paths. The only other error we should see
    // here is EOM, and either way, any errors that we care about should
    // be re-thrown below.
  }

  // If the path isn't absolute, we must have a base path.
  let file = base.clone();
  file.appendRelativePath(path);
  return file;
}

/**
 * Sends local and remote notifications to flush a JAR file cache entry
 *
 * @param {nsIFile} aJarFile
 *        The ZIP/XPI/JAR file as a nsIFile
 */
function flushJarCache(aJarFile) {
  Services.obs.notifyObservers(aJarFile, "flush-cache-entry");
  Services.ppmm.broadcastAsyncMessage(MSG_JAR_FLUSH, {
    path: aJarFile.path,
  });
}

const PREF_EM_UPDATE_BACKGROUND_URL = "extensions.update.background.url";
const PREF_EM_UPDATE_URL = "extensions.update.url";
const PREF_XPI_SIGNATURES_DEV_ROOT = "xpinstall.signatures.dev-root";

const KEY_TEMPDIR = "TmpD";

// This is a random number array that can be used as "salt" when generating
// an automatic ID based on the directory path of an add-on. It will prevent
// someone from creating an ID for a permanent add-on that could be replaced
// by a temporary add-on (because that would be confusing, I guess).
const TEMP_INSTALL_ID_GEN_SESSION = new Uint8Array(
  Float64Array.of(Math.random()).buffer
);

const MSG_JAR_FLUSH = "Extension:FlushJarCache";

/**
 * Valid IDs fit this pattern.
 */
var gIDTest =
  /^(\{[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\}|[a-z0-9-\._]*\@[a-z0-9-\._]+)$/i;

import { Log } from "resource://gre/modules/Log.sys.mjs";

const LOGGER_ID = "addons.xpi";

// Create a new logger for use by all objects in this Addons XPI Provider module
// (Requires AddonManager.sys.mjs)
var logger = Log.repository.getLogger(LOGGER_ID);

// Stores the ID of the theme which was selected during the last session,
// if any. When installing a new built-in theme with this ID, it will be
// automatically enabled.
let lastSelectedTheme = null;

function getJarURI(file, path = "") {
  if (file instanceof Ci.nsIFile) {
    file = Services.io.newFileURI(file);
  }
  if (file instanceof Ci.nsIURI) {
    file = file.spec;
  }
  return Services.io.newURI(`jar:${file}!/${path}`);
}

let DirPackage;
let XPIPackage;
class Package {
  static get(file) {
    if (file.isFile()) {
      return new XPIPackage(file);
    }
    return new DirPackage(file);
  }

  constructor(file, rootURI) {
    this.file = file;
    this.filePath = file.path;
    this.rootURI = rootURI;
  }

  close() {}

  async readString(...path) {
    let buffer = await this.readBinary(...path);
    return new TextDecoder().decode(buffer);
  }

  async verifySignedState(addonId, addonType, addonLocation) {
    if (!shouldVerifySignedState(addonType, addonLocation)) {
      return {
        signedState: AddonManager.SIGNEDSTATE_NOT_REQUIRED,
        cert: null,
      };
    }

    let root = Ci.nsIX509CertDB.AddonsPublicRoot;
    if (
      (!AppConstants.MOZ_REQUIRE_SIGNING ||
        // Allow mochitests to switch to dev-root on all channels.
        Cu.isInAutomation ||
        // Allow xpcshell tests to switch to dev-root on all channels,
        // included tests where "security.turn_off_all_security_so_that_viruses_can_take_over_this_computer"
        // pref is set to false and Cu.isInAutomation is going to be false (e.g. test_signed_langpack.js).
        // TODO(Bug 1598804): we should be able to remove the following checks once Cu.isAutomation is fixed.
        (Services.env.exists("XPCSHELL_TEST_PROFILE_DIR") &&
          Services.appinfo.name === "XPCShell")) &&
      Services.prefs.getBoolPref(PREF_XPI_SIGNATURES_DEV_ROOT, false)
    ) {
      root = Ci.nsIX509CertDB.AddonsStageRoot;
    }

    return this.verifySignedStateForRoot(addonId, root);
  }

  flushCache() {}
}

DirPackage = class DirPackage extends Package {
  constructor(file) {
    super(file, Services.io.newFileURI(file));
  }

  hasResource(...path) {
    return IOUtils.exists(PathUtils.join(this.filePath, ...path));
  }

  async iterDirectory(path, callback) {
    let fullPath = PathUtils.join(this.filePath, ...path);

    let children = await IOUtils.getChildren(fullPath);
    for (let path of children) {
      let { type } = await IOUtils.stat(path);
      callback({
        isDir: type == "directory",
        name: PathUtils.filename(path),
        path,
      });
    }
  }

  iterFiles(callback, path = []) {
    return this.iterDirectory(path, async entry => {
      let entryPath = [...path, entry.name];
      if (entry.isDir) {
        callback({
          path: entryPath.join("/"),
          isDir: true,
        });
        await this.iterFiles(callback, entryPath);
      } else {
        callback({
          path: entryPath.join("/"),
          isDir: false,
        });
      }
    });
  }

  readBinary(...path) {
    return IOUtils.read(PathUtils.join(this.filePath, ...path));
  }

  async verifySignedStateForRoot() {
    return { signedState: AddonManager.SIGNEDSTATE_UNKNOWN, cert: null };
  }
};

XPIPackage = class XPIPackage extends Package {
  constructor(file) {
    super(file, getJarURI(file));

    this.zipReader = new ZipReader(file);
  }

  close() {
    this.zipReader.close();
    this.zipReader = null;
    this.flushCache();
  }

  async hasResource(...path) {
    return this.zipReader.hasEntry(path.join("/"));
  }

  async iterFiles(callback) {
    for (let path of this.zipReader.findEntries("*")) {
      let entry = this.zipReader.getEntry(path);
      callback({
        path,
        isDir: entry.isDirectory,
      });
    }
  }

  async readBinary(...path) {
    let response = await fetch(this.rootURI.resolve(path.join("/")));
    return response.arrayBuffer();
  }

  verifySignedStateForRoot(addonId, root) {
    return new Promise(resolve => {
      let callback = {
        openSignedAppFileFinished(aRv, aZipReader, aSignatureInfos) {
          // aSignatureInfos is an array of nsIAppSignatureInfo.
          // In the future, this code can iterate through the array to
          // determine if one of the verified signatures used a satisfactory
          // algorithm and signing certificate.
          // For now, any verified signature is acceptable.
          let cert;
          if (aRv == Cr.NS_OK && aSignatureInfos.length) {
            cert = aSignatureInfos[0].signerCert;
          }
          if (aZipReader) {
            aZipReader.close();
          }
          resolve({
            cert,
            signedState: getSignedStatus(aRv, cert, addonId),
            signedTypes: aSignatureInfos?.map(
              signatureInfo => signatureInfo.signatureAlgorithm
            ),
          });
        },
      };
      // This allows the certificate DB to get the raw JS callback object so the
      // test code can pass through objects that XPConnect would reject.
      callback.wrappedJSObject = callback;

      lazy.gCertDB.openSignedAppFileAsync(root, this.file, callback);
    });
  }

  flushCache() {
    flushJarCache(this.file);
  }
};

/**
 * Return an object that implements enough of the Package interface
 * to allow loadManifest() to work for a built-in addon (ie, one loaded
 * from a resource: url)
 *
 * @param {nsIURL} baseURL The URL for the root of the add-on.
 * @returns {object}
 */
function builtinPackage(baseURL) {
  return {
    rootURI: baseURL,
    filePath: baseURL.spec,
    file: null,
    verifySignedState() {
      return {
        signedState: AddonManager.SIGNEDSTATE_NOT_REQUIRED,
        cert: null,
      };
    },
    async hasResource(path) {
      try {
        let response = await fetch(this.rootURI.resolve(path));
        return response.ok;
      } catch (e) {
        return false;
      }
    },
  };
}

/**
 * Determine the reason to pass to an extension's bootstrap methods when
 * switch between versions.
 *
 * @param {string} oldVersion The version of the existing extension instance.
 * @param {string} newVersion The version of the extension being installed.
 *
 * @returns {integer}
 *        BOOSTRAP_REASONS.ADDON_UPGRADE or BOOSTRAP_REASONS.ADDON_DOWNGRADE
 */
function newVersionReason(oldVersion, newVersion) {
  return Services.vc.compare(oldVersion, newVersion) <= 0
    ? XPIExports.XPIInternal.BOOTSTRAP_REASONS.ADDON_UPGRADE
    : XPIExports.XPIInternal.BOOTSTRAP_REASONS.ADDON_DOWNGRADE;
}

// Behaves like Promise.all except waits for all promises to resolve/reject
// before resolving/rejecting itself
function waitForAllPromises(promises) {
  return new Promise((resolve, reject) => {
    let shouldReject = false;
    let rejectValue = null;

    let newPromises = promises.map(p =>
      p.catch(value => {
        shouldReject = true;
        rejectValue = value;
      })
    );
    Promise.all(newPromises).then(results =>
      shouldReject ? reject(rejectValue) : resolve(results)
    );
  });
}

/**
 * Reads an AddonInternal object from a webextension manifest.json
 *
 * @param {Package} aPackage
 *        The install package for the add-on
 * @param {XPIStateLocation} aLocation
 *        The install location the add-on is installed in, or will be
 *        installed to.
 * @returns {{ addon: AddonInternal, verifiedSignedState: object}}
 * @throws if the install manifest in the stream is corrupt or could not
 *         be read
 */
async function loadManifestFromWebManifest(aPackage, aLocation) {
  let verifiedSignedState;
  const temporarilyInstalled = aLocation.isTemporary;
  let extension = await lazy.ExtensionData.constructAsync({
    rootURI: XPIExports.XPIInternal.maybeResolveURI(aPackage.rootURI),
    temporarilyInstalled,
    async checkPrivileged(type, id) {
      verifiedSignedState = await aPackage.verifySignedState(
        id,
        type,
        aLocation
      );
      return lazy.ExtensionData.getIsPrivileged({
        signedState: verifiedSignedState.signedState,
        builtIn: aLocation.isBuiltin,
        temporarilyInstalled,
      });
    },
  });

  let manifest = await extension.loadManifest();

  // Read the list of available locales, and pre-load messages for
  // all locales.
  let locales = !extension.errors.length
    ? await extension.initAllLocales()
    : null;

  if (extension.errors.length) {
    let error = new Error("Extension is invalid");
    // Add detailed errors on the error object so that the front end can display them
    // if needed (eg in about:debugging).
    error.additionalErrors = extension.errors;
    throw error;
  }

  // Internally, we use the `applications` key but it is because we assign the value
  // of `browser_specific_settings` to `applications` in `ExtensionData.parseManifest()`.
  // Yet, as of MV3, only `browser_specific_settings` is accepted in manifest.json files.
  let bss = manifest.applications?.gecko || {};

  // A * is illegal in strict_min_version
  if (bss.strict_min_version?.split(".").some(part => part == "*")) {
    throw new Error("The use of '*' in strict_min_version is invalid");
  }

  let addon = new XPIExports.AddonInternal();
  addon.id = bss.id;
  addon.version = manifest.version;
  addon.manifestVersion = manifest.manifest_version;
  addon.name = manifest.name;
  addon.type = extension.type;
  addon.loader = null;
  addon.strictCompatibility = true;
  addon.internalName = null;
  addon.updateURL = bss.update_url;
  addon.installOrigins = manifest.install_origins;
  addon.optionsBrowserStyle = true;
  addon.optionsURL = null;
  addon.optionsType = null;
  addon.aboutURL = null;
  addon.dependencies = Object.freeze(Array.from(extension.dependencies));
  addon.startupData = extension.startupData;
  addon.hidden = extension.isPrivileged && manifest.hidden;
  addon.incognito = manifest.incognito;

  if (addon.type === "theme" && (await aPackage.hasResource("preview.png"))) {
    addon.previewImage = "preview.png";
  }

  const { optionsPageProperties } = extension;
  if (optionsPageProperties) {
    // Store just the relative path here, the AddonWrapper getURL
    // wrapper maps this to a full URL.
    addon.optionsURL = optionsPageProperties.page;
    if (optionsPageProperties.open_in_tab) {
      addon.optionsType = AddonManager.OPTIONS_TYPE_TAB;
    } else {
      addon.optionsType = AddonManager.OPTIONS_TYPE_INLINE_BROWSER;
    }

    addon.optionsBrowserStyle = optionsPageProperties.browser_style;
  }

  // WebExtensions don't use iconURLs
  addon.iconURL = null;
  addon.icons = manifest.icons || {};
  addon.userPermissions = extension.getRequiredPermissions();
  addon.optionalPermissions = extension.manifestOptionalPermissions;
  addon.requestedPermissions = extension.getRequestedPermissions();
  addon.applyBackgroundUpdates = AddonManager.AUTOUPDATE_DEFAULT;

  function getLocale(aLocale) {
    // Use the raw manifest, here, since we need values with their
    // localization placeholders still in place.
    let rawManifest = extension.rawManifest;

    // As a convenience, allow author to be set if its a string bug 1313567.
    let creator =
      typeof rawManifest.author === "string" ? rawManifest.author : null;
    let homepageURL = rawManifest.homepage_url;

    // Allow developer to override creator and homepage_url.
    if (rawManifest.developer) {
      if (rawManifest.developer.name) {
        creator = rawManifest.developer.name;
      }
      if (rawManifest.developer.url) {
        homepageURL = rawManifest.developer.url;
      }
    }

    let result = {
      name: extension.localize(rawManifest.name, aLocale),
      description: extension.localize(rawManifest.description, aLocale),
      creator: extension.localize(creator, aLocale),
      homepageURL: extension.localize(homepageURL, aLocale),

      developers: null,
      translators: null,
      contributors: null,
      locales: [aLocale],
    };
    if (result.name.length > lazy.ExtensionData.EXT_NAME_MAX_LEN) {
      // See comment at EXT_NAME_MAX_LEN in Extension.sys.mjs.
      logger.warn(`Truncating add-on name ${addon.id} for locale ${aLocale}`);
      result.name = result.name.slice(0, lazy.ExtensionData.EXT_NAME_MAX_LEN);
    }
    return result;
  }

  addon.defaultLocale = getLocale(extension.defaultLocale);
  addon.locales = Array.from(locales.keys(), getLocale);

  delete addon.defaultLocale.locales;

  addon.targetApplications = [
    {
      id: TOOLKIT_ID,
      minVersion: bss.strict_min_version,
      maxVersion: bss.strict_max_version,
    },
  ];

  addon.adminInstallOnly = bss.admin_install_only;

  addon.targetPlatforms = [];
  // Themes are disabled by default, except when they're installed from a web page.
  addon.userDisabled = extension.type === "theme";
  addon.softDisabled =
    addon.blocklistState == nsIBlocklistService.STATE_SOFTBLOCKED;

  return { addon, verifiedSignedState };
}

async function readRecommendationStates(aPackage, aAddonID) {
  let recommendationData;
  try {
    recommendationData = await aPackage.readString(
      "mozilla-recommendation.json"
    );
  } catch (e) {
    // Ignore I/O errors.
    return null;
  }

  try {
    recommendationData = JSON.parse(recommendationData);
  } catch (e) {
    logger.warn("Failed to parse recommendation", e);
  }

  if (recommendationData) {
    let { addon_id, states, validity } = recommendationData;

    if (addon_id === aAddonID && Array.isArray(states) && validity) {
      let validNotAfter = Date.parse(validity.not_after);
      let validNotBefore = Date.parse(validity.not_before);
      if (validNotAfter && validNotBefore) {
        return {
          validNotAfter,
          validNotBefore,
          states,
        };
      }
    }
    logger.warn(
      `Invalid recommendation for ${aAddonID}: ${JSON.stringify(
        recommendationData
      )}`
    );
  }

  return null;
}

function defineSyncGUID(aAddon) {
  // Define .syncGUID as a lazy property which is also settable
  Object.defineProperty(aAddon, "syncGUID", {
    get: () => {
      aAddon.syncGUID = Services.uuid.generateUUID().toString();
      return aAddon.syncGUID;
    },
    set: val => {
      delete aAddon.syncGUID;
      aAddon.syncGUID = val;
    },
    configurable: true,
    enumerable: true,
  });
}

// Generate a unique ID based on the path to this temporary add-on location.
function generateTemporaryInstallID(aFile) {
  const hasher = CryptoHash("sha1");
  const data = new TextEncoder().encode(aFile.path);
  // Make it so this ID cannot be guessed.
  const sess = TEMP_INSTALL_ID_GEN_SESSION;
  hasher.update(sess, sess.length);
  hasher.update(data, data.length);
  let id = `${getHashStringForCrypto(hasher)}${
    XPIExports.XPIInternal.TEMPORARY_ADDON_SUFFIX
  }`;
  logger.info(`Generated temp id ${id} (${sess.join("")}) for ${aFile.path}`);
  return id;
}

var loadManifest = async function (aPackage, aLocation, aOldAddon) {
  let addon;
  let verifiedSignedState;
  if (await aPackage.hasResource("manifest.json")) {
    ({ addon, verifiedSignedState } = await loadManifestFromWebManifest(
      aPackage,
      aLocation
    ));
  } else {
    // TODO bug 1674799: Remove this unused branch.
    for (let loader of AddonManagerPrivate.externalExtensionLoaders.values()) {
      if (await aPackage.hasResource(loader.manifestFile)) {
        addon = await loader.loadManifest(aPackage);
        addon.loader = loader.name;
        verifiedSignedState = await aPackage.verifySignedState(
          addon.id,
          addon.type,
          aLocation
        );
        break;
      }
    }
  }

  if (!addon) {
    throw new Error(
      `File ${aPackage.filePath} does not contain a valid manifest`
    );
  }

  addon._sourceBundle = aPackage.file;
  addon.rootURI = aPackage.rootURI.spec;
  addon.location = aLocation;

  let { cert, signedState, signedTypes } = verifiedSignedState;
  addon.signedState = signedState;
  addon.signedDate = cert?.validity?.notBefore / 1000 || null;
  // An array of the algorithms used by the signatures found in the signed XPI files,
  // as an array of integers (see nsIAppSignatureInfo_SignatureAlgorithm enum defined
  // in nsIX509CertDB.idl).
  addon.signedTypes = signedTypes;

  if (!addon.id) {
    if (cert) {
      addon.id = cert.commonName;
      if (!gIDTest.test(addon.id)) {
        throw new Error(`Extension is signed with an invalid id (${addon.id})`);
      }
    }
    if (!addon.id && aLocation.isTemporary) {
      addon.id = generateTemporaryInstallID(aPackage.file);
    }
  }

  addon.propagateDisabledState(aOldAddon);
  if (!aLocation.isSystem && !aLocation.isBuiltin) {
    if (addon.type === "extension" && !aLocation.isTemporary) {
      addon.recommendationState = await readRecommendationStates(
        aPackage,
        addon.id
      );
    }

    await addon.updateBlocklistState();
    addon.appDisabled = !XPIExports.XPIDatabase.isUsableAddon(addon);

    // Always report when there is an attempt to install a blocked add-on.
    // (transitions from STATE_BLOCKED to STATE_NOT_BLOCKED are checked
    //  in the individual AddonInstall subclasses).
    if (addon.blocklistState > nsIBlocklistService.STATE_NOT_BLOCKED) {
      addon.recordAddonBlockChangeTelemetry(
        aOldAddon ? "addon_update" : "addon_install"
      );
    }
  }

  defineSyncGUID(addon);

  return addon;
};

/**
 * Loads an add-on's manifest from the given file or directory.
 *
 * @param {nsIFile} aFile
 *        The file to load the manifest from.
 * @param {XPIStateLocation} aLocation
 *        The install location the add-on is installed in, or will be
 *        installed to.
 * @param {AddonInternal?} aOldAddon
 *        The currently-installed add-on with the same ID, if one exist.
 *        This is used to migrate user settings like the add-on's
 *        disabled state.
 * @returns {AddonInternal}
 *        The parsed Addon object for the file's manifest.
 */
var loadManifestFromFile = async function (aFile, aLocation, aOldAddon) {
  let pkg = Package.get(aFile);
  try {
    let addon = await loadManifest(pkg, aLocation, aOldAddon);
    return addon;
  } finally {
    pkg.close();
  }
};

/*
 * A synchronous method for loading an add-on's manifest. Do not use
 * this.
 */
function syncLoadManifest(state, location, oldAddon) {
  const { KEY_APP_BUILTINS, KEY_APP_SYSTEM_BUILTINS, awaitPromise } =
    XPIExports.XPIInternal;
  if ([KEY_APP_BUILTINS, KEY_APP_SYSTEM_BUILTINS].includes(location.name)) {
    let pkg = builtinPackage(Services.io.newURI(state.rootURI));
    return awaitPromise(loadManifest(pkg, location, oldAddon));
  }

  let file = new nsIFile(state.path);
  let pkg = Package.get(file);
  return XPIExports.XPIInternal.awaitPromise(
    (async () => {
      try {
        let addon = await loadManifest(pkg, location, oldAddon);
        addon.rootURI = XPIExports.XPIInternal.getURIForResourceInFile(
          file,
          ""
        ).spec;
        return addon;
      } finally {
        pkg.close();
      }
    })()
  );
}

/**
 * Creates and returns a new unique temporary file. The caller should delete
 * the file when it is no longer needed.
 *
 * @returns {nsIFile}
 *       An nsIFile that points to a randomly named, initially empty file in
 *       the OS temporary files directory
 */
function getTemporaryFile() {
  let file = lazy.FileUtils.getDir(KEY_TEMPDIR, []);
  let random = Math.round(Math.random() * 36 ** 3).toString(36);
  file.append(`tmp-${random}.xpi`);
  file.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, lazy.FileUtils.PERMS_FILE);
  return file;
}

function getHashForFile(file, algorithm) {
  let crypto = CryptoHash(algorithm);
  let fis = new FileInputStream(file, -1, -1, false);
  try {
    crypto.updateFromStream(fis, file.fileSize);
  } finally {
    fis.close();
  }
  return getHashStringForCrypto(crypto);
}

/**
 * Returns the signedState for a given return code and certificate by verifying
 * it against the expected ID.
 *
 * @param {nsresult} aRv
 *        The result code returned by the signature checker for the
 *        signature check operation.
 * @param {nsIX509Cert?} aCert
 *        The certificate the add-on was signed with, if a valid
 *        certificate exists.
 * @param {string?} aAddonID
 *        The expected ID of the add-on. If passed, this must match the
 *        ID in the certificate's CN field.
 * @returns {number}
 *        A SIGNEDSTATE result code constant, as defined on the
 *        AddonManager class.
 */
function getSignedStatus(aRv, aCert, aAddonID) {
  let expectedCommonName = aAddonID;
  if (aAddonID && aAddonID.length > 64) {
    expectedCommonName = computeSha256HashAsString(aAddonID);
  }

  switch (aRv) {
    case Cr.NS_OK:
      if (expectedCommonName && expectedCommonName != aCert.commonName) {
        return AddonManager.SIGNEDSTATE_BROKEN;
      }

      if (aCert.organizationalUnit == "Mozilla Components") {
        return AddonManager.SIGNEDSTATE_SYSTEM;
      }

      if (aCert.organizationalUnit == "Mozilla Extensions") {
        return AddonManager.SIGNEDSTATE_PRIVILEGED;
      }

      return /preliminary/i.test(aCert.organizationalUnit)
        ? AddonManager.SIGNEDSTATE_PRELIMINARY
        : AddonManager.SIGNEDSTATE_SIGNED;
    case Cr.NS_ERROR_SIGNED_JAR_NOT_SIGNED:
      return AddonManager.SIGNEDSTATE_MISSING;
    case Cr.NS_ERROR_SIGNED_JAR_MANIFEST_INVALID:
    case Cr.NS_ERROR_SIGNED_JAR_ENTRY_INVALID:
    case Cr.NS_ERROR_SIGNED_JAR_ENTRY_MISSING:
    case Cr.NS_ERROR_SIGNED_JAR_ENTRY_TOO_LARGE:
    case Cr.NS_ERROR_SIGNED_JAR_UNSIGNED_ENTRY:
    case Cr.NS_ERROR_SIGNED_JAR_MODIFIED_ENTRY:
      return AddonManager.SIGNEDSTATE_BROKEN;
    default:
      // Any other error indicates that either the add-on isn't signed or it
      // is signed by a signature that doesn't chain to the trusted root.
      logger.warn(`Failed to verify signature for ${aAddonID}: ${aRv}`);
      return AddonManager.SIGNEDSTATE_UNKNOWN;
  }
}

function shouldVerifySignedState(aAddonType, aLocation) {
  // Updated system add-ons should always have their signature checked (unless they are built
  // into the omni jar).
  if (
    aLocation.isSystem &&
    aLocation.name !== XPIExports.XPIProvider.KEY_APP_SYSTEM_BUILTINS
  ) {
    return true;
  }

  if (aLocation.isBuiltin || aLocation.scope & lazy.MOZ_UNSIGNED_SCOPES) {
    return false;
  }

  // Otherwise only check signatures if the add-on is one of the signed
  // types.
  return XPIExports.XPIDatabase.SIGNED_TYPES.has(aAddonType);
}

/**
 * Verifies that a bundle's contents are all correctly signed by an
 * AMO-issued certificate
 *
 * @param {nsIFile} aBundle
 *        The nsIFile for the bundle to check, either a directory or zip file.
 * @param {AddonInternal} aAddon
 *        The add-on object to verify.
 * @returns {Promise<{ signedState: number, signedTypes: Array<number>}>?}
 *        A Promise that resolves to object including a signedState property set to
 *        an AddonManager.SIGNEDSTATE_* constant and a signedTypes property set to
 *        either an array of Ci.nsIAppSignatureInfo SignatureAlgorithm enum values
 *        or undefined if the file wasn't signed.
 */
export var verifyBundleSignedState = async function (aBundle, aAddon) {
  let pkg = Package.get(aBundle);
  try {
    let { signedState, signedTypes } = await pkg.verifySignedState(
      aAddon.id,
      aAddon.type,
      aAddon.location
    );
    return { signedState, signedTypes };
  } finally {
    pkg.close();
  }
};

/**
 * Replaces %...% strings in an addon url (update and updateInfo) with
 * appropriate values.
 *
 * @param {AddonInternal} aAddon
 *        The AddonInternal representing the add-on
 * @param {string} aUri
 *        The URI to escape
 * @param {integer?} aUpdateType
 *        An optional number representing the type of update, only applicable
 *        when creating a url for retrieving an update manifest
 * @param {string?} aAppVersion
 *        The optional application version to use for %APP_VERSION%
 * @returns {string}
 *       The appropriately escaped URI.
 */
function escapeAddonURI(aAddon, aUri, aUpdateType, aAppVersion) {
  let uri = AddonManager.escapeAddonURI(aAddon, aUri, aAppVersion);

  // If there is an updateType then replace the UPDATE_TYPE string
  if (aUpdateType) {
    uri = uri.replace(/%UPDATE_TYPE%/g, aUpdateType);
  }

  // If this add-on has compatibility information for either the current
  // application or toolkit then replace the ITEM_MAXAPPVERSION with the
  // maxVersion
  let app = aAddon.matchingTargetApplication;
  if (app) {
    var maxVersion = app.maxVersion;
  } else {
    maxVersion = "";
  }
  uri = uri.replace(/%ITEM_MAXAPPVERSION%/g, maxVersion);

  let compatMode = "normal";
  if (!AddonManager.checkCompatibility) {
    compatMode = "ignore";
  } else if (AddonManager.strictCompatibility) {
    compatMode = "strict";
  }
  uri = uri.replace(/%COMPATIBILITY_MODE%/g, compatMode);

  return uri;
}

/**
 * Converts an iterable of addon objects into a map with the add-on's ID as key.
 *
 * @param {sequence<AddonInternal>} addons
 *        A sequence of AddonInternal objects.
 *
 * @returns {Map<string, AddonInternal>}
 */
function addonMap(addons) {
  return new Map(addons.map(a => [a.id, a]));
}

async function removeAsync(aFile) {
  await IOUtils.remove(aFile.path, { ignoreAbsent: true, recursive: true });
}

/**
 * Recursively removes a directory or file fixing permissions when necessary.
 *
 * @param {nsIFile} aFile
 *        The nsIFile to remove
 */
function recursiveRemove(aFile) {
  let isDir = null;

  try {
    isDir = aFile.isDirectory();
  } catch (e) {
    // If the file has already gone away then don't worry about it, this can
    // happen on OSX where the resource fork is automatically moved with the
    // data fork for the file. See bug 733436.
    if (e.result == Cr.NS_ERROR_FILE_NOT_FOUND) {
      return;
    }

    throw e;
  }

  setFilePermissions(
    aFile,
    isDir ? lazy.FileUtils.PERMS_DIRECTORY : lazy.FileUtils.PERMS_FILE
  );

  try {
    aFile.remove(true);
    return;
  } catch (e) {
    if (!aFile.isDirectory() || aFile.isSymlink()) {
      logger.error("Failed to remove file " + aFile.path, e);
      throw e;
    }
  }

  // Use a snapshot of the directory contents to avoid possible issues with
  // iterating over a directory while removing files from it (the YAFFS2
  // embedded filesystem has this issue, see bug 772238), and to remove
  // normal files before their resource forks on OSX (see bug 733436).
  let entries = Array.from(XPIExports.XPIInternal.iterDirectory(aFile));
  entries.forEach(recursiveRemove);

  try {
    aFile.remove(true);
  } catch (e) {
    logger.error("Failed to remove empty directory " + aFile.path, e);
    throw e;
  }
}

/**
 * Sets permissions on a file
 *
 * @param {nsIFile} aFile
 *        The file or directory to operate on.
 * @param {integer} aPermissions
 *        The permissions to set
 */
function setFilePermissions(aFile, aPermissions) {
  try {
    aFile.permissions = aPermissions;
  } catch (e) {
    logger.warn(
      "Failed to set permissions " +
        aPermissions.toString(8) +
        " on " +
        aFile.path,
      e
    );
  }
}

/**
 * Write a given string to a file
 *
 * @param {nsIFile} file
 *        The nsIFile instance to write into
 * @param {string} string
 *        The string to write
 */
function writeStringToFile(file, string) {
  let fileStream = new FileOutputStream(
    file,
    lazy.FileUtils.MODE_WRONLY |
      lazy.FileUtils.MODE_CREATE |
      lazy.FileUtils.MODE_TRUNCATE,
    lazy.FileUtils.PERMS_FILE,
    0
  );

  try {
    let binStream = new BinaryOutputStream(fileStream);

    binStream.writeByteArray(new TextEncoder().encode(string));
  } finally {
    fileStream.close();
  }
}

/**
 * A safe way to install a file or the contents of a directory to a new
 * directory. The file or directory is moved or copied recursively and if
 * anything fails an attempt is made to rollback the entire operation. The
 * operation may also be rolled back to its original state after it has
 * completed by calling the rollback method.
 *
 * Operations can be chained. Calling move or copy multiple times will remember
 * the whole set and if one fails all of the operations will be rolled back.
 */
function SafeInstallOperation() {
  this._installedFiles = [];
  this._createdDirs = [];
}

SafeInstallOperation.prototype = {
  _installedFiles: null,
  _createdDirs: null,

  _installFile(aFile, aTargetDirectory, aCopy) {
    let oldFile = aCopy ? null : aFile.clone();
    let newFile = aFile.clone();
    try {
      if (aCopy) {
        newFile.copyTo(aTargetDirectory, null);
        // copyTo does not update the nsIFile with the new.
        newFile = getFile(aFile.leafName, aTargetDirectory);
        // Windows roaming profiles won't properly sync directories if a new file
        // has an older lastModifiedTime than a previous file, so update.
        newFile.lastModifiedTime = Date.now();
      } else {
        newFile.moveTo(aTargetDirectory, null);
      }
    } catch (e) {
      logger.error(
        "Failed to " +
          (aCopy ? "copy" : "move") +
          " file " +
          aFile.path +
          " to " +
          aTargetDirectory.path,
        e
      );
      throw e;
    }
    this._installedFiles.push({ oldFile, newFile });
  },

  /**
   * Moves a file or directory into a new directory. If an error occurs then all
   * files that have been moved will be moved back to their original location.
   *
   * @param {nsIFile} aFile
   *        The file or directory to be moved.
   * @param {nsIFile} aTargetDirectory
   *        The directory to move into, this is expected to be an empty
   *        directory.
   */
  moveUnder(aFile, aTargetDirectory) {
    try {
      this._installFile(aFile, aTargetDirectory, false);
    } catch (e) {
      this.rollback();
      throw e;
    }
  },

  /**
   * Renames a file to a new location.  If an error occurs then all
   * files that have been moved will be moved back to their original location.
   *
   * @param {nsIFile} aOldLocation
   *        The old location of the file.
   * @param {nsIFile} aNewLocation
   *        The new location of the file.
   */
  moveTo(aOldLocation, aNewLocation) {
    try {
      let oldFile = aOldLocation.clone(),
        newFile = aNewLocation.clone();
      oldFile.moveTo(newFile.parent, newFile.leafName);
      this._installedFiles.push({ oldFile, newFile, isMoveTo: true });
    } catch (e) {
      this.rollback();
      throw e;
    }
  },

  /**
   * Copies a file or directory into a new directory. If an error occurs then
   * all new files that have been created will be removed.
   *
   * @param {nsIFile} aFile
   *        The file or directory to be copied.
   * @param {nsIFile} aTargetDirectory
   *        The directory to copy into, this is expected to be an empty
   *        directory.
   */
  copy(aFile, aTargetDirectory) {
    try {
      this._installFile(aFile, aTargetDirectory, true);
    } catch (e) {
      this.rollback();
      throw e;
    }
  },

  /**
   * Rolls back all the moves that this operation performed. If an exception
   * occurs here then both old and new directories are left in an indeterminate
   * state
   */
  rollback() {
    while (this._installedFiles.length) {
      let move = this._installedFiles.pop();
      if (move.isMoveTo) {
        move.newFile.moveTo(move.oldDir.parent, move.oldDir.leafName);
      } else if (move.newFile.isDirectory() && !move.newFile.isSymlink()) {
        let oldDir = getFile(move.oldFile.leafName, move.oldFile.parent);
        oldDir.create(
          Ci.nsIFile.DIRECTORY_TYPE,
          lazy.FileUtils.PERMS_DIRECTORY
        );
      } else if (!move.oldFile) {
        // No old file means this was a copied file
        move.newFile.remove(true);
      } else {
        move.newFile.moveTo(move.oldFile.parent, null);
      }
    }

    while (this._createdDirs.length) {
      recursiveRemove(this._createdDirs.pop());
    }
  },
};

// A hash algorithm if the caller of AddonInstall did not specify one.
const DEFAULT_HASH_ALGO = "sha256";

/**
 * Base class for objects that manage the installation of an addon.
 * This class isn't instantiated directly, see the derived classes below.
 */
class AddonInstall {
  /**
   * Instantiates an AddonInstall.
   *
   * @param {XPIStateLocation} installLocation
   *        The install location the add-on will be installed into
   * @param {nsIURL} url
   *        The nsIURL to get the add-on from. If this is an nsIFileURL then
   *        the add-on will not need to be downloaded
   * @param {Object} [options = {}]
   *        Additional options for the install
   * @param {string} [options.hash]
   *        An optional hash for the add-on
   * @param {AddonInternal} [options.existingAddon]
   *        The add-on this install will update if known
   * @param {string} [options.name]
   *        An optional name for the add-on
   * @param {string} [options.type]
   *        An optional type for the add-on
   * @param {object} [options.icons]
   *        Optional icons for the add-on
   * @param {string} [options.version]
   *        The expected version for the add-on.
   *        Required for updates, i.e. when existingAddon is set.
   * @param {Object?} [options.telemetryInfo]
   *        An optional object which provides details about the installation source
   *        included in the addon manager telemetry events.
   * @param {boolean} [options.isUserRequestedUpdate]
   *        An optional boolean, true if the install object is related to a user triggered update.
   * @param {nsIURL} [options.releaseNotesURI]
   *        An optional nsIURL that release notes where release notes can be retrieved.
   * @param {function(string) : Promise<void>} [options.promptHandler]
   *        A callback to prompt the user before installing.
   */
  constructor(installLocation, url, options = {}) {
    this.wrapper = new AddonInstallWrapper(this);
    this.location = installLocation;
    this.sourceURI = url;

    if (options.hash) {
      let hashSplit = options.hash.toLowerCase().split(":");
      this.originalHash = {
        algorithm: hashSplit[0],
        data: hashSplit[1],
      };
    }
    this.hash = this.originalHash;
    this.fileHash = null;
    this.existingAddon = options.existingAddon || null;
    this.promptHandler = options.promptHandler || (() => Promise.resolve());
    this.releaseNotesURI = options.releaseNotesURI || null;

    this._startupPromise = null;

    this._installPromise = new Promise(resolve => {
      this._resolveInstallPromise = resolve;
    });
    // Ignore uncaught rejections for this promise, since they're
    // handled by install listeners.
    this._installPromise.catch(() => {});

    this.listeners = [];
    this.icons = options.icons || {};
    this.error = 0;

    this.progress = 0;
    this.maxProgress = -1;

    // Giving each instance of AddonInstall a reference to the logger.
    this.logger = logger;

    this.name = options.name || null;
    this.type = options.type || null;
    this.version = options.version || null;
    this.isUserRequestedUpdate = options.isUserRequestedUpdate;
    this.installTelemetryInfo = null;

    if (options.telemetryInfo) {
      this.installTelemetryInfo = options.telemetryInfo;
    } else if (this.existingAddon) {
      // Inherits the installTelemetryInfo on updates (so that the source of the original
      // installation telemetry data is being preserved across the extension updates).
      this.installTelemetryInfo = this.existingAddon.installTelemetryInfo;
      this.existingAddon._updateInstall = this;
    }

    this.file = null;
    this.ownsTempFile = null;

    this.addon = null;
    this.state = null;

    // Backing up currently active theme id when a new install flow is about to start
    // (this will then be propagated to the AddonInternal object if the new addon
    // being installed is a static theme and it doesn't have the same addon id of
    // the theme already active, and eventually used to rollback to the previously
    // active theme through the Undo button available in the theme post-install dialog).
    this.initialActiveThemeID = Services.prefs.getCharPref(
      "extensions.activeThemeID",
      lazy.AddonSettings.DEFAULT_THEME_ID
    );

    XPIInstall.installs.add(this);
  }

  /**
   * Called when we are finished with this install and are ready to remove
   * any external references to it.
   */
  _cleanup() {
    XPIInstall.installs.delete(this);
    if (this.addon && this.addon._install) {
      if (this.addon._install === this) {
        this.addon._install = null;
      } else {
        Cu.reportError(new Error("AddonInstall mismatch"));
      }
    }
    if (this.existingAddon && this.existingAddon._updateInstall) {
      if (this.existingAddon._updateInstall === this) {
        this.existingAddon._updateInstall = null;
      } else {
        Cu.reportError(new Error("AddonInstall existingAddon mismatch"));
      }
    }
  }

  /**
   * Starts installation of this add-on from whatever state it is currently at
   * if possible.
   *
   * Note this method is overridden to handle additional state in
   * the subclassses below.
   *
   * @returns {Promise<Addon>}
   * @throws if installation cannot proceed from the current state
   */
  install() {
    switch (this.state) {
      case AddonManager.STATE_DOWNLOADED:
        this.checkPrompt();
        break;
      case AddonManager.STATE_PROMPTS_DONE:
        this.checkForBlockers();
        break;
      case AddonManager.STATE_READY:
        this.startInstall();
        break;
      case AddonManager.STATE_POSTPONED:
        logger.debug(`Postponing install of ${this.addon.id}`);
        break;
      case AddonManager.STATE_DOWNLOADING:
      case AddonManager.STATE_CHECKING_UPDATE:
      case AddonManager.STATE_INSTALLING:
        // Installation is already running
        break;
      default:
        throw new Error("Cannot start installing from this state");
    }
    return this._installPromise;
  }

  continuePostponedInstall() {
    if (this.state !== AddonManager.STATE_POSTPONED) {
      throw new Error("AddonInstall not in postponed state");
    }

    // Force the postponed install to continue.
    logger.info(`${this.addon.id} has resumed a previously postponed upgrade`);
    this.state = AddonManager.STATE_READY;
    this.install();
  }

  /**
   * Called during XPIProvider shutdown so that we can do any necessary
   * pre-shutdown cleanup.
   */
  onShutdown() {
    switch (this.state) {
      case AddonManager.STATE_POSTPONED:
        this.removeTemporaryFile();
        break;
    }
  }

  /**
   * Cancels installation of this add-on.
   *
   * Note this method is overridden to handle additional state in
   * the subclass DownloadAddonInstall.
   *
   * @throws if installation cannot be cancelled from the current state
   */
  cancel() {
    switch (this.state) {
      case AddonManager.STATE_AVAILABLE:
      case AddonManager.STATE_DOWNLOADED:
        logger.debug("Cancelling download of " + this.sourceURI.spec);
        this.state = AddonManager.STATE_CANCELLED;
        this._cleanup();
        this._callInstallListeners("onDownloadCancelled");
        this.removeTemporaryFile();
        break;
      case AddonManager.STATE_POSTPONED:
        logger.debug(`Cancelling postponed install of ${this.addon.id}`);
        this.state = AddonManager.STATE_CANCELLED;
        this._cleanup();
        this._callInstallListeners(
          "onInstallCancelled",
          /* aCancelledByUser */ false
        );
        this.removeTemporaryFile();

        let stagingDir = this.location.installer.getStagingDir();
        let stagedAddon = stagingDir.clone();

        this.unstageInstall(stagedAddon);
        break;
      default:
        throw new Error(
          "Cannot cancel install of " +
            this.sourceURI.spec +
            " from this state (" +
            this.state +
            ")"
        );
    }
  }

  /**
   * Adds an InstallListener for this instance if the listener is not already
   * registered.
   *
   * @param {InstallListener} aListener
   *        The InstallListener to add
   */
  addListener(aListener) {
    if (
      !this.listeners.some(function (i) {
        return i == aListener;
      })
    ) {
      this.listeners.push(aListener);
    }
  }

  /**
   * Removes an InstallListener for this instance if it is registered.
   *
   * @param {InstallListener} aListener
   *        The InstallListener to remove
   */
  removeListener(aListener) {
    this.listeners = this.listeners.filter(function (i) {
      return i != aListener;
    });
  }

  /**
   * Removes the temporary file owned by this AddonInstall if there is one.
   */
  removeTemporaryFile() {
    // Only proceed if this AddonInstall owns its XPI file
    if (!this.ownsTempFile) {
      this.logger.debug(
        `removeTemporaryFile: ${this.sourceURI.spec} does not own temp file`
      );
      return;
    }

    try {
      this.logger.debug(
        `removeTemporaryFile: ${this.sourceURI.spec} removing temp file ` +
          this.file.path
      );
      flushJarCache(this.file);
      this.file.remove(true);
      this.ownsTempFile = false;
    } catch (e) {
      this.logger.warn(
        `Failed to remove temporary file ${this.file.path} for addon ` +
          this.sourceURI.spec,
        e
      );
    }
  }

  _setFileHash(calculatedHash) {
    this.fileHash = {
      algorithm: this.hash ? this.hash.algorithm : DEFAULT_HASH_ALGO,
      data: calculatedHash,
    };

    if (this.hash && calculatedHash != this.hash.data) {
      return false;
    }
    return true;
  }

  /**
   * Updates the addon metadata that has to be propagated across restarts.
   */
  updatePersistedMetadata() {
    this.addon.sourceURI = this.sourceURI.spec;

    if (this.releaseNotesURI) {
      this.addon.releaseNotesURI = this.releaseNotesURI.spec;
    }

    if (this.installTelemetryInfo) {
      this.addon.installTelemetryInfo = this.installTelemetryInfo;
    }
  }

  /**
   * Called after the add-on is a local file and the signature and install
   * manifest can be read.
   *
   * @param {nsIFile} file
   *        The file from which to load the manifest.
   * @returns {Promise<void>}
   */
  async loadManifest(file) {
    let pkg;
    try {
      pkg = Package.get(file);
    } catch (e) {
      return Promise.reject([AddonManager.ERROR_CORRUPT_FILE, e]);
    }

    try {
      try {
        this.addon = await loadManifest(pkg, this.location, this.existingAddon);
        // Set the install.name property to the addon name if it is not set yet,
        // install.name is expected to be set to the addon name and used to
        // fill the addon name in the fluent strings when reporting install
        // errors.
        this.name = this.name ?? this.addon.name;
      } catch (e) {
        return Promise.reject([AddonManager.ERROR_CORRUPT_FILE, e]);
      }

      if (!this.addon.id) {
        let msg = `Cannot find id for addon ${file.path}.`;
        if (Services.prefs.getBoolPref(PREF_XPI_SIGNATURES_DEV_ROOT, false)) {
          msg += ` Preference ${PREF_XPI_SIGNATURES_DEV_ROOT} is set.`;
        }

        return Promise.reject([
          AddonManager.ERROR_CORRUPT_FILE,
          new Error(msg),
        ]);
      }

      if (
        AppConstants.platform == "android" &&
        this.addon.type !== "extension"
      ) {
        return Promise.reject([
          AddonManager.ERROR_UNSUPPORTED_ADDON_TYPE,
          `Unsupported add-on type: ${this.addon.type}`,
        ]);
      }

      if (this.existingAddon) {
        // Check various conditions related to upgrades
        if (this.addon.id != this.existingAddon.id) {
          return Promise.reject([
            AddonManager.ERROR_INCORRECT_ID,
            `Refusing to upgrade addon ${this.existingAddon.id} to different ID ${this.addon.id}`,
          ]);
        }

        if (this.existingAddon.isWebExtension && !this.addon.isWebExtension) {
          // This condition is never met on regular Firefox builds.
          // Remove it along with externalExtensionLoaders (bug 1674799).
          return Promise.reject([
            AddonManager.ERROR_UNEXPECTED_ADDON_TYPE,
            "WebExtensions may not be updated to other extension types",
          ]);
        }
        if (this.existingAddon.type != this.addon.type) {
          return Promise.reject([
            AddonManager.ERROR_UNEXPECTED_ADDON_TYPE,
            `Refusing to change addon type from ${this.existingAddon.type} to ${this.addon.type}`,
          ]);
        }

        if (this.version !== this.addon.version) {
          return Promise.reject([
            AddonManager.ERROR_UNEXPECTED_ADDON_VERSION,
            `Expected addon version ${this.version} instead of ${this.addon.version}`,
          ]);
        }
      }

      if (XPIExports.XPIDatabase.mustSign(this.addon.type)) {
        if (this.addon.signedState <= AddonManager.SIGNEDSTATE_MISSING) {
          // This add-on isn't properly signed by a signature that chains to the
          // trusted root.
          let state = this.addon.signedState;
          this.addon = null;

          if (state == AddonManager.SIGNEDSTATE_MISSING) {
            return Promise.reject([
              AddonManager.ERROR_SIGNEDSTATE_REQUIRED,
              "signature is required but missing",
            ]);
          }

          return Promise.reject([
            AddonManager.ERROR_CORRUPT_FILE,
            "signature verification failed",
          ]);
        }

        if (
          this.addon.adminInstallOnly &&
          !this.addon.wrapper.isInstalledByEnterprisePolicy
        ) {
          return Promise.reject([
            AddonManager.ERROR_ADMIN_INSTALL_ONLY,
            "This addon can only be installed through Enterprise Policies",
          ]);
        }

        // Restrict install for signed extension only signed with weak signature algorithms, unless the
        // restriction is explicitly disabled through prefs or enterprise policies.
        if (
          !XPIInstall.isWeakSignatureInstallAllowed() &&
          this.addon.signedDate &&
          !hasStrongSignature(this.addon)
        ) {
          const addonAllowedByPolicies =
            Services.policies?.getExtensionSettings(
              this.addon.id
            )?.temporarily_allow_weak_signatures;

          const globallyAllowedByPolicies =
            Services.policies?.getExtensionSettings(
              "*"
            )?.temporarily_allow_weak_signatures;

          const allowedByPolicies =
            (globallyAllowedByPolicies &&
              (addonAllowedByPolicies || addonAllowedByPolicies == null)) ||
            addonAllowedByPolicies;

          if (
            !allowedByPolicies &&
            (!this.existingAddon || hasStrongSignature(this.existingAddon))
          ) {
            // Reject if it is a new install or installing over an existing addon including
            // strong cryptographic signatures.
            return Promise.reject([
              AddonManager.ERROR_CORRUPT_FILE,
              "install rejected due to the package not including a strong cryptographic signature",
            ]);
          }

          // Still allow installs using weak signatures to install if either:
          // - it is explicitly allowed through Enterprise Policies Settings
          // - or there is an existing addon with a weak signature.
          logger.warn(
            allowedByPolicies
              ? `Allow weak signature install for ${this.addon.id} XPI due to Enterprise Policies`
              : `Allow weak signature install over existing "${this.existingAddon.id}" XPI`
          );
        }
      }
    } finally {
      pkg.close();
    }

    this.updatePersistedMetadata();

    this.addon._install = this;
    this.name = this.addon.selectedLocale.name;
    this.type = this.addon.type;
    this.version = this.addon.version;

    // Setting the iconURL to something inside the XPI locks the XPI and
    // makes it impossible to delete on Windows.

    // Try to load from the existing cache first
    let repoAddon = await lazy.AddonRepository.getCachedAddonByID(
      this.addon.id
    );

    // It wasn't there so try to re-download it
    if (!repoAddon) {
      try {
        [repoAddon] = await lazy.AddonRepository.cacheAddons([this.addon.id]);
      } catch (err) {
        logger.debug(
          `Error getting metadata for ${this.addon.id}: ${err.message}`
        );
      }
    }

    this.addon._repositoryAddon = repoAddon;
    this.name = this.name || this.addon._repositoryAddon.name;
    this.addon.appDisabled = !XPIExports.XPIDatabase.isUsableAddon(this.addon);
    return undefined;
  }

  getIcon(desiredSize = 64) {
    if (!this.addon.icons || !this.file) {
      return null;
    }

    let { icon } = lazy.IconDetails.getPreferredIcon(
      this.addon.icons,
      null,
      desiredSize
    );
    if (icon.startsWith("chrome://")) {
      return icon;
    }
    return getJarURI(this.file, icon).spec;
  }

  /**
   * This method should be called when the XPI is ready to be installed,
   * i.e., when a download finishes or when a local file has been verified.
   * It should only be called from install() when the install is in
   * STATE_DOWNLOADED (which actually means that the file is available
   * and has been verified).
   */
  checkPrompt() {
    (async () => {
      if (this.promptHandler) {
        let info = {
          existingAddon: this.existingAddon ? this.existingAddon.wrapper : null,
          addon: this.addon.wrapper,
          icon: this.getIcon(),
          // Used in AMTelemetry to detect the install flow related to this prompt.
          install: this.wrapper,
        };

        try {
          await this.promptHandler(info);
        } catch (err) {
          if (this.error < 0) {
            logger.info(`Install of ${this.addon.id} failed ${this.error}`);
            this.state = AddonManager.STATE_INSTALL_FAILED;
            this._cleanup();
            // In some cases onOperationCancelled is called during failures
            // to install/uninstall/enable/disable addons.  We may need to
            // do that here in the future.
            this._callInstallListeners("onInstallFailed");
            this.removeTemporaryFile();
          } else {
            logger.info(`Install of ${this.addon.id} cancelled by user`);
            this.state = AddonManager.STATE_CANCELLED;
            this._cleanup();
            this._callInstallListeners(
              "onInstallCancelled",
              /* aCancelledByUser */ true
            );
          }
          return;
        }
      }
      this.state = AddonManager.STATE_PROMPTS_DONE;
      this.install();
    })();
  }

  /**
   * This method should be called when we have the XPI and any needed
   * permissions prompts have been completed.  If there are any upgrade
   * listeners, they are invoked and the install moves into STATE_POSTPONED.
   * Otherwise, the install moves into STATE_INSTALLING
   */
  checkForBlockers() {
    // If an upgrade listener is registered for this add-on, pass control
    // over the upgrade to the add-on.
    if (AddonManagerPrivate.hasUpgradeListener(this.addon.id)) {
      logger.info(
        `add-on ${this.addon.id} has an upgrade listener, postponing upgrade until restart`
      );
      let resumeFn = () => {
        this.continuePostponedInstall();
      };
      this.postpone(resumeFn);
      return;
    }

    this.state = AddonManager.STATE_READY;
    this.install();
  }

  /**
   * Installs the add-on into the install location.
   */
  async startInstall() {
    this.state = AddonManager.STATE_INSTALLING;
    if (!this._callInstallListeners("onInstallStarted")) {
      this.state = AddonManager.STATE_DOWNLOADED;
      this.removeTemporaryFile();
      this._cleanup();
      this._callInstallListeners(
        "onInstallCancelled",
        /* aCancelledByUser */ false
      );
      return;
    }

    // Reinstall existing user-disabled addon (of the same installed version).
    // If addon is marked to be uninstalled - don't reinstall it.
    if (
      this.existingAddon &&
      this.existingAddon.location === this.location &&
      this.existingAddon.version === this.addon.version &&
      this.existingAddon.userDisabled &&
      !this.existingAddon.pendingUninstall
    ) {
      await XPIExports.XPIDatabase.updateAddonDisabledState(
        this.existingAddon,
        {
          userDisabled: false,
        }
      );
      this.state = AddonManager.STATE_INSTALLED;
      this._callInstallListeners("onInstallEnded", this.existingAddon.wrapper);
      this._cleanup();
      return;
    }

    let isSameLocation = this.existingAddon?.location == this.location;
    let willActivate =
      isSameLocation ||
      !this.existingAddon ||
      this.location.hasPrecedence(this.existingAddon.location);

    logger.debug(
      "Starting install of " + this.addon.id + " from " + this.sourceURI.spec
    );

    if (
      this.addon.type === "theme" &&
      this.addon.id !== this.initialActiveThemeID
    ) {
      // Propagate the theme ID that was active when a new theme install flow has
      // started, in case we need to restore it (on user clicking Undo in the post-install
      // theme dialog).
      this.addon.previousActiveThemeID = this.initialActiveThemeID;
    }

    AddonManagerPrivate.callAddonListeners(
      "onInstalling",
      this.addon.wrapper,
      false
    );

    let stagedAddon = this.location.installer.getStagingDir();

    try {
      await this.location.installer.requestStagingDir();

      // remove any previously staged files
      await this.unstageInstall(stagedAddon);

      stagedAddon.append(`${this.addon.id}.xpi`);

      await this.stageInstall(false, stagedAddon, isSameLocation);

      this._cleanup();

      let install = async () => {
        // Mark this instance of the addon as inactive if it is being
        // superseded by an addon in a different location.
        if (
          willActivate &&
          this.existingAddon &&
          this.existingAddon.active &&
          !isSameLocation
        ) {
          XPIExports.XPIDatabase.updateAddonActive(this.existingAddon, false);
        }

        // Install the new add-on into its final location
        let file = await this.location.installer.installAddon({
          id: this.addon.id,
          source: stagedAddon,
        });

        // Update the metadata in the database
        this.addon.sourceBundle = file;
        // If this addon will be the active addon, make it visible.
        this.addon.visible = willActivate;

        if (isSameLocation) {
          this.addon = XPIExports.XPIDatabase.updateAddonMetadata(
            this.existingAddon,
            this.addon,
            file.path
          );
          let state = this.location.get(this.addon.id);
          if (state) {
            state.syncWithDB(this.addon, true);
          } else {
            logger.warn(
              "Unexpected missing XPI state for add-on ${id}",
              this.addon
            );
          }
        } else {
          this.addon.active = this.addon.visible && !this.addon.disabled;
          this.addon = XPIExports.XPIDatabase.addToDatabase(
            this.addon,
            file.path
          );
          XPIExports.XPIInternal.XPIStates.addAddon(this.addon);
          this.addon.installDate = this.addon.updateDate;
          XPIExports.XPIDatabase.saveChanges();
        }
        XPIExports.XPIInternal.XPIStates.save();

        AddonManagerPrivate.callAddonListeners(
          "onInstalled",
          this.addon.wrapper
        );

        logger.debug(`Install of ${this.sourceURI.spec} completed.`);
        this.state = AddonManager.STATE_INSTALLED;
        this._callInstallListeners("onInstallEnded", this.addon.wrapper);

        XPIExports.XPIDatabase.recordAddonTelemetry(this.addon);

        // Notify providers that a new theme has been enabled.
        if (this.addon.type === "theme" && this.addon.active) {
          AddonManagerPrivate.notifyAddonChanged(
            this.addon.id,
            this.addon.type
          );
        }
      };

      this._startupPromise = (async () => {
        if (!willActivate) {
          await install();
        } else if (this.existingAddon) {
          await XPIExports.XPIInternal.BootstrapScope.get(
            this.existingAddon
          ).update(this.addon, !this.addon.disabled, install);

          if (this.addon.disabled) {
            flushJarCache(this.file);
          }
        } else {
          await install();
          await XPIExports.XPIInternal.BootstrapScope.get(this.addon).install(
            undefined,
            true
          );
        }
      })();

      await this._startupPromise;
    } catch (e) {
      logger.warn(
        `Failed to install ${this.file.path} from ${this.sourceURI.spec} to ${stagedAddon.path}`,
        e
      );

      if (stagedAddon.exists()) {
        recursiveRemove(stagedAddon);
      }
      this.state = AddonManager.STATE_INSTALL_FAILED;
      this.error = AddonManager.ERROR_FILE_ACCESS;
      this._cleanup();
      AddonManagerPrivate.callAddonListeners(
        "onOperationCancelled",
        this.addon.wrapper
      );
      this._callInstallListeners("onInstallFailed");
    } finally {
      this.removeTemporaryFile();
      this.location.installer.releaseStagingDir();
    }
  }

  /**
   * Stages an add-on for install.
   *
   * @param {boolean} restartRequired
   *        If true, the final installation will be deferred until the
   *        next app startup.
   * @param {nsIFile} stagedAddon
   *        The file where the add-on should be staged.
   * @param {boolean} isSameLocation
   *        True if this installation is an upgrade for an existing
   *        add-on in the same location.
   * @throws if the file cannot be staged.
   */
  async stageInstall(restartRequired, stagedAddon, isSameLocation) {
    logger.debug(`Addon ${this.addon.id} will be installed as a packed xpi`);
    stagedAddon.leafName = `${this.addon.id}.xpi`;

    try {
      await IOUtils.copy(this.file.path, stagedAddon.path);

      let calculatedHash = getHashForFile(stagedAddon, this.fileHash.algorithm);
      if (calculatedHash != this.fileHash.data) {
        logger.warn(
          `Staged file hash (${calculatedHash}) did not match initial hash (${this.fileHash.data})`
        );
        throw new Error("Refusing to stage add-on because it has been damaged");
      }
    } catch (e) {
      await IOUtils.remove(stagedAddon.path, { ignoreAbsent: true });
      throw e;
    }

    if (restartRequired) {
      // Point the add-on to its extracted files as the xpi may get deleted
      this.addon.sourceBundle = stagedAddon;

      logger.debug(
        `Staged install of ${this.addon.id} from ${this.sourceURI.spec} ready; waiting for restart.`
      );
      if (isSameLocation) {
        delete this.existingAddon.pendingUpgrade;
        this.existingAddon.pendingUpgrade = this.addon;
      }
    }

    if (this.state === AddonManager.STATE_POSTPONED) {
      // Cache the AddonInternal as it may have updated compatibility info. We
      // do that unconditionally in case the staged install isn't finalized in
      // the same session. That way, on the next app startup, the add-on will
      // be installed.
      this.location.stageAddon(this.addon.id, this.addon.toJSON());
    }
  }

  /**
   * Removes any previously staged upgrade.
   *
   * @param {nsIFile} stagingDir
   *        The staging directory from which to unstage the install.
   */
  async unstageInstall(stagingDir) {
    this.location.unstageAddon(this.addon.id);

    await removeAsync(getFile(this.addon.id, stagingDir));

    await removeAsync(getFile(`${this.addon.id}.xpi`, stagingDir));
  }

  /**
   * Postone a pending update, until restart or until the add-on resumes.
   *
   * @param {function} resumeFn
   *        A function for the add-on to run when resuming.
   * @param {boolean} requiresRestart
   *        Whether this add-on requires restart.
   */
  async postpone(resumeFn, requiresRestart = true) {
    this.state = AddonManager.STATE_POSTPONED;

    let stagingDir = this.location.installer.getStagingDir();

    try {
      await this.location.installer.requestStagingDir();
      await this.unstageInstall(stagingDir);

      let stagedAddon = getFile(`${this.addon.id}.xpi`, stagingDir);

      await this.stageInstall(requiresRestart, stagedAddon, true);
    } catch (e) {
      logger.warn(`Failed to postpone install of ${this.addon.id}`, e);
      this.state = AddonManager.STATE_INSTALL_FAILED;
      this.error = AddonManager.ERROR_FILE_ACCESS;
      this._cleanup();
      this.removeTemporaryFile();
      this.location.installer.releaseStagingDir();
      this._callInstallListeners("onInstallFailed");
      return;
    }

    this._callInstallListeners("onInstallPostponed");

    // upgrade has been staged for restart, provide a way for it to call the
    // resume function.
    let callback = AddonManagerPrivate.getUpgradeListener(this.addon.id);
    if (callback) {
      callback({
        version: this.version,
        install: () => {
          switch (this.state) {
            case AddonManager.STATE_POSTPONED:
              if (resumeFn) {
                resumeFn();
              }
              break;
            default:
              logger.warn(
                `${this.addon.id} cannot resume postponed upgrade from state (${this.state})`
              );
              break;
          }
        },
      });
    }
    // Release the staging directory lock, but since the staging dir is populated
    // it will not be removed until resumed or installed by restart.
    // See also cleanStagingDir()
    this.location.installer.releaseStagingDir();
  }

  _callInstallListeners(event, ...args) {
    switch (event) {
      case "onDownloadCancelled":
      case "onDownloadFailed":
      case "onInstallCancelled":
      case "onInstallFailed":
        let rej = Promise.reject(new Error(`Install failed: ${event}`));
        rej.catch(() => {});
        this._resolveInstallPromise(rej);
        break;
      case "onInstallEnded":
        this._resolveInstallPromise(
          Promise.resolve(this._startupPromise).then(() => args[0])
        );
        break;
    }
    return AddonManagerPrivate.callInstallListeners(
      event,
      this.listeners,
      this.wrapper,
      ...args
    );
  }
}

var LocalAddonInstall = class extends AddonInstall {
  /**
   * Initialises this install to be an install from a local file.
   */
  async init() {
    this.file = this.sourceURI.QueryInterface(Ci.nsIFileURL).file;

    if (!this.file.exists()) {
      logger.warn("XPI file " + this.file.path + " does not exist");
      this.state = AddonManager.STATE_DOWNLOAD_FAILED;
      this.error = AddonManager.ERROR_NETWORK_FAILURE;
      this._cleanup();
      return;
    }

    this.state = AddonManager.STATE_DOWNLOADED;
    this.progress = this.file.fileSize;
    this.maxProgress = this.file.fileSize;

    let algorithm = this.hash ? this.hash.algorithm : DEFAULT_HASH_ALGO;
    if (this.hash) {
      try {
        CryptoHash(this.hash.algorithm);
      } catch (e) {
        logger.warn(
          "Unknown hash algorithm '" +
            this.hash.algorithm +
            "' for addon " +
            this.sourceURI.spec,
          e
        );
        this.state = AddonManager.STATE_DOWNLOAD_FAILED;
        this.error = AddonManager.ERROR_INCORRECT_HASH;
        this._cleanup();
        return;
      }
    }

    if (!this._setFileHash(getHashForFile(this.file, algorithm))) {
      logger.warn(
        `File hash (${this.fileHash.data}) did not match provided hash (${this.hash.data})`
      );
      this.state = AddonManager.STATE_DOWNLOAD_FAILED;
      this.error = AddonManager.ERROR_INCORRECT_HASH;
      this._cleanup();
      return;
    }

    try {
      await this.loadManifest(this.file);
    } catch ([error, message]) {
      logger.warn("Invalid XPI", message);
      this.state = AddonManager.STATE_DOWNLOAD_FAILED;
      this.error = error;
      this._cleanup();
      this._callInstallListeners("onNewInstall");
      flushJarCache(this.file);
      return;
    }

    let addon = await XPIExports.XPIDatabase.getVisibleAddonForID(
      this.addon.id
    );

    this.existingAddon = addon;
    this.addon.propagateDisabledState(this.existingAddon);
    await this.addon.updateBlocklistState();
    this.addon.updateDate = Date.now();
    this.addon.installDate = addon ? addon.installDate : this.addon.updateDate;

    // Report if blocked add-on becomes unblocked through this install.
    if (
      addon?.blocklistState > nsIBlocklistService.STATE_NOT_BLOCKED &&
      this.addon.blocklistState === nsIBlocklistService.STATE_NOT_BLOCKED
    ) {
      this.addon.recordAddonBlockChangeTelemetry("addon_install");
    }

    if (this.addon.blocklistState === nsIBlocklistService.STATE_BLOCKED) {
      this.error = AddonManager.ERROR_BLOCKLISTED;
    }

    if (this.addon.blocklistState === nsIBlocklistService.STATE_SOFTBLOCKED) {
      // We show a different error message to the user and so we need a separate
      // error code (translated into the related localized error message from
      // browser-addons.js on Firefox Desktop and from WebExtensionPromptFeature.kt
      // on Firefox for Android).
      this.error = AddonManager.ERROR_SOFT_BLOCKED;
    }

    if (!this.addon.isCompatible) {
      this.state = AddonManager.STATE_CHECKING_UPDATE;

      await new Promise(resolve => {
        new UpdateChecker(
          this.addon,
          {
            onUpdateFinished: (aAddon, aError) => {
              this.state = AddonManager.STATE_DOWNLOADED;
              // If checking for an updated compatibility range fails or the
              // add-on is still incompatible, then set the expected
              // `install.error` to `ERROR_INCOMPATIBLE`.
              if (!this.addon.isCompatible) {
                this.error = AddonManager.ERROR_INCOMPATIBLE;
              }
              if (aError < 0) {
                logger.warn(
                  `UpdateChecker failed to download updates for ${this.addon.id}, error code: ${aError}`
                );
              } else {
                this._callInstallListeners("onNewInstall");
              }
              resolve();
            },
          },
          AddonManager.UPDATE_WHEN_ADDON_INSTALLED
        );
      });
    } else {
      this._callInstallListeners("onNewInstall");
    }
  }

  install() {
    if (this.state == AddonManager.STATE_DOWNLOAD_FAILED) {
      // For a local install, this state means that verification of the
      // file failed (e.g., the hash or signature or manifest contents
      // were invalid).  It doesn't make sense to retry anything in this
      // case but we have callers who don't know if their AddonInstall
      // object is a local file or a download so accommodate them here.
      this._callInstallListeners("onDownloadFailed");
      return this._installPromise;
    }
    return super.install();
  }
};

var DownloadAddonInstall = class extends AddonInstall {
  /**
   * Instantiates a DownloadAddonInstall
   *
   * @param {XPIStateLocation} installLocation
   *        The XPIStateLocation the add-on will be installed into
   * @param {nsIURL} url
   *        The nsIURL to get the add-on from
   * @param {Object} [options = {}]
   *        Additional options for the install
   * @param {string} [options.hash]
   *        An optional hash for the add-on
   * @param {AddonInternal} [options.existingAddon]
   *        The add-on this install will update if known
   * @param {XULElement} [options.browser]
   *        The browser performing the install, used to display
   *        authentication prompts.
   * @param {nsIPrincipal} [options.principal]
   *        The principal to use. If not present, will default to browser.contentPrincipal.
   * @param {string} [options.name]
   *        An optional name for the add-on
   * @param {string} [options.type]
   *        An optional type for the add-on
   * @param {Object} [options.icons]
   *        Optional icons for the add-on
   * @param {string} [options.version]
   *        The expected version for the add-on.
   *        Required for updates, i.e. when existingAddon is set.
   * @param {function(string) : Promise<void>} [options.promptHandler]
   *        A callback to prompt the user before installing.
   * @param {boolean} [options.sendCookies]
   *        Whether cookies should be sent when downloading the add-on.
   */
  constructor(installLocation, url, options = {}) {
    super(installLocation, url, options);

    this.browser = options.browser;
    this.loadingPrincipal =
      options.triggeringPrincipal ||
      (this.browser && this.browser.contentPrincipal) ||
      Services.scriptSecurityManager.getSystemPrincipal();
    this.sendCookies = Boolean(options.sendCookies);

    this.state = AddonManager.STATE_AVAILABLE;

    this.stream = null;
    this.crypto = null;
    this.badCertHandler = null;
    this.restartDownload = false;
    this.downloadStartedAt = null;

    this._callInstallListeners("onNewInstall", this.listeners, this.wrapper);
  }

  install() {
    switch (this.state) {
      case AddonManager.STATE_AVAILABLE:
        this.startDownload();
        break;
      case AddonManager.STATE_DOWNLOAD_FAILED:
      case AddonManager.STATE_INSTALL_FAILED:
      case AddonManager.STATE_CANCELLED:
        this.removeTemporaryFile();
        this.state = AddonManager.STATE_AVAILABLE;
        this.error = 0;
        this.progress = 0;
        this.maxProgress = -1;
        this.hash = this.originalHash;
        this.fileHash = null;
        this.startDownload();
        break;
      default:
        return super.install();
    }
    return this._installPromise;
  }

  cancel() {
    // If we're done downloading the file but still processing it we cannot
    // cancel the installation. We just call the base class which will handle
    // the request by throwing an error.
    if (this.channel && this.state == AddonManager.STATE_DOWNLOADING) {
      logger.debug("Cancelling download of " + this.sourceURI.spec);
      this.channel.cancel(Cr.NS_BINDING_ABORTED);
    } else {
      super.cancel();
    }
  }

  observe() {
    // Network is going offline
    this.cancel();
  }

  /**
   * Starts downloading the add-on's XPI file.
   */
  startDownload() {
    this.downloadStartedAt = Cu.now();

    this.state = AddonManager.STATE_DOWNLOADING;
    if (!this._callInstallListeners("onDownloadStarted")) {
      logger.debug(
        "onDownloadStarted listeners cancelled installation of addon " +
          this.sourceURI.spec
      );
      this.state = AddonManager.STATE_CANCELLED;
      this._cleanup();
      this._callInstallListeners("onDownloadCancelled");
      return;
    }

    // If a listener changed our state then do not proceed with the download
    if (this.state != AddonManager.STATE_DOWNLOADING) {
      return;
    }

    if (this.channel) {
      // A previous download attempt hasn't finished cleaning up yet, signal
      // that it should restart when complete
      logger.debug("Waiting for previous download to complete");
      this.restartDownload = true;
      return;
    }

    this.openChannel();
  }

  openChannel() {
    this.restartDownload = false;

    try {
      this.file = getTemporaryFile();
      this.ownsTempFile = true;
      this.stream = new FileOutputStream(
        this.file,
        lazy.FileUtils.MODE_WRONLY |
          lazy.FileUtils.MODE_CREATE |
          lazy.FileUtils.MODE_TRUNCATE,
        lazy.FileUtils.PERMS_FILE,
        0
      );
    } catch (e) {
      logger.warn(
        "Failed to start download for addon " + this.sourceURI.spec,
        e
      );
      this.state = AddonManager.STATE_DOWNLOAD_FAILED;
      this.error = AddonManager.ERROR_FILE_ACCESS;
      this._cleanup();
      this._callInstallListeners("onDownloadFailed");
      return;
    }

    let listener = Cc[
      "@mozilla.org/network/stream-listener-tee;1"
    ].createInstance(Ci.nsIStreamListenerTee);
    listener.init(this, this.stream);
    try {
      this.badCertHandler = new lazy.CertUtils.BadCertHandler(
        !lazy.AddonSettings.INSTALL_REQUIREBUILTINCERTS
      );

      this.channel = lazy.NetUtil.newChannel({
        uri: this.sourceURI,
        securityFlags:
          Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT,
        contentPolicyType: Ci.nsIContentPolicy.TYPE_SAVEAS_DOWNLOAD,
        loadingPrincipal: this.loadingPrincipal,
      });
      this.channel.notificationCallbacks = this;
      if (this.sendCookies) {
        if (this.channel instanceof Ci.nsIHttpChannelInternal) {
          this.channel.forceAllowThirdPartyCookie = true;
        }
      } else {
        this.channel.loadFlags |= Ci.nsIRequest.LOAD_ANONYMOUS;
      }
      this.channel.asyncOpen(listener);

      Services.obs.addObserver(this, "network:offline-about-to-go-offline");
    } catch (e) {
      logger.warn(
        "Failed to start download for addon " + this.sourceURI.spec,
        e
      );
      this.state = AddonManager.STATE_DOWNLOAD_FAILED;
      this.error = AddonManager.ERROR_NETWORK_FAILURE;
      this._cleanup();
      this._callInstallListeners("onDownloadFailed");
    }
  }

  /*
   * Update the crypto hasher with the new data and call the progress listeners.
   *
   * @see nsIStreamListener
   */
  onDataAvailable(aRequest, aInputstream, aOffset, aCount) {
    this.crypto.updateFromStream(aInputstream, aCount);
    this.progress += aCount;
    if (!this._callInstallListeners("onDownloadProgress")) {
      // TODO cancel the download and make it available again (bug 553024)
    }
  }

  /*
   * Check the redirect response for a hash of the target XPI and verify that
   * we don't end up on an insecure channel.
   *
   * @see nsIChannelEventSink
   */
  asyncOnChannelRedirect(aOldChannel, aNewChannel, aFlags, aCallback) {
    if (
      !this.hash &&
      aOldChannel.originalURI.schemeIs("https") &&
      aOldChannel instanceof Ci.nsIHttpChannel
    ) {
      try {
        let hashStr = aOldChannel.getResponseHeader("X-Target-Digest");
        let hashSplit = hashStr.toLowerCase().split(":");
        this.hash = {
          algorithm: hashSplit[0],
          data: hashSplit[1],
        };
      } catch (e) {}
    }

    // Verify that we don't end up on an insecure channel if we haven't got a
    // hash to verify with (see bug 537761 for discussion)
    if (!this.hash) {
      this.badCertHandler.asyncOnChannelRedirect(
        aOldChannel,
        aNewChannel,
        aFlags,
        aCallback
      );
    } else {
      aCallback.onRedirectVerifyCallback(Cr.NS_OK);
    }

    this.channel = aNewChannel;
  }

  /*
   * This is the first chance to get at real headers on the channel.
   *
   * @see nsIStreamListener
   */
  onStartRequest(aRequest) {
    if (this.hash) {
      try {
        this.crypto = CryptoHash(this.hash.algorithm);
      } catch (e) {
        logger.warn(
          "Unknown hash algorithm '" +
            this.hash.algorithm +
            "' for addon " +
            this.sourceURI.spec,
          e
        );
        this.state = AddonManager.STATE_DOWNLOAD_FAILED;
        this.error = AddonManager.ERROR_INCORRECT_HASH;
        this._cleanup();
        this._callInstallListeners("onDownloadFailed");
        aRequest.cancel(Cr.NS_BINDING_ABORTED);
        return;
      }
    } else {
      // We always need something to consume data from the inputstream passed
      // to onDataAvailable so just create a dummy cryptohasher to do that.
      this.crypto = CryptoHash(DEFAULT_HASH_ALGO);
    }

    this.progress = 0;
    if (aRequest instanceof Ci.nsIChannel) {
      try {
        this.maxProgress = aRequest.contentLength;
      } catch (e) {}
      logger.debug(
        "Download started for " +
          this.sourceURI.spec +
          " to file " +
          this.file.path
      );
    }
  }

  /*
   * The download is complete.
   *
   * @see nsIStreamListener
   */
  onStopRequest(aRequest, aStatus) {
    this.stream.close();
    this.channel = null;
    this.badCerthandler = null;
    Services.obs.removeObserver(this, "network:offline-about-to-go-offline");

    let crypto = this.crypto;
    this.crypto = null;

    // If the download was cancelled then update the state and send events
    if (aStatus == Cr.NS_BINDING_ABORTED) {
      if (this.state == AddonManager.STATE_DOWNLOADING) {
        logger.debug("Cancelled download of " + this.sourceURI.spec);
        this.state = AddonManager.STATE_CANCELLED;
        this._cleanup();
        this._callInstallListeners("onDownloadCancelled");
        // If a listener restarted the download then there is no need to
        // remove the temporary file
        if (this.state != AddonManager.STATE_CANCELLED) {
          return;
        }
      }

      this.removeTemporaryFile();
      if (this.restartDownload) {
        this.openChannel();
      }
      return;
    }

    logger.debug("Download of " + this.sourceURI.spec + " completed.");

    if (Components.isSuccessCode(aStatus)) {
      if (
        !(aRequest instanceof Ci.nsIHttpChannel) ||
        aRequest.requestSucceeded
      ) {
        if (!this.hash && aRequest instanceof Ci.nsIChannel) {
          try {
            lazy.CertUtils.checkCert(
              aRequest,
              !lazy.AddonSettings.INSTALL_REQUIREBUILTINCERTS
            );
          } catch (e) {
            this.downloadFailed(AddonManager.ERROR_NETWORK_FAILURE, e);
            return;
          }
        }

        if (!this._setFileHash(getHashStringForCrypto(crypto))) {
          this.downloadFailed(
            AddonManager.ERROR_INCORRECT_HASH,
            `Downloaded file hash (${this.fileHash.data}) did not match provided hash (${this.hash.data})`
          );
          return;
        }

        this.loadManifest(this.file).then(
          () => {
            if (this.addon.isCompatible) {
              this.downloadCompleted();
            } else {
              // TODO Should we send some event here (bug 557716)?
              this.state = AddonManager.STATE_CHECKING_UPDATE;
              new UpdateChecker(
                this.addon,
                {
                  onUpdateFinished: () => this.downloadCompleted(),
                },
                AddonManager.UPDATE_WHEN_ADDON_INSTALLED
              );
            }
          },
          ([error, message]) => {
            this.removeTemporaryFile();
            this.downloadFailed(error, message);
          }
        );
      } else if (aRequest instanceof Ci.nsIHttpChannel) {
        this.downloadFailed(
          AddonManager.ERROR_NETWORK_FAILURE,
          aRequest.responseStatus + " " + aRequest.responseStatusText
        );
      } else {
        this.downloadFailed(AddonManager.ERROR_NETWORK_FAILURE, aStatus);
      }
    } else {
      this.downloadFailed(AddonManager.ERROR_NETWORK_FAILURE, aStatus);
    }
  }

  /**
   * Notify listeners that the download failed.
   *
   * @param {string} aReason
   *        Something to log about the failure
   * @param {integer} aError
   *        The error code to pass to the listeners
   */
  downloadFailed(aReason, aError) {
    logger.warn("Download of " + this.sourceURI.spec + " failed", aError);
    this.state = AddonManager.STATE_DOWNLOAD_FAILED;
    this.error = aReason;
    this._cleanup();
    this._callInstallListeners("onDownloadFailed");

    // If the listener hasn't restarted the download then remove any temporary
    // file
    if (this.state == AddonManager.STATE_DOWNLOAD_FAILED) {
      logger.debug(
        "downloadFailed: removing temp file for " + this.sourceURI.spec
      );
      this.removeTemporaryFile();
    } else {
      logger.debug(
        "downloadFailed: listener changed AddonInstall state for " +
          this.sourceURI.spec +
          " to " +
          this.state
      );
    }
  }

  /**
   * Notify listeners that the download completed.
   */
  async downloadCompleted() {
    let wasUpdate = !!this.existingAddon;
    let aAddon = await XPIExports.XPIDatabase.getVisibleAddonForID(
      this.addon.id
    );
    if (aAddon) {
      this.existingAddon = aAddon;
    }

    this.state = AddonManager.STATE_DOWNLOADED;
    this.addon.updateDate = Date.now();

    if (this.existingAddon) {
      this.addon.installDate = this.existingAddon.installDate;
    } else {
      this.addon.installDate = this.addon.updateDate;
    }
    this.addon.propagateDisabledState(this.existingAddon);
    await this.addon.updateBlocklistState();

    // Report if blocked add-on becomes unblocked through this install/update.
    if (
      aAddon?.blocklistState > nsIBlocklistService.STATE_NOT_BLOCKED &&
      this.addon.blocklistState === nsIBlocklistService.STATE_NOT_BLOCKED
    ) {
      this.addon.recordAddonBlockChangeTelemetry(
        wasUpdate ? "addon_update" : "addon_install"
      );
    }

    if (this.addon.blocklistState === nsIBlocklistService.STATE_BLOCKED) {
      this.error = AddonManager.ERROR_BLOCKLISTED;
    } else if (
      this.addon.blocklistState === nsIBlocklistService.STATE_SOFTBLOCKED
    ) {
      // We show a different error message to the user and so we need a separate
      // error code (translated into the related localized error message from
      // browser-addons.js on Firefox Desktop and from WebExtensionPromptFeature.kt
      // on Firefox for Android).
      this.error = AddonManager.ERROR_SOFT_BLOCKED;
    } else if (!this.addon.isCompatible) {
      this.error = AddonManager.ERROR_INCOMPATIBLE;
    }

    if (this._callInstallListeners("onDownloadEnded")) {
      // If a listener changed our state then do not proceed with the install
      if (this.state != AddonManager.STATE_DOWNLOADED) {
        return;
      }

      // proceed with the install state machine.
      this.install();
    }
  }

  getInterface(iid) {
    if (iid.equals(Ci.nsIAuthPrompt2)) {
      let win = null;
      if (this.browser) {
        win = this.browser.contentWindow || this.browser.ownerGlobal;
      }

      let factory = Cc["@mozilla.org/prompter;1"].getService(
        Ci.nsIPromptFactory
      );
      let prompt = factory.getPrompt(win, Ci.nsIAuthPrompt2);

      if (this.browser && prompt instanceof Ci.nsILoginManagerAuthPrompter) {
        prompt.browser = this.browser;
      }

      return prompt;
    } else if (iid.equals(Ci.nsIChannelEventSink)) {
      return this;
    }

    return this.badCertHandler.getInterface(iid);
  }
};

/**
 * Creates a new AddonInstall for an update.
 *
 * @param {function} aCallback
 *        The callback to pass the new AddonInstall to
 * @param {AddonInternal} aAddon
 *        The add-on being updated
 * @param {Object} aUpdate
 *        The metadata about the new version from the update manifest
 * @param {boolean} isUserRequested
 *        An optional boolean, true if the install object is related to a user triggered update.
 */
function createUpdate(aCallback, aAddon, aUpdate, isUserRequested) {
  let url = Services.io.newURI(aUpdate.updateURL);

  (async function () {
    let opts = {
      hash: aUpdate.updateHash,
      existingAddon: aAddon,
      name: aAddon.selectedLocale.name,
      type: aAddon.type,
      icons: aAddon.icons,
      version: aUpdate.version,
      isUserRequestedUpdate: isUserRequested,
    };

    try {
      if (aUpdate.updateInfoURL) {
        opts.releaseNotesURI = Services.io.newURI(
          escapeAddonURI(aAddon, aUpdate.updateInfoURL)
        );
      }
    } catch (e) {
      // If the releaseNotesURI cannot be parsed then just ignore it.
    }

    let install;
    if (url instanceof Ci.nsIFileURL) {
      install = new LocalAddonInstall(aAddon.location, url, opts);
      await install.init();
    } else {
      install = new DownloadAddonInstall(aAddon.location, url, opts);
    }

    aCallback(install);
  })();
}

// Maps instances of AddonInstall to AddonInstallWrapper
const wrapperMap = new WeakMap();
let installFor = wrapper => wrapperMap.get(wrapper);

// Numeric id included in the install telemetry events to correlate multiple events related
// to the same install or update flow.
let nextInstallId = 0;

/**
 * Creates a wrapper for an AddonInstall that only exposes the public API
 *
 * @param {AddonInstall} aInstall
 *        The AddonInstall to create a wrapper for
 */
function AddonInstallWrapper(aInstall) {
  wrapperMap.set(this, aInstall);
  this.installId = ++nextInstallId;
}

AddonInstallWrapper.prototype = {
  get __AddonInstallInternal__() {
    return AppConstants.DEBUG ? installFor(this) : undefined;
  },

  get error() {
    return installFor(this).error;
  },

  set error(err) {
    installFor(this).error = err;
  },

  get type() {
    return installFor(this).type;
  },

  get iconURL() {
    return installFor(this).icons[32];
  },

  get existingAddon() {
    let install = installFor(this);
    return install.existingAddon ? install.existingAddon.wrapper : null;
  },

  get addon() {
    let install = installFor(this);
    return install.addon ? install.addon.wrapper : null;
  },

  get sourceURI() {
    return installFor(this).sourceURI;
  },

  set promptHandler(handler) {
    installFor(this).promptHandler = handler;
  },

  get promptHandler() {
    return installFor(this).promptHandler;
  },

  get installTelemetryInfo() {
    return installFor(this).installTelemetryInfo;
  },

  get isUserRequestedUpdate() {
    return Boolean(installFor(this).isUserRequestedUpdate);
  },

  get downloadStartedAt() {
    return installFor(this).downloadStartedAt;
  },

  get hashedAddonId() {
    const addon = this.addon;

    if (!addon) {
      return null;
    }

    return computeSha256HashAsString(addon.id);
  },

  install() {
    return installFor(this).install();
  },

  postpone(returnFn, requiresRestart) {
    return installFor(this).postpone(returnFn, requiresRestart);
  },

  cancel() {
    installFor(this).cancel();
  },

  continuePostponedInstall() {
    return installFor(this).continuePostponedInstall();
  },

  addListener(listener) {
    installFor(this).addListener(listener);
  },

  removeListener(listener) {
    installFor(this).removeListener(listener);
  },
};

[
  "name",
  "version",
  "icons",
  "releaseNotesURI",
  "file",
  "state",
  "progress",
  "maxProgress",
].forEach(function (aProp) {
  Object.defineProperty(AddonInstallWrapper.prototype, aProp, {
    get() {
      return installFor(this)[aProp];
    },
    enumerable: true,
  });
});

/**
 * Creates a new update checker.
 *
 * @param {AddonInternal} aAddon
 *        The add-on to check for updates
 * @param {UpdateListener} aListener
 *        An UpdateListener to notify of updates
 * @param {integer} aReason
 *        The reason for the update check
 * @param {string} [aAppVersion]
 *        An optional application version to check for updates for
 * @param {string} [aPlatformVersion]
 *        An optional platform version to check for updates for
 * @throws if the aListener or aReason arguments are not valid
 */
var AddonUpdateChecker;

export var UpdateChecker = function (
  aAddon,
  aListener,
  aReason,
  aAppVersion,
  aPlatformVersion
) {
  if (!aListener || !aReason) {
    throw Components.Exception("", Cr.NS_ERROR_INVALID_ARG);
  }

  ({ AddonUpdateChecker } = ChromeUtils.importESModule(
    "resource://gre/modules/addons/AddonUpdateChecker.sys.mjs"
  ));

  this.addon = aAddon;
  aAddon._updateCheck = this;
  XPIInstall.doing(this);
  this.listener = aListener;
  this.appVersion = aAppVersion;
  this.platformVersion = aPlatformVersion;
  this.syncCompatibility =
    aReason == AddonManager.UPDATE_WHEN_NEW_APP_INSTALLED;
  this.isUserRequested = aReason == AddonManager.UPDATE_WHEN_USER_REQUESTED;

  let updateURL = aAddon.updateURL;
  if (!updateURL) {
    if (
      aReason == AddonManager.UPDATE_WHEN_PERIODIC_UPDATE &&
      Services.prefs.getPrefType(PREF_EM_UPDATE_BACKGROUND_URL) ==
        Services.prefs.PREF_STRING
    ) {
      updateURL = Services.prefs.getCharPref(PREF_EM_UPDATE_BACKGROUND_URL);
    } else {
      updateURL = Services.prefs.getCharPref(PREF_EM_UPDATE_URL);
    }
  }

  const UPDATE_TYPE_COMPATIBILITY = 32;
  const UPDATE_TYPE_NEWVERSION = 64;

  aReason |= UPDATE_TYPE_COMPATIBILITY;
  if ("onUpdateAvailable" in this.listener) {
    aReason |= UPDATE_TYPE_NEWVERSION;
  }

  let url = escapeAddonURI(aAddon, updateURL, aReason, aAppVersion);
  this._parser = AddonUpdateChecker.checkForUpdates(aAddon.id, url, this);
};

UpdateChecker.prototype = {
  addon: null,
  listener: null,
  appVersion: null,
  platformVersion: null,
  syncCompatibility: null,

  /**
   * Calls a method on the listener passing any number of arguments and
   * consuming any exceptions.
   *
   * @param {string} aMethod
   *        The method to call on the listener
   * @param {any[]} aArgs
   *        Additional arguments to pass to the listener.
   */
  callListener(aMethod, ...aArgs) {
    if (!(aMethod in this.listener)) {
      return;
    }

    try {
      this.listener[aMethod].apply(this.listener, aArgs);
    } catch (e) {
      logger.warn("Exception calling UpdateListener method " + aMethod, e);
    }
  },

  /**
   * Called when AddonUpdateChecker completes the update check
   *
   * @param {object[]} aUpdates
   *        The list of update details for the add-on
   */
  async onUpdateCheckComplete(aUpdates) {
    XPIInstall.done(this.addon._updateCheck);
    this.addon._updateCheck = null;
    let AUC = AddonUpdateChecker;
    let ignoreMaxVersion = false;
    // Ignore strict compatibility for dictionaries by default.
    let ignoreStrictCompat = this.addon.type == "dictionary";
    if (!AddonManager.checkCompatibility) {
      ignoreMaxVersion = true;
      ignoreStrictCompat = true;
    } else if (
      !AddonManager.strictCompatibility &&
      !this.addon.strictCompatibility
    ) {
      ignoreMaxVersion = true;
    }

    // Always apply any compatibility update for the current version
    let compatUpdate = AUC.getCompatibilityUpdate(
      aUpdates,
      this.addon.version,
      this.syncCompatibility,
      null,
      null,
      ignoreMaxVersion,
      ignoreStrictCompat
    );
    // Apply the compatibility update to the database
    if (compatUpdate) {
      this.addon.applyCompatibilityUpdate(compatUpdate, this.syncCompatibility);
    }

    // If the request is for an application or platform version that is
    // different to the current application or platform version then look for a
    // compatibility update for those versions.
    if (
      (this.appVersion &&
        Services.vc.compare(this.appVersion, Services.appinfo.version) != 0) ||
      (this.platformVersion &&
        Services.vc.compare(
          this.platformVersion,
          Services.appinfo.platformVersion
        ) != 0)
    ) {
      compatUpdate = AUC.getCompatibilityUpdate(
        aUpdates,
        this.addon.version,
        false,
        this.appVersion,
        this.platformVersion,
        ignoreMaxVersion,
        ignoreStrictCompat
      );
    }

    if (compatUpdate) {
      this.callListener("onCompatibilityUpdateAvailable", this.addon.wrapper);
    } else {
      this.callListener("onNoCompatibilityUpdateAvailable", this.addon.wrapper);
    }

    function sendUpdateAvailableMessages(aSelf, aInstall) {
      if (aInstall) {
        aSelf.callListener(
          "onUpdateAvailable",
          aSelf.addon.wrapper,
          aInstall.wrapper
        );
      } else {
        aSelf.callListener("onNoUpdateAvailable", aSelf.addon.wrapper);
      }
      aSelf.callListener(
        "onUpdateFinished",
        aSelf.addon.wrapper,
        AddonManager.UPDATE_STATUS_NO_ERROR
      );
    }

    let update = await AUC.getNewestCompatibleUpdate(
      aUpdates,
      this.addon,
      this.appVersion,
      this.platformVersion,
      ignoreMaxVersion,
      ignoreStrictCompat
    );

    if (update && !this.addon.location.locked) {
      for (let currentInstall of XPIInstall.installs) {
        // Skip installs that don't match the available update
        if (
          currentInstall.existingAddon != this.addon ||
          currentInstall.version != update.version
        ) {
          continue;
        }

        // If the existing install has not yet started downloading then send an
        // available update notification. If it is already downloading then
        // don't send any available update notification
        if (currentInstall.state == AddonManager.STATE_AVAILABLE) {
          logger.debug("Found an existing AddonInstall for " + this.addon.id);
          sendUpdateAvailableMessages(this, currentInstall);
        } else {
          sendUpdateAvailableMessages(this, null);
        }
        return;
      }

      createUpdate(
        aInstall => {
          sendUpdateAvailableMessages(this, aInstall);
        },
        this.addon,
        update,
        this.isUserRequested
      );
    } else {
      sendUpdateAvailableMessages(this, null);
    }
  },

  /**
   * Called when AddonUpdateChecker fails the update check
   *
   * @param {any} aError
   *        An error status
   */
  onUpdateCheckError(aError) {
    XPIInstall.done(this.addon._updateCheck);
    this.addon._updateCheck = null;
    this.callListener("onNoCompatibilityUpdateAvailable", this.addon.wrapper);
    this.callListener("onNoUpdateAvailable", this.addon.wrapper);
    this.callListener("onUpdateFinished", this.addon.wrapper, aError);
  },

  /**
   * Called to cancel an in-progress update check
   */
  cancel() {
    let parser = this._parser;
    if (parser) {
      this._parser = null;
      // This will call back to onUpdateCheckError with a CANCELLED error
      parser.cancel();
    }
  },
};

/**
 * Creates a new AddonInstall to install an add-on from a local file.
 *
 * @param {nsIFile} file
 *        The file to install
 * @param {XPIStateLocation} location
 *        The location to install to
 * @param {Object?} [telemetryInfo]
 *        An optional object which provides details about the installation source
 *        included in the addon manager telemetry events.
 * @returns {Promise<AddonInstall>}
 *        A Promise that resolves with the new install object.
 */
function createLocalInstall(file, location, telemetryInfo) {
  if (!location) {
    location = XPIExports.XPIInternal.XPIStates.getLocation(
      XPIExports.XPIInternal.KEY_APP_PROFILE
    );
  }
  let url = Services.io.newFileURI(file);

  try {
    let install = new LocalAddonInstall(location, url, { telemetryInfo });
    return install.init().then(() => install);
  } catch (e) {
    logger.error("Error creating install", e);
    return Promise.resolve(null);
  }
}

/**
 * Uninstall an addon from a location.  This allows removing non-visible
 * addons, such as system addon upgrades, when a higher precedence addon
 * is installed.
 *
 * @param {string} addonID
 *        ID of the addon being removed.
 * @param {XPIStateLocation} location
 *        The location to remove the addon from.
 */
async function uninstallAddonFromLocation(addonID, location) {
  let existing = await XPIExports.XPIDatabase.getAddonInLocation(
    addonID,
    location.name
  );
  if (!existing) {
    return;
  }
  if (existing.active) {
    let a = await AddonManager.getAddonByID(addonID);
    if (a) {
      await a.uninstall();
    }
  } else {
    XPIExports.XPIDatabase.removeAddonMetadata(existing);
    location.removeAddon(addonID);
    XPIExports.XPIInternal.XPIStates.save();
    AddonManagerPrivate.callAddonListeners("onUninstalled", existing);
  }
}

class DirectoryInstaller {
  constructor(location) {
    this.location = location;

    this._stagingDirLock = 0;
    this._stagingDirPromise = null;
  }

  get name() {
    return this.location.name;
  }

  get dir() {
    return this.location.dir;
  }
  set dir(val) {
    this.location.dir = val;
    this.location.path = val.path;
  }

  /**
   * Gets the staging directory to put add-ons that are pending install and
   * uninstall into.
   *
   * @returns {nsIFile}
   */
  getStagingDir() {
    return getFile(XPIExports.XPIInternal.DIR_STAGE, this.dir);
  }

  requestStagingDir() {
    this._stagingDirLock++;

    if (this._stagingDirPromise) {
      return this._stagingDirPromise;
    }

    let stagepath = PathUtils.join(
      this.dir.path,
      XPIExports.XPIInternal.DIR_STAGE
    );
    return (this._stagingDirPromise = IOUtils.makeDirectory(stagepath, {
      createAncestors: true,
      ignoreExisting: true,
    }).catch(e => {
      logger.error("Failed to create staging directory", e);
      throw e;
    }));
  }

  releaseStagingDir() {
    this._stagingDirLock--;

    if (this._stagingDirLock == 0) {
      this._stagingDirPromise = null;
      this.cleanStagingDir();
    }

    return Promise.resolve();
  }

  /**
   * Removes the specified files or directories in the staging directory and
   * then if the staging directory is empty attempts to remove it.
   *
   * @param {string[]} [aLeafNames = []]
   *        An array of file or directory to remove from the directory, the
   *        array may be empty
   */
  cleanStagingDir(aLeafNames = []) {
    let dir = this.getStagingDir();

    // SystemAddonInstaller getStatingDir may return null if there isn't
    // any addon set directory returned by SystemAddonInstaller._loadAddonSet.
    if (!dir) {
      return;
    }

    for (let name of aLeafNames) {
      let file = getFile(name, dir);
      recursiveRemove(file);
    }

    if (this._stagingDirLock > 0) {
      return;
    }

    // eslint-disable-next-line no-unused-vars
    for (let file of XPIExports.XPIInternal.iterDirectory(dir)) {
      return;
    }

    try {
      setFilePermissions(dir, lazy.FileUtils.PERMS_DIRECTORY);
      dir.remove(false);
    } catch (e) {
      logger.warn("Failed to remove staging dir", e);
      // Failing to remove the staging directory is ignorable
    }
  }

  /**
   * Returns a directory that is normally on the same filesystem as the rest of
   * the install location and can be used for temporarily storing files during
   * safe move operations. Calling this method will delete the existing trash
   * directory and its contents.
   *
   * @returns {nsIFile}
   */
  getTrashDir() {
    let trashDir = getFile(XPIExports.XPIInternal.DIR_TRASH, this.dir);
    let trashDirExists = trashDir.exists();
    try {
      if (trashDirExists) {
        recursiveRemove(trashDir);
      }
      trashDirExists = false;
    } catch (e) {
      logger.warn("Failed to remove trash directory", e);
    }
    if (!trashDirExists) {
      trashDir.create(
        Ci.nsIFile.DIRECTORY_TYPE,
        lazy.FileUtils.PERMS_DIRECTORY
      );
    }

    return trashDir;
  }

  /**
   * Installs an add-on into the install location.
   *
   * @param {Object} options
   *        Installation options.
   * @param {string} options.id
   *        The ID of the add-on to install
   * @param {nsIFile} options.source
   *        The source nsIFile to install from
   * @param {string} options.action
   *        What to we do with the given source file:
   *          "move"
   *          Default action, the source files will be moved to the new
   *          location,
   *          "copy"
   *          The source files will be copied,
   *          "proxy"
   *          A "proxy file" is going to refer to the source file path
   * @returns {nsIFile}
   *        An nsIFile indicating where the add-on was installed to
   */
  installAddon({ id, source, action = "move" }) {
    let trashDir = this.getTrashDir();

    let transaction = new SafeInstallOperation();

    let moveOldAddon = aId => {
      let file = getFile(aId, this.dir);
      if (file.exists()) {
        transaction.moveUnder(file, trashDir);
      }

      file = getFile(`${aId}.xpi`, this.dir);
      if (file.exists()) {
        flushJarCache(file);
        transaction.moveUnder(file, trashDir);
      }
    };

    // If any of these operations fails the finally block will clean up the
    // temporary directory
    try {
      moveOldAddon(id);
      if (action == "copy") {
        transaction.copy(source, this.dir);
      } else if (action == "move") {
        flushJarCache(source);
        transaction.moveUnder(source, this.dir);
      }
      // Do nothing for the proxy file as we sideload an addon permanently
    } finally {
      // It isn't ideal if this cleanup fails but it isn't worth rolling back
      // the install because of it.
      try {
        recursiveRemove(trashDir);
      } catch (e) {
        logger.warn(
          `Failed to remove trash directory when installing ${id}`,
          e
        );
      }
    }

    let newFile = this.dir.clone();

    if (action == "proxy") {
      // When permanently installing sideloaded addon, we just put a proxy file
      // referring to the addon sources
      newFile.append(id);

      writeStringToFile(newFile, source.path);
    } else {
      newFile.append(source.leafName);
    }

    try {
      newFile.lastModifiedTime = Date.now();
    } catch (e) {
      logger.warn(`failed to set lastModifiedTime on ${newFile.path}`, e);
    }

    return newFile;
  }

  /**
   * Uninstalls an add-on from this location.
   *
   * @param {string} aId
   *        The ID of the add-on to uninstall
   * @throws if the ID does not match any of the add-ons installed
   */
  uninstallAddon(aId) {
    let file = getFile(aId, this.dir);
    if (!file.exists()) {
      file.leafName += ".xpi";
    }

    if (!file.exists()) {
      logger.warn(
        `Attempted to remove ${aId} from ${this.name} but it was already gone`
      );
      this.location.delete(aId);
      return;
    }

    if (file.leafName != aId) {
      logger.debug(
        `uninstallAddon: flushing jar cache ${file.path} for addon ${aId}`
      );
      flushJarCache(file);
    }

    // In case this is a foreignInstall we do not want to remove the file if
    // the location is locked.
    if (!this.location.locked) {
      let trashDir = this.getTrashDir();
      let transaction = new SafeInstallOperation();

      try {
        transaction.moveUnder(file, trashDir);
      } finally {
        // It isn't ideal if this cleanup fails, but it is probably better than
        // rolling back the uninstall at this point
        try {
          recursiveRemove(trashDir);
        } catch (e) {
          logger.warn(
            `Failed to remove trash directory when uninstalling ${aId}`,
            e
          );
        }
      }
    }

    this.location.removeAddon(aId);
  }
}

class SystemAddonInstaller extends DirectoryInstaller {
  constructor(location) {
    super(location);

    this._baseDir = location._baseDir;
    this._nextDir = null;
  }

  get _addonSet() {
    return this.location._addonSet;
  }
  set _addonSet(val) {
    this.location._addonSet = val;
  }

  /**
   * Saves the current set of system add-ons
   *
   * @param {Object} aAddonSet - object containing schema, directory and set
   *                 of system add-on IDs and versions.
   */
  static _saveAddonSet(aAddonSet) {
    Services.prefs.setStringPref(
      XPIExports.XPIInternal.PREF_SYSTEM_ADDON_SET,
      JSON.stringify(aAddonSet)
    );
  }

  static _loadAddonSet() {
    return XPIExports.XPIInternal.SystemAddonLocation._loadAddonSet();
  }

  /**
   * Gets the staging directory to put add-ons that are pending install and
   * uninstall into.
   *
   * @returns {nsIFile}
   *        Staging directory for system add-on upgrades.
   */
  getStagingDir() {
    this._addonSet = SystemAddonInstaller._loadAddonSet();
    let dir = null;
    if (this._addonSet.directory) {
      this.dir = getFile(this._addonSet.directory, this._baseDir);
      dir = getFile(XPIExports.XPIInternal.DIR_STAGE, this.dir);
    } else {
      logger.info("SystemAddonInstaller directory is missing");
    }

    return dir;
  }

  requestStagingDir() {
    this._addonSet = SystemAddonInstaller._loadAddonSet();
    if (this._addonSet.directory) {
      this.dir = getFile(this._addonSet.directory, this._baseDir);
    }
    return super.requestStagingDir();
  }

  isValidAddon(aAddon) {
    if (aAddon.appDisabled) {
      logger.warn(
        `System add-on ${aAddon.id} isn't compatible with the application.`
      );
      return false;
    }

    return true;
  }

  /**
   * Tests whether the loaded add-on information matches what is expected.
   *
   * @param {Map<string, AddonInternal>} aAddons
   *        The set of add-ons to check.
   * @returns {boolean}
   *        True if all of the given add-ons are valid.
   */
  isValid(aAddons) {
    for (let id of Object.keys(this._addonSet.addons)) {
      if (!aAddons.has(id)) {
        logger.warn(
          `Expected add-on ${id} is missing from the system add-on location.`
        );
        return false;
      }

      let addon = aAddons.get(id);
      if (addon.version != this._addonSet.addons[id].version) {
        logger.warn(
          `Expected system add-on ${id} to be version ${this._addonSet.addons[id].version} but was ${addon.version}.`
        );
        return false;
      }

      if (!this.isValidAddon(addon)) {
        return false;
      }
    }

    return true;
  }

  /**
   * Resets the add-on set so on the next startup the default set will be used.
   */
  async resetAddonSet() {
    logger.info("Removing all system add-on upgrades.");

    // remove everything from the pref first, if uninstall
    // fails then at least they will not be re-activated on
    // next restart.
    let addonSet = this._addonSet;
    this._addonSet = { schema: 1, addons: {} };
    SystemAddonInstaller._saveAddonSet(this._addonSet);

    // If this is running at app startup, the pref being cleared
    // will cause later stages of startup to notice that the
    // old updates are now gone.
    //
    // Updates will only be explicitly uninstalled if they are
    // removed restartlessly, for instance if they are no longer
    // part of the latest update set.
    if (addonSet) {
      for (let addonID of Object.keys(addonSet.addons)) {
        await uninstallAddonFromLocation(addonID, this.location);
      }
    }
  }

  /**
   * Removes any directories not currently in use or pending use after a
   * restart. Any errors that happen here don't really matter as we'll attempt
   * to cleanup again next time.
   */
  async cleanDirectories() {
    try {
      let children = await IOUtils.getChildren(this._baseDir.path, {
        ignoreAbsent: true,
      });
      for (let path of children) {
        // Skip the directory currently in use
        if (this.dir && this.dir.path == path) {
          continue;
        }

        // Skip the next directory
        if (this._nextDir && this._nextDir.path == path) {
          continue;
        }

        await IOUtils.remove(path, {
          ignoreAbsent: true,
          recursive: true,
        });
      }
    } catch (e) {
      logger.error("Failed to clean updated system add-ons directories.", e);
    }
  }

  /**
   * Installs a new set of system add-ons into the location and updates the
   * add-on set in prefs.
   *
   * @param {Array} aAddons - An array of addons to install.
   */
  async installAddonSet(aAddons) {
    // Make sure the base dir exists
    await IOUtils.makeDirectory(this._baseDir.path, { ignoreExisting: true });

    let addonSet = SystemAddonInstaller._loadAddonSet();

    // Remove any add-ons that are no longer part of the set.
    const ids = aAddons.map(a => a.id);
    for (let addonID of Object.keys(addonSet.addons)) {
      if (!ids.includes(addonID)) {
        await uninstallAddonFromLocation(addonID, this.location);
      }
    }

    let newDir = this._baseDir.clone();
    newDir.append("blank");

    while (true) {
      newDir.leafName = Services.uuid.generateUUID().toString();
      try {
        await IOUtils.makeDirectory(newDir.path, { ignoreExisting: false });
        break;
      } catch (e) {
        logger.debug(
          "Could not create new system add-on updates dir, retrying",
          e
        );
      }
    }

    // Record the new upgrade directory.
    let state = { schema: 1, directory: newDir.leafName, addons: {} };
    SystemAddonInstaller._saveAddonSet(state);

    this._nextDir = newDir;

    let installs = [];
    for (let addon of aAddons) {
      let install = await createLocalInstall(
        addon._sourceBundle,
        this.location,
        // Make sure that system addons being installed for the first time through
        // Balrog have telemetryInfo associated with them (on the contrary the ones
        // updated through Balrog but part of the build will already have the same
        // `source`, but we expect no `method` to be set for them).
        {
          source: "system-addon",
          method: "product-updates",
        }
      );
      installs.push(install);
    }

    async function installAddon(install) {
      // Make the new install own its temporary file.
      install.ownsTempFile = true;
      install.install();
    }

    async function postponeAddon(install) {
      install.ownsTempFile = true;
      let resumeFn;
      if (AddonManagerPrivate.hasUpgradeListener(install.addon.id)) {
        logger.info(
          `system add-on ${install.addon.id} has an upgrade listener, postponing upgrade set until restart`
        );
        resumeFn = () => {
          logger.info(
            `${install.addon.id} has resumed a previously postponed addon set`
          );
          install.location.installer.resumeAddonSet(installs);
        };
      }
      await install.postpone(resumeFn);
    }

    let previousState;

    try {
      // All add-ons in position, create the new state and store it in prefs
      state = { schema: 1, directory: newDir.leafName, addons: {} };
      for (let addon of aAddons) {
        state.addons[addon.id] = {
          version: addon.version,
        };
      }

      previousState = SystemAddonInstaller._loadAddonSet();
      SystemAddonInstaller._saveAddonSet(state);

      let blockers = aAddons.filter(addon =>
        AddonManagerPrivate.hasUpgradeListener(addon.id)
      );

      if (blockers.length) {
        await waitForAllPromises(installs.map(postponeAddon));
      } else {
        await waitForAllPromises(installs.map(installAddon));
      }
    } catch (e) {
      // Roll back to previous upgrade set (if present) on restart.
      if (previousState) {
        SystemAddonInstaller._saveAddonSet(previousState);
      }
      // Otherwise, roll back to built-in set on restart.
      // TODO try to do these restartlessly
      await this.resetAddonSet();

      try {
        await IOUtils.remove(newDir.path, { recursive: true });
      } catch (e) {
        logger.warn(
          `Failed to remove failed system add-on directory ${newDir.path}.`,
          e
        );
      }
      throw e;
    }
  }

  /**
   * Resumes upgrade of a previously-delayed add-on set.
   *
   * @param {AddonInstall[]} installs
   *        The set of installs to resume.
   */
  async resumeAddonSet(installs) {
    async function resumeAddon(install) {
      install.state = AddonManager.STATE_DOWNLOADED;
      install.location.installer.releaseStagingDir();
      install.install();
    }

    let blockers = installs.filter(install =>
      AddonManagerPrivate.hasUpgradeListener(install.addon.id)
    );

    if (blockers.length > 1) {
      logger.warn(
        "Attempted to resume system add-on install but upgrade blockers are still present"
      );
    } else {
      await waitForAllPromises(installs.map(resumeAddon));
    }
  }

  /**
   * Returns a directory that is normally on the same filesystem as the rest of
   * the install location and can be used for temporarily storing files during
   * safe move operations. Calling this method will delete the existing trash
   * directory and its contents.
   *
   * @returns {nsIFile}
   */
  getTrashDir() {
    let trashDir = getFile(XPIExports.XPIInternal.DIR_TRASH, this.dir);
    let trashDirExists = trashDir.exists();
    try {
      if (trashDirExists) {
        recursiveRemove(trashDir);
      }
      trashDirExists = false;
    } catch (e) {
      logger.warn("Failed to remove trash directory", e);
    }
    if (!trashDirExists) {
      trashDir.create(
        Ci.nsIFile.DIRECTORY_TYPE,
        lazy.FileUtils.PERMS_DIRECTORY
      );
    }

    return trashDir;
  }

  /**
   * Installs an add-on into the install location.
   *
   * @param {string} id
   *        The ID of the add-on to install
   * @param {nsIFile} source
   *        The source nsIFile to install from
   * @returns {nsIFile}
   *        An nsIFile indicating where the add-on was installed to
   */
  installAddon({ id, source }) {
    let trashDir = this.getTrashDir();
    let transaction = new SafeInstallOperation();

    // If any of these operations fails the finally block will clean up the
    // temporary directory
    try {
      flushJarCache(source);

      transaction.moveUnder(source, this.dir);
    } finally {
      // It isn't ideal if this cleanup fails but it isn't worth rolling back
      // the install because of it.
      try {
        recursiveRemove(trashDir);
      } catch (e) {
        logger.warn(
          `Failed to remove trash directory when installing ${id}`,
          e
        );
      }
    }

    let newFile = getFile(source.leafName, this.dir);

    try {
      newFile.lastModifiedTime = Date.now();
    } catch (e) {
      logger.warn("failed to set lastModifiedTime on " + newFile.path, e);
    }

    return newFile;
  }

  // old system add-on upgrade dirs get automatically removed
  uninstallAddon() {}
}

var AppUpdate = {
  findAddonUpdates(addon, reason, appVersion, platformVersion) {
    return new Promise((resolve, reject) => {
      let update = null;
      addon.findUpdates(
        {
          onUpdateAvailable(addon2, install) {
            update = install;
          },

          onUpdateFinished(addon2, error) {
            if (error == AddonManager.UPDATE_STATUS_NO_ERROR) {
              resolve(update);
            } else {
              reject(error);
            }
          },
        },
        reason,
        appVersion,
        platformVersion || appVersion
      );
    });
  },

  stageInstall(installer) {
    return new Promise((resolve, reject) => {
      let listener = {
        onDownloadEnded: install => {
          install.postpone();
        },
        onInstallFailed: install => {
          install.removeListener(listener);
          reject();
        },
        onInstallEnded: install => {
          // We shouldn't end up here, but if we do, resolve
          // since we've installed.
          install.removeListener(listener);
          resolve();
        },
        onInstallPostponed: install => {
          // At this point the addon is staged for restart.
          install.removeListener(listener);
          resolve();
        },
      };

      installer.addListener(listener);
      installer.install();
    });
  },

  async stageLangpackUpdates(nextVersion, nextPlatformVersion) {
    let updates = [];
    let addons = await AddonManager.getAddonsByTypes(["locale"]);
    for (let addon of addons) {
      updates.push(
        this.findAddonUpdates(
          addon,
          AddonManager.UPDATE_WHEN_NEW_APP_DETECTED,
          nextVersion,
          nextPlatformVersion
        )
          .then(update => update && this.stageInstall(update))
          .catch(e => {
            logger.debug(`addon.findUpdate error: ${e}`);
          })
      );
    }
    return Promise.all(updates);
  },
};

export var XPIInstall = {
  // An array of currently active AddonInstalls
  installs: new Set(),

  createLocalInstall,
  flushJarCache,
  newVersionReason,
  recursiveRemove,
  syncLoadManifest,
  loadManifestFromFile,
  uninstallAddonFromLocation,

  stageLangpacksForAppUpdate(nextVersion, nextPlatformVersion) {
    return AppUpdate.stageLangpackUpdates(nextVersion, nextPlatformVersion);
  },

  // Keep track of in-progress operations that support cancel()
  _inProgress: [],

  doing(aCancellable) {
    this._inProgress.push(aCancellable);
  },

  done(aCancellable) {
    let i = this._inProgress.indexOf(aCancellable);
    if (i != -1) {
      this._inProgress.splice(i, 1);
      return true;
    }
    return false;
  },

  cancelAll() {
    // Cancelling one may alter _inProgress, so don't use a simple iterator
    while (this._inProgress.length) {
      let c = this._inProgress.shift();
      try {
        c.cancel();
      } catch (e) {
        logger.warn("Cancel failed", e);
      }
    }
  },

  /**
   * @param {string} id
   *        The expected ID of the add-on.
   * @param {nsIFile} file
   *        The XPI file to install the add-on from.
   * @param {XPIStateLocation} location
   *        The install location to install the add-on to.
   * @param {string?} [oldAppVersion]
   *        The version of the application last run with this profile or null
   *        if it is a new profile or the version is unknown
   * @returns {AddonInternal}
   *        The installed Addon object, upon success.
   */
  async installDistributionAddon(id, file, location, oldAppVersion) {
    let addon = await loadManifestFromFile(file, location);
    addon.installTelemetryInfo = { source: "distribution" };

    if (addon.id != id) {
      throw new Error(
        `File file ${file.path} contains an add-on with an incorrect ID`
      );
    }

    let state = location.get(id);

    if (state) {
      try {
        let existingAddon = await loadManifestFromFile(state.file, location);

        if (Services.vc.compare(addon.version, existingAddon.version) <= 0) {
          return null;
        }
      } catch (e) {
        // Bad add-on in the profile so just proceed and install over the top
        logger.warn(
          "Profile contains an add-on with a bad or missing install " +
            `manifest at ${state.path}, overwriting`,
          e
        );
      }
    } else if (
      addon.type === "locale" &&
      oldAppVersion &&
      Services.vc.compare(oldAppVersion, "67") < 0
    ) {
      /* Distribution language packs didn't get installed due to the signing
           issues so we need to force them to be reinstalled. */
      Services.prefs.clearUserPref(
        XPIExports.XPIInternal.PREF_BRANCH_INSTALLED_ADDON + id
      );
    } else if (
      Services.prefs.getBoolPref(
        XPIExports.XPIInternal.PREF_BRANCH_INSTALLED_ADDON + id,
        false
      )
    ) {
      return null;
    }

    // Install the add-on
    addon.sourceBundle = location.installer.installAddon({
      id,
      source: file,
      action: "copy",
    });

    XPIExports.XPIInternal.XPIStates.addAddon(addon);
    logger.debug(`Installed distribution add-on ${id}`);

    Services.prefs.setBoolPref(
      XPIExports.XPIInternal.PREF_BRANCH_INSTALLED_ADDON + id,
      true
    );

    return addon;
  },

  /**
   * Completes the install of an add-on which was staged during the last
   * session.
   *
   * @param {string} id
   *        The expected ID of the add-on.
   * @param {object} metadata
   *        The parsed metadata for the staged install.
   * @param {XPIStateLocation} location
   *        The install location to install the add-on to.
   * @returns {AddonInternal}
   *        The installed Addon object, upon success.
   */
  async installStagedAddon(id, metadata, location) {
    let source = getFile(`${id}.xpi`, location.installer.getStagingDir());

    // Check that the directory's name is a valid ID.
    if (!gIDTest.test(id) || !source.exists() || !source.isFile()) {
      throw new Error(`Ignoring invalid staging directory entry: ${id}`);
    }

    let addon = await loadManifestFromFile(source, location);

    if (
      XPIExports.XPIDatabase.mustSign(addon.type) &&
      addon.signedState <= AddonManager.SIGNEDSTATE_MISSING
    ) {
      throw new Error(
        `Refusing to install staged add-on ${id} with signed state ${addon.signedState}`
      );
    }

    // Import saved metadata before checking for compatibility.
    addon.importMetadata(metadata);

    // Ensure a staged addon is compatible with the current running version of
    // Firefox.  If a prior version of the addon is installed, it will remain.
    if (!addon.isCompatible) {
      throw new Error(
        `Add-on ${addon.id} is not compatible with application version.`
      );
    }

    logger.debug(`Processing install of ${id} in ${location.name}`);
    let existingAddon = XPIExports.XPIInternal.XPIStates.findAddon(id);
    // This part of the startup file changes is called from
    // processPendingFileChanges, no addons are started yet.
    // Here we handle copying the xpi into its proper place, later
    // processFileChanges will call update.
    try {
      addon.sourceBundle = location.installer.installAddon({
        id,
        source,
      });
      XPIExports.XPIInternal.XPIStates.addAddon(addon);
    } catch (e) {
      if (existingAddon) {
        // Re-install the old add-on
        XPIExports.XPIInternal.get(existingAddon).install();
      }
      throw e;
    }

    return addon;
  },

  async updateSystemAddons() {
    let systemAddonLocation = XPIExports.XPIInternal.XPIStates.getLocation(
      XPIExports.XPIInternal.KEY_APP_SYSTEM_ADDONS
    );
    if (!systemAddonLocation) {
      return;
    }

    let installer = systemAddonLocation.installer;

    // Don't do anything in safe mode
    if (Services.appinfo.inSafeMode) {
      return;
    }

    // Download the list of system add-ons
    let url = Services.prefs.getStringPref(PREF_SYSTEM_ADDON_UPDATE_URL, null);
    if (!url) {
      await installer.cleanDirectories();
      return;
    }

    url = await lazy.UpdateUtils.formatUpdateURL(url);

    logger.info(`Starting system add-on update check from ${url}.`);
    let res = await lazy.ProductAddonChecker.getProductAddonList(
      url,
      true
    ).catch(e => logger.error(`System addon update list error ${e}`));

    // If there was no list then do nothing.
    if (!res || !res.addons) {
      logger.info("No system add-ons list was returned.");
      await installer.cleanDirectories();
      return;
    }

    let addonList = new Map(
      res.addons.map(spec => [spec.id, { spec, path: null, addon: null }])
    );

    let setMatches = (wanted, existing) => {
      if (wanted.size != existing.size) {
        return false;
      }

      for (let [id, addon] of existing) {
        let wantedInfo = wanted.get(id);

        if (!wantedInfo) {
          return false;
        }
        if (wantedInfo.spec.version != addon.version) {
          return false;
        }
      }

      return true;
    };

    // If this matches the current set in the profile location then do nothing.
    let updatedAddons = addonMap(
      await XPIExports.XPIDatabase.getAddonsInLocation(
        XPIExports.XPIInternal.KEY_APP_SYSTEM_ADDONS
      )
    );
    if (setMatches(addonList, updatedAddons)) {
      logger.info("Retaining existing updated system add-ons.");
      await installer.cleanDirectories();
      return;
    }

    // If this matches the current set in the default locations then reset the
    // updated set.
    let defaultAddons = addonMap(
      await XPIExports.XPIDatabase.getAddonsInLocation(
        XPIExports.XPIInternal.KEY_APP_SYSTEM_BUILTINS
      )
    );
    if (setMatches(addonList, defaultAddons)) {
      logger.info("Resetting system add-ons.");
      await installer.resetAddonSet();
      await installer.cleanDirectories();
      return;
    }

    // Download all the add-ons
    async function downloadAddon(item) {
      try {
        let sourceAddon = updatedAddons.get(item.spec.id);
        if (sourceAddon && sourceAddon.version == item.spec.version) {
          // Copying the file to a temporary location has some benefits. If the
          // file is locked and cannot be read then we'll fall back to
          // downloading a fresh copy. We later mark the install object with
          // ownsTempFile so that we will cleanup later (see installAddonSet).
          try {
            let tmpDir = Services.dirsvc.get("TmpD", Ci.nsIFile).path;
            let uniquePath = await IOUtils.createUniqueFile(tmpDir, "tmpaddon");
            await IOUtils.copy(sourceAddon._sourceBundle.path, uniquePath);
            // Make sure to update file modification times so this is detected
            // as a new add-on.
            await IOUtils.setModificationTime(uniquePath);
            item.path = uniquePath;
          } catch (e) {
            logger.warn(
              `Failed make temporary copy of ${sourceAddon._sourceBundle.path}.`,
              e
            );
          }
        }
        if (!item.path) {
          item.path = await lazy.ProductAddonChecker.downloadAddon(item.spec);
        }
        item.addon = await loadManifestFromFile(
          nsIFile(item.path),
          systemAddonLocation
        );
      } catch (e) {
        logger.error(`Failed to download system add-on ${item.spec.id}`, e);
      }
    }
    await Promise.all(Array.from(addonList.values()).map(downloadAddon));

    // The download promises all resolve regardless, now check if they all
    // succeeded
    let validateAddon = item => {
      if (item.spec.id != item.addon.id) {
        logger.warn(
          `Downloaded system add-on expected to be ${item.spec.id} but was ${item.addon.id}.`
        );
        return false;
      }

      if (item.spec.version != item.addon.version) {
        logger.warn(
          `Expected system add-on ${item.spec.id} to be version ${item.spec.version} but was ${item.addon.version}.`
        );
        return false;
      }

      if (!installer.isValidAddon(item.addon)) {
        return false;
      }

      return true;
    };

    if (
      !Array.from(addonList.values()).every(
        item => item.path && item.addon && validateAddon(item)
      )
    ) {
      throw new Error(
        "Rejecting updated system add-on set that either could not " +
          "be downloaded or contained unusable add-ons."
      );
    }

    // Install into the install location
    logger.info("Installing new system add-on set");
    await installer.installAddonSet(
      Array.from(addonList.values()).map(a => a.addon)
    );
  },

  /**
   * Called to test whether installing XPI add-ons is enabled.
   *
   * @returns {boolean}
   *        True if installing is enabled.
   */
  isInstallEnabled() {
    // Default to enabled if the preference does not exist
    return Services.prefs.getBoolPref(PREF_XPI_ENABLED, true);
  },

  /**
   * Called to test whether installing XPI add-ons by direct URL requests is
   * whitelisted.
   *
   * @returns {boolean}
   *        True if installing by direct requests is whitelisted
   */
  isDirectRequestWhitelisted() {
    // Default to whitelisted if the preference does not exist.
    return Services.prefs.getBoolPref(PREF_XPI_DIRECT_WHITELISTED, true);
  },

  /**
   * Called to test whether installing XPI add-ons from file referrers is
   * whitelisted.
   *
   * @returns {boolean}
   *       True if installing from file referrers is whitelisted
   */
  isFileRequestWhitelisted() {
    // Default to whitelisted if the preference does not exist.
    return Services.prefs.getBoolPref(PREF_XPI_FILE_WHITELISTED, true);
  },

  isWeakSignatureInstallAllowed() {
    return Services.prefs.getBoolPref(PREF_XPI_WEAK_SIGNATURES_ALLOWED, false);
  },

  getWeakSignatureInstallPrefName() {
    return PREF_XPI_WEAK_SIGNATURES_ALLOWED;
  },

  /**
   * Called to test whether installing XPI add-ons from a URI is allowed.
   *
   * @param {nsIPrincipal}  aInstallingPrincipal
   *        The nsIPrincipal that initiated the install
   * @returns {boolean}
   *        True if installing is allowed
   */
  isInstallAllowed(aInstallingPrincipal) {
    if (!this.isInstallEnabled()) {
      return false;
    }

    let uri = aInstallingPrincipal.URI;

    // Direct requests without a referrer are either whitelisted or blocked.
    if (!uri) {
      return this.isDirectRequestWhitelisted();
    }

    // Local referrers can be whitelisted.
    if (
      this.isFileRequestWhitelisted() &&
      (uri.schemeIs("chrome") || uri.schemeIs("file"))
    ) {
      return true;
    }

    XPIExports.XPIDatabase.importPermissions();

    let permission = Services.perms.testPermissionFromPrincipal(
      aInstallingPrincipal,
      XPIExports.XPIInternal.XPI_PERMISSION
    );
    if (permission == Ci.nsIPermissionManager.DENY_ACTION) {
      return false;
    }

    let requireWhitelist = Services.prefs.getBoolPref(
      PREF_XPI_WHITELIST_REQUIRED,
      true
    );
    if (
      requireWhitelist &&
      permission != Ci.nsIPermissionManager.ALLOW_ACTION
    ) {
      return false;
    }

    let requireSecureOrigin = Services.prefs.getBoolPref(
      PREF_INSTALL_REQUIRESECUREORIGIN,
      true
    );
    let safeSchemes = ["https", "chrome", "file"];
    if (requireSecureOrigin && !safeSchemes.includes(uri.scheme)) {
      return false;
    }

    return true;
  },

  /**
   * Called to get an AddonInstall to download and install an add-on from a URL.
   *
   * @param {nsIURI} aUrl
   *        The URL to be installed
   * @param {object} [aOptions]
   *        Additional options for this install.
   * @param {string?} [aOptions.hash]
   *        A hash for the install
   * @param {string} [aOptions.name]
   *        A name for the install
   * @param {Object} [aOptions.icons]
   *        Icon URLs for the install
   * @param {string} [aOptions.version]
   *        A version for the install
   * @param {XULElement} [aOptions.browser]
   *        The browser performing the install
   * @param {Object} [aOptions.telemetryInfo]
   *        An optional object which provides details about the installation source
   *        included in the addon manager telemetry events.
   * @param {boolean} [aOptions.sendCookies = false]
   *        Whether cookies should be sent when downloading the add-on.
   * @param {string} [aOptions.useSystemLocation = false]
   *        If true installs to the system profile location.
   * @returns {AddonInstall}
   */
  async getInstallForURL(aUrl, aOptions) {
    let locationName = aOptions.useSystemLocation
      ? XPIExports.XPIInternal.KEY_APP_SYSTEM_PROFILE
      : XPIExports.XPIInternal.KEY_APP_PROFILE;
    let location = XPIExports.XPIInternal.XPIStates.getLocation(locationName);
    if (!location) {
      throw Components.Exception(
        "Invalid location name",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    let url = Services.io.newURI(aUrl);

    if (url instanceof Ci.nsIFileURL) {
      let install = new LocalAddonInstall(location, url, aOptions);
      await install.init();
      return install.wrapper;
    }

    let install = new DownloadAddonInstall(location, url, aOptions);
    return install.wrapper;
  },

  /**
   * Called to get an AddonInstall to install an add-on from a local file.
   *
   * @param {nsIFile} aFile
   *        The file to be installed
   * @param {Object?} [aInstallTelemetryInfo]
   *        An optional object which provides details about the installation source
   *        included in the addon manager telemetry events.
   * @param {boolean} [aUseSystemLocation = false]
   *        If true install to the system profile location.
   * @returns {AddonInstall?}
   */
  async getInstallForFile(
    aFile,
    aInstallTelemetryInfo,
    aUseSystemLocation = false
  ) {
    let location = XPIExports.XPIInternal.XPIStates.getLocation(
      aUseSystemLocation
        ? XPIExports.XPIInternal.KEY_APP_SYSTEM_PROFILE
        : XPIExports.XPIInternal.KEY_APP_PROFILE
    );
    let install = await createLocalInstall(
      aFile,
      location,
      aInstallTelemetryInfo
    );
    return install ? install.wrapper : null;
  },

  /**
   * Called to get the current AddonInstalls, optionally limiting to a list of
   * types.
   *
   * @param {Array<string>?} aTypes
   *        An array of types or null to get all types
   * @returns {AddonInstall[]}
   */
  getInstallsByTypes(aTypes) {
    let results = [...this.installs];
    if (aTypes) {
      results = results.filter(install => {
        return aTypes.includes(install.type);
      });
    }

    return results.map(install => install.wrapper);
  },

  /**
   * Temporarily installs add-on from a local XPI file or directory.
   * As this is intended for development, the signature is not checked and
   * the add-on does not persist on application restart.
   *
   * @param {nsIFile} aFile
   *        An nsIFile for the unpacked add-on directory or XPI file.
   *
   * @returns {Promise<Addon>}
   *        A Promise that resolves to an Addon object on success, or rejects
   *        if the add-on is not a valid restartless add-on or if the
   *        same ID is already installed.
   */
  async installTemporaryAddon(aFile) {
    let installLocation = XPIExports.XPIInternal.TemporaryInstallLocation;

    if (XPIExports.XPIInternal.isXPI(aFile.leafName)) {
      flushJarCache(aFile);
    }
    let addon = await loadManifestFromFile(aFile, installLocation);
    addon.rootURI = XPIExports.XPIInternal.getURIForResourceInFile(
      aFile,
      ""
    ).spec;

    await this._activateAddon(addon, { temporarilyInstalled: true });

    logger.debug(`Install of temporary addon in ${aFile.path} completed.`);
    return addon.wrapper;
  },

  /**
   * Installs an add-on from a built-in location
   *  (ie a resource: url referencing assets shipped with the application)
   *
   * @param  {string} base
   *         A string containing the base URL.  Must be a resource: URL.
   * @returns {Promise<Addon>}
   *          A Promise that resolves to an Addon object when the addon is
   *          installed.
   */
  async installBuiltinAddon(base) {
    // We have to get this before the install, as the install will overwrite
    // the pref. We then keep the value for this run, so we can restore
    // the selected theme once it becomes available.
    if (lastSelectedTheme === null) {
      lastSelectedTheme = Services.prefs.getCharPref(PREF_SELECTED_THEME, "");
    }

    let baseURL = Services.io.newURI(base);

    // WebExtensions need to be able to iterate through the contents of
    // an extension (for localization).  It knows how to do this with
    // jar: and file: URLs, so translate the provided base URL to
    // something it can use.
    if (baseURL.scheme !== "resource") {
      throw new Error("Built-in addons must use resource: URLS");
    }

    let pkg = builtinPackage(baseURL);
    let addon = await loadManifest(pkg, XPIExports.XPIInternal.BuiltInLocation);
    addon.rootURI = base;

    // If this is a theme, decide whether to enable it. Themes are
    // disabled by default. However:
    //
    // We always want one theme to be active, falling back to the
    // default theme when the active theme is disabled.
    // During a theme migration, such as a change in the path to the addon, we
    // will need to ensure a correct theme is enabled.
    if (addon.type === "theme") {
      if (
        addon.id === lastSelectedTheme ||
        (!lastSelectedTheme.endsWith("@mozilla.org") &&
          addon.id === lazy.AddonSettings.DEFAULT_THEME_ID &&
          !XPIExports.XPIDatabase.getAddonsByType("theme").some(
            theme => !theme.disabled
          ))
      ) {
        addon.userDisabled = false;
      }
    }
    await this._activateAddon(addon);
    return addon.wrapper;
  },

  /**
   * Activate a newly installed addon.
   * This function handles all the bookkeeping related to a new addon
   * and invokes whatever bootstrap methods are necessary.
   * Note that this function is only used for temporary and built-in
   * installs, it is very similar to AddonInstall::startInstall().
   * It would be great to merge this function with that one some day.
   *
   * @param {AddonInternal} addon  The addon to activate
   * @param {object} [extraParams] Any extra parameters to pass to the
   *                               bootstrap install() method
   *
   * @returns {Promise<void>}
   */
  async _activateAddon(addon, extraParams = {}) {
    if (addon.appDisabled) {
      let message = `Add-on ${addon.id} is not compatible with application version.`;

      let app = addon.matchingTargetApplication;
      if (app) {
        if (app.minVersion) {
          message += ` add-on minVersion: ${app.minVersion}.`;
        }
        if (app.maxVersion) {
          message += ` add-on maxVersion: ${app.maxVersion}.`;
        }
      }
      throw new Error(message);
    }

    let oldAddon = await XPIExports.XPIDatabase.getVisibleAddonForID(addon.id);

    let willActivate =
      !oldAddon ||
      oldAddon.location == addon.location ||
      addon.location.hasPrecedence(oldAddon.location);

    let install = () => {
      addon.visible = willActivate;
      // Themes are generally not enabled by default at install time,
      // unless enabled by the front-end code. If they are meant to be
      // enabled, they will already have been enabled by this point.
      if (addon.type !== "theme" || addon.location.isTemporary) {
        addon.userDisabled = false;
      }
      addon.active = addon.visible && !addon.disabled;

      addon = XPIExports.XPIDatabase.addToDatabase(
        addon,
        addon._sourceBundle ? addon._sourceBundle.path : null
      );

      XPIExports.XPIInternal.XPIStates.addAddon(addon);
      XPIExports.XPIInternal.XPIStates.save();
    };

    AddonManagerPrivate.callAddonListeners("onInstalling", addon.wrapper);

    if (!willActivate) {
      addon.installDate = Date.now();

      install();
    } else if (oldAddon) {
      logger.warn(
        `Addon with ID ${oldAddon.id} already installed, ` +
          "older version will be disabled"
      );

      addon.installDate = oldAddon.installDate;

      await XPIExports.XPIInternal.BootstrapScope.get(oldAddon).update(
        addon,
        true,
        install
      );
    } else {
      addon.installDate = Date.now();

      install();
      let bootstrap = XPIExports.XPIInternal.BootstrapScope.get(addon);
      await bootstrap.install(undefined, true, extraParams);
    }

    AddonManagerPrivate.callInstallListeners(
      "onExternalInstall",
      null,
      addon.wrapper,
      oldAddon ? oldAddon.wrapper : null,
      false
    );
    AddonManagerPrivate.callAddonListeners("onInstalled", addon.wrapper);

    // Notify providers that a new theme has been enabled.
    if (addon.type === "theme" && !addon.userDisabled) {
      AddonManagerPrivate.notifyAddonChanged(addon.id, addon.type, false);
    }
  },

  /**
   * Uninstalls an add-on, immediately if possible or marks it as pending
   * uninstall if not.
   *
   * @param {DBAddonInternal} aAddon
   *        The DBAddonInternal to uninstall
   * @param {boolean} aForcePending
   *        Force this addon into the pending uninstall state (used
   *        e.g. while the add-on manager is open and offering an
   *        "undo" button)
   * @throws if the addon cannot be uninstalled because it is in an install
   *         location that does not allow it
   */
  async uninstallAddon(aAddon, aForcePending) {
    if (!aAddon.inDatabase) {
      throw new Error(
        `Cannot uninstall addon ${aAddon.id} because it is not installed`
      );
    }
    let { location } = aAddon;

    // If the addon is sideloaded into a location that does not allow
    // sideloads, it is a legacy sideload.  We allow those to be uninstalled.
    let isLegacySideload =
      aAddon.foreignInstall &&
      !(location.scope & lazy.AddonSettings.SCOPES_SIDELOAD);

    if (location.locked && !isLegacySideload) {
      throw new Error(
        `Cannot uninstall addon ${aAddon.id} ` +
          `from locked install location ${location.name}`
      );
    }

    if (aForcePending && aAddon.pendingUninstall) {
      throw new Error("Add-on is already marked to be uninstalled");
    }

    if (aAddon._updateCheck) {
      logger.debug(`Cancel in-progress update check for ${aAddon.id}`);
      aAddon._updateCheck.cancel();
    }

    let wasActive = aAddon.active;
    let wasPending = aAddon.pendingUninstall;

    if (aForcePending) {
      // We create an empty directory in the staging directory to indicate
      // that an uninstall is necessary on next startup. Temporary add-ons are
      // automatically uninstalled on shutdown anyway so there is no need to
      // do this for them.
      if (!aAddon.location.isTemporary && aAddon.location.installer) {
        let stage = getFile(
          aAddon.id,
          aAddon.location.installer.getStagingDir()
        );
        if (!stage.exists()) {
          stage.create(
            Ci.nsIFile.DIRECTORY_TYPE,
            lazy.FileUtils.PERMS_DIRECTORY
          );
        }
      }

      XPIExports.XPIDatabase.setAddonProperties(aAddon, {
        pendingUninstall: true,
      });
      Services.prefs.setBoolPref(PREF_PENDING_OPERATIONS, true);
      let xpiState = aAddon.location.get(aAddon.id);
      if (xpiState) {
        xpiState.enabled = false;
        XPIExports.XPIInternal.XPIStates.save();
      } else {
        logger.warn(
          "Can't find XPI state while uninstalling ${id} from ${location}",
          aAddon
        );
      }
    }

    // If the add-on is not visible then there is no need to notify listeners.
    if (!aAddon.visible) {
      return;
    }

    let wrapper = aAddon.wrapper;

    // If the add-on wasn't already pending uninstall then notify listeners.
    if (!wasPending) {
      AddonManagerPrivate.callAddonListeners(
        "onUninstalling",
        wrapper,
        !!aForcePending
      );
    }

    let existingAddon = XPIExports.XPIInternal.XPIStates.findAddon(
      aAddon.id,
      loc => loc != aAddon.location
    );

    let bootstrap = XPIExports.XPIInternal.BootstrapScope.get(aAddon);
    if (!aForcePending) {
      let existing;
      if (existingAddon) {
        existing = await XPIExports.XPIDatabase.getAddonInLocation(
          aAddon.id,
          existingAddon.location.name
        );
      }

      let uninstall = () => {
        XPIExports.XPIInternal.XPIStates.disableAddon(aAddon.id);
        if (aAddon.location.installer) {
          aAddon.location.installer.uninstallAddon(aAddon.id);
        }
        XPIExports.XPIDatabase.removeAddonMetadata(aAddon);
        aAddon.location.removeAddon(aAddon.id);
        AddonManagerPrivate.callAddonListeners("onUninstalled", wrapper);

        if (existing) {
          XPIExports.XPIDatabase.makeAddonVisible(existing);
          AddonManagerPrivate.callAddonListeners(
            "onInstalling",
            existing.wrapper,
            false
          );

          if (!existing.disabled) {
            XPIExports.XPIDatabase.updateAddonActive(existing, true);
          }
        }
      };

      // Migrate back to the existing addon, unless it was a builtin colorway theme.
      if (existing) {
        await bootstrap.update(existing, !existing.disabled, uninstall);
        AddonManagerPrivate.callAddonListeners("onInstalled", existing.wrapper);
      } else {
        aAddon.location.removeAddon(aAddon.id);
        await bootstrap.uninstall();
        uninstall();
      }
    } else if (aAddon.active) {
      XPIExports.XPIInternal.XPIStates.disableAddon(aAddon.id);
      bootstrap.shutdown(
        XPIExports.XPIInternal.BOOTSTRAP_REASONS.ADDON_UNINSTALL
      );
      XPIExports.XPIDatabase.updateAddonActive(aAddon, false);
    }

    // Notify any other providers that a new theme has been enabled
    // (when the active theme is uninstalled, the default theme is enabled).
    if (aAddon.type === "theme" && wasActive) {
      AddonManagerPrivate.notifyAddonChanged(null, aAddon.type);
    }
  },

  /**
   * Cancels the pending uninstall of an add-on.
   *
   * @param {DBAddonInternal} aAddon
   *        The DBAddonInternal to cancel uninstall for
   */
  cancelUninstallAddon(aAddon) {
    if (!aAddon.inDatabase) {
      throw new Error("Can only cancel uninstall for installed addons.");
    }
    if (!aAddon.pendingUninstall) {
      throw new Error("Add-on is not marked to be uninstalled");
    }

    if (!aAddon.location.isTemporary && aAddon.location.installer) {
      aAddon.location.installer.cleanStagingDir([aAddon.id]);
    }

    XPIExports.XPIDatabase.setAddonProperties(aAddon, {
      pendingUninstall: false,
    });

    if (!aAddon.visible) {
      return;
    }

    aAddon.location.get(aAddon.id).syncWithDB(aAddon);
    XPIExports.XPIInternal.XPIStates.save();

    Services.prefs.setBoolPref(PREF_PENDING_OPERATIONS, true);

    if (!aAddon.disabled) {
      XPIExports.XPIInternal.BootstrapScope.get(aAddon).startup(
        XPIExports.XPIInternal.BOOTSTRAP_REASONS.ADDON_INSTALL
      );
      XPIExports.XPIDatabase.updateAddonActive(aAddon, true);
    }

    let wrapper = aAddon.wrapper;
    AddonManagerPrivate.callAddonListeners("onOperationCancelled", wrapper);

    // Notify any other providers that this theme is now enabled again.
    if (aAddon.type === "theme" && aAddon.active) {
      AddonManagerPrivate.notifyAddonChanged(aAddon.id, aAddon.type, false);
    }
  },

  DirectoryInstaller,
  SystemAddonInstaller,
};
