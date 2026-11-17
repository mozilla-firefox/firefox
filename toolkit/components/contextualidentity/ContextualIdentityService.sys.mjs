/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const SAVE_DELAY_MS = 1500;
const CONTEXTUAL_IDENTITY_ENABLED_PREF = "privacy.userContext.enabled";

// Canonical container colors and icons live in the fxcontainers component.
// Adding/renaming one there must be mirrored in (kept in sync by
// browser_container_definitions_in_sync.js):
//   - usercontext.css: .identity-color-* / .identity-icon-* rules
//   - the matching content/<icon>.svg file
//   - toolkit/global/contextual-identity.ftl: user-context-color-* /
//     user-context-icon-* labels
//   - enterprisepolicies/schemas/policies-schema.json: Containers.Default enums

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "l10n", function () {
  return new Localization(["toolkit/global/contextual-identity.ftl"], true);
});

ChromeUtils.defineESModuleGetters(lazy, {
  ContainerColor:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  ContainerIcon:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  ContainerLabel:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  ContainersCallback:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  ContainersStore:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  DefaultIdentity:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  colorCode:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  colorFromName:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  colorGeckoL10nId:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  colorName:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  containerColorAliases:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  containerColors:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  containerIcons:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  iconFromName:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  iconGeckoL10nId:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  iconName:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  labelGeckoL10nId:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  latestVersion:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  normalizeSite:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  resolveColor:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs",
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
});

function _TabRemovalObserver(resolver, remoteTabIds) {
  this._resolver = resolver;
  this._remoteTabIds = remoteTabIds;
  Services.obs.addObserver(this, "ipc:browser-destroyed");
}

_TabRemovalObserver.prototype = {
  _resolver: null,
  _remoteTabIds: null,

  QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),

  observe(subject) {
    let remoteTab = subject.QueryInterface(Ci.nsIRemoteTab);
    if (this._remoteTabIds.has(remoteTab.tabId)) {
      this._remoteTabIds.delete(remoteTab.tabId);
      if (this._remoteTabIds.size == 0) {
        Services.obs.removeObserver(this, "ipc:browser-destroyed");
        this._resolver();
      }
    }
  },
};

function _ContextualIdentityService(path) {
  this.init(path);
}

_ContextualIdentityService.prototype = {
  QueryInterface: ChromeUtils.generateQI([
    "nsISiteContainerService",
    "nsIObserver",
  ]),

  get LAST_CONTAINERS_JSON_VERSION() {
    return lazy.latestVersion();
  },

  _store: null,
  _path: null,
  _saver: null,
  _saverCallback: null,

  init(path) {
    this._path = path;

    Services.prefs.addObserver(CONTEXTUAL_IDENTITY_ENABLED_PREF, this);

    // uniffi reports a callback still registered at xpcom-shutdown as a leak.
    lazy.AsyncShutdown.profileChangeTeardown.addBlocker(
      `ContextualIdentityService: releasing the containers callback (${path})`,
      () => this._store?.unsetCallback()
    );
  },

  async observe(aSubject, aTopic) {
    if (aTopic === "nsPref:changed") {
      const contextualIdentitiesEnabled = Services.prefs.getBoolPref(
        CONTEXTUAL_IDENTITY_ENABLED_PREF
      );
      if (!contextualIdentitiesEnabled) {
        await this.closeContainerTabs();
        this.notifyAllContainersCleared();
        this.resetDefault();
      }

      Glean.containers.containersEnabled.record({
        enabled: contextualIdentitiesEnabled,
      });
    }
  },

  async load() {
    await IOUtils.read(this._path).then(
      bytes => {
        this._openStore(bytes);
      },
      error => {
        if (error?.name != "NotFoundError") {
          console.error(error);
        }
        this._openStore(null);
      }
    );

    Glean.containers.containerProfileLoaded.record({
      containers: this.getPublicUserContextIds().length,
    });

    Services.obs.notifyObservers(
      null,
      "contextual-identity-service-load-finished"
    );
  },

  _openStore(bytes) {
    if (this._store) {
      return;
    }

    const service = this;
    const callback = new (class extends lazy.ContainersCallback {
      persist() {
        service.saveSoon();
      }
    })();

    const policyIdentities =
      Services.policies?.getActivePolicies()?.Containers?.Default;
    const seed = policyIdentities?.map(
      identity =>
        new lazy.DefaultIdentity({
          icon: this._icon(identity.icon),
          color: this._color(identity.color),
          label: new lazy.ContainerLabel.Name({ name: identity.name }),
        })
    );

    try {
      this._store = lazy.ContainersStore.init(bytes, seed, callback);
      return;
    } catch (error) {
      if (!bytes) {
        throw error;
      }
      console.error(error);
    }

    this.resetDefault();
  },

  resetDefault() {
    this._store?.unsetCallback();
    this._store = null;
    this._openStore(null);
    this.deleteContainerData();
    this.saveSoon();
  },

  saveSoon() {
    if (!this._saver) {
      this._saverCallback = () => this._saver.finalize();

      this._saver = new lazy.DeferredTask(() => this.save(), SAVE_DELAY_MS);
      lazy.AsyncShutdown.profileBeforeChange.addBlocker(
        "ContextualIdentityService: writing data",
        this._saverCallback
      );
    } else {
      this._saver.disarm();
    }

    this._saver.arm();
  },

  save() {
    lazy.AsyncShutdown.profileBeforeChange.removeBlocker(this._saverCallback);

    this._saver = null;
    this._saverCallback = null;

    return IOUtils.write(this._path, this._store.serialize(), {
      tmpPath: this._path + ".tmp",
    });
  },

  create(name, icon, color) {
    this.ensureDataReady();
    this._requireName(name);

    const identity = this._toIdentity(
      this._store.create(name, this._icon(icon), this._color(color))
    );

    this.saveSoon();

    Services.obs.notifyObservers(
      this.getIdentityObserverOutput(identity),
      "contextual-identity-created"
    );

    Glean.containers.containerCreated.record({
      container_id: identity.userContextId.toString(),
    });

    return Cu.cloneInto(identity, {});
  },

  createForPolicy(policyId) {
    this.ensureDataReady();

    const identity = this._toIdentity(this._store.createForPolicy(policyId));
    this.saveSoon();

    return Cu.cloneInto(identity, {});
  },

  removePolicyIdentity(userContextId) {
    this.ensureDataReady();

    userContextId = this._containerId(userContextId);
    if (userContextId == null) {
      return false;
    }

    if (!this._store.removePolicyIdentity(userContextId)) {
      return false;
    }

    this.saveSoon();
    Services.clearData.deleteDataFromOriginAttributesPattern({ userContextId });

    return true;
  },

  getPolicyIdentities() {
    this.ensureDataReady();

    return Cu.cloneInto(
      this._store
        .policyIdentities()
        .map(container => this._toIdentity(container)),
      {}
    );
  },

  update(userContextId, name, icon, color) {
    this.ensureDataReady();
    this._requireName(name);

    const id = this._containerId(userContextId);

    let container = null;
    if (id != null) {
      container = this._store.update(
        id,
        name,
        this._icon(icon),
        this._color(color)
      );
    }

    if (container) {
      this.saveSoon();
      Services.obs.notifyObservers(
        this.getIdentityObserverOutput(this._toIdentity(container)),
        "contextual-identity-updated"
      );
    }

    Glean.containers.containerModified.record({
      container_id: userContextId.toString(),
    });

    return !!container;
  },

  move(userContextIds, position) {
    this.ensureDataReady();

    const ids = userContextIds
      .map(id => this._containerId(id))
      .filter(id => id != null);

    const moved = this._store.moveContainers(ids, position);

    if (moved) {
      this.saveSoon();
      Services.obs.notifyObservers(null, "contextual-identity-reordered");
    }

    return moved;
  },

  remove(userContextId) {
    this.ensureDataReady();

    userContextId = this._containerId(userContextId);
    if (userContextId == null) {
      return false;
    }

    const container = this._store.publicIdentityFromId(userContextId);
    if (!container) {
      return false;
    }
    const deletedOutput = this.getIdentityObserverOutput(
      this._toIdentity(container)
    );

    // The component drops the container and any site bound to it, and reports
    // which sites those were; clearing the data it held is this service's job.
    const before = this._store.getSiteAssociations(userContextId);
    this._store.remove(userContextId);

    this.saveSoon();
    Services.clearData.deleteDataFromOriginAttributesPattern({ userContextId });

    for (const association of before) {
      this._notifySiteAssociationChanged(association.site, null);
    }

    Services.obs.notifyObservers(deletedOutput, "contextual-identity-deleted");

    Glean.containers.containerDeleted.record({
      container_id: userContextId.toString(),
    });

    return true;
  },

  // The normalized host (lower case, IDN-encoded) for `site`, or null if
  // `site` can never match a navigation. Delegated so that what observers are
  // told matches what the component actually stored.
  normalizeSite(site) {
    return lazy.normalizeSite(site);
  },

  _notifySiteAssociationChanged(site, userContextId) {
    Services.obs.notifyObservers(
      { wrappedJSObject: { site, userContextId } },
      "contextual-identity-site-association-changed"
    );
  },

  setSiteAssociation(site, userContextId) {
    this.ensureDataReady();

    if (!Number.isInteger(userContextId)) {
      throw new Error(
        `Invalid container id for site association: ${userContextId}`
      );
    }

    if (!this.getPublicIdentityFromId(userContextId)) {
      throw new Error(
        `Cannot associate a site to unknown container ${userContextId}`
      );
    }

    const host = this.normalizeSite(site);
    if (!host) {
      throw new Error("Invalid site for container association.");
    }

    if (this._store.getSiteAssociation(host) === userContextId) {
      return;
    }

    this._store.setSiteAssociation(host, userContextId);
    this.saveSoon();
    this._notifySiteAssociationChanged(host, userContextId);
  },

  removeSiteAssociation(site) {
    this.ensureDataReady();

    const host = this.normalizeSite(site);
    if (!host || !this._store.getSiteAssociation(host)) {
      return;
    }

    this._store.removeSiteAssociation(host);
    this.saveSoon();
    this._notifySiteAssociationChanged(host, null);
  },

  getSiteAssociation(site) {
    this.ensureDataReady();

    return this._store.getSiteAssociation(site);
  },

  getSiteAssociations(userContextId = 0) {
    this.ensureDataReady();

    let id = null;
    if (userContextId) {
      id = this._containerId(userContextId);
      if (id == null) {
        return [];
      }
    }

    return this._store
      .getSiteAssociations(id)
      .map(({ site, userContextId: owner }) => ({
        site,
        userContextId: owner,
      }));
  },

  lookup(host) {
    return this.getSiteAssociation(host);
  },

  containerForNavigation(uri, baselineUserContextId) {
    let host;
    try {
      host = uri.asciiHost;
    } catch (e) {
      return baselineUserContextId;
    }

    try {
      let policyContainer = Services.policies?.getContainerForURI(uri);
      if (policyContainer) {
        return policyContainer;
      }
    } catch (e) {}

    return this.getSiteAssociation(host) || baselineUserContextId;
  },

  getIdentityObserverOutput(identity) {
    let wrappedJSObject = {
      name: this.getUserContextLabel(identity.userContextId),
      icon: identity.icon,
      color: identity.color,
      userContextId: identity.userContextId,
    };

    return { wrappedJSObject };
  },

  ensureDataReady() {
    if (this._store) {
      return;
    }

    let bytes = null;
    try {
      // Check whether the file exists before reading, to avoid a warning in
      // the console when it does not.
      const file = new lazy.FileUtils.File(this._path);
      if (file.exists()) {
        const inputStream = Cc[
          "@mozilla.org/network/file-input-stream;1"
        ].createInstance(Ci.nsIFileInputStream);
        inputStream.init(
          file,
          lazy.FileUtils.MODE_RDONLY,
          lazy.FileUtils.PERMS_FILE,
          0
        );
        try {
          bytes = new Uint8Array(
            lazy.NetUtil.readInputStream(inputStream, inputStream.available())
          );
        } finally {
          inputStream.close();
        }
      }
    } catch (error) {
      console.error(error);
    }

    this._openStore(bytes);
  },

  _requireName(name) {
    if (!name.trim()) {
      throw new Error(
        "Contextual identity names cannot contain only whitespace."
      );
    }
  },

  _containerId(value) {
    const id = Number(value);
    return Number.isInteger(id) && id >= 0 && id <= 0xffffffff ? id : null;
  },

  // A container always carries one of the component's fixed icons and colors,
  // so the callers that pass neither -- the WebDriver user contexts -- and the
  // ones that pass a name from an older Firefox still get something storable.
  _icon(name) {
    return lazy.iconFromName(name ?? "") ?? lazy.ContainerIcon.CIRCLE;
  },

  _color(name) {
    return (
      lazy.colorFromName(lazy.resolveColor(name ?? "")) ??
      lazy.ContainerColor.GRAY
    );
  },

  _toIdentity(container) {
    const identity = {
      userContextId: container.userContextId,
      public: container.isPublic,
    };

    if (container.icon != null) {
      identity.icon = lazy.iconName(container.icon);
    }

    if (container.color != null) {
      identity.color = lazy.colorName(container.color);
    }

    if (container.label instanceof lazy.ContainerLabel.Name) {
      identity.name = container.label.name;
    }

    if (container.policyId != null) {
      identity.policy = true;
      identity.policyId = container.policyId;
    }

    return identity;
  },

  getPublicUserContextIds() {
    this.ensureDataReady();
    return this._store.publicUserContextIds();
  },

  getPrivateUserContextIds() {
    this.ensureDataReady();
    return this._store.privateUserContextIds();
  },

  getPublicIdentities() {
    this.ensureDataReady();
    return Cu.cloneInto(
      this._store
        .publicIdentities()
        .map(container => this._toIdentity(container)),
      {}
    );
  },

  getPrivateIdentity(name) {
    this.ensureDataReady();
    const container = this._store.privateIdentity(name);
    return container
      ? Cu.cloneInto(this._toIdentity(container), {})
      : undefined;
  },

  getPublicIdentityFromId(userContextId) {
    this.ensureDataReady();

    const id = this._containerId(userContextId);
    if (id == null) {
      return undefined;
    }

    const container = this._store.publicIdentityFromId(id);
    return container
      ? Cu.cloneInto(this._toIdentity(container), {})
      : undefined;
  },

  formatContextLabel(l10nId) {
    const [msg] = lazy.l10n.formatMessagesSync([l10nId]);
    for (let attr of msg.attributes) {
      if (attr.name === "label") {
        return attr.value;
      }
    }
    return "";
  },

  getUserContextLabel(userContextId) {
    this.ensureDataReady();

    const id = this._containerId(userContextId);
    if (id == null) {
      return "";
    }

    const container = this._store.publicIdentityFromId(id);
    if (!container) {
      return "";
    }

    if (container.label instanceof lazy.ContainerLabel.Name) {
      return container.label.name;
    }

    const l10nId = lazy.labelGeckoL10nId(container.label);
    return l10nId ? this.formatContextLabel(l10nId) : "";
  },

  get containerColors() {
    return lazy.containerColors().map(color => lazy.colorName(color));
  },

  get containerIcons() {
    return lazy.containerIcons().map(icon => lazy.iconName(icon));
  },

  // Legacy color names accepted at the contextualIdentities WebExtension API
  // boundary, as a Map from each one to the canonical name that replaced it.
  get containerColorAliases() {
    return lazy.containerColorAliases();
  },

  // Maps a possibly-legacy color name to its canonical name. Unknown and
  // already-canonical names are returned as-is.  Only the contextualIdentities
  // WebExtension API should need this; internal callers always pass canonical
  // names.
  resolveContainerColor(color) {
    return lazy.resolveColor(color);
  },

  getContainerColorCode(color) {
    const value = lazy.colorFromName(color);
    if (value == null) {
      return null;
    }
    return lazy.colorCode(
      value,
      Services.prefs.getBoolPref("browser.nova.enabled", false)
    );
  },

  getContainerIconURL(icon) {
    const value = lazy.iconFromName(icon);
    if (value == null) {
      return null;
    }
    return `resource://usercontext-content/${lazy.iconName(value)}.svg`;
  },

  getContainerColorL10nId(color) {
    const value = lazy.colorFromName(color);
    return value == null ? null : lazy.colorGeckoL10nId(value);
  },

  getContainerIconL10nId(icon) {
    const value = lazy.iconFromName(icon);
    return value == null ? null : lazy.iconGeckoL10nId(value);
  },

  getContainerColorLabel(color) {
    let l10nId = this.getContainerColorL10nId(color);
    return l10nId ? this.formatContextLabel(l10nId) : "";
  },

  getContainerIconLabel(icon) {
    let l10nId = this.getContainerIconL10nId(icon);
    return l10nId ? this.formatContextLabel(l10nId) : "";
  },

  setTabStyle(tab) {
    if (!tab.hasAttribute("usercontextid")) {
      return;
    }

    let userContextId = tab.getAttribute("usercontextid");
    let identity = this.getPublicIdentityFromId(userContextId);

    /* Remove any existing container color/icon classes. */
    for (let className of [...tab.classList]) {
      if (
        className.startsWith("identity-color-") ||
        className.startsWith("identity-icon-")
      ) {
        tab.classList.remove(className);
      }
    }
    if (identity?.color) {
      tab.classList.add("identity-color-" + identity.color);
    }
    if (identity?.icon) {
      tab.classList.add("identity-icon-" + identity.icon);
    }
  },

  countContainerTabs(userContextId = 0) {
    let count = 0;
    this._forEachContainerTab(function () {
      ++count;
    }, userContextId);
    return count;
  },

  closeContainerTabs(userContextId = 0, removeTabOptions = {}) {
    return new Promise(resolve => {
      let remoteTabIds = new Set();
      this._forEachContainerTab((tab, tabbrowser) => {
        let frameLoader = tab.linkedBrowser.frameLoader;

        // We don't have remoteTab in non-e10s mode.
        if (frameLoader?.remoteTab) {
          remoteTabIds.add(frameLoader.remoteTab.tabId);
        }

        tabbrowser.removeTab(tab, removeTabOptions);
      }, userContextId);

      if (remoteTabIds.size == 0) {
        resolve();
        return;
      }

      new _TabRemovalObserver(resolve, remoteTabIds);
    });
  },

  notifyAllContainersCleared() {
    for (const userContextId of this.getPublicUserContextIds()) {
      Services.clearData.deleteDataFromOriginAttributesPattern({
        userContextId,
      });
    }
  },

  _forEachContainerTab(callback, userContextId = 0) {
    for (let win of Services.wm.getEnumerator("navigator:browser")) {
      if (win.closed || !win.gBrowser) {
        continue;
      }

      let tabbrowser = win.gBrowser;
      for (let i = tabbrowser.tabs.length - 1; i >= 0; --i) {
        let tab = tabbrowser.tabs[i];
        if (
          tab.hasAttribute("usercontextid") &&
          (!userContextId ||
            parseInt(tab.getAttribute("usercontextid"), 10) == userContextId)
        ) {
          callback(tab, tabbrowser);
        }
      }
    }
  },

  createNewInstanceForTesting(path) {
    return new _ContextualIdentityService(path);
  },

  deleteContainerData() {
    // The userContextId 0 is reserved to the default firefox identity,
    // and it should not be clear when we delete the public containers data.
    let minUserContextId = 1;

    // Collect the userContextId related to the identities that should not be cleared
    // (the ones marked as `public = false`).
    const keepDataContextIds = this.getPrivateUserContextIds();

    // Collect the userContextIds currently used by any stored cookie.
    let cookiesUserContextIds = new Set();

    for (let cookie of Services.cookies.cookies) {
      // Skip any userContextIds that should not be cleared.
      if (
        cookie.originAttributes.userContextId >= minUserContextId &&
        !keepDataContextIds.includes(cookie.originAttributes.userContextId)
      ) {
        cookiesUserContextIds.add(cookie.originAttributes.userContextId);
      }
    }

    for (let userContextId of cookiesUserContextIds) {
      Services.clearData.deleteDataFromOriginAttributesPattern({
        userContextId,
      });
    }
  },
};

let path = PathUtils.join(
  Services.dirsvc.get("ProfD", Ci.nsIFile).path,
  "containers.json"
);
export var ContextualIdentityService = new _ContextualIdentityService(path);

export function SiteContainerService() {
  return ContextualIdentityService;
}
