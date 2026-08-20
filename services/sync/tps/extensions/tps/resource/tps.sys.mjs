/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* This is a JavaScript module to be imported via
 * ChromeUtils.importESModule() and acts as a singleton. Only the following
 * listed symbols will exposed on import, and only when and where imported.
 */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  Addon: "resource://tps/modules/addons.sys.mjs",
  AddonValidator: "resource://services-sync/engines/addons.sys.mjs",
  Address: "resource://tps/modules/formautofill.sys.mjs",
  Async: "resource://services-common/async.sys.mjs",
  Authentication: "resource://tps/auth/fxaccounts.sys.mjs",
  Bookmark: "resource://tps/modules/bookmarks.sys.mjs",
  BookmarkFolder: "resource://tps/modules/bookmarks.sys.mjs",
  BookmarkValidator: "resource://tps/modules/bookmarkValidator.sys.mjs",
  BrowserTabs: "resource://tps/modules/tabs.sys.mjs",
  BrowserWindows: "resource://tps/modules/windows.sys.mjs",
  CommonUtils: "resource://services-common/utils.sys.mjs",
  CreditCard: "resource://tps/modules/formautofill.sys.mjs",
  DumpAddresses: "resource://tps/modules/formautofill.sys.mjs",
  DumpBookmarks: "resource://tps/modules/bookmarks.sys.mjs",
  DumpCreditCards: "resource://tps/modules/formautofill.sys.mjs",
  DumpHistory: "resource://tps/modules/history.sys.mjs",
  DumpPasswords: "resource://tps/modules/passwords.sys.mjs",
  FormData: "resource://tps/modules/forms.sys.mjs",
  FormValidator: "resource://services-sync/engines/forms.sys.mjs",
  HistoryEntry: "resource://tps/modules/history.sys.mjs",
  Livemark: "resource://tps/modules/bookmarks.sys.mjs",
  Log: "resource://gre/modules/Log.sys.mjs",
  Logger: "resource://tps/logger.sys.mjs",
  Password: "resource://tps/modules/passwords.sys.mjs",
  PasswordValidator: "resource://services-sync/engines/passwords.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  Preference: "resource://tps/modules/prefs.sys.mjs",
  STATUS_OK: "resource://services-sync/constants.sys.mjs",
  Separator: "resource://tps/modules/bookmarks.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  Svc: "resource://services-sync/util.sys.mjs",
  SyncTelemetry: "resource://services-sync/telemetry.sys.mjs",
  WEAVE_VERSION: "resource://services-sync/constants.sys.mjs",
  Weave: "resource://services-sync/main.sys.mjs",
  extensionStorageSync: "resource://gre/modules/ExtensionStorageSync.sys.mjs",
});

// Options for wiping data during a sync
const SYNC_RESET_CLIENT = "resetClient";
const SYNC_WIPE_CLIENT = "wipeClient";
const SYNC_WIPE_REMOTE = "wipeRemote";

// Actions a test can perform
const ACTION_ADD = "add";
const ACTION_DELETE = "delete";
const ACTION_MODIFY = "modify";
const ACTION_SET_ENABLED = "set-enabled";
const ACTION_SYNC = "sync";
const ACTION_SYNC_RESET_CLIENT = SYNC_RESET_CLIENT;
const ACTION_SYNC_WIPE_CLIENT = SYNC_WIPE_CLIENT;
const ACTION_SYNC_WIPE_REMOTE = SYNC_WIPE_REMOTE;
const ACTION_VERIFY = "verify";
const ACTION_VERIFY_NOT = "verify-not";

const OBSERVER_TOPICS = [
  "fxaccounts:onlogin",
  "fxaccounts:onverified",
  "fxaccounts:onlogout",
  "profile-before-change",
  "weave:service:tracking-started",
  "weave:service:tracking-stopped",
  "weave:service:login:error",
  "weave:service:setup-complete",
  "weave:service:sync:finish",
  "weave:service:sync:delayed",
  "weave:service:sync:error",
  "weave:service:sync:start",
  "weave:service:resyncs-finished",
  "places-browser-init-complete",
];

export var TPS = {
  _currentAction: -1,
  _currentPhase: -1,
  _enabledEngines: null,
  _errors: 0,
  _isTracking: false,
  _phaseFinished: false,
  _phaselist: {},
  _setupComplete: false,
  _syncActive: false,
  _syncCount: 0,
  _syncsReportedViaTelemetry: 0,
  _syncErrors: 0,
  _syncWipeAction: null,
  _tabsAdded: 0,
  _tabsFinished: 0,
  _test: null,
  _triggeredSync: false,
  _msSinceEpoch: 0,
  _requestedQuit: false,
  shouldValidateAddons: false,
  shouldValidateBookmarks: false,
  shouldValidatePasswords: false,
  shouldValidateForms: false,
  _placesInitDeferred: Promise.withResolvers(),
  SYNC_WIPE_CLIENT,
  SYNC_WIPE_REMOTE,
  ACTIONS: [
    ACTION_ADD,
    ACTION_DELETE,
    ACTION_MODIFY,
    ACTION_SET_ENABLED,
    ACTION_SYNC,
    ACTION_SYNC_RESET_CLIENT,
    ACTION_SYNC_WIPE_CLIENT,
    ACTION_SYNC_WIPE_REMOTE,
    ACTION_VERIFY,
    ACTION_VERIFY_NOT,
  ],

  _init: function TPS__init() {
    this.delayAutoSync();

    OBSERVER_TOPICS.forEach(function (aTopic) {
      Services.obs.addObserver(this, aTopic, true);
    }, this);

    // Some engines bump their score during their sync, which then causes
    // another sync immediately (notably, prefs and addons). We don't want
    // this to happen, and there's no obvious preference to kill it - so
    // we do this nasty hack to ensure the global score is always zero.
    Services.prefs.addObserver("services.sync.globalScore", () => {
      if (lazy.Weave.Service.scheduler.globalScore != 0) {
        lazy.Weave.Service.scheduler.globalScore = 0;
      }
    });
  },

  DumpError(msg, exc = null) {
    this._errors++;
    let errInfo;
    if (exc) {
      errInfo = lazy.Log.exceptionStr(exc); // includes details and stack-trace.
    } else {
      // always write a stack even if no error passed.
      errInfo = lazy.Log.stackTrace(new Error());
    }
    lazy.Logger.logError(`[phase ${this._currentPhase}] ${msg} - ${errInfo}`);
    this.quit();
  },

  QueryInterface: ChromeUtils.generateQI([
    "nsIObserver",
    "nsISupportsWeakReference",
  ]),

  observe: function TPS__observe(subject, topic) {
    try {
      lazy.Logger.logInfo("----------event observed: " + topic);

      switch (topic) {
        case "profile-before-change":
          OBSERVER_TOPICS.forEach(function (aTopic) {
            Services.obs.removeObserver(this, aTopic);
          }, this);

          lazy.Logger.close();

          break;

        case "places-browser-init-complete":
          this._placesInitDeferred.resolve();
          break;

        case "weave:service:setup-complete":
          this._setupComplete = true;

          if (this._syncWipeAction) {
            lazy.Weave.Svc.PrefBranch.setStringPref(
              "firstSync",
              this._syncWipeAction
            );
            this._syncWipeAction = null;
          }

          break;

        case "weave:service:sync:error":
          this._syncActive = false;

          this.delayAutoSync();

          // If this is the first sync error, retry...
          if (this._syncErrors === 0) {
            lazy.Logger.logInfo("Sync error; retrying...");
            this._syncErrors++;
            lazy.CommonUtils.nextTick(() => {
              this.RunNextTestAction().catch(err => {
                this.DumpError("RunNextTestActionFailed", err);
              });
            });
          } else {
            this._triggeredSync = false;
            this.DumpError("Sync error; aborting test");
          }

          break;

        case "weave:service:resyncs-finished":
          this._syncActive = false;
          this._syncErrors = 0;
          this._triggeredSync = false;

          this.delayAutoSync();
          break;

        case "weave:service:sync:start":
          // Ensure that the sync operation has been started by TPS
          if (!this._triggeredSync) {
            this.DumpError(
              "Automatic sync got triggered, which is not allowed."
            );
          }

          this._syncActive = true;
          break;

        case "weave:service:tracking-started":
          this._isTracking = true;
          break;

        case "weave:service:tracking-stopped":
          this._isTracking = false;
          break;

        case "fxaccounts:onlogin":
          // User logged in but sync keys may not be ready yet (OAuth still in progress)
          lazy.Logger.logInfo(
            `User logged in via ${topic}, OAuth flow in progress...`
          );
          break;

        case "fxaccounts:onverified":
          // User verification status changed from false to true - configure sync
          // Note: This only fires when verification status CHANGES from false to true.
          // For pre-verified accounts, this event won't fire at all.
          lazy.Logger.logInfo(
            `User verified via ${topic}, configuring sync...`
          );
          lazy.Weave.Service.configure().catch(e => {
            this.DumpError("Configuring sync failed.", e);
          });
          break;

        default:
          lazy.Logger.logInfo(`unhandled event: ${topic}`);
      }
    } catch (e) {
      this.DumpError("Observer failed", e);
    }
  },

  /**
   * Given that we cannot completely disable the automatic sync operations, we
   * massively delay the next sync. Sync operations have to only happen when
   * directly called via TPS.Sync()!
   */
  delayAutoSync: function TPS_delayAutoSync() {
    lazy.Weave.Svc.PrefBranch.setIntPref("scheduler.immediateInterval", 7200);
    lazy.Weave.Svc.PrefBranch.setIntPref("scheduler.idleInterval", 7200);
    lazy.Weave.Svc.PrefBranch.setIntPref("scheduler.activeInterval", 7200);
    lazy.Weave.Svc.PrefBranch.setIntPref("syncThreshold", 10000000);
  },

  quit: function TPS__quit() {
    lazy.Logger.logInfo("quitting");
    this._requestedQuit = true;
    this.goQuitApplication();
  },

  async HandleWindows(aWindow, action) {
    lazy.Logger.logInfo(
      "executing action " +
        action.toUpperCase() +
        " on window " +
        JSON.stringify(aWindow)
    );
    switch (action) {
      case ACTION_ADD:
        await lazy.BrowserWindows.Add(aWindow.private);
        break;
    }
    lazy.Logger.logPass(
      "executing action " + action.toUpperCase() + " on windows"
    );
  },

  async HandleTabs(tabs, action) {
    for (let tab of tabs) {
      lazy.Logger.logInfo(
        "executing action " +
          action.toUpperCase() +
          " on tab " +
          JSON.stringify(tab)
      );
      switch (action) {
        case ACTION_ADD:
          await lazy.BrowserTabs.Add(tab.uri);
          break;
        case ACTION_VERIFY:
          lazy.Logger.AssertTrue(
            typeof tab.profile != "undefined",
            "profile must be defined when verifying tabs"
          );
          lazy.Logger.AssertTrue(
            await lazy.BrowserTabs.Find(tab.uri, tab.title, tab.profile),
            "error locating tab"
          );
          break;
        case ACTION_VERIFY_NOT:
          lazy.Logger.AssertTrue(
            typeof tab.profile != "undefined",
            "profile must be defined when verifying tabs"
          );
          lazy.Logger.AssertTrue(
            !(await lazy.BrowserTabs.Find(tab.uri, tab.title, tab.profile)),
            "tab found which was expected to be absent"
          );
          break;
        default:
          lazy.Logger.AssertTrue(false, "invalid action: " + action);
      }
    }
    lazy.Logger.logPass(
      "executing action " + action.toUpperCase() + " on tabs"
    );
  },

  async HandlePrefs(prefs, action) {
    for (let pref of prefs) {
      lazy.Logger.logInfo(
        "executing action " +
          action.toUpperCase() +
          " on pref " +
          JSON.stringify(pref)
      );
      let preference = new lazy.Preference(pref);
      switch (action) {
        case ACTION_MODIFY:
          preference.Modify();
          break;
        case ACTION_VERIFY:
          preference.Find();
          break;
        default:
          lazy.Logger.AssertTrue(false, "invalid action: " + action);
      }
    }
    lazy.Logger.logPass(
      "executing action " + action.toUpperCase() + " on pref"
    );
  },

  async HandleForms(data, action) {
    this.shouldValidateForms = true;
    for (let datum of data) {
      lazy.Logger.logInfo(
        "executing action " +
          action.toUpperCase() +
          " on form entry " +
          JSON.stringify(datum)
      );
      let formdata = new lazy.FormData(datum, this._msSinceEpoch);
      switch (action) {
        case ACTION_ADD:
          await formdata.Create();
          break;
        case ACTION_DELETE:
          await formdata.Remove();
          break;
        case ACTION_VERIFY:
          lazy.Logger.AssertTrue(await formdata.Find(), "form data not found");
          break;
        case ACTION_VERIFY_NOT:
          lazy.Logger.AssertTrue(
            !(await formdata.Find()),
            "form data found, but it shouldn't be present"
          );
          break;
        default:
          lazy.Logger.AssertTrue(false, "invalid action: " + action);
      }
    }
    lazy.Logger.logPass(
      "executing action " + action.toUpperCase() + " on formdata"
    );
  },

  async HandleHistory(entries, action) {
    try {
      for (let entry of entries) {
        const entryString = JSON.stringify(entry);
        lazy.Logger.logInfo(
          "executing action " +
            action.toUpperCase() +
            " on history entry " +
            entryString
        );
        switch (action) {
          case ACTION_ADD:
            await lazy.HistoryEntry.Add(entry, this._msSinceEpoch);
            break;
          case ACTION_DELETE:
            await lazy.HistoryEntry.Delete(entry, this._msSinceEpoch);
            break;
          case ACTION_VERIFY:
            lazy.Logger.AssertTrue(
              await lazy.HistoryEntry.Find(entry, this._msSinceEpoch),
              "Uri visits not found in history database: " + entryString
            );
            break;
          case ACTION_VERIFY_NOT:
            lazy.Logger.AssertTrue(
              !(await lazy.HistoryEntry.Find(entry, this._msSinceEpoch)),
              "Uri visits found in history database, but they shouldn't be: " +
                entryString
            );
            break;
          default:
            lazy.Logger.AssertTrue(false, "invalid action: " + action);
        }
      }
      lazy.Logger.logPass(
        "executing action " + action.toUpperCase() + " on history"
      );
    } catch (e) {
      await lazy.DumpHistory();
      throw e;
    }
  },

  async HandlePasswords(passwords, action) {
    this.shouldValidatePasswords = true;
    try {
      for (let password of passwords) {
        lazy.Logger.logInfo(
          "executing action " +
            action.toUpperCase() +
            " on password " +
            JSON.stringify(password)
        );
        let passwordOb = new lazy.Password(password);
        switch (action) {
          case ACTION_ADD:
            lazy.Logger.AssertTrue(
              (await passwordOb.Create()) > -1,
              "error adding password"
            );
            break;
          case ACTION_VERIFY:
            lazy.Logger.AssertTrue(
              (await passwordOb.Find()) != -1,
              "password not found"
            );
            break;
          case ACTION_VERIFY_NOT:
            lazy.Logger.AssertTrue(
              (await passwordOb.Find()) == -1,
              "password found, but it shouldn't exist"
            );
            break;
          case ACTION_DELETE:
            lazy.Logger.AssertTrue(
              (await passwordOb.Find()) != -1,
              "password not found"
            );
            passwordOb.Remove();
            break;
          case ACTION_MODIFY:
            if (passwordOb.updateProps != null) {
              lazy.Logger.AssertTrue(
                (await passwordOb.Find()) != -1,
                "password not found"
              );
              await passwordOb.Update();
            }
            break;
          default:
            lazy.Logger.AssertTrue(false, "invalid action: " + action);
        }
      }
      lazy.Logger.logPass(
        "executing action " + action.toUpperCase() + " on passwords"
      );
    } catch (e) {
      await lazy.DumpPasswords();
      throw e;
    }
  },

  async HandleAddons(addons, action, state) {
    this.shouldValidateAddons = true;
    for (let entry of addons) {
      lazy.Logger.logInfo(
        "executing action " +
          action.toUpperCase() +
          " on addon " +
          JSON.stringify(entry)
      );
      let addon = new lazy.Addon(this, entry);
      switch (action) {
        case ACTION_ADD:
          await addon.install();
          break;
        case ACTION_DELETE:
          await addon.uninstall();
          break;
        case ACTION_VERIFY:
          lazy.Logger.AssertTrue(
            await addon.find(state),
            "addon " + addon.id + " not found"
          );
          break;
        case ACTION_VERIFY_NOT:
          lazy.Logger.AssertFalse(
            await addon.find(state),
            "addon " + addon.id + " is present, but it shouldn't be"
          );
          break;
        case ACTION_SET_ENABLED:
          lazy.Logger.AssertTrue(
            await addon.setEnabled(state),
            "addon " + addon.id + " not found"
          );
          break;
        default:
          throw new Error("Unknown action for add-on: " + action);
      }
    }
    lazy.Logger.logPass(
      "executing action " + action.toUpperCase() + " on addons"
    );
  },

  async HandleBookmarks(bookmarks, action) {
    // wait for default bookmarks to be created.
    await this._placesInitDeferred.promise;
    this.shouldValidateBookmarks = true;
    try {
      let items = [];
      for (let folder in bookmarks) {
        let last_item_pos = -1;
        for (let bookmark of bookmarks[folder]) {
          lazy.Logger.clearPotentialError();
          let placesItem;
          bookmark.location = folder;

          if (last_item_pos != -1) {
            bookmark.last_item_pos = last_item_pos;
          }
          let itemGuid = null;

          if (action != ACTION_MODIFY && action != ACTION_DELETE) {
            lazy.Logger.logInfo(
              "executing action " +
                action.toUpperCase() +
                " on bookmark " +
                JSON.stringify(bookmark)
            );
          }

          if ("uri" in bookmark) {
            placesItem = new lazy.Bookmark(bookmark);
          } else if ("folder" in bookmark) {
            placesItem = new lazy.BookmarkFolder(bookmark);
          } else if ("livemark" in bookmark) {
            placesItem = new lazy.Livemark(bookmark);
          } else if ("separator" in bookmark) {
            placesItem = new lazy.Separator(bookmark);
          }

          if (action == ACTION_ADD) {
            itemGuid = await placesItem.Create();
          } else {
            itemGuid = await placesItem.Find();
            if (action == ACTION_VERIFY_NOT) {
              lazy.Logger.AssertTrue(
                itemGuid == null,
                "places item exists but it shouldn't: " +
                  JSON.stringify(bookmark)
              );
            } else {
              lazy.Logger.AssertTrue(itemGuid, "places item not found", true);
            }
          }

          last_item_pos = await placesItem.GetItemIndex();
          items.push(placesItem);
        }
      }

      if (action == ACTION_DELETE || action == ACTION_MODIFY) {
        for (let item of items) {
          lazy.Logger.logInfo(
            "executing action " +
              action.toUpperCase() +
              " on bookmark " +
              JSON.stringify(item)
          );
          switch (action) {
            case ACTION_DELETE:
              await item.Remove();
              break;
            case ACTION_MODIFY:
              if (item.updateProps != null) {
                await item.Update();
              }
              break;
          }
        }
      }

      lazy.Logger.logPass(
        "executing action " + action.toUpperCase() + " on bookmarks"
      );
    } catch (e) {
      await lazy.DumpBookmarks();
      throw e;
    }
  },

  async HandleAddresses(addresses, action) {
    try {
      for (let address of addresses) {
        lazy.Logger.logInfo(
          "executing action " +
            action.toUpperCase() +
            " on address " +
            JSON.stringify(address)
        );
        let addressOb = new lazy.Address(address);
        switch (action) {
          case ACTION_ADD:
            await addressOb.Create();
            break;
          case ACTION_MODIFY:
            await addressOb.Update();
            break;
          case ACTION_VERIFY:
            lazy.Logger.AssertTrue(await addressOb.Find(), "address not found");
            break;
          case ACTION_VERIFY_NOT:
            lazy.Logger.AssertTrue(
              !(await addressOb.Find()),
              "address found, but it shouldn't exist"
            );
            break;
          case ACTION_DELETE:
            lazy.Logger.AssertTrue(await addressOb.Find(), "address not found");
            await addressOb.Remove();
            break;
          default:
            lazy.Logger.AssertTrue(false, "invalid action: " + action);
        }
      }
      lazy.Logger.logPass(
        "executing action " + action.toUpperCase() + " on addresses"
      );
    } catch (e) {
      await lazy.DumpAddresses();
      throw e;
    }
  },

  async HandleCreditCards(creditCards, action) {
    try {
      for (let creditCard of creditCards) {
        lazy.Logger.logInfo(
          "executing action " +
            action.toUpperCase() +
            " on creditCard " +
            JSON.stringify(creditCard)
        );
        let creditCardOb = new lazy.CreditCard(creditCard);
        switch (action) {
          case ACTION_ADD:
            await creditCardOb.Create();
            break;
          case ACTION_MODIFY:
            await creditCardOb.Update();
            break;
          case ACTION_VERIFY:
            lazy.Logger.AssertTrue(
              await creditCardOb.Find(),
              "creditCard not found"
            );
            break;
          case ACTION_VERIFY_NOT:
            lazy.Logger.AssertTrue(
              !(await creditCardOb.Find()),
              "creditCard found, but it shouldn't exist"
            );
            break;
          case ACTION_DELETE:
            lazy.Logger.AssertTrue(
              await creditCardOb.Find(),
              "creditCard not found"
            );
            await creditCardOb.Remove();
            break;
          default:
            lazy.Logger.AssertTrue(false, "invalid action: " + action);
        }
      }
      lazy.Logger.logPass(
        "executing action " + action.toUpperCase() + " on creditCards"
      );
    } catch (e) {
      await lazy.DumpCreditCards();
      throw e;
    }
  },

  async Cleanup() {
    try {
      await this.WipeServer();
    } catch (ex) {
      lazy.Logger.logError(
        "Failed to wipe server: " + lazy.Log.exceptionStr(ex)
      );
    }
    try {
      if (await lazy.Authentication.isLoggedIn()) {
        // signout and wait for Sync to completely reset itself.
        lazy.Logger.logInfo("signing out");
        let waiter = this.promiseObserver("weave:service:start-over:finish");
        await lazy.Authentication.signOut();
        await waiter;
        lazy.Logger.logInfo("signout complete");
      }
      await lazy.Authentication.deleteEmail(this.config.fx_account.username);
    } catch (e) {
      lazy.Logger.logError("Failed to sign out: " + lazy.Log.exceptionStr(e));
    }
  },

  /**
   * Use Sync's bookmark validation code to see if we've corrupted the tree.
   */
  async ValidateBookmarks() {
    let getServerBookmarkState = async () => {
      let bookmarkEngine = lazy.Weave.Service.engineManager.get("bookmarks");
      let collection = bookmarkEngine.itemSource();
      let collectionKey =
        bookmarkEngine.service.collectionKeys.keyForCollection(
          bookmarkEngine.name
        );
      collection.full = true;
      let items = [];
      let resp = await collection.get();
      for (let json of resp.obj) {
        let record = new collection._recordObj();
        record.deserialize(json);
        await record.decrypt(collectionKey);
        items.push(record.cleartext);
      }
      return items;
    };
    let serverRecordDumpStr;
    try {
      lazy.Logger.logInfo("About to perform bookmark validation");
      let clientTree = await lazy.PlacesUtils.promiseBookmarksTree("", {
        includeItemIds: true,
      });
      let serverRecords = await getServerBookmarkState();
      // We can't wait until catch to stringify this, since at that point it will have cycles.
      serverRecordDumpStr = JSON.stringify(serverRecords);

      let validator = new lazy.BookmarkValidator();
      let { problemData } = await validator.compareServerWithClient(
        serverRecords,
        clientTree
      );

      for (let { name, count } of problemData.getSummary()) {
        // Exclude mobile showing up on the server hackily so that we don't
        // report it every time, see bug 1273234 and 1274394 for more information.
        if (
          name === "serverUnexpected" &&
          problemData.serverUnexpected.includes("mobile")
        ) {
          --count;
        }
        if (count) {
          // Log this out before we assert. This is useful in the context of TPS logs, since we
          // can see the IDs in the test files.
          lazy.Logger.logInfo(
            `Validation problem: "${name}": ${JSON.stringify(
              problemData[name]
            )}`
          );
        }
        lazy.Logger.AssertEqual(
          count,
          0,
          `Bookmark validation error of type ${name}`
        );
      }
    } catch (e) {
      // Dump the client records (should always be doable)
      lazy.DumpBookmarks();
      // Dump the server records if gotten them already.
      if (serverRecordDumpStr) {
        lazy.Logger.logInfo(
          "Server bookmark records:\n" + serverRecordDumpStr + "\n"
        );
      }
      this.DumpError("Bookmark validation failed", e);
    }
    lazy.Logger.logInfo("Bookmark validation finished");
  },

  async ValidateCollection(engineName, ValidatorType) {
    let serverRecordDumpStr;
    let clientRecordDumpStr;
    try {
      lazy.Logger.logInfo(`About to perform validation for "${engineName}"`);
      let engine = lazy.Weave.Service.engineManager.get(engineName);
      let validator = new ValidatorType(engine);
      let serverRecords = await validator.getServerItems(engine);
      let clientRecords = await validator.getClientItems();
      try {
        // This substantially improves the logs for addons while not making a
        // substantial difference for the other two
        clientRecordDumpStr = JSON.stringify(
          clientRecords.map(r => {
            let res = validator.normalizeClientItem(r);
            delete res.original; // Try and prevent cyclic references
            return res;
          })
        );
      } catch (e) {
        // ignore the error, the dump string is just here to make debugging easier.
        clientRecordDumpStr = "<Cyclic value>";
      }
      try {
        serverRecordDumpStr = JSON.stringify(serverRecords);
      } catch (e) {
        // as above
        serverRecordDumpStr = "<Cyclic value>";
      }
      let { problemData } = await validator.compareClientWithServer(
        clientRecords,
        serverRecords
      );
      for (let { name, count } of problemData.getSummary()) {
        if (count) {
          lazy.Logger.logInfo(
            `Validation problem: "${name}": ${JSON.stringify(
              problemData[name]
            )}`
          );
        }
        lazy.Logger.AssertEqual(
          count,
          0,
          `Validation error for "${engineName}" of type "${name}"`
        );
      }
    } catch (e) {
      // Dump the client records if possible
      if (clientRecordDumpStr) {
        lazy.Logger.logInfo(
          `Client state for ${engineName}:\n${clientRecordDumpStr}\n`
        );
      }
      // Dump the server records if gotten them already.
      if (serverRecordDumpStr) {
        lazy.Logger.logInfo(
          `Server state for ${engineName}:\n${serverRecordDumpStr}\n`
        );
      }
      this.DumpError(`Validation failed for ${engineName}`, e);
    }
    lazy.Logger.logInfo(`Validation finished for ${engineName}`);
  },

  ValidatePasswords() {
    return this.ValidateCollection("passwords", lazy.PasswordValidator);
  },

  ValidateForms() {
    return this.ValidateCollection("forms", lazy.FormValidator);
  },

  ValidateAddons() {
    return this.ValidateCollection("addons", lazy.AddonValidator);
  },

  async RunNextTestAction() {
    lazy.Logger.logInfo("Running next test action");
    try {
      if (this._currentAction >= this._phaselist[this._currentPhase].length) {
        // Run necessary validations and then finish up
        lazy.Logger.logInfo("No more actions - running validations...");
        if (this.shouldValidateBookmarks) {
          await this.ValidateBookmarks();
        }
        if (this.shouldValidatePasswords) {
          await this.ValidatePasswords();
        }
        if (this.shouldValidateForms) {
          await this.ValidateForms();
        }
        if (this.shouldValidateAddons) {
          await this.ValidateAddons();
        }
        // Force this early so that we run the validation and detect missing pings
        // *before* we start shutting down, since if we do it after, the python
        // code won't notice the failure.
        lazy.SyncTelemetry.shutdown();
        // we're all done
        lazy.Logger.logInfo(
          "test phase " +
            this._currentPhase +
            ": " +
            (this._errors ? "FAIL" : "PASS")
        );
        this._phaseFinished = true;
        this.quit();
        return;
      }
      this.seconds_since_epoch = Services.prefs.getIntPref(
        "tps.seconds_since_epoch"
      );
      if (this.seconds_since_epoch) {
        // Places dislikes it if we add visits in the future. We pretend the
        // real time is 1 minute ago to avoid issues caused by places using a
        // different clock than the one that set the seconds_since_epoch pref.
        this._msSinceEpoch = (this.seconds_since_epoch - 60) * 1000;
      } else {
        this.DumpError("seconds-since-epoch not set");
        return;
      }

      let phase = this._phaselist[this._currentPhase];
      let action = phase[this._currentAction];
      lazy.Logger.logInfo("starting action: " + action[0].name);
      await action[0].apply(this, action.slice(1));

      this._currentAction++;
    } catch (e) {
      if (lazy.Async.isShutdownException(e)) {
        if (this._requestedQuit) {
          lazy.Logger.logInfo("Sync aborted due to requested shutdown");
        } else {
          this.DumpError(
            "Sync aborted due to shutdown, but we didn't request it"
          );
        }
      } else {
        this.DumpError("RunNextTestAction failed", e);
      }
      return;
    }
    await this.RunNextTestAction();
  },

  /**
   * Runs a single test phase.
   *
   * This is the main entry point for each phase of a test. The TPS command
   * line driver loads this module and calls into the function with the
   * arguments from the command line.
   *
   * When a phase is executed, the file is loaded as JavaScript into the
   * current object.
   *
   * The following keys in the options argument have meaning:
   *
   *   - ignoreUnusedEngines  If true, unused engines will be unloaded from
   *                          Sync. This makes output easier to parse and is
   *                          useful for debugging test failures.
   *
   * @param  file
   *         String URI of the file to open.
   * @param  phase
   *         String name of the phase to run.
   * @param  logpath
   *         String path of the log file to write to.
   * @param  options
   *         Object defining addition run-time options.
   */
  async RunTestPhase(file, phase, logpath, options) {
    try {
      let settings = options || {};

      lazy.Logger.init(logpath);
      lazy.Logger.logInfo("Sync version: " + lazy.WEAVE_VERSION);
      lazy.Logger.logInfo("Firefox buildid: " + Services.appinfo.appBuildID);
      lazy.Logger.logInfo("Firefox version: " + Services.appinfo.version);
      lazy.Logger.logInfo(
        "Firefox source revision: " +
          (AppConstants.SOURCE_REVISION_URL || "unknown")
      );
      lazy.Logger.logInfo("Firefox platform: " + AppConstants.platform);

      // do some sync housekeeping
      if (lazy.Weave.Service.isLoggedIn) {
        this.DumpError("Sync logged in on startup...profile may be dirty");
        return;
      }

      // Wait for Sync service to become ready.
      if (!lazy.Weave.Status.ready) {
        this.waitForEvent("weave:service:ready");
      }

      await lazy.Weave.Service.promiseInitialized;

      // We only want to do this if we modified the bookmarks this phase.
      this.shouldValidateBookmarks = false;

      // Always give Sync an extra tick to initialize. If we waited for the
      // service:ready event, this is required to ensure all handlers have
      // executed.
      await lazy.Async.promiseYield();
      await this._executeTestPhase(file, phase, settings);
    } catch (e) {
      this.DumpError("RunTestPhase failed", e);
    }
  },

  /**
   * Executes a single test phase.
   *
   * This is called by RunTestPhase() after the environment is validated.
   */
  async _executeTestPhase(file, phase, settings) {
    try {
      this.config = JSON.parse(Services.prefs.getStringPref("tps.config"));
      // parse the test file
      Services.scriptloader.loadSubScriptWithOptions(file, {
        target: this,
        allowUnsafeURL: true,
      });
      this._currentPhase = phase;
      // cleanup phases are in the format `cleanup-${profileName}`.
      if (this._currentPhase.startsWith("cleanup-")) {
        let profileToClean = this._currentPhase.slice("cleanup-".length);
        this.phases[this._currentPhase] = profileToClean;
        this.Phase(this._currentPhase, [[this.Cleanup]]);
      }
      let this_phase = this._phaselist[this._currentPhase];

      if (this_phase == undefined) {
        this.DumpError("invalid phase " + this._currentPhase);
        return;
      }

      if (this.phases[this._currentPhase] == undefined) {
        this.DumpError("no profile defined for phase " + this._currentPhase);
        return;
      }

      // If we have restricted the active engines, unregister engines we don't
      // care about.
      if (settings.ignoreUnusedEngines && Array.isArray(this._enabledEngines)) {
        let names = {};
        for (let name of this._enabledEngines) {
          names[name] = true;
        }
        for (let engine of lazy.Weave.Service.engineManager.getEnabled()) {
          if (!(engine.name in names)) {
            lazy.Logger.logInfo("Unregistering unused engine: " + engine.name);
            await lazy.Weave.Service.engineManager.unregister(engine);
          }
        }
      }
      lazy.Logger.logInfo("Starting phase " + this._currentPhase);

      lazy.Logger.logInfo(
        "setting client.name to " + this.phases[this._currentPhase]
      );
      lazy.Weave.Svc.PrefBranch.setStringPref(
        "client.name",
        this.phases[this._currentPhase]
      );

      this._interceptSyncTelemetry();

      // start processing the test actions
      this._currentAction = 0;
      await lazy.SessionStore.promiseAllWindowsRestored;
      await this.RunNextTestAction();
    } catch (e) {
      this.DumpError("_executeTestPhase failed", e);
    }
  },

  /**
   * Override sync telemetry functions so that we can detect errors generating
   * the sync ping, and count how many pings we report.
   */
  _interceptSyncTelemetry() {
    let originalObserve = lazy.SyncTelemetry.observe;
    let self = this;
    lazy.SyncTelemetry.observe = function () {
      try {
        originalObserve.apply(this, arguments);
      } catch (e) {
        self.DumpError("Error when generating sync telemetry", e);
      }
    };
    lazy.SyncTelemetry.submit = record => {
      lazy.Logger.logInfo(
        "Intercepted sync telemetry submission: " + JSON.stringify(record)
      );
      this._syncsReportedViaTelemetry +=
        record.syncs.length + (record.discarded || 0);
      if (record.discarded) {
        if (record.syncs.length != lazy.SyncTelemetry.maxPayloadCount) {
          this.DumpError(
            "Syncs discarded from ping before maximum payload count reached"
          );
        }
      }
      // If this is the shutdown ping, check and see that the telemetry saw all the syncs.
      if (record.why === "shutdown") {
        // If we happen to sync outside of tps manually causing it, its not an
        // error in the telemetry, so we only complain if we didn't see all of them.
        if (this._syncsReportedViaTelemetry < this._syncCount) {
          this.DumpError(
            `Telemetry missed syncs: Saw ${this._syncsReportedViaTelemetry}, should have >= ${this._syncCount}.`
          );
        }
      }
    };
  },

  /**
   * Register a single phase with the test harness.
   *
   * This is called when loading individual test files.
   *
   * @param  phasename
   *         String name of the phase being loaded.
   * @param  fnlist
   *         Array of functions/actions to perform.
   */
  Phase: function Test__Phase(phasename, fnlist) {
    if (Object.keys(this._phaselist).length === 0) {
      // This is the first phase we should force a log in
      fnlist.unshift([this.Login]);
    }
    this._phaselist[phasename] = fnlist;
  },

  /**
   * Restrict enabled Sync engines to a specified set.
   *
   * This can be called by a test to limit what engines are enabled. It is
   * recommended to call it to reduce the overhead and log clutter for the
   * test.
   *
   * The "clients" engine is special and is always enabled, so there is no
   * need to specify it.
   *
   * @param  names
   *         Array of Strings for engines to make active during the test.
   */
  EnableEngines: function EnableEngines(names) {
    if (!Array.isArray(names)) {
      throw new Error(
        "Argument to RestrictEngines() is not an array: " + typeof names
      );
    }

    this._enabledEngines = names;
  },

  /**
   * Returns a promise that resolves when a specific observer notification is
   * resolved. This is similar to the various waitFor* functions, although is
   * typically safer if you need to do some other work that may make the event
   * fire.
   *
   * eg:
   *    doSomething(); // causes the event to be fired.
   *    await promiseObserver("something");
   * is risky as the call to doSomething may trigger the event before the
   * promiseObserver call is made. Contrast with:
   *
   *   let waiter = promiseObserver("something");
   *   doSomething(); // causes the event to be fired.
   *   await waiter;  // will return as soon as the event fires, even if it fires
   *                  // before this function is called.
   *
   * @param aEventName
   *        String event to wait for.
   */
  promiseObserver(aEventName) {
    return new Promise(resolve => {
      lazy.Logger.logInfo("Setting up wait for " + aEventName + "...");
      let handler = () => {
        lazy.Logger.logInfo("Observed " + aEventName);
        lazy.Svc.Obs.remove(aEventName, handler);
        resolve();
      };
      lazy.Svc.Obs.add(aEventName, handler);
    });
  },

  /**
   * Wait for the named event to be observed.
   *
   * Note that in general, you should probably use promiseObserver unless you
   * are 100% sure that the event being waited on can only be sent after this
   * call adds the listener.
   *
   * @param aEventName
   *        String event to wait for.
   */
  async waitForEvent(aEventName) {
    await this.promiseObserver(aEventName);
  },

  /**
   * Waits for Sync to logged in before returning
   */
  async waitForSetupComplete() {
    if (!this._setupComplete) {
      await this.waitForEvent("weave:service:setup-complete");
    }
  },

  /**
   * Waits for Sync to be finished before returning
   */
  async waitForSyncFinished() {
    if (lazy.Weave.Service.locked) {
      await this.waitForEvent("weave:service:resyncs-finished");
    }
  },

  /**
   * Waits for Sync to start tracking before returning.
   */
  async waitForTracking() {
    if (!this._isTracking) {
      await this.waitForEvent("weave:service:tracking-started");
    }
  },

  /**
   * Login on the server
   */
  async Login() {
    if (await lazy.Authentication.isReady()) {
      return;
    }

    // Configure FxA staging server if requested
    if (this.config.fxaStaging) {
      const STAGING_ROOT = "https://accounts.stage.mozaws.net";
      Services.prefs.setStringPref(
        "identity.fxaccounts.autoconfig.uri",
        STAGING_ROOT
      );
      lazy.Logger.logInfo("Using FxA staging autoconfig: " + STAGING_ROOT);
    } else {
      Services.prefs.clearUserPref("identity.fxaccounts.autoconfig.uri");
    }

    if (this.config.autoCreateAccount) {
      await lazy.Authentication.createAndVerifyAccount(
        this.config.fx_account.username,
        this.config.fx_account.password,
        this.config.fxaApiUrl
      );
    }

    lazy.Logger.logInfo("Setting client credentials and login.");
    // wait for setup-complete before calling signIn()
    let setupCompleteWaiter = this.promiseObserver(
      "weave:service:setup-complete"
    );
    await lazy.Authentication.signIn(this.config.fx_account);

    // fxaccounts:onverified only fires when verification status CHANGES from false to true,
    // so for pre-verified accounts we need to check and call configure() ourselves.
    const user = await lazy.Authentication.getSignedInUser();
    if (user && user.verified) {
      lazy.Logger.logInfo(
        "User is verified (pre-verified account), configuring sync..."
      );
      await lazy.Weave.Service.configure();
    } else {
      lazy.Logger.logInfo(
        "User not yet verified, waiting for fxaccounts:onverified to configure sync..."
      );
    }

    await setupCompleteWaiter;
    lazy.Logger.AssertEqual(
      lazy.Weave.Status.service,
      lazy.STATUS_OK,
      "Weave status OK"
    );
    await this.waitForTracking();
  },

  /**
   * Triggers a sync operation
   *
   * @param {string} [wipeAction]
   *        Type of wipe to perform (resetClient, wipeClient, wipeRemote)
   */
  async Sync(wipeAction) {
    if (this._syncActive) {
      this.DumpError("Sync currently active which should be impossible");
      return;
    }
    lazy.Logger.logInfo(
      "Executing Sync" + (wipeAction ? ": " + wipeAction : "")
    );

    // Force a wipe action if requested. In case of an initial sync the pref
    // will be overwritten by Sync itself (see bug 992198), so ensure that we
    // also handle it via the "weave:service:setup-complete" notification.
    if (wipeAction) {
      this._syncWipeAction = wipeAction;
      lazy.Weave.Svc.PrefBranch.setStringPref("firstSync", wipeAction);
    } else {
      lazy.Weave.Svc.PrefBranch.clearUserPref("firstSync");
    }
    if (!(await lazy.Weave.Service.login())) {
      // We need to complete verification.
      lazy.Logger.logInfo("Logging in before performing sync");
      await this.Login();
    }
    ++this._syncCount;

    lazy.Logger.logInfo(
      "Executing Sync" + (wipeAction ? ": " + wipeAction : "")
    );
    this._triggeredSync = true;
    await lazy.Weave.Service.sync();
    lazy.Logger.logInfo("Sync is complete");
    // wait a second for things to settle...
    await new Promise(resolve => {
      lazy.CommonUtils.namedTimer(resolve, 1000, this, "postsync");
    });
  },

  async WipeServer() {
    lazy.Logger.logInfo("Wiping data from server.");

    await this.Login();
    if (!lazy.Weave.Service.storageURL) {
      lazy.Logger.logInfo(
        "No storage node assigned yet, attempting sync login before wipe"
      );
      try {
        await lazy.Weave.Service.login();
      } catch (error) {
        lazy.Logger.logInfo(
          "Sync login before wipe failed: " + (error?.message || error)
        );
      }
    }

    if (!lazy.Weave.Service.storageURL) {
      throw new Error(
        "Cannot wipe server: no storage node assigned after sync login"
      );
    }

    await lazy.Weave.Service.wipeServer();
  },

  /**
   * Action which ensures changes are being tracked before returning.
   */
  async EnsureTracking() {
    await this.Login();
    await this.waitForTracking();
  },

  Addons: {
    async install(addons) {
      await TPS.HandleAddons(addons, ACTION_ADD);
    },
    async setEnabled(addons, state) {
      await TPS.HandleAddons(addons, ACTION_SET_ENABLED, state);
    },
    async uninstall(addons) {
      await TPS.HandleAddons(addons, ACTION_DELETE);
    },
    async verify(addons, state) {
      await TPS.HandleAddons(addons, ACTION_VERIFY, state);
    },
    async verifyNot(addons) {
      await TPS.HandleAddons(addons, ACTION_VERIFY_NOT);
    },
    skipValidation() {
      TPS.shouldValidateAddons = false;
    },
  },

  Addresses: {
    async add(addresses) {
      await this.HandleAddresses(addresses, ACTION_ADD);
    },
    async modify(addresses) {
      await this.HandleAddresses(addresses, ACTION_MODIFY);
    },
    async delete(addresses) {
      await this.HandleAddresses(addresses, ACTION_DELETE);
    },
    async verify(addresses) {
      await this.HandleAddresses(addresses, ACTION_VERIFY);
    },
    async verifyNot(addresses) {
      await this.HandleAddresses(addresses, ACTION_VERIFY_NOT);
    },
  },

  Bookmarks: {
    async add(bookmarks) {
      await TPS.HandleBookmarks(bookmarks, ACTION_ADD);
    },
    async modify(bookmarks) {
      await TPS.HandleBookmarks(bookmarks, ACTION_MODIFY);
    },
    async delete(bookmarks) {
      await TPS.HandleBookmarks(bookmarks, ACTION_DELETE);
    },
    async verify(bookmarks) {
      await TPS.HandleBookmarks(bookmarks, ACTION_VERIFY);
    },
    async verifyNot(bookmarks) {
      await TPS.HandleBookmarks(bookmarks, ACTION_VERIFY_NOT);
    },
    skipValidation() {
      TPS.shouldValidateBookmarks = false;
    },
  },
  CreditCards: {
    async add(creditCards) {
      await this.HandleCreditCards(creditCards, ACTION_ADD);
    },
    async modify(creditCards) {
      await this.HandleCreditCards(creditCards, ACTION_MODIFY);
    },
    async delete(creditCards) {
      await this.HandleCreditCards(creditCards, ACTION_DELETE);
    },
    async verify(creditCards) {
      await this.HandleCreditCards(creditCards, ACTION_VERIFY);
    },
    async verifyNot(creditCards) {
      await this.HandleCreditCards(creditCards, ACTION_VERIFY_NOT);
    },
  },

  Formdata: {
    async add(formdata) {
      await this.HandleForms(formdata, ACTION_ADD);
    },
    async delete(formdata) {
      await this.HandleForms(formdata, ACTION_DELETE);
    },
    async verify(formdata) {
      await this.HandleForms(formdata, ACTION_VERIFY);
    },
    async verifyNot(formdata) {
      await this.HandleForms(formdata, ACTION_VERIFY_NOT);
    },
  },
  History: {
    async add(history) {
      await this.HandleHistory(history, ACTION_ADD);
    },
    async delete(history) {
      await this.HandleHistory(history, ACTION_DELETE);
    },
    async verify(history) {
      await this.HandleHistory(history, ACTION_VERIFY);
    },
    async verifyNot(history) {
      await this.HandleHistory(history, ACTION_VERIFY_NOT);
    },
  },
  Passwords: {
    async add(passwords) {
      await this.HandlePasswords(passwords, ACTION_ADD);
    },
    async modify(passwords) {
      await this.HandlePasswords(passwords, ACTION_MODIFY);
    },
    async delete(passwords) {
      await this.HandlePasswords(passwords, ACTION_DELETE);
    },
    async verify(passwords) {
      await this.HandlePasswords(passwords, ACTION_VERIFY);
    },
    async verifyNot(passwords) {
      await this.HandlePasswords(passwords, ACTION_VERIFY_NOT);
    },
    skipValidation() {
      TPS.shouldValidatePasswords = false;
    },
  },
  Prefs: {
    async modify(prefs) {
      await TPS.HandlePrefs(prefs, ACTION_MODIFY);
    },
    async verify(prefs) {
      await TPS.HandlePrefs(prefs, ACTION_VERIFY);
    },
  },
  Tabs: {
    async add(tabs) {
      await TPS.HandleTabs(tabs, ACTION_ADD);
    },
    async verify(tabs) {
      await TPS.HandleTabs(tabs, ACTION_VERIFY);
    },
    async verifyNot(tabs) {
      await TPS.HandleTabs(tabs, ACTION_VERIFY_NOT);
    },
  },
  Windows: {
    async add(aWindow) {
      await TPS.HandleWindows(aWindow, ACTION_ADD);
    },
  },

  // Jumping through loads of hoops via calling back into a "HandleXXX" method
  // and adding an ACTION_XXX indirection adds no value - let's KISS!
  // eslint-disable-next-line no-unused-vars
  ExtStorage: {
    async set(id, data) {
      lazy.Logger.logInfo(`setting data for '${id}': ${data}`);
      await lazy.extensionStorageSync.set({ id }, data);
    },
    async verify(id, keys, data) {
      let got = await lazy.extensionStorageSync.get({ id }, keys);
      lazy.Logger.AssertEqual(got, data, `data for '${id}'/${keys}`);
    },
  },
};

// Initialize TPS
TPS._init();
