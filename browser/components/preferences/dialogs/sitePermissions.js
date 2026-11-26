/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from ../extensionControlled.js */
/* import-globals-from permissionContainers.js */

var { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { SitePermissions } = ChromeUtils.importESModule(
  "resource:///modules/SitePermissions.sys.mjs"
);
var { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  isTrailingDotPolicyDuplicate:
    "resource://gre/modules/PoliciesHelpers.sys.mjs",
  PushNotificationHelper:
    "resource://gre/modules/PushNotificationHelper.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  this,
  "SiteCategory",
  "@mozilla.org/site-category;1",
  Ci.nsISiteCategory
);

const sitePermissionsL10n = {
  "desktop-notification": {
    window: "permissions-site-notification-window2",
    description: "permissions-site-notification-desc",
    disableLabel: "permissions-site-notification-disable-label",
    disableDescription: "permissions-site-notification-disable-desc",
  },
  geo: {
    window: "permissions-site-location-window2",
    description: "permissions-site-location-desc",
    disableLabel: "permissions-site-location-disable-label",
    disableDescription: "permissions-site-location-disable-desc",
  },
  xr: {
    window: "permissions-site-xr-window2",
    description: "permissions-site-xr-desc",
    disableLabel: "permissions-site-xr-disable-label",
    disableDescription: "permissions-site-xr-disable-desc",
  },
  "register-protocol-handler": {
    window: "permissions-site-app-links-window",
    description: "permissions-site-app-links-desc",
  },
  camera: {
    window: "permissions-site-camera-window2",
    description: "permissions-site-camera-desc",
    disableLabel: "permissions-site-camera-disable-label",
    disableDescription: "permissions-site-camera-disable-desc",
  },
  microphone: {
    window: "permissions-site-microphone-window2",
    description: "permissions-site-microphone-desc",
    disableLabel: "permissions-site-microphone-disable-label",
    disableDescription: "permissions-site-microphone-disable-desc",
  },
  speaker: {
    window: "permissions-site-speaker-window",
    description: "permissions-site-speaker-desc",
  },
  "autoplay-media": {
    window: "permissions-site-autoplay-window2",
    description: "permissions-site-autoplay-desc",
  },
  "loopback-network": {
    window: "permissions-site-localhost-window",
    description: "permissions-site-localhost-desc",
    disableLabel: "permissions-site-localhost-disable-label",
    disableDescription: "permissions-site-localhost-disable-desc",
  },
  "local-network": {
    window: "permissions-site-local-network-window",
    description: "permissions-site-local-network-desc",
    disableLabel: "permissions-site-local-network-disable-label",
    disableDescription: "permissions-site-local-network-disable-desc",
  },
};

const sitePermissionsConfig = {
  "autoplay-media": {
    _getCapabilityString(capability) {
      switch (capability) {
        case SitePermissions.ALLOW:
          return "permissions-capabilities-autoplay-allow";
        case SitePermissions.BLOCK:
          return "permissions-capabilities-autoplay-block";
        case SitePermissions.AUTOPLAY_BLOCKED_ALL:
          return "permissions-capabilities-autoplay-blockall";
      }
      throw new Error(`Unknown capability: ${capability}`);
    },
  },
};

// A set of permissions for a single origin.  One PermissionGroup instance
// corresponds to one row in the gSitePermissionsManager._list richlistbox.
// Permissions may be single or double keyed, but the primary key of all
// permissions matches the permission type of the dialog.
class PermissionGroup {
  #changedCapability;

  constructor(perm) {
    this.principal = perm.principal;
    this.origin = perm.principal.origin;
    this.perms = [perm];
  }
  addPermission(perm) {
    this.perms.push(perm);
  }
  removePermission(perm) {
    this.perms = this.perms.filter(p => p.type != perm.type);
  }
  set capability(cap) {
    this.#changedCapability = cap;
  }
  get capability() {
    if (this.#changedCapability) {
      return this.#changedCapability;
    }
    return this.savedCapability;
  }
  revert() {
    this.#changedCapability = null;
  }
  // The origin to show in the website column, which leaves out the container
  // it is isolated in.
  get displayOrigin() {
    return gPermissionContainers.displayOriginFor(this.principal);
  }
  get containerLabel() {
    return gPermissionContainers.labelFor(this.principal);
  }
  get savedCapability() {
    // This logic to present a single capability for permissions of different
    // keys and capabilities caters for speaker-selection, where a block
    // permission may be set for all devices with no second key, which would
    // override any device-specific double-keyed allow permissions.
    let cap;
    for (let perm of this.perms) {
      let [type] = perm.type.split(SitePermissions.PERM_KEY_DELIMITER);
      if (type == perm.type) {
        // No second key.  This overrides double-keyed perms.
        return perm.capability;
      }
      // Double-keyed perms are not expected to have different capabilities.
      cap = perm.capability;
    }
    return cap;
  }
}

const PERMISSION_STATES = [
  SitePermissions.ALLOW,
  SitePermissions.BLOCK,
  SitePermissions.PROMPT,
  SitePermissions.AUTOPLAY_BLOCKED_ALL,
];

const NOTIFICATIONS_PERMISSION_OVERRIDE_KEY = "webNotificationsDisabled";
const NOTIFICATIONS_PERMISSION_PREF =
  "permissions.default.desktop-notification";

const AUTOPLAY_PREF = "media.autoplay.default";

const CLOSED_DELIVERY_AVAILABLE_PREF =
  "app.backgroundNotifications.helper.available";
const CLOSED_DELIVERY_ENABLED_PREF =
  "app.backgroundNotifications.helper.enabled";

var gSitePermissionsManager = {
  _type: "",
  _isObserving: false,
  _permissionGroups: new Map(),
  _permissionsToChange: new Map(),
  _permissionsToDelete: new Map(),
  _list: null,
  _removeButton: null,
  _removeAllButton: null,
  _searchBox: null,
  _checkbox: null,
  _currentDefaultPermissionsState: null,
  _defaultPermissionStatePrefName: null,
  _showContainerColumn: false,

  onLoad() {
    let params = window.arguments[0];
    document.mozSubdialogReady = this.init(params);
  },

  async init(params) {
    if (!this._isObserving) {
      Services.obs.addObserver(this, "perm-changed");
      this._isObserving = true;
    }

    document.addEventListener("command", this);
    document.addEventListener("dialogaccept", this);
    window.addEventListener("unload", this);

    document
      .getElementById("siteCol")
      .addEventListener("click", event =>
        this.buildPermissionsList(event.target)
      );
    document
      .getElementById("statusCol")
      .addEventListener("click", event =>
        this.buildPermissionsList(event.target)
      );
    document
      .getElementById("containerCol")
      .addEventListener("click", event =>
        this.buildPermissionsList(event.target)
      );

    this._type = params.permissionType;
    this._list = document.getElementById("permissionsBox");
    this._removeButton = document.getElementById("removePermission");
    this._removeAllButton = document.getElementById("removeAllPermissions");
    this._searchBox = document.getElementById("searchBox");
    this._searchBox.addEventListener("MozInputSearch:search", () =>
      this.buildPermissionsList()
    );
    this._checkbox = document.getElementById("permissionsDisableCheckbox");
    this._disableExtensionButton = document.getElementById(
      "disableNotificationsPermissionExtension"
    );
    this._permissionsDisableDescription = document.getElementById(
      "permissionsDisableDescription"
    );
    this._setAutoplayPref = document.getElementById("setAutoplayPref");
    this._closedDeliveryBox = document.getElementById("closedDeliveryBox");
    this._closedDeliveryCheckbox = document.getElementById(
      "closedDeliveryCheckbox"
    );

    this._list.addEventListener("keypress", event =>
      this.onPermissionKeyPress(event)
    );
    this._list.addEventListener("select", () => this.onPermissionSelect());

    let permissionsText = document.getElementById("permissionsText");

    document.l10n.pauseObserving();
    let l10n = sitePermissionsL10n[this._type];
    document.l10n.setAttributes(permissionsText, l10n.description);
    if (l10n.disableLabel) {
      document.l10n.setAttributes(this._checkbox, l10n.disableLabel);
    } else {
      this._checkbox.hidden = true;
      this._permissionsDisableDescription.hidden = true;
    }
    if (l10n.disableDescription) {
      document.l10n.setAttributes(
        this._permissionsDisableDescription,
        l10n.disableDescription
      );
    }
    document.l10n.setAttributes(document.documentElement, l10n.window);

    await document.l10n.translateElements([
      permissionsText,
      this._checkbox,
      this._permissionsDisableDescription,
      document.documentElement,
    ]);
    document.l10n.resumeObserving();

    // Initialize the checkbox state and handle showing notification permission UI
    // when it is disabled by an extension.
    this._defaultPermissionStatePrefName = "permissions.default." + this._type;
    this._watchPermissionPrefChange();
    if (this._type == "desktop-notification") {
      this._watchClosedDeliveryPrefs();
    }

    this._loadPermissions();
    this.buildPermissionsList();

    if (params.permissionType == "autoplay-media") {
      await this.buildAutoplayMenulist();
      this._setAutoplayPref.hidden = false;
    }

    this._searchBox.focus();
  },

  uninit() {
    if (this._isObserving) {
      Services.obs.removeObserver(this, "perm-changed");
      this._isObserving = false;
    }
    if (this._setAutoplayPref) {
      this._setAutoplayPref.hidden = true;
    }
  },

  observe(subject, topic, data) {
    if (topic !== "perm-changed") {
      return;
    }

    if (data === "cleared") {
      this._permissionGroups.clear();
      this._permissionsToChange.clear();
      this._permissionsToDelete.clear();
      this._loadPermissions();
      this.buildPermissionsList();
      return;
    }

    let permission = subject.QueryInterface(Ci.nsIPermission);
    let [type] = permission.type.split(SitePermissions.PERM_KEY_DELIMITER);

    // Ignore unrelated permission types and permissions with unknown states.
    if (
      type !== this._type ||
      !PERMISSION_STATES.includes(permission.capability)
    ) {
      return;
    }

    if (data == "added") {
      this._addPermissionToList(permission);
    } else {
      let group = this._permissionGroups.get(permission.principal.origin);
      if (!group) {
        // already moved to _permissionsToDelete
        // or private browsing session permission
        return;
      }
      if (data == "changed") {
        group.removePermission(permission);
        group.addPermission(permission);
      } else if (data == "deleted") {
        group.removePermission(permission);
        if (!group.perms.length) {
          this._removePermissionFromList(permission.principal.origin);
          return;
        }
      }
    }
    this.buildPermissionsList();
  },

  handleEvent(event) {
    switch (event.type) {
      case "command":
        switch (event.target.id) {
          case "key_close":
            window.close();
            break;
          case "removePermission":
            this.onPermissionDelete();
            break;
          case "removeAllPermissions":
            this.onAllPermissionsDelete();
            break;
        }
        break;
      case "dialogaccept":
        this.onApplyChanges();
        break;
      case "unload":
        this.uninit();
        break;
    }
  },

  _handleCheckboxUIUpdates() {
    let pref = Services.prefs.getPrefType(this._defaultPermissionStatePrefName);
    if (pref != Services.prefs.PREF_INVALID) {
      this._currentDefaultPermissionsState = Services.prefs.getIntPref(
        this._defaultPermissionStatePrefName
      );
    }

    if (this._currentDefaultPermissionsState === null) {
      this._checkbox.hidden = true;
      this._permissionsDisableDescription.hidden = true;
    } else if (this._currentDefaultPermissionsState == SitePermissions.BLOCK) {
      this._checkbox.checked = true;
    } else {
      this._checkbox.checked = false;
    }

    if (Services.prefs.prefIsLocked(this._defaultPermissionStatePrefName)) {
      this._checkbox.disabled = true;
    }
  },

  /**
   * Listen for changes to the permissions.default.* pref and make
   * necessary changes to the UI.
   */
  _watchPermissionPrefChange() {
    this._handleCheckboxUIUpdates();

    if (this._type == "desktop-notification") {
      this._handleWebNotificationsDisable();

      this._disableExtensionButton.addEventListener(
        "command",
        makeDisableControllingExtension(
          PREF_SETTING_TYPE,
          NOTIFICATIONS_PERMISSION_OVERRIDE_KEY
        )
      );
    }

    let observer = () => {
      this._handleCheckboxUIUpdates();
      if (this._type == "desktop-notification") {
        this._handleWebNotificationsDisable();
      }
    };
    Services.prefs.addObserver(this._defaultPermissionStatePrefName, observer);
    window.addEventListener("unload", () => {
      Services.prefs.removeObserver(
        this._defaultPermissionStatePrefName,
        observer
      );
    });
  },

  /**
   * Shows the closed-browser delivery checkbox only on Windows, where
   * PushNotificationHelper is packaged, and where Nimbus has made the feature
   * available. Keeps it in step with writes made elsewhere, such as from
   * messaging, while the dialog is open.
   */
  _watchClosedDeliveryPrefs() {
    if (AppConstants.platform != "win") {
      return;
    }
    let updateVisibility = () => {
      this._closedDeliveryBox.hidden = !Services.prefs.getBoolPref(
        CLOSED_DELIVERY_AVAILABLE_PREF,
        false
      );
    };
    let updateChecked = () => {
      this._closedDeliveryCheckbox.checked = Services.prefs.getBoolPref(
        CLOSED_DELIVERY_ENABLED_PREF,
        false
      );
    };
    updateVisibility();
    updateChecked();

    Services.prefs.addObserver(
      CLOSED_DELIVERY_AVAILABLE_PREF,
      updateVisibility
    );
    Services.prefs.addObserver(CLOSED_DELIVERY_ENABLED_PREF, updateChecked);
    window.addEventListener("unload", () => {
      Services.prefs.removeObserver(
        CLOSED_DELIVERY_AVAILABLE_PREF,
        updateVisibility
      );
      Services.prefs.removeObserver(
        CLOSED_DELIVERY_ENABLED_PREF,
        updateChecked
      );
    });
  },

  /**
   * Handles the UI update for web notifications disable by extensions.
   */
  async _handleWebNotificationsDisable() {
    let prefLocked = Services.prefs.prefIsLocked(NOTIFICATIONS_PERMISSION_PREF);
    if (prefLocked) {
      // An extension can't control these settings if they're locked.
      hideControllingExtension(NOTIFICATIONS_PERMISSION_OVERRIDE_KEY);
    } else {
      let isControlled = await handleControllingExtension(
        PREF_SETTING_TYPE,
        NOTIFICATIONS_PERMISSION_OVERRIDE_KEY
      );
      this._checkbox.disabled = isControlled;
    }
  },

  _getCapabilityL10nId(element, type, capability) {
    if (
      type in sitePermissionsConfig &&
      sitePermissionsConfig[type]._getCapabilityString
    ) {
      return sitePermissionsConfig[type]._getCapabilityString(capability);
    }
    switch (element.tagName) {
      case "menuitem":
        switch (capability) {
          case Services.perms.ALLOW_ACTION:
            return "permissions-capabilities-allow";
          case Services.perms.DENY_ACTION:
            return "permissions-capabilities-block";
          case Services.perms.PROMPT_ACTION:
            return "permissions-capabilities-prompt";
          default:
            throw new Error(`Unknown capability: ${capability}`);
        }
      case "label":
        switch (capability) {
          case Services.perms.ALLOW_ACTION:
            return "permissions-capabilities-listitem-allow";
          case Services.perms.DENY_ACTION:
            return "permissions-capabilities-listitem-block";
          default:
            throw new Error(`Unexpected capability: ${capability}`);
        }
      default:
        throw new Error(`Unexpected tag: ${element.tagName}`);
    }
  },

  _addPermissionToList(perm) {
    let [type] = perm.type.split(SitePermissions.PERM_KEY_DELIMITER);
    // Ignore unrelated permission types and permissions with unknown states.
    if (
      type !== this._type ||
      !PERMISSION_STATES.includes(perm.capability) ||
      !SitePermissions.isSupportedPrincipal(perm.principal) ||
      (perm.expireType === Services.perms.EXPIRE_POLICY &&
        lazy.isTrailingDotPolicyDuplicate(perm)) ||
      // Skip private browsing session permissions
      (perm.principal.privateBrowsingId !==
        Services.scriptSecurityManager.DEFAULT_PRIVATE_BROWSING_ID &&
        perm.expireType === Services.perms.EXPIRE_SESSION)
    ) {
      return;
    }
    let group = this._permissionGroups.get(perm.principal.origin);
    if (group) {
      group.addPermission(perm);
    } else {
      group = new PermissionGroup(perm);
      this._permissionGroups.set(group.origin, group);
    }
  },

  _removePermissionFromList(origin) {
    this._permissionGroups.delete(origin);
    this._permissionsToChange.delete(origin);
    let permissionlistitem = document.getElementsByAttribute(
      "origin",
      origin
    )[0];
    if (permissionlistitem) {
      permissionlistitem.remove();
    }
  },

  _loadPermissions() {
    // load permissions into a table.
    for (let nextPermission of Services.perms.all) {
      this._addPermissionToList(nextPermission);
    }
  },

  _createPermissionListItem(permissionGroup) {
    let richlistitem = document.createXULElement("richlistitem");
    richlistitem.setAttribute("origin", permissionGroup.origin);
    richlistitem.setAttribute("displayorigin", permissionGroup.displayOrigin);
    richlistitem.setAttribute("containerlabel", permissionGroup.containerLabel);
    let row = document.createXULElement("hbox");

    if (this._showContainerColumn) {
      row.appendChild(
        gPermissionContainers.createCell(document, permissionGroup.principal)
      );
    }

    let website = document.createXULElement("label");
    website.textContent = permissionGroup.displayOrigin;
    website.className = "website-name";

    let states = SitePermissions.getAvailableStates(this._type).filter(
      state => state != SitePermissions.UNKNOWN
    );
    // Handle the cases of a double-keyed ALLOW permission or a PROMPT
    // permission after the default has been changed back to UNKNOWN.
    if (!states.includes(permissionGroup.savedCapability)) {
      states.unshift(permissionGroup.savedCapability);
    }
    let siteStatus;
    if (states.length == 1) {
      // Only a single state is available.  Show a label.
      siteStatus = document.createXULElement("label");
      document.l10n.setAttributes(
        siteStatus,
        this._getCapabilityL10nId(
          siteStatus,
          this._type,
          permissionGroup.capability
        )
      );
    } else {
      // Multiple states are available.  Show a menulist.
      siteStatus = document.createXULElement("menulist");
      for (let state of states) {
        let m = siteStatus.appendItem(undefined, state);
        document.l10n.setAttributes(
          m,
          this._getCapabilityL10nId(m, this._type, state)
        );
      }
      siteStatus.addEventListener("select", () => {
        this.onPermissionChange(permissionGroup, Number(siteStatus.value));
      });
    }
    siteStatus.className = "website-status";
    siteStatus.value = permissionGroup.capability;

    row.appendChild(website);
    row.appendChild(siteStatus);
    richlistitem.appendChild(row);
    return richlistitem;
  },

  onPermissionKeyPress(event) {
    if (!this._list.selectedItem) {
      return;
    }

    if (
      event.keyCode == KeyEvent.DOM_VK_DELETE ||
      (AppConstants.platform == "macosx" &&
        event.keyCode == KeyEvent.DOM_VK_BACK_SPACE)
    ) {
      this.onPermissionDelete();
      event.preventDefault();
    }
  },

  _setRemoveButtonState() {
    if (!this._list) {
      return;
    }

    let hasSelection = this._list.selectedIndex >= 0;
    let hasRows = this._list.itemCount > 0;
    this._removeButton.disabled = !hasSelection;
    this._removeAllButton.disabled = !hasRows;
  },

  onPermissionDelete() {
    let richlistitem = this._list.selectedItem;
    let origin = richlistitem.getAttribute("origin");
    let permissionGroup = this._permissionGroups.get(origin);

    this._removePermissionFromList(origin);
    this._permissionsToDelete.set(permissionGroup.origin, permissionGroup);

    this._setRemoveButtonState();
  },

  onAllPermissionsDelete() {
    for (let permissionGroup of this._permissionGroups.values()) {
      this._removePermissionFromList(permissionGroup.origin);
      this._permissionsToDelete.set(permissionGroup.origin, permissionGroup);
    }

    this._setRemoveButtonState();
  },

  onPermissionSelect() {
    this._setRemoveButtonState();
  },

  onPermissionChange(perm, capability) {
    let group = this._permissionGroups.get(perm.origin);
    if (group.capability == capability) {
      return;
    }
    if (capability == group.savedCapability) {
      group.revert();
      this._permissionsToChange.delete(group.origin);
    } else {
      group.capability = capability;
      this._permissionsToChange.set(group.origin, group);
    }

    // enable "remove all" button as needed
    this._setRemoveButtonState();
  },

  onApplyChanges() {
    // Stop observing permission changes since we are about
    // to write out the pending adds/deletes and don't need
    // to update the UI
    this.uninit();

    // Record telemetry for notification permission revocation via preferences
    if (this._type === "desktop-notification") {
      for (let group of this._permissionsToDelete.values()) {
        Glean.webNotificationPermission.permissionRevokedPreferences.record({
          site_category: SiteCategory.getCategory(group.principal),
        });
      }
    }

    // Delete even _permissionsToChange to clear out double-keyed permissions
    for (let group of [
      ...this._permissionsToDelete.values(),
      ...this._permissionsToChange.values(),
    ]) {
      for (let perm of group.perms) {
        SitePermissions.removeFromPrincipal(perm.principal, perm.type);
      }
    }

    for (let group of this._permissionsToChange.values()) {
      SitePermissions.setForPrincipal(
        group.principal,
        this._type,
        group.capability
      );
    }

    if (this._checkbox.checked) {
      Services.prefs.setIntPref(
        this._defaultPermissionStatePrefName,
        SitePermissions.BLOCK
      );
    } else if (this._currentDefaultPermissionsState == SitePermissions.BLOCK) {
      Services.prefs.setIntPref(
        this._defaultPermissionStatePrefName,
        SitePermissions.UNKNOWN
      );
    }

    if (
      this._type == "desktop-notification" &&
      !this._closedDeliveryBox.hidden
    ) {
      let { checked } = this._closedDeliveryCheckbox;
      if (
        checked !=
        Services.prefs.getBoolPref(CLOSED_DELIVERY_ENABLED_PREF, false)
      ) {
        lazy.PushNotificationHelper.setEnabled(checked);
      }
    }
  },

  buildPermissionsList(sortCol) {
    // Clear old entries.
    let oldItems = this._list.querySelectorAll("richlistitem");
    for (let item of oldItems) {
      item.remove();
    }
    let frag = document.createDocumentFragment();

    let permissionGroups = Array.from(this._permissionGroups.values());

    this._showContainerColumn =
      gPermissionContainers.anyInContainer(permissionGroups);
    document.getElementById("containerCol").hidden = !this._showContainerColumn;

    let keyword = this._searchBox.value.toLowerCase().trim();
    for (let permissionGroup of permissionGroups) {
      if (
        keyword &&
        !permissionGroup.displayOrigin.includes(keyword) &&
        !permissionGroup.containerLabel.toLowerCase().includes(keyword)
      ) {
        continue;
      }

      let richlistitem = this._createPermissionListItem(permissionGroup);
      frag.appendChild(richlistitem);
    }

    // Sort permissions.
    this._sortPermissions(this._list, frag, sortCol);

    this._list.appendChild(frag);

    this._setRemoveButtonState();
  },

  async buildAutoplayMenulist() {
    let menulist = document.createXULElement("menulist");
    let states = SitePermissions.getAvailableStates("autoplay-media");
    document.l10n.pauseObserving();
    for (let state of states) {
      let m = menulist.appendItem(undefined, state);
      document.l10n.setAttributes(
        m,
        this._getCapabilityL10nId(m, "autoplay-media", state)
      );
    }

    menulist.value = SitePermissions.getDefault("autoplay-media");

    menulist.addEventListener("select", () => {
      SitePermissions.setDefault("autoplay-media", Number(menulist.value));
    });

    menulist.menupopup.setAttribute("escapecontentshell", true);

    menulist.disabled = Services.prefs.prefIsLocked(AUTOPLAY_PREF);

    document.getElementById("setAutoplayPref").appendChild(menulist);
    await document.l10n.translateFragment(menulist);
    document.l10n.resumeObserving();
  },

  _sortPermissions(list, frag, column) {
    let sortDirection;

    if (!column) {
      column = document.querySelector("treecol[data-isCurrentSortCol=true]");
      sortDirection =
        column.getAttribute("data-last-sortDirection") || "ascending";
    } else {
      sortDirection = column.getAttribute("data-last-sortDirection");
      sortDirection =
        sortDirection === "ascending" ? "descending" : "ascending";
    }

    let sortFunc = null;
    switch (column.id) {
      case "siteCol":
      case "containerCol":
        sortFunc = gPermissionContainers.sortFunctionFor(column.id);
        break;

      case "statusCol":
        sortFunc = (a, b) => {
          return (
            parseInt(a.querySelector(".website-status").value) >
            parseInt(b.querySelector(".website-status").value)
          );
        };
        break;
    }

    let items = Array.from(frag.querySelectorAll("richlistitem"));

    if (sortDirection === "descending") {
      items.sort((a, b) => sortFunc(b, a));
    } else {
      items.sort(sortFunc);
    }

    // Re-append items in the correct order:
    items.forEach(item => frag.appendChild(item));

    let cols = list.previousElementSibling.querySelectorAll("treecol");
    cols.forEach(c => {
      c.removeAttribute("data-isCurrentSortCol");
      c.removeAttribute("sortDirection");
    });
    column.setAttribute("data-isCurrentSortCol", "true");
    column.setAttribute("sortDirection", sortDirection);
    column.setAttribute("data-last-sortDirection", sortDirection);
  },
};

window.addEventListener("load", () => gSitePermissionsManager.onLoad());
