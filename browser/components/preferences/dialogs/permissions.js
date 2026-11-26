/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from permissionContainers.js */

var { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

var { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  isTrailingDotPolicyDuplicate:
    "resource://gre/modules/PoliciesHelpers.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "contentBlockingAllowList",
  "@mozilla.org/content-blocking-allow-list;1",
  Ci.nsIContentBlockingAllowList
);

const DEFAULT_FAVICON = "chrome://global/skin/icons/defaultFavicon.svg";

const permissionExceptionsL10n = {
  trackingprotection: {
    window: "permissions-exceptions-etp-window2",
    description: "permissions-exceptions-manage-etp-desc",
  },
  cookie: {
    window: "permissions-exceptions-cookie-window2",
    description: "permissions-exceptions-cookie-desc",
  },
  popup: {
    window: "permissions-exceptions-popup-window3",
    description: "permissions-exceptions-popup-desc2",
  },
  "login-saving": {
    window: "permissions-exceptions-saved-passwords-window",
    description: "permissions-exceptions-saved-passwords-desc",
  },
  "https-only-load-insecure": {
    window: "permissions-exceptions-https-only-window2",
    description: "permissions-exceptions-https-only-desc2",
  },
  install: {
    window: "permissions-exceptions-addons-window2",
    description: "permissions-exceptions-addons-desc",
  },
  "ipp-vpn": {
    window: "ip-protection-exceptions-dialog-window",
    description: "ip-protection-exclusions-desc",
  },
  "persist-data-on-shutdown": {
    window: "permissions-exceptions-shutdown-clearing-window",
    description: "permissions-exceptions-shutdown-clearing-desc",
  },
};

function Permission(principal, type, capability) {
  this.principal = principal;
  this.origin = principal.origin;
  this.type = type;
  this.capability = capability;
}

var gPermissionManager = {
  _type: "",
  _isObserving: false,
  _permissions: new Map(),
  _permissionsToAdd: new Map(),
  _permissionsToDelete: new Map(),
  _bundle: null,
  _list: null,
  _removeButton: null,
  _removeAllButton: null,
  _forcedHTTP: null,
  _capabilityFilter: null,
  _showContainerColumn: false,

  onLoad() {
    let params = window.arguments[0];
    document.mozSubdialogReady = this.init(params);
  },

  /**
   * @param {object} params
   * @param {string} params.permissionType Permission type for which the dialog should be shown
   * @param {string} params.prefilledHost The value which the URL field should initially contain
   * @param {boolean} params.blockVisible Display the "Block" button in the dialog
   * @param {boolean} params.sessionVisible Display the "Allow for Session" button in the dialog (Only for Cookie & HTTPS-Only permissions)
   * @param {boolean} params.allowVisible Display the "Allow" button in the dialog
   * @param {boolean} params.disableETPVisible Display the "Add Exception" button in the dialog (Only for ETP permissions)
   * @param {boolean} params.addVisible Display the "Add" button in the dialog (Only for ipp-vpn permissions)
   * @param {boolean} params.hideStatusColumn Hide the "Status" column in the dialog
   * @param {boolean} params.forcedHTTP Save inputs whose URI has a HTTPS scheme with a HTTP scheme (Used by HTTPS-Only)
   * @param {number} params.capabilityFilter Display permissions that have the specified capability only. See Ci.nsIPermissionManager.
   */
  async init(params) {
    if (!this._isObserving) {
      Services.obs.addObserver(this, "perm-changed");
      this._isObserving = true;
    }

    document.addEventListener("dialogaccept", () => this.onApplyChanges());

    this._type = params.permissionType;
    this._list = document.getElementById("permissionsBox");
    this._removeButton = document.getElementById("removePermission");
    this._removeAllButton = document.getElementById("removeAllPermissions");

    this._btnCookieSession = document.getElementById("btnCookieSession");
    this._btnBlock = document.getElementById("btnBlock");
    this._btnDisableETP = document.getElementById("btnDisableETP");
    this._btnAllow = document.getElementById("btnAllow");
    this._btnHttpsOnlyOff = document.getElementById("btnHttpsOnlyOff");
    this._btnHttpsOnlyOffTmp = document.getElementById("btnHttpsOnlyOffTmp");
    this._btnAdd = document.getElementById("btnAdd");

    this._capabilityFilter = params.capabilityFilter;

    let permissionsText = document.getElementById("permissionsText");

    let l10n = permissionExceptionsL10n[this._type];

    document.l10n.setAttributes(permissionsText, l10n.description);
    document.l10n.setAttributes(document.documentElement, l10n.window);

    let urlFieldVisible =
      params.blockVisible ||
      params.sessionVisible ||
      params.allowVisible ||
      params.disableETPVisible ||
      params.addVisible;

    this._urlField = document.getElementById("url");
    this._urlField.value = params.prefilledHost;
    this._urlField.hidden = !urlFieldVisible;

    this._forcedHTTP = params.forcedHTTP;

    await document.l10n.translateElements([permissionsText]);

    document.getElementById("btnDisableETP").hidden = !params.disableETPVisible;
    document.getElementById("btnBlock").hidden = !params.blockVisible;
    document.getElementById("btnCookieSession").hidden = !(
      params.sessionVisible && this._type == "cookie"
    );
    document.getElementById("btnHttpsOnlyOff").hidden = !(
      this._type == "https-only-load-insecure"
    );
    document.getElementById("btnHttpsOnlyOffTmp").hidden = !(
      params.sessionVisible && this._type == "https-only-load-insecure"
    );
    document.getElementById("btnAllow").hidden = !params.allowVisible;
    document.getElementById("btnAdd").hidden = !params.addVisible;

    this.onHostInput(this._urlField);

    document.getElementById("urlLabel").hidden = !urlFieldVisible;

    this._hideStatusColumn = params.hideStatusColumn;
    let statusCol = document.getElementById("statusCol");
    statusCol.hidden = this._hideStatusColumn;
    const siteCol = document.getElementById("siteCol");
    if (this._hideStatusColumn) {
      statusCol.removeAttribute("data-isCurrentSortCol");
      siteCol.setAttribute("data-isCurrentSortCol", "true");
    }

    window.addEventListener("unload", () => {
      gPermissionManager.uninit();
    });
    window.addEventListener("keypress", event => {
      gPermissionManager.onWindowKeyPress(event);
    });
    document
      .getElementById("permissionsDialogCloseKey")
      .addEventListener("command", () => {
        window.close();
      });
    this._list.addEventListener("keypress", event => {
      gPermissionManager.onPermissionKeyPress(event);
    });
    this._list.addEventListener("select", () => {
      gPermissionManager.onPermissionSelect();
    });
    this.addCommandListeners();
    this._urlField.addEventListener("input", event => {
      gPermissionManager.onHostInput(event.target);
    });
    this._urlField.addEventListener("keypress", event => {
      gPermissionManager.onHostKeyPress(event);
    });
    statusCol.addEventListener("click", event => {
      gPermissionManager.buildPermissionsList(event.target);
    });
    siteCol.addEventListener("click", event => {
      gPermissionManager.buildPermissionsList(event.target);
    });
    document.getElementById("containerCol").addEventListener("click", event => {
      gPermissionManager.buildPermissionsList(event.target);
    });

    Services.obs.notifyObservers(null, "flush-pending-permissions", this._type);

    this._loadPermissions();
    this.buildPermissionsList();

    this._urlField.focus();
  },

  addCommandListeners() {
    window.addEventListener("click", event => {
      switch (event.target.id) {
        case "removePermission":
          gPermissionManager.onPermissionDelete();
          break;
        case "removeAllPermissions":
          gPermissionManager.onAllPermissionsDelete();
          break;
        case "btnCookieSession":
          gPermissionManager.addPermission(
            Ci.nsICookiePermission.ACCESS_SESSION
          );
          break;
        case "btnBlock":
          gPermissionManager.addPermission(Ci.nsIPermissionManager.DENY_ACTION);
          break;
        case "btnDisableETP":
          gPermissionManager.addPermission(
            Ci.nsIPermissionManager.ALLOW_ACTION
          );
          break;
        case "btnAllow":
          gPermissionManager.addPermission(
            Ci.nsIPermissionManager.ALLOW_ACTION
          );
          break;
        case "btnHttpsOnlyOff":
          gPermissionManager.addPermission(
            Ci.nsIPermissionManager.ALLOW_ACTION
          );
          break;
        case "btnHttpsOnlyOffTmp":
          gPermissionManager.addPermission(
            Ci.nsIHttpsOnlyModePermission.LOAD_INSECURE_ALLOW_SESSION
          );
          break;
        case "btnAdd":
          // This button is for ipp-vpn, which only supports
          // site exclusions at this time.
          gPermissionManager.addPermission(Ci.nsIPermissionManager.DENY_ACTION);
      }
    });
  },

  uninit() {
    if (this._isObserving) {
      Services.obs.removeObserver(this, "perm-changed");
      this._isObserving = false;
    }
  },

  observe(subject, topic, data) {
    if (topic !== "perm-changed") {
      return;
    }

    if (data === "cleared") {
      this._permissions.clear();
      this._permissionsToAdd.clear();
      this._permissionsToDelete.clear();
      this._loadPermissions();
      this.buildPermissionsList();
      return;
    }

    let permission = subject.QueryInterface(Ci.nsIPermission);

    // Ignore unrelated permission types.
    if (permission.type !== this._type) {
      return;
    }

    if (data == "added") {
      this._addPermissionToList(permission);
      this.buildPermissionsList();
    } else if (data == "changed") {
      let p = this._permissions.get(permission.principal.origin);
      // Maybe this item has been excluded before because it had an invalid capability.
      if (p) {
        p.capability = permission.capability;
        this._handleCapabilityChange(p);
      } else {
        this._addPermissionToList(permission);
      }
      this.buildPermissionsList();
    } else if (data == "deleted") {
      this._removePermissionFromList(permission.principal.origin);
    }
  },

  _handleCapabilityChange(perm) {
    let permissionlistitem = document.getElementsByAttribute(
      "origin",
      perm.origin
    )[0];
    document.l10n.setAttributes(
      permissionlistitem.querySelector(".website-capability-value"),
      this._getCapabilityL10nId(perm.capability)
    );
  },

  _isCapabilitySupported(capability) {
    return (
      capability == Ci.nsIPermissionManager.ALLOW_ACTION ||
      capability == Ci.nsIPermissionManager.DENY_ACTION ||
      capability == Ci.nsICookiePermission.ACCESS_SESSION ||
      // Bug 1753600 there are still a few legacy cookies around that have the capability 9,
      // _getCapabilityL10nId will throw if it receives a capability of 9
      // that is not in combination with the type https-only-load-insecure
      (capability ==
        Ci.nsIHttpsOnlyModePermission.LOAD_INSECURE_ALLOW_SESSION &&
        this._type == "https-only-load-insecure")
    );
  },

  _getCapabilityL10nId(capability) {
    // HTTPS-Only Mode phrases exceptions as turning it off
    if (this._type == "https-only-load-insecure") {
      return this._getHttpsOnlyCapabilityL10nId(capability);
    }

    switch (capability) {
      case Ci.nsIPermissionManager.ALLOW_ACTION:
        return "permissions-capabilities-listitem-allow";
      case Ci.nsIPermissionManager.DENY_ACTION:
        return "permissions-capabilities-listitem-block";
      case Ci.nsICookiePermission.ACCESS_SESSION:
        return "permissions-capabilities-listitem-allow-session";
      default:
        throw new Error(`Unknown capability: ${capability}`);
    }
  },

  _getHttpsOnlyCapabilityL10nId(capability) {
    switch (capability) {
      case Ci.nsIPermissionManager.ALLOW_ACTION:
        return "permissions-capabilities-listitem-off";
      case Ci.nsIHttpsOnlyModePermission.LOAD_INSECURE_ALLOW_SESSION:
        return "permissions-capabilities-listitem-off-temporarily";
      default:
        throw new Error(`Unknown HTTPS-Only Mode capability: ${capability}`);
    }
  },

  _addPermissionToList(perm) {
    if (perm.type !== this._type) {
      return;
    }
    if (
      perm.expireType === Services.perms.EXPIRE_POLICY &&
      lazy.isTrailingDotPolicyDuplicate(perm)
    ) {
      return;
    }
    if (!this._isCapabilitySupported(perm.capability)) {
      return;
    }

    // If filtering is enabled, don't bother showing permissions that don't have
    // the capability we want.
    if (this._capabilityFilter && perm.capability !== this._capabilityFilter) {
      return;
    }

    // Skip private browsing session permissions.
    if (
      perm.principal.privateBrowsingId !==
        Services.scriptSecurityManager.DEFAULT_PRIVATE_BROWSING_ID &&
      perm.expireType === Services.perms.EXPIRE_SESSION
    ) {
      return;
    }

    let p = new Permission(perm.principal, perm.type, perm.capability);
    this._permissions.set(p.origin, p);
  },

  _addOrModifyPermission(principal, capability) {
    // check whether the permission already exists, if not, add it
    let permissionParams = { principal, type: this._type, capability };
    let existingPermission = this._permissions.get(principal.origin);
    if (!existingPermission) {
      this._permissionsToAdd.set(principal.origin, permissionParams);
      this._addPermissionToList(permissionParams);
      this.buildPermissionsList();
    } else if (existingPermission.capability != capability) {
      existingPermission.capability = capability;
      this._permissionsToAdd.set(principal.origin, permissionParams);
      this._handleCapabilityChange(existingPermission);
    }
  },

  _addNewPrincipalToList(list, uri) {
    if (uri.host?.includes("*")) {
      throw new Error("Wildcard in host");
    }
    list.push(Services.scriptSecurityManager.createContentPrincipal(uri, {}));
    // If we have ended up with an unknown scheme, the following will throw.
    list[list.length - 1].origin;
  },

  addPermission(capability) {
    let textbox = document.getElementById("url");
    let input_url = textbox.value.trim(); // trim any leading and trailing space
    let principals = [];
    try {
      // The origin accessor on the principal object will throw if the
      // principal doesn't have a canonical origin representation. This will
      // help catch cases where the URI parser parsed something like
      // `localhost:8080` as having the scheme `localhost`, rather than being
      // an invalid URI. A canonical origin representation is required by the
      // permission manager for storage, so this won't prevent any valid
      // permissions from being entered by the user.
      try {
        let uri = Services.io.newURI(input_url);
        // nsIURI.host throws for schemes without an authority (e.g. about:),
        // so only reject wildcards for URIs that actually have a host.
        if (uri instanceof Ci.nsIURL && uri.host.includes("*")) {
          throw new Error("Wildcard in host");
        }
        if (this._forcedHTTP && uri.schemeIs("https")) {
          uri = uri.mutate().setScheme("http").finalize();
        }
        let principal = Services.scriptSecurityManager.createContentPrincipal(
          uri,
          {}
        );
        if (principal.origin.startsWith("moz-nullprincipal:")) {
          throw new Error("Null principal");
        }
        principals.push(principal);
      } catch (ex) {
        // If the `input_url` already starts with http:// or https://, it is
        // definetely invalid here and can't be fixed by prefixing it with
        // http:// or https://.
        if (
          input_url.startsWith("http://") ||
          input_url.startsWith("https://")
        ) {
          throw ex;
        }
        this._addNewPrincipalToList(
          principals,
          Services.io.newURI("http://" + input_url)
        );
        if (!this._forcedHTTP) {
          this._addNewPrincipalToList(
            principals,
            Services.io.newURI("https://" + input_url)
          );
        }
      }
    } catch (ex) {
      document.l10n
        .formatValues([
          { id: "permissions-invalid-uri-title" },
          { id: "permissions-invalid-uri-label" },
        ])
        .then(([title, message]) => {
          Services.prompt.alert(window, title, message);
        });
      return;
    }
    // In case of an ETP exception we compute the contentBlockingAllowList principal
    // to align with the allow list behavior triggered by the protections panel
    if (this._type == "trackingprotection") {
      principals = principals.map(
        lazy.contentBlockingAllowList.computeContentBlockingAllowListPrincipal
      );
    }
    for (let principal of principals) {
      this._addOrModifyPermission(principal, capability);
    }

    textbox.value = "";
    textbox.focus();

    // covers a case where the site exists already, so the buttons don't disable
    this.onHostInput(textbox);

    // enable "remove all" button as needed
    this._setRemoveButtonState();
  },

  _removePermission(permission) {
    this._removePermissionFromList(permission.origin);

    // If this permission was added during this session, let's remove
    // it from the pending adds list to prevent calls to the
    // permission manager.
    let isNewPermission = this._permissionsToAdd.delete(permission.origin);
    if (!isNewPermission) {
      this._permissionsToDelete.set(permission.origin, permission);
    }
  },

  _removePermissionFromList(origin) {
    this._permissions.delete(origin);
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

  _createPermissionListItem(permission) {
    let disabledByPolicy = this._permissionDisabledByPolicy(permission);
    let richlistitem = document.createXULElement("richlistitem");
    richlistitem.setAttribute("origin", permission.origin);
    richlistitem.setAttribute(
      "displayorigin",
      gPermissionContainers.displayOriginFor(permission.principal)
    );
    richlistitem.setAttribute(
      "containerlabel",
      gPermissionContainers.labelFor(permission.principal)
    );
    let row = document.createXULElement("hbox");
    row.setAttribute("style", "flex: 1");

    if (this._showContainerColumn) {
      row.appendChild(
        gPermissionContainers.createCell(document, permission.principal)
      );
    }

    let icon = document.createXULElement("image");
    icon.setAttribute("class", "website-icon");
    icon.setAttribute("src", DEFAULT_FAVICON);
    row.appendChild(icon);
    this._setSiteIcon(permission.principal.originNoSuffix, icon);

    let hbox = document.createXULElement("hbox");
    let website = document.createXULElement("label");
    website.toggleAttribute("disabled", disabledByPolicy);
    website.setAttribute("class", "website-name-value");
    website.setAttribute(
      "value",
      gPermissionContainers.displayOriginFor(permission.principal)
    );
    hbox.setAttribute("class", "website-name");
    hbox.setAttribute("style", "flex: 3 3; width: 0");
    hbox.appendChild(website);
    row.appendChild(hbox);

    if (!this._hideStatusColumn) {
      hbox = document.createXULElement("hbox");
      let capability = document.createXULElement("label");
      capability.toggleAttribute("disabled", disabledByPolicy);
      capability.setAttribute("class", "website-capability-value");
      document.l10n.setAttributes(
        capability,
        this._getCapabilityL10nId(permission.capability)
      );
      hbox.setAttribute("class", "website-name");
      hbox.setAttribute("style", "flex: 1; width: 0");
      hbox.appendChild(capability);
      row.appendChild(hbox);
    }

    richlistitem.appendChild(row);
    return richlistitem;
  },

  async _setSiteIcon(origin, icon) {
    let iconURI;
    try {
      iconURI = Services.io.newURI(origin);
    } catch {
      return;
    }
    let favicon = await lazy.PlacesUtils.favicons
      .getFaviconForPage(iconURI)
      .catch(() => null);
    if (favicon) {
      icon.setAttribute("src", `page-icon:${origin}`);
    }
  },

  onWindowKeyPress(event) {
    // Prevent dialog.js from closing the dialog when the user submits the input
    // field via the return key.
    if (
      event.keyCode == KeyEvent.DOM_VK_RETURN &&
      document.activeElement == this._urlField
    ) {
      event.preventDefault();
    }
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

  onHostKeyPress(event) {
    if (event.keyCode == KeyEvent.DOM_VK_RETURN) {
      if (!document.getElementById("btnAllow").hidden) {
        document.getElementById("btnAllow").click();
      } else if (!document.getElementById("btnBlock").hidden) {
        document.getElementById("btnBlock").click();
      } else if (!document.getElementById("btnHttpsOnlyOff").hidden) {
        document.getElementById("btnHttpsOnlyOff").click();
      } else if (!document.getElementById("btnDisableETP").hidden) {
        document.getElementById("btnDisableETP").click();
      } else if (!document.getElementById("btnAdd").hidden) {
        document.getElementById("btnAdd").click();
      }
    }
  },

  onHostInput(siteField) {
    this._btnCookieSession.disabled =
      this._btnCookieSession.hidden || !siteField.value;
    this._btnHttpsOnlyOff.disabled =
      this._btnHttpsOnlyOff.hidden || !siteField.value;
    this._btnHttpsOnlyOffTmp.disabled =
      this._btnHttpsOnlyOffTmp.hidden || !siteField.value;
    this._btnBlock.disabled = this._btnBlock.hidden || !siteField.value;
    this._btnDisableETP.disabled =
      this._btnDisableETP.hidden || !siteField.value;
    this._btnAllow.disabled = this._btnAllow.hidden || !siteField.value;
    this._btnAdd.disabled = this._btnAdd.hidden || !siteField.value;
  },

  _setRemoveButtonState() {
    if (!this._list) {
      return;
    }

    let hasSelection = this._list.selectedIndex >= 0;

    let disabledByPolicy = false;
    if (Services.policies.status === Services.policies.ACTIVE && hasSelection) {
      let origin = this._list.selectedItem.getAttribute("origin");
      disabledByPolicy = this._permissionDisabledByPolicy(
        this._permissions.get(origin)
      );
    }

    this._removeButton.disabled = !hasSelection || disabledByPolicy;
    let disabledItems = this._list.querySelectorAll(
      "label.website-name-value[disabled='true']"
    );

    this._removeAllButton.disabled =
      this._list.itemCount == disabledItems.length;
  },

  onPermissionDelete() {
    let richlistitem = this._list.selectedItem;
    let origin = richlistitem.getAttribute("origin");
    let permission = this._permissions.get(origin);
    if (this._permissionDisabledByPolicy(permission)) {
      return;
    }

    this._removePermission(permission);

    this._setRemoveButtonState();
  },

  onAllPermissionsDelete() {
    for (let permission of this._permissions.values()) {
      if (this._permissionDisabledByPolicy(permission)) {
        continue;
      }
      this._removePermission(permission);
    }

    this._setRemoveButtonState();
  },

  onPermissionSelect() {
    this._setRemoveButtonState();
  },

  onApplyChanges() {
    // Stop observing permission changes since we are about
    // to write out the pending adds/deletes and don't need
    // to update the UI
    this.uninit();

    for (let p of this._permissionsToDelete.values()) {
      Services.perms.removeFromPrincipal(p.principal, p.type);
    }

    for (let p of this._permissionsToAdd.values()) {
      // If this sets the HTTPS-Only exemption only for this
      // session, then the expire-type has to be set.
      if (
        p.capability ==
        Ci.nsIHttpsOnlyModePermission.LOAD_INSECURE_ALLOW_SESSION
      ) {
        Services.perms.addFromPrincipal(
          p.principal,
          p.type,
          p.capability,
          Ci.nsIPermissionManager.EXPIRE_SESSION
        );
      } else {
        Services.perms.addFromPrincipal(p.principal, p.type, p.capability);
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

    let permissions = Array.from(this._permissions.values());

    this._showContainerColumn =
      gPermissionContainers.anyInContainer(permissions);
    document.getElementById("containerCol").hidden = !this._showContainerColumn;

    for (let permission of permissions) {
      let richlistitem = this._createPermissionListItem(permission);
      frag.appendChild(richlistitem);
    }

    // Sort permissions.
    this._sortPermissions(this._list, frag, sortCol);

    this._list.appendChild(frag);

    this._setRemoveButtonState();
  },

  _permissionDisabledByPolicy(permission) {
    let permissionObject = Services.perms.getPermissionObject(
      permission.principal,
      this._type,
      false
    );
    return (
      permissionObject?.expireType == Ci.nsIPermissionManager.EXPIRE_POLICY
    );
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
          // The capabilities values ("Allow" and "Block") are localized asynchronously.
          // Sort based on the guaranteed-present localization ID instead, note that the
          // ascending/descending arrow may be pointing the wrong way.
          return (
            a
              .querySelector(".website-capability-value")
              .getAttribute("data-l10n-id") >
            b
              .querySelector(".website-capability-value")
              .getAttribute("data-l10n-id")
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

window.addEventListener("load", () => {
  gPermissionManager.onLoad();
});
