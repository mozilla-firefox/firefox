/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { LoginHelper } from "resource://gre/modules/LoginHelper.sys.mjs";
import { DataSourceBase } from "resource://gre/modules/megalist/aggregator/datasources/DataSourceBase.sys.mjs";
import { LoginCSVImport } from "resource://gre/modules/LoginCSVImport.sys.mjs";
import { LoginExport } from "resource://gre/modules/LoginExport.sys.mjs";
import { setTimeout, clearTimeout } from "resource://gre/modules/Timer.sys.mjs";

const AUTH_TIMEOUT_MS = 5 * 60 * 1000; // 5 minutes

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  LoginBreaches: "resource:///modules/LoginBreaches.sys.mjs",
  MigrationUtils: "resource:///modules/MigrationUtils.sys.mjs",
  UIState: "resource://services-sync/UIState.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "BREACH_ALERTS_ENABLED",
  "signon.management.page.breach-alerts.enabled",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "PASSWORD_SYNC_ENABLED",
  "services.sync.engine.passwords",
  false
);

// Precedence values when sorting logins by alerts.
const ALERT_VALUES = {
  breached: 0,
  vulnerable: 1,
  noUsername: 2,
  none: 3,
};

export const DISPLAY_MODES = {
  ALERTS: "DisplayAlerts",
  ALL: "DisplayAll",
};

const VIEW_MODES = {
  LIST: "List",
  ADD: "Add",
  EDIT: "Edit",
};

export const SUPPORT_URL =
  Services.urlFormatter.formatURLPref("app.support.baseURL") +
  "password-manager-remember-delete-edit-logins";

export const PREFERENCES_URL = "about:preferences#privacy-logins";

const ORIGIN_BREACHED_URL =
  "https://support.mozilla.org/en-US/kb/firefox-password-manager-alerts-breached-websites";

const VULNERABLE_PASSWORD_URL =
  "https://support.mozilla.org/en-US/kb/create-secure-passwords-keep-your-identity-safe";

const IMPORT_FILE_SUPPORT_URL =
  "https://support.mozilla.org/kb/import-login-data-file";

const IMPORT_FILE_REPORT_URL = "about:loginsimportreport";

/**
 * Data source for Logins.
 *
 * Each login is represented by 3 lines: origin, username and password.
 *
 * Protypes are used to reduce memory need because for different records
 * similar lines will differ in values only.
 */
export class LoginDataSource extends DataSourceBase {
  doneReloadDataSource;
  #originPrototype;
  #usernamePrototype;
  #passwordPrototype;
  #enabled;
  #header;
  #exportPasswordsStrings;
  #displayMode;

  constructor(...args) {
    super(...args);
    // Wait for Fluent to provide strings before loading data
    this.localizeStrings({
      originLabel: { id: "contextual-manager-passwords-origin-label" },
      usernameLabel: { id: "contextual-manager-passwords-username-label" },
      passwordLabel: { id: "contextual-manager-passwords-password-label" },
      revealPasswordOSAuthDialogPrompt: {
        id: this.getPlatformFtl(
          "contextual-manager-passwords-reveal-password-os-auth-dialog-message"
        ),
      },
      copyPasswordOSAuthDialogPrompt: {
        id: this.getPlatformFtl(
          "contextual-manager-passwords-copy-password-os-auth-dialog-message"
        ),
      },
      editPasswordOSAuthDialogPrompt: {
        id: this.getPlatformFtl(
          "contextual-manager-passwords-edit-password-os-auth-dialog-message"
        ),
      },
      passwordOSAuthDialogCaption: {
        id: "contextual-manager-passwords-os-auth-dialog-caption",
      },
      passwordsImportFilePickerTitle: {
        id: "contextual-manager-passwords-import-file-picker-title",
      },
      passwordsImportFilePickerImportButton: {
        id: "contextual-manager-passwords-import-file-picker-import-button",
      },
      passwordsImportFilePickerCsvFilterTitle: {
        id: "contextual-manager-passwords-import-file-picker-csv-filter-title",
      },
      passwordsImportFilePickerTsvFilterTitle: {
        id: "contextual-manager-passwords-import-file-picker-tsv-filter-title",
      },
      exportPasswordsOSReauthMessage: {
        id: this.getPlatformFtl(
          "contextual-manager-passwords-export-os-auth-dialog-message"
        ),
      },
      passwordsExportFilePickerTitle: {
        id: "contextual-manager-passwords-export-file-picker-title",
      },
      passwordsExportFilePickerDefaultFileName: {
        id: "contextual-manager-passwords-export-file-picker-default-filename",
      },
      passwordsExportFilePickerExportButton: {
        id: "contextual-manager-passwords-export-file-picker-export-button",
      },
      passwordsExportFilePickerCsvFilterTitle: {
        id: "contextual-manager-passwords-export-file-picker-csv-filter-title",
      },
    }).then(strings => {
      const copyCommand = { id: "Copy" };
      const editCommand = { id: "Edit" };
      const deleteCommand = { id: "Delete" };
      const dismissBreachCommand = {
        id: "DismissBreach",
      };
      this.#header = this.createHeaderLine();
      this.#header.commands.push(
        { id: "AddLogin" },
        { id: "UpdateLogin" },
        { id: "DeleteLogin" },
        { id: "DiscardChanges" },
        { id: "ConfirmDiscardChanges" },
        {
          id: "ImportFromBrowser",
        },
        { id: "Import" },
        { id: "Export" },
        { id: "RemoveAll" },
        {
          id: "Settings",
          url: PREFERENCES_URL,
        },
        { id: "Help", url: SUPPORT_URL },
        { id: "UpdateDisplayMode" },
        { id: "OpenLink" }
      );
      this.#header.executeImport = async () =>
        this.#importFromFile(
          strings.passwordsImportFilePickerTitle,
          strings.passwordsImportFilePickerImportButton,
          strings.passwordsImportFilePickerCsvFilterTitle,
          strings.passwordsImportFilePickerTsvFilterTitle
        );

      this.#header.executeOpenLink = url => this.#openLink(url);
      this.#header.executeImportFromBrowser = () => this.#importFromBrowser();
      this.#header.executeRemoveAll = () => this.#removeAllPasswords();
      this.#header.executeExport = async () => this.#exportLogins();
      this.#header.executeAddLogin = newLogin => this.#addLogin(newLogin);
      this.#header.executeUpdateLogin = login => this.#updateLogin(login);
      this.#header.executeDeleteLogin = login => this.#deleteLogin(login);
      this.#header.executeDiscardChanges = options => this.#cancelEdit(options);
      this.#header.executeConfirmDiscardChanges = options =>
        this.#discardChangesConfirmed(options);

      this.#exportPasswordsStrings = {
        OSReauthMessage: strings.exportPasswordsOSReauthMessage,
        OSAuthDialogCaption: strings.passwordOSAuthDialogCaption,
        ExportFilePickerTitle: strings.passwordsExportFilePickerTitle,
        FilePickerExportButton: strings.passwordsExportFilePickerExportButton,
        FilePickerDefaultFileName:
          strings.passwordsExportFilePickerDefaultFileName.concat(".csv"),
        FilePickerCsvFilterTitle:
          strings.passwordsExportFilePickerCsvFilterTitle,
      };

      this.#header.executeUpdateDisplayMode = displayMode => {
        if (this.#displayMode !== displayMode) {
          this.#displayMode = displayMode;
          this.#reloadDataSource();
          this.setDisplayMode(this.#displayMode);
        }
      };

      const openOriginInNewTab = origin => this.#openLink(origin);

      this.#originPrototype = this.prototypeDataLine({
        field: { value: "origin" },
        label: { value: strings.originLabel },
        start: { value: true },
        value: {
          get() {
            return this.record.displayOrigin;
          },
        },
        valueIcon: {
          get() {
            return `page-icon:${this.record.origin}`;
          },
        },
        href: {
          get() {
            return this.record.origin;
          },
        },
        breachedNotification: {
          value: {
            id: "breached-origin-warning",
            url: ORIGIN_BREACHED_URL,
          },
        },
        commands: {
          value: [
            { id: "Open" },
            copyCommand,
            editCommand,
            deleteCommand,
            dismissBreachCommand,
          ],
        },
        executeDismissBreach: {
          value() {
            lazy.LoginBreaches.recordBreachAlertDismissal(this.record.guid);
            delete this.breached;
            this.refreshOnScreen();
          },
        },
        executeCopy: {
          value() {
            this.copyToClipboard(this.record.origin);
          },
        },
        executeDelete: {
          value() {
            this.setLayout({ id: "remove-login" });
          },
        },
        executeOpenLink: {
          value() {
            openOriginInNewTab(this.record.origin);
          },
        },
      });
      this.#usernamePrototype = this.prototypeDataLine({
        field: { value: "username" },
        label: { value: strings.usernameLabel },
        value: {
          get() {
            return this.editingValue ?? this.record.username;
          },
        },
        noUsernameNotification: {
          value: {
            id: "no-username-warning",
          },
        },
        commands: { value: [copyCommand, editCommand, "-", deleteCommand] },
        executeEdit: {
          value() {
            this.editingValue = this.record.username ?? "";
            this.refreshOnScreen();
          },
        },
      });
      this.#passwordPrototype = this.prototypeDataLine({
        field: { value: "password" },
        label: { value: strings.passwordLabel },
        concealed: { value: true, writable: true },
        end: { value: true },
        value: {
          get() {
            return (
              this.editingValue ??
              (this.concealed ? "••••••••" : this.record.password)
            );
          },
        },
        vulnerableNotification: {
          value: {
            id: "vulnerable-password-warning",
            url: VULNERABLE_PASSWORD_URL,
          },
        },
        commands: {
          value: [
            {
              ...copyCommand,
              verify: true,
              OSAuthPromptMessage: strings.copyPasswordOSAuthDialogPrompt,
              OSAuthCaptionMessage: strings.passwordOSAuthDialogCaption,
            },
            {
              id: "Reveal",
              verify: true,
              OSAuthPromptMessage: strings.revealPasswordOSAuthDialogPrompt,
              OSAuthCaptionMessage: strings.passwordOSAuthDialogCaption,
            },
            { id: "Conceal" },
            {
              ...editCommand,
              verify: true,
              OSAuthPromptMessage: strings.editPasswordOSAuthDialogPrompt,
              OSAuthCaptionMessage: strings.passwordOSAuthDialogCaption,
            },
            deleteCommand,
            { id: "Cancel" },
          ],
        },
        executeReveal: {
          value() {
            this.concealed = false;
            clearTimeout(this.concealPasswordTimeout);
            this.concealPasswordTimeout = setTimeout(
              () => this.executeConceal(),
              AUTH_TIMEOUT_MS
            );
            this.refreshOnScreen();
          },
        },
        executeConceal: {
          value() {
            this.concealed = true;
            this.refreshOnScreen();
          },
        },
        executeCopy: {
          value() {
            this.copyToClipboard(this.record.password);
          },
        },
        executeEdit: {
          value() {
            this.editingValue = this.record.password ?? "";
            this.refreshOnScreen();
          },
        },
      });

      // Sort by origin, then by username, then by GUID
      this.#displayMode = DISPLAY_MODES.ALL;
      Services.obs.addObserver(this, "passwordmgr-storage-changed");
      Services.obs.addObserver(this, "passwordmgr-crypto-login");
      Services.prefs.addObserver("signon.rememberSignons", this);
      Services.prefs.addObserver(
        "signon.management.page.breach-alerts.enabled",
        this
      );
      Services.prefs.addObserver(
        "signon.management.page.vulnerable-passwords.enabled",
        this
      );
      this.#reloadDataSource();
    });
  }

  #recordLoginsUpdate(changeType) {
    Glean.contextualManager.recordsUpdate.record({
      change_type: changeType,
    });
  }

  async #importFromFile(title, buttonLabel, csvTitle, tsvTitle) {
    const { BrowserWindowTracker } = ChromeUtils.importESModule(
      "resource:///modules/BrowserWindowTracker.sys.mjs"
    );
    const browsingContext = BrowserWindowTracker.getTopWindow().browsingContext;
    let { result, path } = await this.openFilePickerDialog(
      title,
      buttonLabel,
      [
        {
          title: csvTitle,
          extensionPattern: "*.csv",
        },
        {
          title: tsvTitle,
          extensionPattern: "*.tsv",
        },
      ],
      browsingContext
    );

    if (result != Ci.nsIFilePicker.returnCancel) {
      try {
        const summary = await LoginCSVImport.importFromCSV(path);
        const counts = { added: 0, modified: 0 };

        for (const item of summary) {
          if (item.result in counts) {
            counts[item.result] += 1;
          }
        }
        this.setNotification({
          id: "import-success",
          l10nArgs: counts,
          url: IMPORT_FILE_REPORT_URL,
        });
      } catch (e) {
        this.setNotification({
          id: "import-error",
          url: IMPORT_FILE_SUPPORT_URL,
          commands: {
            onRetry: "Import",
          },
        });
      }
    }
  }

  async openFilePickerDialog(
    title,
    okButtonLabel,
    appendFilters,
    browsingContext
  ) {
    return new Promise(resolve => {
      let fp = Cc["@mozilla.org/filepicker;1"].createInstance(Ci.nsIFilePicker);
      fp.init(browsingContext, title, Ci.nsIFilePicker.modeOpen);
      for (const appendFilter of appendFilters) {
        fp.appendFilter(appendFilter.title, appendFilter.extensionPattern);
      }
      fp.appendFilters(Ci.nsIFilePicker.filterAll);
      fp.okButtonLabel = okButtonLabel;
      fp.open(async result => {
        resolve({ result, path: fp.file ? fp.file.path : "" });
      });
    });
  }

  #importFromBrowser() {
    const { BrowserWindowTracker } = ChromeUtils.importESModule(
      "resource:///modules/BrowserWindowTracker.sys.mjs"
    );
    const browser = BrowserWindowTracker.getTopWindow().gBrowser;
    try {
      lazy.MigrationUtils.showMigrationWizard(browser.ownerGlobal, {
        entrypoint: lazy.MigrationUtils.MIGRATION_ENTRYPOINTS.PASSWORDS,
      });
    } catch (ex) {
      console.error(ex);
    }
  }

  #isPasswordSyncEnabled() {
    const state = lazy.UIState.get();
    return state.syncEnabled && lazy.PASSWORD_SYNC_ENABLED;
  }

  async #removeAllPasswords() {
    const { total } = this.#header.value;
    const messageId = this.#isPasswordSyncEnabled()
      ? "contextual-manager-passwords-remove-all-message-sync"
      : "contextual-manager-passwords-remove-all-message";

    const { confirmed, checked } = await this.#showWindowPrompt(
      { id: "contextual-manager-passwords-remove-all-title", args: { total } },
      { id: messageId, args: { total } },
      {
        id: "contextual-manager-passwords-remove-all-confirm-button",
        args: { total },
      },
      {
        id: "contextual-manager-passwords-remove-all-passwords-checkbox",
        args: { total },
      }
    );

    if (!confirmed || !checked) {
      return;
    }

    if (confirmed) {
      Services.logins.removeAllLogins();
      this.setNotification({
        id: "delete-login-success",
        l10nArgs: { total },
        viewMode: VIEW_MODES.LIST,
      });
      this.#recordLoginsUpdate("remove_all");
    }
  }

  confirmRemoveLogin([record]) {
    Services.logins.removeLogin(record);
    this.cancelDialog();
  }

  confirmRetryImport() {
    this.#header.executeImport();
    this.cancelDialog();
  }

  async #exportLogins() {
    const { BrowserWindowTracker } = ChromeUtils.importESModule(
      "resource:///modules/BrowserWindowTracker.sys.mjs"
    );
    const browsingContext = BrowserWindowTracker.getTopWindow().browsingContext;

    const isOSAuthEnabled = LoginHelper.getOSAuthEnabled(
      LoginHelper.OS_AUTH_FOR_PASSWORDS_PREF
    );

    const reason = "export_cpm";
    let { isAuthorized, telemetryEvent } = await LoginHelper.requestReauth(
      browsingContext,
      isOSAuthEnabled,
      null, // Prompt regardless of a recent prompt
      this.#exportPasswordsStrings.OSReauthMessage,
      this.#exportPasswordsStrings.OSAuthDialogCaption,
      reason
    );

    let { name, extra = {}, value = null } = telemetryEvent;
    if (value) {
      extra.value = value;
    }
    Glean.pwmgr[name].record(extra);

    if (!isAuthorized) {
      return;
    }

    const { confirmed } = await this.#showWindowPrompt(
      { id: "contextual-manager-export-passwords-dialog-title" },
      { id: "contextual-manager-export-passwords-dialog-message" },
      { id: "contextual-manager-export-passwords-dialog-confirm-button" }
    );

    if (confirmed) {
      this.exportFilePickerDialog(browsingContext);
    }
  }

  exportFilePickerDialog(browsingContext) {
    let fp = Cc["@mozilla.org/filepicker;1"].createInstance(Ci.nsIFilePicker);
    const fpCallback = aResult => {
      if (aResult != Ci.nsIFilePicker.returnCancel) {
        LoginExport.exportAsCSV(fp.file.path);
        Glean.pwmgr.mgmtMenuItemUsedExportComplete.record();
        this.setNotification({
          id: "export-passwords-success",
        });
      }
    };
    fp.init(
      browsingContext,
      this.#exportPasswordsStrings.ExportFilePickerTitle,
      Ci.nsIFilePicker.modeSave
    );
    fp.appendFilter(
      this.#exportPasswordsStrings.FilePickerCsvFilterTitle,
      "*.csv"
    );
    fp.appendFilters(Ci.nsIFilePicker.filterAll);
    fp.defaultString = this.#exportPasswordsStrings.FilePickerDefaultFileName;
    fp.defaultExtension = "csv";
    fp.okButtonLabel = this.#exportPasswordsStrings.FilePickerExportButton;
    fp.open(fpCallback);
  }

  #openLink(url) {
    const { BrowserWindowTracker } = ChromeUtils.importESModule(
      "resource:///modules/BrowserWindowTracker.sys.mjs"
    );
    const browser = BrowserWindowTracker.getTopWindow().gBrowser;
    browser.ownerGlobal.switchToTabHavingURI(url, true, {
      ignoreFragment: "whenComparingAndReplace",
    });
  }

  async #showWindowPrompt(
    titleL10n,
    messageL10n,
    confirmButtonL10n,
    checkboxL10n = null
  ) {
    const { BrowserWindowTracker } = ChromeUtils.importESModule(
      "resource:///modules/BrowserWindowTracker.sys.mjs"
    );
    const win = BrowserWindowTracker.getTopWindow();

    const { title, message, confirmButton } = await this.localizeStrings({
      title: titleL10n,
      message: messageL10n,
      confirmButton: confirmButtonL10n,
    });

    let checkbox = null;
    if (checkboxL10n) {
      const { checkboxArg } = await this.localizeStrings({
        checkboxArg: checkboxL10n,
      });
      checkbox = checkboxArg;
    }

    // For more context on what these flags mean:
    // https://firefox-source-docs.mozilla.org/toolkit/components/prompts/prompts/nsIPromptService-reference.html#Prompter.confirmEx
    const flags =
      Services.prompt.BUTTON_TITLE_IS_STRING * Services.prompt.BUTTON_POS_0 +
      Services.prompt.BUTTON_TITLE_CANCEL * Services.prompt.BUTTON_POS_1;

    const result = await Services.prompt.asyncConfirmEx(
      win.browsingContext,
      Services.prompt.MODAL_TYPE_INTERNAL_WINDOW,
      title,
      message,
      flags,
      confirmButton,
      null,
      null,
      checkbox,
      false,
      {}
    );

    const propBag = result.QueryInterface(Ci.nsIPropertyBag2);
    const options = {
      // buttonNumClicked will be:
      //  - 0 for confirm
      //  - 1 for cancelling/declining.
      confirmed: propBag.get("buttonNumClicked") == 0,
      checked: propBag.get("checked"),
    };

    return options;
  }

  async #addLogin(newLogin) {
    const origin = LoginHelper.getLoginOrigin(newLogin.origin);
    newLogin.origin = origin;
    Object.assign(newLogin, {
      formActionOrigin: "",
      usernameField: "",
      passwordField: "",
    });
    newLogin = LoginHelper.vanillaObjectToLogin(newLogin);
    try {
      newLogin = await Services.logins.addLoginAsync(newLogin);
      this.setNotification({
        id: "add-login-success",
        l10nArgs: { url: origin },
        guid: newLogin.guid,
        viewMode: VIEW_MODES.LIST,
      });
      this.#recordLoginsUpdate("add");
    } catch (error) {
      this.#handleLoginStorageErrors(origin, error);
    }
  }

  async #updateLogin(login) {
    const logins = await Services.logins.searchLoginsAsync({
      origin: login.origin,
      guid: login.guid,
    });
    if (logins.length != 1) {
      return;
    }
    const modifiedLogin = logins[0].clone();
    if (login.hasOwnProperty("username")) {
      modifiedLogin.username = login.username;
    }
    if (login.hasOwnProperty("password")) {
      modifiedLogin.password = login.password;
    }
    try {
      Services.logins.modifyLogin(logins[0], modifiedLogin);
      this.setNotification({
        id: "update-login-success",
        viewMode: VIEW_MODES.LIST,
      });
      this.#recordLoginsUpdate("edit");
    } catch (error) {
      this.#handleLoginStorageErrors(modifiedLogin.origin, error);
    }
  }

  #cancelEdit(options = {}) {
    this.setNotification({
      id: "discard-changes",
      fromSidebar: options.fromSidebar,
      passwordIndex: options.passwordIndex,
    });
  }

  #discardChangesConfirmed(options = {}) {
    if (options.fromSidebar) {
      const { BrowserWindowTracker } = ChromeUtils.importESModule(
        "resource:///modules/BrowserWindowTracker.sys.mjs"
      );
      const window = BrowserWindowTracker.getTopWindow();
      window.SidebarController.hide();
    } else {
      this.discardChangesConfirmed();
    }
  }

  #handleLoginStorageErrors(origin, error) {
    if (error.message.includes("This login already exists")) {
      const existingLoginGuid = error.data.toString();
      this.setNotification({
        id: "login-already-exists-warning",
        l10nArgs: { url: origin },
        guid: existingLoginGuid,
      });
    }
  }

  async #deleteLogin(login) {
    const logins = await Services.logins.searchLoginsAsync({
      origin: login.origin,
      guid: login.guid,
    });
    if (logins.length != 1) {
      return;
    }
    Services.logins.removeLogin(logins[0]);
    this.setNotification({
      id: "delete-login-success",
      l10nArgs: { total: 1 },
      viewMode: VIEW_MODES.LIST,
    });
    this.#recordLoginsUpdate("remove");
  }

  /**
   * Enumerate all the lines provided by this data source.
   *
   * @param {string} searchText used to filter data
   */
  *enumerateLines(searchText) {
    if (this.#enabled === undefined) {
      // Async Fluent API makes it possible to have data source waiting
      // for the localized strings, which can be detected by undefined in #enabled.
      return;
    }

    yield this.#header;
    if (this.#header.collapsed || !this.#enabled) {
      return;
    }

    const stats = { count: 0, total: 0 };
    searchText = searchText.toUpperCase();
    yield* this.enumerateLinesForMatchingRecords(
      searchText,
      stats,
      login =>
        login.displayOrigin.toUpperCase().includes(searchText) ||
        login.username.toUpperCase().includes(searchText) ||
        login.password.toUpperCase().includes(searchText)
    );

    this.#header.value.count = stats.count;
  }

  /**
   * Sync lines array with the actual data source.
   * This function reads all logins from the storage, adds or updates lines and
   * removes lines for the removed logins.
   */
  async #reloadDataSource() {
    this.doneReloadDataSource = false;
    this.#enabled = Services.prefs.getBoolPref("signon.rememberSignons");
    if (!this.#enabled) {
      this.#reloadEmptyDataSource();
      this.doneReloadDataSource = true;
      return;
    }

    const logins = await LoginHelper.getAllUserFacingLogins();
    this.beforeReloadingDataSource();

    const breachesMap = lazy.BREACH_ALERTS_ENABLED
      ? await lazy.LoginBreaches.getPotentialBreachesByLoginGUID(logins)
      : new Map();

    const loginsWithAlerts = logins.filter(
      login =>
        breachesMap.has(login.guid) ||
        lazy.LoginBreaches.isVulnerablePassword(login) ||
        !login.username.length
    );

    const filteredLogins =
      this.#displayMode === DISPLAY_MODES.ALERTS ? loginsWithAlerts : logins;

    filteredLogins.forEach(login => {
      // Similar domains will be grouped together
      // www. will have least effect on the sorting
      const parts = login.displayOrigin.split(".");

      // Exclude TLD domain
      //todo support eTLD and use public suffix here https://publicsuffix.org
      if (parts.length > 1) {
        parts.length -= 1;
      }
      const isLoginBreached = breachesMap.has(login.guid);
      const isLoginVulnerable = lazy.LoginBreaches.isVulnerablePassword(login);
      const loginNoUsername = !login.username.length;

      let alertValue;
      if (isLoginBreached) {
        alertValue = ALERT_VALUES.breached;
      } else if (isLoginVulnerable) {
        alertValue = ALERT_VALUES.vulnerable;
      } else if (loginNoUsername) {
        alertValue = ALERT_VALUES.noUsername;
      } else {
        alertValue = ALERT_VALUES.none;
      }

      const domain = parts.reverse().join(".");
      const lineId =
        this.#displayMode === DISPLAY_MODES.ALERTS
          ? `${alertValue}:${domain}:${login.username}:${login.guid}`
          : `${domain}:${login.username}:${login.guid}`;

      let originLine = this.addOrUpdateLine(
        login,
        lineId + "0",
        this.#originPrototype
      );
      this.addOrUpdateLine(login, lineId + "1", this.#usernamePrototype);
      let passwordLine = this.addOrUpdateLine(
        login,
        lineId + "2",
        this.#passwordPrototype
      );

      originLine.breached = isLoginBreached;
      passwordLine.vulnerable = isLoginVulnerable;
    });

    this.#header.value.total = logins.length;
    this.#header.value.alerts = loginsWithAlerts.length;
    this.afterReloadingDataSource();
    this.doneReloadDataSource = true;
  }

  #reloadEmptyDataSource() {
    this.lines.length = 0;
    //todo: user can enable passwords by activating Passwords header line
    this.#header.value.total = 0;
    this.#header.value.alerts = 0;
    this.refreshAllLinesOnScreen();
  }

  getAuthTimeoutMs() {
    return AUTH_TIMEOUT_MS;
  }

  observe(_subj, topic, message) {
    if (
      topic == "passwordmgr-storage-changed" ||
      topic == "passwordmgr-crypto-login" ||
      message == "signon.rememberSignons" ||
      message == "signon.management.page.breach-alerts.enabled" ||
      message == "signon.management.page.vulnerable-passwords.enabled"
    ) {
      if (
        topic == "passwordmgr-storage-changed" &&
        message === "importLogins"
      ) {
        this.#recordLoginsUpdate("import");
      }

      this.#reloadDataSource();
    }
  }
}
