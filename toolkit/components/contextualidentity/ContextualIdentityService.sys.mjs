/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The maximum valid numeric value for the userContextId.
const MAX_USER_CONTEXT_ID = -1 >>> 0;
const LAST_CONTAINERS_JSON_VERSION = 8;
const SAVE_DELAY_MS = 1500;
const CONTEXTUAL_IDENTITY_ENABLED_PREF = "privacy.userContext.enabled";

// Canonical container colors and icons. Adding/renaming an entry here must be
// mirrored in (kept in sync by browser_container_definitions_in_sync.js):
//   - usercontext.css: .identity-color-* / .identity-icon-* rules
//   - the matching content/<icon>.svg file
//   - toolkit/global/contextual-identity.ftl: user-context-color-* /
//     user-context-icon-* labels
//   - enterprisepolicies/schemas/policies-schema.json: Containers.Default enums
// Reordering only affects the swatch order and is local to this file.
//
// Each color carries two codes: `code` is the legacy value used when
// `browser.nova.enabled` is off, `codeNova` is the refreshed value used when it
// is on. getContainerColorCode() picks between them based on the pref. Under
// nova `codeNova` matches the light-mode value of --identity-color in
// usercontext.css.
export const CONTAINER_COLORS = [
  {
    name: "gray",
    code: "#7c7c7d",
    codeNova: "#949297",
    l10nId: "user-context-color-gray",
  },
  {
    name: "yellow",
    code: "#ffcb00",
    codeNova: "#f3a81e",
    l10nId: "user-context-color-yellow",
  },
  {
    name: "orange",
    code: "#ff9f00",
    codeNova: "#f4682c",
    l10nId: "user-context-color-orange",
  },
  {
    name: "red",
    code: "#ff613d",
    codeNova: "#ed566e",
    l10nId: "user-context-color-red",
  },
  {
    name: "pink",
    code: "#ff4bda",
    codeNova: "#db54bf",
    l10nId: "user-context-color-pink",
  },
  {
    name: "purple",
    code: "#af51f5",
    codeNova: "#b864ee",
    l10nId: "user-context-color-purple",
  },
  {
    name: "violet",
    code: "#764edd",
    codeNova: "#764edd",
    l10nId: "user-context-color-violet",
  },
  {
    name: "blue",
    code: "#37adff",
    codeNova: "#455fe7",
    l10nId: "user-context-color-blue",
  },
  {
    name: "cyan",
    code: "#00c79a",
    codeNova: "#10a4ca",
    l10nId: "user-context-color-cyan",
  },
  {
    name: "green",
    code: "#51cd00",
    codeNova: "#008865",
    l10nId: "user-context-color-green",
  },
];

// Legacy container color names accepted only at the contextualIdentities
// WebExtension API boundary (see ext-contextualIdentities.js), for extensions
// and containers created before the color refresh. Each maps to the canonical
// name that replaced it in CONTAINER_COLORS. Resolved via
// resolveContainerColor(). Internal callers always use canonical names.
export const CONTAINER_COLOR_ALIASES = {
  __proto__: null,
  turquoise: "cyan",
  toolbar: "gray",
};

const CONTAINER_ICONS = [
  { name: "fingerprint", l10nId: "user-context-icon-fingerprint" },
  { name: "briefcase", l10nId: "user-context-icon-briefcase" },
  { name: "dollar", l10nId: "user-context-icon-dollar" },
  { name: "cart", l10nId: "user-context-icon-cart" },
  { name: "vacation", l10nId: "user-context-icon-vacation" },
  { name: "gift", l10nId: "user-context-icon-gift" },
  { name: "food", l10nId: "user-context-icon-food" },
  { name: "fruit", l10nId: "user-context-icon-fruit" },
  { name: "pet", l10nId: "user-context-icon-pet" },
  { name: "tree", l10nId: "user-context-icon-tree" },
  { name: "chill", l10nId: "user-context-icon-chill" },
  { name: "circle", l10nId: "user-context-icon-circle" },
  { name: "fence", l10nId: "user-context-icon-fence" },
];

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "l10n", function () {
  return new Localization(["toolkit/global/contextual-identity.ftl"], true);
});

ChromeUtils.defineLazyGetter(lazy, "gTextDecoder", function () {
  return new TextDecoder();
});

ChromeUtils.defineLazyGetter(lazy, "gTextEncoder", function () {
  return new TextEncoder();
});

ChromeUtils.defineLazyGetter(lazy, "idnService", function () {
  return Cc["@mozilla.org/network/idn-service;1"].getService(Ci.nsIIDNService);
});

ChromeUtils.defineESModuleGetters(lazy, {
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

  LAST_CONTAINERS_JSON_VERSION,

  _userIdentities: [
    {
      icon: "fingerprint",
      color: "blue",
      l10nId: "user-context-personal2",
    },
    {
      icon: "briefcase",
      color: "orange",
      l10nId: "user-context-work2",
    },
    {
      icon: "dollar",
      color: "green",
      l10nId: "user-context-banking2",
    },
    {
      icon: "cart",
      color: "pink",
      l10nId: "user-context-shopping2",
    },
  ],
  _systemIdentities: [
    {
      public: false,
      icon: "",
      color: "",
      name: "userContextIdInternal.thumbnail",
      accessKey: "",
    },
    // This userContextId is used by ExtensionStorageIDB.sys.mjs to create an IndexedDB database
    // opened with the extension principal but not directly accessible to the extension code
    // (do not change the userContextId assigned here, otherwise the installed extensions will
    // not be able to access the data previously stored with the browser.storage.local API).
    {
      userContextId: MAX_USER_CONTEXT_ID,
      public: false,
      icon: "",
      color: "",
      name: "userContextIdInternal.webextStorageLocal",
      accessKey: "",
    },
  ],

  _defaultIdentities: [],

  _identities: null,
  _openedIdentities: new Set(),
  _lastUserContextId: 0,

  // Map<host, userContextId> binding an exact host to a container.
  _siteAssociations: null,

  _path: null,
  _dataReady: false,

  _saver: null,

  init(path) {
    this._path = path;

    Services.prefs.addObserver(CONTEXTUAL_IDENTITY_ENABLED_PREF, this);

    // Initialize default identities based on policy if available
    this._defaultIdentities = [];
    let userContextId = 1;
    let policyIdentities =
      Services.policies?.getActivePolicies()?.Containers?.Default;
    if (policyIdentities) {
      for (let identity of policyIdentities) {
        identity.public = true;
        identity.userContextId = userContextId;
        userContextId++;
        this._defaultIdentities.push(identity);
      }
    } else {
      for (let identity of this._userIdentities) {
        identity.public = true;
        identity.userContextId = userContextId;
        userContextId++;
        this._defaultIdentities.push(identity);
      }
    }
    for (let identity of this._systemIdentities) {
      if (!("userContextId" in identity)) {
        identity.userContextId = userContextId;
        userContextId++;
      }
      this._defaultIdentities.push(identity);
    }
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
        // If synchronous loading happened in the meantime, exit now.
        if (this._dataReady) {
          return;
        }

        try {
          this.parseData(bytes);
        } catch (error) {
          this.loadError(error);
        }
      },
      error => {
        this.loadError(error);
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

  resetDefault() {
    this._identities = [];

    // Compute the last used context id (excluding the reserved userContextIds
    // "userContextIdInternal.webextStorageLocal" which has UINT32_MAX as its
    // userContextId).
    this._lastUserContextId = this._defaultIdentities
      .filter(identity => identity.userContextId < MAX_USER_CONTEXT_ID)
      .map(identity => identity.userContextId)
      .sort((a, b) => a >= b)
      .pop();

    // Clone the array
    for (let identity of this._defaultIdentities) {
      let stored = Object.assign({}, identity);
      delete stored.l10nId;
      this._identities.push(stored);
    }
    this._openedIdentities = new Set();
    this._siteAssociations = new Map();

    this._dataReady = true;

    // Let's delete all the data of any userContextId. 1 is the first valid
    // userContextId value.
    this.deleteContainerData();

    this.saveSoon();
  },

  loadError(error) {
    if (error != null && error.name != "NotFoundError") {
      // Let's report the error.
      console.error(error);
    }

    // If synchronous loading happened in the meantime, exit now.
    if (this._dataReady) {
      return;
    }

    this.resetDefault();
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

    let object = {
      version: LAST_CONTAINERS_JSON_VERSION,
      lastUserContextId: this._lastUserContextId,
      identities: this._identities,
      siteAssociations: Object.fromEntries(this._siteAssociations),
    };

    let bytes = lazy.gTextEncoder.encode(JSON.stringify(object));
    return IOUtils.write(this._path, bytes, {
      tmpPath: this._path + ".tmp",
    });
  },

  create(name, icon, color) {
    this.ensureDataReady();

    // Retrieve the next userContextId available.
    let userContextId = ++this._lastUserContextId;

    // Throw an error if the next userContextId available is invalid (the one associated to
    // MAX_USER_CONTEXT_ID is already reserved to "userContextIdInternal.webextStorageLocal", which
    // is also the last valid userContextId available, because its userContextId is equal to UINT32_MAX).
    if (userContextId >= MAX_USER_CONTEXT_ID) {
      throw new Error(
        `Unable to create a new userContext with id '${userContextId}'`
      );
    }

    if (!name.trim()) {
      throw new Error(
        "Contextual identity names cannot contain only whitespace."
      );
    }

    let identity = {
      userContextId,
      public: true,
      icon,
      color,
      name,
    };

    this._identities.push(identity);
    this.saveSoon();
    Services.obs.notifyObservers(
      this.getIdentityObserverOutput(identity),
      "contextual-identity-created"
    );

    Glean.containers.containerCreated.record({
      container_id: userContextId.toString(),
    });

    return Cu.cloneInto(identity, {});
  },

  createForPolicy(policyId) {
    this.ensureDataReady();

    let userContextId = ++this._lastUserContextId;

    if (userContextId >= MAX_USER_CONTEXT_ID) {
      throw new Error(
        `Unable to create a new userContext with id '${userContextId}'`
      );
    }

    let identity = {
      userContextId,
      public: false,
      name: policyId,
      policy: true,
      policyId,
    };

    this._identities.push(identity);
    this.saveSoon();

    return Cu.cloneInto(identity, {});
  },

  removePolicyIdentity(userContextId) {
    this.ensureDataReady();

    let index = this._identities.findIndex(
      i => i.userContextId == userContextId && i.policy
    );
    if (index == -1) {
      return false;
    }

    Services.clearData.deleteDataFromOriginAttributesPattern({ userContextId });
    this._identities.splice(index, 1);
    this.saveSoon();

    return true;
  },

  getPolicyIdentities() {
    this.ensureDataReady();

    return Cu.cloneInto(
      this._identities.filter(info => info.policy),
      {}
    );
  },

  update(userContextId, name, icon, color) {
    this.ensureDataReady();

    let identity = this._identities.find(
      i => i.userContextId == userContextId && i.public
    );

    if (!name.trim()) {
      throw new Error(
        "Contextual identity names cannot contain only whitespace."
      );
    }

    if (identity && name) {
      identity.name = name;
      identity.color = color;
      identity.icon = icon;

      this.saveSoon();
      Services.obs.notifyObservers(
        this.getIdentityObserverOutput(identity),
        "contextual-identity-updated"
      );
    }

    Glean.containers.containerModified.record({
      container_id: userContextId.toString(),
    });

    return !!identity;
  },

  move(userContextIds, position) {
    this.ensureDataReady();

    if (position < -1) {
      return false;
    }
    let movedIdentities = this._identities.filter(
      identity =>
        identity.public && userContextIds.includes(identity.userContextId)
    );

    if (movedIdentities.length) {
      if (position === -1) {
        position = this._identities.length;
      }
      // Shift position by preceding non-public identities
      position = this._identities.reduce(
        (dest, identity, idx) =>
          dest + (!identity.public && dest >= idx ? 1 : 0),
        position
      );
      this._identities = this._identities.filter(
        identity =>
          !identity.public || !userContextIds.includes(identity.userContextId)
      );
      this._identities.splice(position, 0, ...movedIdentities);
      this.saveSoon();
      Services.obs.notifyObservers(null, "contextual-identity-reordered");
    }

    return !!movedIdentities.length;
  },

  remove(userContextId) {
    this.ensureDataReady();

    let index = this._identities.findIndex(
      i => i.userContextId == userContextId && i.public
    );
    if (index == -1) {
      return false;
    }

    Services.clearData.deleteDataFromOriginAttributesPattern({ userContextId });

    let deletedOutput = this.getIdentityObserverOutput(
      this.getPublicIdentityFromId(userContextId)
    );
    this._identities.splice(index, 1);
    this._openedIdentities.delete(userContextId);
    this._removeSiteAssociationsForContainer(userContextId);
    this.saveSoon();
    Services.obs.notifyObservers(deletedOutput, "contextual-identity-deleted");

    Glean.containers.containerDeleted.record({
      container_id: userContextId.toString(),
    });

    return true;
  },

  // Returns the normalized host (lower case, IDN-encoded) for `site`, or null
  // if `site` cannot be associated to a container.
  normalizeSite(site) {
    try {
      // domainToASCII does not reject "*", but a host never contains one, so a
      // wildcard site could never match a navigation.
      let host = lazy.idnService.domainToASCII(site);
      return host.includes("*") ? null : host;
    } catch (e) {
      return null;
    }
  },

  _notifySiteAssociationChanged(site, userContextId) {
    Services.obs.notifyObservers(
      { wrappedJSObject: { site, userContextId } },
      "contextual-identity-site-association-changed"
    );
  },

  _removeSiteAssociationsForContainer(userContextId) {
    let removed = [];
    for (let [site, id] of this._siteAssociations) {
      if (id === userContextId) {
        this._siteAssociations.delete(site);
        removed.push(site);
      }
    }

    for (let site of removed) {
      this._notifySiteAssociationChanged(site, null);
    }
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

    let host = this.normalizeSite(site);
    if (!host) {
      throw new Error("Invalid site for container association.");
    }

    if (this._siteAssociations.get(host) === userContextId) {
      return;
    }

    this._siteAssociations.set(host, userContextId);
    this.saveSoon();
    this._notifySiteAssociationChanged(host, userContextId);
  },

  removeSiteAssociation(site) {
    this.ensureDataReady();

    let host = this.normalizeSite(site);
    if (!host || !this._siteAssociations.has(host)) {
      return;
    }

    this._siteAssociations.delete(host);
    this.saveSoon();
    this._notifySiteAssociationChanged(host, null);
  },

  getSiteAssociation(site) {
    this.ensureDataReady();

    let host = this.normalizeSite(site);
    return (host && this._siteAssociations.get(host)) || 0;
  },

  getSiteAssociations(userContextId = 0) {
    this.ensureDataReady();

    let result = [];
    for (let [site, id] of this._siteAssociations) {
      if (!userContextId || id === userContextId) {
        result.push({ site, userContextId: id });
      }
    }
    return result;
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

    let result = this.getSiteAssociation(host);
    if (result) {
      return result;
    }

    return baselineUserContextId;
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

  parseData(bytes) {
    let data = JSON.parse(lazy.gTextDecoder.decode(bytes));
    if (data.version == 1) {
      this.resetDefault();
      return;
    }

    let saveNeeded = false;

    if (data.version == 2) {
      data = this.migrate2to3(data);
      saveNeeded = true;
    }

    if (data.version == 3) {
      data = this.migrate3to4(data);
      saveNeeded = true;
    }

    if (data.version == 4) {
      data = this.migrate4to5(data);
      saveNeeded = true;
    }

    if (data.version == 5) {
      data = this.migrate5to6(data);
      saveNeeded = true;
    }

    if (data.version == 6) {
      data = this.migrate6to7(data);
      saveNeeded = true;
    }

    if (data.version == 7) {
      data = this.migrate7to8(data);
      saveNeeded = true;
    }

    if (data.version != LAST_CONTAINERS_JSON_VERSION) {
      dump(
        "ERROR - ContextualIdentityService - Unknown version found in " +
          this._path +
          "\n"
      );
      this.loadError(null);
      return;
    }

    this._identities = data.identities;
    this._lastUserContextId = data.lastUserContextId;

    this._siteAssociations = new Map(
      Object.entries(data.siteAssociations ?? {})
    );

    // If we had a migration, let's force the saving of the file.
    if (saveNeeded) {
      this.saveSoon();
    }

    this._dataReady = true;
  },

  ensureDataReady() {
    if (this._dataReady) {
      return;
    }

    try {
      // Check if the containers.json file exists before attempting to read
      // from it (to avoid a warning in the console). If the file is missing,
      // reset the service to its default state and return early.
      let file = new lazy.FileUtils.File(this._path);
      if (!file.exists()) {
        this.resetDefault();
        return;
      }
      // This reads the file and automatically detects the UTF-8 encoding.
      let inputStream = Cc[
        "@mozilla.org/network/file-input-stream;1"
      ].createInstance(Ci.nsIFileInputStream);
      inputStream.init(
        file,
        lazy.FileUtils.MODE_RDONLY,
        lazy.FileUtils.PERMS_FILE,
        0
      );
      try {
        let bytes = lazy.NetUtil.readInputStream(
          inputStream,
          inputStream.available()
        );
        this.parseData(bytes);
      } finally {
        inputStream.close();
      }
    } catch (error) {
      this.loadError(error);
    }
  },

  getPublicUserContextIds() {
    return this._identities
      .filter(identity => identity.public)
      .map(identity => identity.userContextId);
  },

  getPrivateUserContextIds() {
    return this._identities
      .filter(identity => !identity.public)
      .map(identity => identity.userContextId);
  },

  getPublicIdentities() {
    this.ensureDataReady();
    return Cu.cloneInto(
      this._identities.filter(info => info.public),
      {}
    );
  },

  getPrivateIdentity(name) {
    this.ensureDataReady();
    return Cu.cloneInto(
      this._identities.find(info => !info.public && info.name == name),
      {}
    );
  },

  // getDefaultPrivateIdentity is similar to getPrivateIdentity but it only looks in the
  // default identities (e.g. it is used in the data migration methods to retrieve a new default
  // private identity and add it to the containers data stored on file).
  getDefaultPrivateIdentity(name) {
    return Cu.cloneInto(
      this._defaultIdentities.find(info => !info.public && info.name == name),
      {}
    );
  },

  getPublicIdentityFromId(userContextId) {
    this.ensureDataReady();
    return Cu.cloneInto(
      this._identities.find(
        info => info.userContextId == userContextId && info.public
      ),
      {}
    );
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
    let identity = this.getPublicIdentityFromId(userContextId);
    if (!identity) {
      return "";
    }

    // We cannot localize the user-created identity names.
    if (identity.name) {
      return identity.name;
    }

    let l10nId = this._defaultIdentities.find(
      info => info.public && info.userContextId == userContextId
    )?.l10nId;
    return l10nId ? this.formatContextLabel(l10nId) : "";
  },

  get containerColors() {
    return CONTAINER_COLORS.map(color => color.name);
  },

  get containerIcons() {
    return CONTAINER_ICONS.map(icon => icon.name);
  },

  // Maps a possibly-legacy color name to its canonical name. Unknown and
  // already-canonical names are returned as-is.  Only the contextualIdentities
  // WebExtension API should need this; internal callers always pass canonical
  // names.
  resolveContainerColor(color) {
    return CONTAINER_COLOR_ALIASES[color] ?? color;
  },

  getContainerColorCode(color) {
    let entry = CONTAINER_COLORS.find(e => e.name === color);
    if (!entry) {
      return null;
    }
    return Services.prefs.getBoolPref("browser.nova.enabled", false)
      ? entry.codeNova
      : entry.code;
  },

  getContainerIconURL(icon) {
    if (!CONTAINER_ICONS.some(entry => entry.name === icon)) {
      return null;
    }
    return `resource://usercontext-content/${icon}.svg`;
  },

  getContainerColorL10nId(color) {
    return CONTAINER_COLORS.find(entry => entry.name === color)?.l10nId ?? null;
  },

  getContainerIconL10nId(icon) {
    return CONTAINER_ICONS.find(entry => entry.name === icon)?.l10nId ?? null;
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
    for (let identity of this._identities) {
      // Don't clear the data related to private identities (e.g. the one used internally
      // for the thumbnails and the one used for the storage.local IndexedDB backend).
      if (!identity.public) {
        continue;
      }
      Services.clearData.deleteDataFromOriginAttributesPattern({
        userContextId: identity.userContextId,
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

  migrate2to3(data) {
    // migrating from 2 to 3 is basically just increasing the version id.
    // This migration was needed for bug 1419591. See bug 1419591 to know more.
    data.version = 3;

    return data;
  },

  migrate3to4(data) {
    // Migrating from 3 to 4 is:
    // - adding the reserver userContextId used by the webextension storage.local API
    // - add the keepData property to all the existent identities
    // - increasing the version id.
    //
    // This migration was needed for Bug 1406181. See bug 1406181 for rationale.
    const webextStorageLocalIdentity = this.getDefaultPrivateIdentity(
      "userContextIdInternal.webextStorageLocal"
    );

    data.identities.push(webextStorageLocalIdentity);

    data.version = 4;

    return data;
  },

  migrate4to5(data) {
    // Migrating from 4 to 5 is:
    // - replacing the StringBundle l10nID with a Fluent l10nId for default identities
    // - dropping the accessKey property
    // - increasing the version id.
    //
    // This migration was needed for Bug 1814969. See bug 1814969 for rationale.

    for (let identity of data.identities) {
      switch (identity.l10nID) {
        case "userContextPersonal.label":
          identity.l10nId = "user-context-personal2";
          break;
        case "userContextWork.label":
          identity.l10nId = "user-context-work2";
          break;
        case "userContextBanking.label":
          identity.l10nId = "user-context-banking2";
          break;
        case "userContextShopping.label":
          identity.l10nId = "user-context-shopping2";
          break;
      }
      delete identity.l10nID;
      delete identity.accessKey;
    }

    data.version = 5;

    return data;
  },

  migrate5to6(data) {
    // Migrating from 5 to 6 is:
    // - renaming the refreshed container colors, so stored identities only use
    //   canonical names
    // - increasing the version id.
    for (let identity of data.identities) {
      if (identity.color) {
        identity.color = this.resolveContainerColor(identity.color);
      }
    }

    data.version = 6;

    return data;
  },

  migrate6to7(data) {
    // Migrating from 6 to 7 is:
    // - renaming the default identities' Fluent ids, which Bug 2071753
    //   changed when it dropped the accesskeys
    // - increasing the version id.
    const renamedL10nIds = {
      "user-context-personal": "user-context-personal2",
      "user-context-work": "user-context-work2",
      "user-context-banking": "user-context-banking2",
      "user-context-shopping": "user-context-shopping2",
    };
    for (let identity of data.identities) {
      if (Object.hasOwn(renamedL10nIds, identity.l10nId)) {
        identity.l10nId = renamedL10nIds[identity.l10nId];
      }
    }

    data.version = 7;

    return data;
  },

  migrate7to8(data) {
    // Migrating from 7 to 8 is:
    // - dropping the l10nId property; the default identities' Fluent ids are
    //   read from _defaultIdentities
    // - increasing the version id.
    for (let identity of data.identities) {
      delete identity.l10nId;
    }

    data.version = 8;

    return data;
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
