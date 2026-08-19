/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Firefox Accounts Web Channel.
 *
 * Uses the WebChannel component to receive messages
 * about account state changes.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import {
  COMMAND_PROFILE_CHANGE,
  COMMAND_LOGIN,
  COMMAND_LOGOUT,
  COMMAND_OAUTH,
  COMMAND_DELETE,
  COMMAND_CAN_LINK_ACCOUNT,
  COMMAND_SYNC_PREFERENCES,
  COMMAND_CHANGE_PASSWORD,
  COMMAND_FXA_STATUS,
  COMMAND_PAIR_HEARTBEAT,
  COMMAND_PAIR_SUPP_METADATA,
  COMMAND_PAIR_AUTHORIZE,
  COMMAND_PAIR_DECLINE,
  COMMAND_PAIR_COMPLETE,
  COMMAND_PAIR_OAUTH_START,
  COMMAND_PAIR_OAUTH_FINISH,
  COMMAND_PAIR_PREFERENCES,
  COMMAND_FIREFOX_VIEW,
  COMMAND_OAUTH_FLOW_IS_ACTIVE,
  COMMAND_OAUTH_FLOW_BEGIN,
  OAUTH_CLIENT_ID,
  ON_PROFILE_CHANGE_NOTIFICATION,
  ON_SERVICE_ENABLED_NOTIFICATION,
  PREF_LAST_FXA_USER_UID,
  PREF_LAST_FXA_USER_EMAIL,
  SCOPE_OLD_SYNC,
  SCOPE_PROFILE,
  WEBCHANNEL_ID,
  log,
  logPII,
} from "resource://gre/modules/FxAccountsCommon.sys.mjs";
import { SyncDisconnect } from "resource://services-sync/SyncDisconnect.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  CryptoUtils: "moz-src:///services/crypto/modules/utils.sys.mjs",
  FxAccountsPairingFlow: "resource://gre/modules/FxAccountsPairing.sys.mjs",
  FxAccountsStorageManagerCanStoreField:
    "resource://gre/modules/FxAccountsStorage.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  Weave: "resource://services-sync/main.sys.mjs",
  WebChannel: "resource://gre/modules/WebChannel.sys.mjs",
});
ChromeUtils.defineLazyGetter(lazy, "SelectableProfileService", () => {
  try {
    // Only available in Firefox.
    return ChromeUtils.importESModule(
      "resource:///modules/profiles/SelectableProfileService.sys.mjs"
    ).SelectableProfileService;
  } catch (ex) {
    return null;
  }
});
ChromeUtils.defineLazyGetter(lazy, "fxAccounts", () => {
  return ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton();
});
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "pairingEnabled",
  "identity.fxaccounts.pairing.enabled"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "pairingVersion",
  "identity.fxaccounts.pairing.version",
  1
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "separatePrivilegedMozillaWebContentProcess",
  "browser.tabs.remote.separatePrivilegedMozillaWebContentProcess",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "separatedMozillaDomains",
  "browser.tabs.remote.separatedMozillaDomains",
  "",
  false,
  val => val.split(",")
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "accountServer",
  "identity.fxaccounts.remote.root",
  null,
  false,
  val => Services.io.newURI(val)
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "allowSyncMerge",
  "browser.profiles.sync.allow-danger-merge",
  false
);

ChromeUtils.defineLazyGetter(lazy, "l10n", function () {
  return new Localization(["browser/sync.ftl", "branding/brand.ftl"], true);
});

// These engines will be displayed to the user to pick which they would like to
// use.
const CHOOSE_WHAT_TO_SYNC_ALWAYS_AVAILABLE = [
  "addons",
  "bookmarks",
  "history",
  "passwords",
  "prefs",
  "tabs",
];

// Engines which we need to inspect a pref to see if they are available, and
// possibly have their default preference value to disabled.
const CHOOSE_WHAT_TO_SYNC_OPTIONALLY_AVAILABLE = ["addresses", "creditcards"];

/**
 * A helper function that extracts the message and stack from an error object.
 * Returns a `{ message, stack }` tuple. `stack` will be null if the error
 * doesn't have a stack trace.
 */
function getErrorDetails(error) {
  // Replace anything that looks like it might be a filepath on Windows or Unix
  let cleanMessage = String(error)
    .replace(/\\.*\\/gm, "[REDACTED]")
    .replace(/\/.*\//gm, "[REDACTED]");
  let details = { message: cleanMessage, stack: null };

  // Adapted from Console.sys.mjs.
  if (error.stack) {
    let frames = [];
    for (let frame = error.stack; frame; frame = frame.caller) {
      frames.push(String(frame).padStart(4));
    }
    details.stack = frames.join("\n");
  }

  return details;
}

/**
 * Create a new FxAccountsWebChannel to listen for account updates
 *
 * @param {object} options Options
 *   @param {object} options
 *     @param {string} options.content_uri
 *     The FxA Content server uri
 *     @param {string} options.channel_id
 *     The ID of the WebChannel
 *     @param {string} options.helpers
 *     Helpers functions. Should only be passed in for testing.
 * @class
 */
export function FxAccountsWebChannel(options) {
  if (!options) {
    throw new Error("Missing configuration options");
  }
  if (!options.content_uri) {
    throw new Error("Missing 'content_uri' option");
  }
  this._contentUri = options.content_uri;

  if (!options.channel_id) {
    throw new Error("Missing 'channel_id' option");
  }
  this._webChannelId = options.channel_id;

  // options.helpers is only specified by tests.
  ChromeUtils.defineLazyGetter(this, "_helpers", () => {
    return options.helpers || new FxAccountsWebChannelHelpers(options);
  });

  this._setupChannel();
}

FxAccountsWebChannel.prototype = {
  /**
   * WebChannel that is used to communicate with content page
   */
  _channel: null,

  /**
   * Helpers interface that does the heavy lifting.
   */
  _helpers: null,

  /**
   * WebChannel ID.
   */
  _webChannelId: null,
  /**
   * WebChannel origin, used to validate origin of messages
   */
  _webChannelOrigin: null,

  /**
   * The promise which is handling the most recent webchannel message we received.
   * Used to avoid us handling multiple messages concurrently.
   */
  _lastPromise: null,

  /**
   * Release all resources that are in use.
   */
  tearDown() {
    this._channel.stopListening();
    this._channel = null;
    this._channelCallback = null;
  },

  /**
   * Configures and registers a new WebChannel
   *
   * @private
   */
  _setupChannel() {
    // if this.contentUri is present but not a valid URI, then this will throw an error.
    try {
      this._webChannelOrigin = Services.io.newURI(this._contentUri);
      this._registerChannel();
    } catch (e) {
      log.error(e);
      throw e;
    }
  },

  _receiveMessage(message, sendingContext) {
    log.trace(`_receiveMessage for command ${message.command}`);
    let shouldCheckRemoteType =
      lazy.separatePrivilegedMozillaWebContentProcess &&
      lazy.separatedMozillaDomains.some(function (val) {
        return (
          lazy.accountServer.asciiHost == val ||
          lazy.accountServer.asciiHost.endsWith("." + val)
        );
      });
    if (
      shouldCheckRemoteType &&
      sendingContext.remoteType != "privilegedmozilla"
    ) {
      log.error(
        `Rejected FxA webchannel message from remoteType = ${sendingContext.remoteType}`
      );
      return;
    }

    // Here we do some promise dances to ensure we are never handling multiple messages
    // concurrently, which can happen for async message handlers.
    // Not all handlers are async, which is something we should clean up to make this simpler.
    // Start with ensuring the last promise we saw is complete.
    let lastPromise = this._lastPromise || Promise.resolve();
    this._lastPromise = lastPromise
      .then(() => {
        return this._promiseMessage(message, sendingContext);
      })
      .catch(e => {
        log.error("Handling webchannel message failed", e);
        this._sendError(e, message, sendingContext);
      })
      .finally(() => {
        this._lastPromise = null;
      });
  },

  async _promiseMessage(message, sendingContext) {
    const { command, data } = message;
    let browser = sendingContext.browsingContext.top.embedderElement;
    switch (command) {
      case COMMAND_PROFILE_CHANGE:
        Services.obs.notifyObservers(
          null,
          ON_PROFILE_CHANGE_NOTIFICATION,
          data.uid
        );
        break;
      case COMMAND_LOGIN:
        await this._helpers.login(data);
        await this._channel.send(
          { command, messageId: message.messageId, data: { ok: true } },
          sendingContext
        );
        break;
      case COMMAND_OAUTH:
        await this._helpers.oauthLogin(data);
        await this._channel.send(
          { command, messageId: message.messageId, data: { ok: true } },
          sendingContext
        );
        break;
      case COMMAND_OAUTH_FLOW_IS_ACTIVE: {
        const isActive = this._helpers.oauthFlowIsActive();
        await this._channel.send(
          { command, messageId: message.messageId, data: { isActive } },
          sendingContext
        );
        break;
      }
      case COMMAND_OAUTH_FLOW_BEGIN: {
        let params = await this._helpers.oauthBegin(data.scopes);
        await this._channel.send(
          { command, messageId: message.messageId, data: params },
          sendingContext
        );
        break;
      }
      case COMMAND_LOGOUT:
      case COMMAND_DELETE:
        await this._helpers.logout(data.uid);
        await this._channel.send(
          { command, messageId: message.messageId, data: { ok: true } },
          sendingContext
        );
        break;
      case COMMAND_CAN_LINK_ACCOUNT:
        {
          let response = { command, messageId: message.messageId };
          // If browser profiles are not enabled, then we use the old merge sync dialog
          if (!this._helpers._selectableProfilesEnabled()) {
            response.data = { ok: this._helpers.shouldAllowRelink(data) };
            this._channel.send(response, sendingContext);
            break;
          }
          // In the new sync warning, we give users a few more options to
          // control what they want to do with their sync data
          let result =
            await this._helpers.promptProfileSyncWarningIfNeeded(data);
          switch (result.action) {
            case "create-profile":
              lazy.SelectableProfileService.createNewProfile(
                true,
                null,
                "sync-warning"
              );
              response.data = { ok: false };
              break;
            case "switch-profile":
              lazy.SelectableProfileService.launchInstance(result.data);
              response.data = { ok: false };
              break;
            // Either no warning was shown, or user selected the continue option
            // to link the account
            case "continue":
              response.data = { ok: true };
              break;
            case "cancel":
              response.data = { ok: false };
              break;
            default:
              log.error(
                "Invalid FxAccountsWebChannel dialog response: ",
                result.action
              );
              response.data = { ok: false };
              break;
          }
          log.debug("FxAccountsWebChannel response", response);
          // Send the response based on what the user selected above
          this._channel.send(response, sendingContext);
        }
        break;
      case COMMAND_SYNC_PREFERENCES:
        this._helpers.openSyncPreferences(browser, data.entryPoint);
        this._channel.send(
          { command, messageId: message.messageId, data: { ok: true } },
          sendingContext
        );
        break;
      case COMMAND_PAIR_PREFERENCES:
        if (lazy.pairingEnabled) {
          let win = browser.documentGlobal;
          this._channel.send(
            { command, messageId: message.messageId, data: { ok: true } },
            sendingContext
          );
          win.openTrustedLinkIn(
            "about:preferences?action=pair#sync",
            "current"
          );
        }
        break;
      case COMMAND_FIREFOX_VIEW:
        this._helpers.openFirefoxView(browser, data.entryPoint);
        this._channel.send(
          { command, messageId: message.messageId, data: { ok: true } },
          sendingContext
        );
        break;
      case COMMAND_CHANGE_PASSWORD:
        await this._helpers.changePassword(data);
        await this._channel.send(
          { command, messageId: message.messageId, data: { ok: true } },
          sendingContext
        );
        break;
      case COMMAND_FXA_STATUS: {
        log.debug("fxa_status received");
        const service = data && data.service;
        const isPairing = data && data.isPairing;
        const context = data && data.context;
        await this._helpers
          .getFxaStatus(service, sendingContext, isPairing, context)
          .then(fxaStatus => {
            let response = {
              command,
              messageId: message.messageId,
              data: fxaStatus,
            };
            this._channel.send(response, sendingContext);
          });
        break;
      }
      case COMMAND_PAIR_OAUTH_START: {
        this._ensurePairingEnabled(command, 2);
        const params = await this._helpers.pairOAuthStart(data);
        await this._channel.send(
          { command, messageId: message.messageId, data: params },
          sendingContext
        );
        break;
      }
      case COMMAND_PAIR_OAUTH_FINISH: {
        this._ensurePairingEnabled(command, 2);
        const codeAndState = await this._helpers.pairOAuthFinish(data);
        await this._channel.send(
          { command, messageId: message.messageId, data: codeAndState },
          sendingContext
        );
        break;
      }
      case COMMAND_PAIR_HEARTBEAT:
      case COMMAND_PAIR_SUPP_METADATA:
      case COMMAND_PAIR_AUTHORIZE:
      case COMMAND_PAIR_DECLINE:
      case COMMAND_PAIR_COMPLETE: {
        log.debug(`Pairing command ${command} received`);
        const { channel_id: channelId } = data;
        delete data.channel_id;
        const flow = lazy.FxAccountsPairingFlow.get(channelId);
        if (!flow) {
          log.warn(`Could not find a pairing flow for ${channelId}`);
          return;
        }
        flow.onWebChannelMessage(command, data).then(replyData => {
          this._channel.send(
            {
              command,
              messageId: message.messageId,
              data: replyData,
            },
            sendingContext
          );
        });
        break;
      }
      default: {
        let errorMessage = "Unrecognized FxAccountsWebChannel command";
        log.warn(errorMessage, command);
        this._channel.send({
          command,
          messageId: message.messageId,
          data: { error: errorMessage },
        });
        // As a safety measure we also terminate any pending FxA pairing flow.
        lazy.FxAccountsPairingFlow.finalizeAll();
        break;
      }
    }
  },

  _ensurePairingEnabled(command, requiredVersion) {
    if (!lazy.pairingEnabled || lazy.pairingVersion < requiredVersion) {
      throw new Error(`Pairing is disabled for command: ${command}`);
    }
  },

  _sendError(error, incomingMessage, sendingContext) {
    log.error("Failed to handle FxAccountsWebChannel message", error);
    this._channel.send(
      {
        command: incomingMessage.command,
        messageId: incomingMessage.messageId,
        data: {
          error: getErrorDetails(error),
        },
      },
      sendingContext
    );
  },

  /**
   * Create a new channel with the WebChannelBroker, setup a callback listener
   *
   * @private
   */
  _registerChannel() {
    /**
     * Processes messages that are called back from the FxAccountsChannel
     *
     * @param webChannelId {String}
     *        Command webChannelId
     * @param message {Object}
     *        Command message
     * @param sendingContext {Object}
     *        Message sending context.
     *        @param sendingContext.browsingContext {BrowsingContext}
     *               The browsingcontext from which the
     *               WebChannelMessageToChrome was sent.
     *        @param sendingContext.eventTarget {EventTarget}
     *               The <EventTarget> where the message was sent.
     *        @param sendingContext.principal {Principal}
     *               The <Principal> of the EventTarget where the message was sent.
     *        @param sendingContext.remoteType {String}
     *               The remoteType from which the
     *               WebChannelMessageToChrome was sent.
     * @private
     */
    let listener = (webChannelId, message, sendingContext) => {
      if (message) {
        log.debug("FxAccountsWebChannel message received", message.command);
        if (logPII()) {
          log.debug("FxAccountsWebChannel message details", message);
        }
        try {
          this._receiveMessage(message, sendingContext);
        } catch (error) {
          // this should be impossible - _receiveMessage will do this, but better safe than sorry.
          log.error(
            "Unexpected webchannel error escaped from promise error handlers"
          );
          this._sendError(error, message, sendingContext);
        }
      }
    };

    this._channelCallback = listener;
    this._channel = new lazy.WebChannel(
      this._webChannelId,
      this._webChannelOrigin
    );
    this._channel.listen(listener);
    log.debug(
      "FxAccountsWebChannel registered: " +
        this._webChannelId +
        " with origin " +
        this._webChannelOrigin.prePath
    );
  },
};

export function FxAccountsWebChannelHelpers(options) {
  options = options || {};

  this._fxAccounts = options.fxAccounts || lazy.fxAccounts;
  this._weaveXPCOM = options.weaveXPCOM || null;
  this._privateBrowsingUtils =
    options.privateBrowsingUtils || lazy.PrivateBrowsingUtils;
}

FxAccountsWebChannelHelpers.prototype = {
  // If the last fxa account used for sync isn't this account, we display
  // a modal dialog checking they really really want to do this...
  // (This is sync-specific, so ideally would be in sync's identity module,
  // but it's a little more seamless to do here, and sync is currently the
  // only fxa consumer, so...
  shouldAllowRelink(acctData) {
    return (
      !this._needRelinkWarning(acctData) ||
      this._promptForRelink(acctData.email)
    );
  },

  /**
   * Checks if the user is potentially hitting an issue with the current
   * account they're logging into. Returns the choice of the user if shown
   *
   * @returns {string} - The corresponding option the user pressed. Can be either:
   * cancel, continue, switch-profile, or create-profile
   */
  async promptProfileSyncWarningIfNeeded(acctData) {
    // Was a previous account signed into this profile or is there another profile currently signed in
    // to the account we're signing into
    let profileLinkedWithAcct = acctData.uid
      ? await this._getProfileAssociatedWithAcct(acctData.uid)
      : null;
    if (this._needRelinkWarning(acctData) || profileLinkedWithAcct) {
      return this._promptForProfileSyncWarning(
        acctData.email,
        profileLinkedWithAcct
      );
    }
    // The user has no warnings needed and can continue signing in
    return { action: "continue" };
  },

  async _initializeSync() {
    // A sync-specific hack - we want to ensure sync has been initialized
    // before we set the signed-in user.
    // XXX - probably not true any more, especially now we have observerPreloads
    // in FxAccounts.sys.mjs?
    let xps =
      this._weaveXPCOM ||
      Cc["@mozilla.org/weave/service;1"].getService(Ci.nsISupports)
        .wrappedJSObject;
    await xps.whenLoaded();
    return xps;
  },

  _setEnabledEngines(offeredEngines, declinedEngines) {
    if (offeredEngines && declinedEngines) {
      log.debug("Received offered engines", offeredEngines);
      CHOOSE_WHAT_TO_SYNC_OPTIONALLY_AVAILABLE.forEach(engine => {
        if (
          offeredEngines.includes(engine) &&
          !declinedEngines.includes(engine)
        ) {
          // These extra engines are disabled by default.
          log.debug(`Enabling optional engine '${engine}'`);
          Services.prefs.setBoolPref(`services.sync.engine.${engine}`, true);
        }
      });
      log.debug("Received declined engines", declinedEngines);
      lazy.Weave.Service.engineManager.setDeclined(declinedEngines);
      declinedEngines.forEach(engine => {
        Services.prefs.setBoolPref(`services.sync.engine.${engine}`, false);
      });
    } else {
      log.debug("Did not receive any engine selection information");
    }
  },

  /**
   * Internal function used to configure the requested services.
   *
   * The "services" param is an object as received from the FxA server.
   */
  async _enableRequestedServices(requestedServices) {
    if (!requestedServices) {
      log.warn(
        "fxa login completed but we don't have a record of which services were enabled."
      );
      return;
    }
    let services = Object.keys(requestedServices);
    log.debug(`services requested are ${services}`);
    if (requestedServices.sync) {
      const xps = await this._initializeSync();
      const { offeredEngines, declinedEngines } = requestedServices.sync;
      this._setEnabledEngines(offeredEngines, declinedEngines);
      log.debug("Webchannel is enabling sync");
      await xps.Weave.Service.configure();
    }
    for (let service of services) {
      Services.obs.notifyObservers(
        null,
        ON_SERVICE_ENABLED_NOTIFICATION,
        service
      );
    }
  },

  /**
   * The login message is sent when the user user has initially logged in but may not be fully connected.
   * * In the non-oauth flows, if the user is verified, then the browser itself is able to transition the
   *   user to fully connected.
   * * In the oauth flows, we will need an `oauth_login` message with our scoped keys to be fully connected.
   *
   * @param accountData the user's account data and credentials
   */
  async login(accountData) {
    // This is delicate for oauth flows and edge-cases. Consider (a) user logs in but does not verify,
    // (b) browser restarts, (c) user select "finish setup", at which point they are again prompted for their password.
    // In that scenario, we've been sent this `login` message *both* at (a) and at (c).
    // Importantly, the message from (a) is the one that actually has the service information we care about
    // (eg, the sync engine selections) - (c) *will* have `services.sync` but it will be an empty object.
    // This means we need to take care to not lose the services from (a) when processing (c).
    const signedInUser = await this._fxAccounts.getSignedInUser([
      "requestedServices",
    ]);
    let existingServices;
    if (signedInUser) {
      if (signedInUser.uid != accountData.uid) {
        log.warn(
          "the webchannel found a different user signed in - signing them out."
        );
        await this._disconnect();
      } else {
        existingServices = signedInUser.requestedServices
          ? JSON.parse(signedInUser.requestedServices)
          : {};
        log.debug(
          "Webchannel is updating the info for an already logged in user."
        );
      }
    } else {
      log.debug("Webchannel is logging new a user in.");
    }
    // Stuff we never want to keep after being logged in (and no longer get in 2026)
    delete accountData.customizeSync;
    delete accountData.verifiedCanLinkAccount;
    delete accountData.keyFetchToken;
    delete accountData.unwrapBKey;

    // The "services" being connected - see above re our careful handling of existing data.
    // Note that we don't attempt to merge any data - we keep the first value we see for a service
    // and ignore that service subsequently (as it will be common for subsequent messages to
    // name a service but not supply any data for it)
    const requestedServices = {
      ...(accountData.services ?? {}),
      ...existingServices,
    };
    await this._fxAccounts.telemetry.recordConnection(
      Object.keys(requestedServices),
      "webchannel"
    );
    delete accountData.services;
    // We need to remember the requested services because we can't act on them until we get the `oauth_login` message.
    // And because we might not get that message in this browser session (eg, the browser might restart before the
    // user enters their verification code), they are persisted with the account state.
    log.debug(`storing info for services ${Object.keys(requestedServices)}`);
    accountData.requestedServices = JSON.stringify(requestedServices);

    this.setPreviousAccountHashPref(accountData.uid);

    // For scenarios like user is logged in via third-party but wants
    // to enable sync (password) the server will send an additional login command
    // we need to ensure we don't destroy the existing session
    if (signedInUser && signedInUser.uid === accountData.uid) {
      await this._fxAccounts._internal.updateUserAccountData(accountData);
      log.debug("Webchannel finished updating already logged in user.");
    } else {
      await this._fxAccounts._internal.setSignedInUser(accountData);
      log.debug("Webchannel finished logging a user in.");
    }
  },

  /**
   * Logs in to sync by completing an OAuth flow
   *
   * @param {object} oauthData: The oauth code and state as returned by the server
   */
  async oauthLogin(oauthData) {
    log.debug("Webchannel is completing the oauth flow");
    const { uid, sessionToken, requestedServices } =
      await this._fxAccounts._internal.getUserAccountData([
        "uid",
        "sessionToken",
        "requestedServices",
      ]);
    // First we finish the ongoing oauth flow
    const { scopedKeys, refreshToken } =
      await this._fxAccounts._internal.completeOAuthFlow(
        sessionToken,
        oauthData.code,
        oauthData.state
      );

    // We don't currently use the refresh token in Firefox Desktop, lets be good citizens and revoke it.
    await this._fxAccounts._internal.destroyOAuthToken({ token: refreshToken });

    // Remember the account for future merge warnings etc.
    this.setPreviousAccountHashPref(uid);

    if (!scopedKeys) {
      log.info(
        "OAuth login completed without scoped keys; skipping Sync key storage"
      );
    } else {
      // Then, we persist the sync keys
      await this._fxAccounts._internal.setScopedKeys(scopedKeys);
    }

    try {
      let parsedRequestedServices;
      if (requestedServices) {
        parsedRequestedServices = JSON.parse(requestedServices);
      }
      await this._enableRequestedServices(parsedRequestedServices);
    } finally {
      // We don't want them hanging around in storage.
      await this._fxAccounts._internal.updateUserAccountData({
        uid,
        requestedServices: null,
      });
    }

    // Now that we have the scoped keys, we set our status to verified.
    // This will kick off Sync or other services we configured.
    await this._fxAccounts._internal.setUserVerified();
    log.debug("Webchannel completed oauth flows");
  },

  /**
   * Starts a new oauth flow but DOES NOT open any UI for the flow.
   *
   * A browser-driven login takes 2 steps - it begins an oauth flow, then opens a login URL specifying a number of query-params
   * derived from the oauth flow just started. This webchannel message starts a new flow and returns just the query params back over
   * the web channel - it is the responsibility of FxA itself to start a new UI flow using these params.
   *
   * @param [String] scopes: An array of strings.
   */
  async oauthBegin(scopes) {
    log.debug(`Webchannel is starting a new oauth flow for scopes ${scopes}`);
    return await this._fxAccounts._internal.oauth.beginOAuthFlow(scopes);
  },

  /**
   * Starts an OAuth flow on behalf of a pairing supplicant.
   *
   * The browser is the supplicant here - it owns the PKCE verifier and the
   * private key needed to later complete the flow, but it is FxA which relays
   * the resulting public parameters to the authority over the pairing channel.
   *
   * @param {string[]} [scopes] The scopes to request, defaults to the Sync scopes.
   * @returns {Promise<object>} The OAuth parameters the authority needs, ie,
   *   `state`, `scope`, `code_challenge`, `code_challenge_method` and
   *   `keys_jwk`.
   */
  async pairOAuthStart({ scopes = [SCOPE_OLD_SYNC, SCOPE_PROFILE] } = {}) {
    log.debug(`Webchannel is starting a pairing oauth flow for ${scopes}`);
    const { state, scope, code_challenge, code_challenge_method, keys_jwk } =
      await this._fxAccounts._internal.oauth.beginOAuthFlow(scopes);
    return {
      state,
      scope,
      code_challenge,
      code_challenge_method,
      keys_jwk,
    };
  },

  /**
   * Grants an OAuth authorization code for a pairing supplicant.
   *
   * The browser is the pairing authority here - it is already signed in, so it
   * holds the scoped keys and the session token needed to authorize the
   * supplicant's OAuth parameters. FxA relays the returned code and state to
   * the supplicant over the pairing channel.
   *
   * @param {object} oauthParams The supplicant's OAuth parameters, as produced
   *   by `pairOAuthStart` on the supplicant.
   * @returns {Promise<object>} Object containing "code" and "state" properties.
   */
  async pairOAuthFinish({
    client_id,
    state,
    scope,
    code_challenge,
    // `pairOAuthStart` always uses S256, so that's what we assume when the
    // supplicant didn't tell us which method it used.
    code_challenge_method = "S256",
    keys_jwk,
  }) {
    log.debug("Webchannel is authorizing an oauth code for a pairing flow");
    const codeAndState = await this._fxAccounts._internal.authorizeOAuthCode({
      client_id,
      access_type: "offline",
      state,
      scope,
      code_challenge,
      code_challenge_method,
      keys_jwk,
    });
    if (codeAndState.state != state) {
      throw new Error("OAuth state mismatch");
    }
    return codeAndState;
  },

  /**
   * Returns a boolean to indicate whether an oauth flow is in progress.
   */
  oauthFlowIsActive() {
    return this._fxAccounts._internal.oauth.numOfFlows() != 0;
  },

  /**
   * Disconnects the user from Sync and FxA
   */
  _disconnect() {
    return SyncDisconnect.disconnect(false);
  },

  /**
   * logout the fxaccounts service
   *
   * @param the uid of the account which have been logged out
   */
  async logout(uid) {
    let fxa = this._fxAccounts;
    let userData = await fxa._internal.getUserAccountData(["uid"]);
    if (userData && userData.uid === uid) {
      await fxa.telemetry.recordDisconnection(null, "webchannel");
      // true argument is `localOnly`, because server-side stuff
      // has already been taken care of by the content server
      await fxa.signOut(true);
    }
  },

  /**
   * Check if `sendingContext` is in private browsing mode.
   */
  isPrivateBrowsingMode(sendingContext) {
    if (!sendingContext) {
      log.error(
        "Unable to check for private browsing mode (no sending context), assuming true"
      );
      return true;
    }

    let browser = sendingContext.browsingContext.top.embedderElement;
    if (!browser) {
      log.error(
        "Unable to check for private browsing mode (no browser), assuming true"
      );
      return true;
    }
    const isPrivateBrowsing =
      this._privateBrowsingUtils.isBrowserPrivate(browser);
    return isPrivateBrowsing;
  },

  /**
   * Check whether sending fxa_status data should be allowed.
   */
  shouldAllowFxaStatus(service, sendingContext, isPairing, context) {
    // Return user data for any service in non-PB mode. In PB mode,
    // only return user data if service==="sync" or is in pairing mode
    // (as service will be equal to the OAuth client ID and not "sync").
    //
    // This behaviour allows users to click the "Manage Account"
    // link from about:preferences#sync while in PB mode and things
    // "just work". While in non-PB mode, users can sign into
    // Pocket w/o entering their password a 2nd time, while in PB
    // mode they *will* have to enter their email/password again.
    //
    // The difference in behaviour is to try to match user
    // expectations as to what is and what isn't part of the browser.
    // Sync is viewed as an integral part of the browser, interacting
    // with FxA as part of a Sync flow should work all the time. If
    // Sync is broken in PB mode, users will think Firefox is broken.
    // See https://bugzilla.mozilla.org/show_bug.cgi?id=1323853
    let pb = this.isPrivateBrowsingMode(sendingContext);
    let ok = !pb || service === "sync" || isPairing;
    log.debug(
      `fxa status ok=${ok} - private=${pb}, service=${service}, context=${context}, pairing=${isPairing}`
    );
    return ok;
  },

  /**
   * Get fxa_status information. Resolves to { signedInUser: <user_data> }.
   * If returning status information is not allowed or no user is signed into
   * Sync, `user_data` will be null.
   */
  async getFxaStatus(service, sendingContext, isPairing, context) {
    let signedInUser = null;

    if (
      this.shouldAllowFxaStatus(service, sendingContext, isPairing, context)
    ) {
      const userData = await this._fxAccounts._internal.getUserAccountData([
        "email",
        "sessionToken",
        "uid",
        "verified",
      ]);
      if (userData) {
        signedInUser = {
          email: userData.email,
          sessionToken: userData.sessionToken,
          uid: userData.uid,
          verified: userData.verified,
        };
      }
    }

    const capabilities = this._getCapabilities();

    return {
      signedInUser,
      clientId: OAUTH_CLIENT_ID,
      capabilities,
    };
  },

  _getCapabilities() {
    let engines = Array.from(CHOOSE_WHAT_TO_SYNC_ALWAYS_AVAILABLE);
    for (let optionalEngine of CHOOSE_WHAT_TO_SYNC_OPTIONALLY_AVAILABLE) {
      if (
        Services.prefs.getBoolPref(
          `services.sync.engine.${optionalEngine}.available`,
          false
        )
      ) {
        engines.push(optionalEngine);
      }
    }
    return {
      multiService: true,
      pairing: lazy.pairingEnabled,
      pairingVersion: lazy.pairingVersion,
      choose_what_to_sync: true,
      // This capability is for telling FxA that the current build can accept
      // accounts without passwords/sync keys (third-party auth)
      keys_optional: true,
      can_link_account_uid: true,
      engines,
    };
  },

  async changePassword(credentials) {
    // If |credentials| has fields that aren't handled by accounts storage,
    // updateUserAccountData will throw - mainly to prevent errors in code
    // that hard-codes field names.
    // However, in this case the field names aren't really in our control.
    // We *could* still insist the server know what fields names are valid,
    // but that makes life difficult for the server when Firefox adds new
    // features (ie, new fields) - forcing the server to track a map of
    // versions to supported field names doesn't buy us much.
    // So we just remove field names we know aren't handled.
    let newCredentials = {
      device: null, // Force a brand new device registration.
      // We force the re-encryption of the send tab keys using the new sync key after the password change
      encryptedSendTabKeys: null,
    };
    for (let name of Object.keys(credentials)) {
      if (
        name == "email" ||
        name == "uid" ||
        lazy.FxAccountsStorageManagerCanStoreField(name)
      ) {
        newCredentials[name] = credentials[name];
      } else {
        log.info("changePassword ignoring unsupported field", name);
      }
    }
    await this._fxAccounts._internal.updateUserAccountData(newCredentials);
    await this._fxAccounts._internal.updateDeviceRegistration();
  },

  /**
   * Remember that a particular account id was previously signed in to this device.
   *
   * @param uid the account uid
   */
  setPreviousAccountHashPref(uid) {
    if (!uid) {
      throw new Error("No uid specified");
    }
    Services.prefs.setStringPref(
      PREF_LAST_FXA_USER_UID,
      lazy.CryptoUtils.sha256Base64(uid)
    );
    // This should not be necessary but exists just to be safe, to avoid
    // any possibility we somehow end up with *both* prefs set and each indicating
    // a different account.
    Services.prefs.clearUserPref(PREF_LAST_FXA_USER_EMAIL);
  },

  /**
   * Open Sync Preferences in the current tab of the browser
   *
   * @param {object} browser the browser in which to open preferences
   * @param {string} [entryPoint] entryPoint to use for logging
   */
  openSyncPreferences(browser, entryPoint) {
    let uri = "about:preferences";
    if (entryPoint) {
      uri += "?entrypoint=" + encodeURIComponent(entryPoint);
    }
    uri += "#sync";

    browser.loadURI(Services.io.newURI(uri), {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
  },

  /**
   * Open Firefox View in the browser's window
   *
   * @param {object} browser the browser in whose window we'll open Firefox View
   */
  openFirefoxView(browser) {
    browser.documentGlobal.FirefoxViewHandler.openTab("syncedtabs");
  },

  /**
   * If a user signs in using a different account, the data from the
   * previous account and the new account will be merged. Ask the user
   * if they want to continue.
   *
   * @private
   */
  _needRelinkWarning(acctData) {
    // This code *never* expects both PREF_LAST_FXA_USER_EMAIL and PREF_LAST_FXA_USER_UID.
    // * If we have PREF_LAST_FXA_USER_EMAIL it means we were signed out before we migrated
    //   to UID, and can't learn that UID, so have no UID pref set.
    // * If the UID pref exists, our code since that landed will never write to the
    //   PREF_LAST_FXA_USER_EMAIL pref.
    // The only way both could be true would be something catastrophic, such as our
    // "migrate to uid at sign-out" code somehow died between writing the UID and
    // clearing the email.
    //
    // Therefore, we don't even try to handle both being set, but do prefer the UID
    // because that must have been written by the new code paths introduced for that pref.
    const lastUid = Services.prefs.getStringPref(PREF_LAST_FXA_USER_UID, "");
    if (lastUid) {
      // A special case here is for when no uid is specified by the server - that means the
      // server is about to create a new account. Therefore, the new account can't possibly
      // match.
      return (
        !acctData.uid || lastUid != lazy.CryptoUtils.sha256Base64(acctData.uid)
      );
    }

    // no uid pref, check if there's an EMAIL pref (which means a user previously signed out
    // before we landed this uid-aware code, so only know their email.)
    const lastEmail = Services.prefs.getStringPref(
      PREF_LAST_FXA_USER_EMAIL,
      ""
    );
    return (
      lastEmail && lastEmail != lazy.CryptoUtils.sha256Base64(acctData.email)
    );
  },

  // Does this install have multiple profiles available? The SelectableProfileService
  // being enabled isn't enough, because this doesn't tell us whether a new profile
  // as actually created!
  _selectableProfilesEnabled() {
    return (
      lazy.SelectableProfileService?.isEnabled &&
      lazy.SelectableProfileService?.hasCreatedSelectableProfiles()
    );
  },

  // Get the current name of the profile the user is currently on
  _getCurrentProfileName() {
    return lazy.SelectableProfileService?.currentProfile?.name;
  },

  async _getAllProfiles() {
    return await lazy.SelectableProfileService.getAllProfiles();
  },

  /**
   * Checks if a profile is associated with the given account email.
   *
   * @param {string} acctUid - The uid of the account to check.
   * @returns {Promise<SelectableProfile|null>} - The profile associated with the account, or null if none.
   */
  async _getProfileAssociatedWithAcct(acctUid) {
    let profiles = await this._getAllProfiles();
    let currentProfileName = await this._getCurrentProfileName();
    for (let profile of profiles) {
      if (profile.name === currentProfileName) {
        continue; // Skip current profile
      }

      let profilePath = profile.path;
      let signedInUserPath = PathUtils.join(profilePath, "signedInUser.json");
      let signedInUser = await this._readJSONFileAsync(signedInUserPath);
      if (
        signedInUser?.accountData &&
        signedInUser.accountData.uid === acctUid
      ) {
        // The account is signed into another profile
        return profile;
      }
    }
    return null;
  },

  async _readJSONFileAsync(filePath) {
    try {
      let data = await IOUtils.readJSON(filePath);
      if (data && data.version !== 1) {
        throw new Error(
          `Unsupported signedInUser.json version: ${data.version}`
        );
      }
      return data;
    } catch (e) {
      // File not found or error reading/parsing
      return null;
    }
  },

  /**
   * Show the user a warning dialog that the data from the previous account
   * and the new account will be merged. _promptForSyncWarning should be
   * used instead of this
   *
   * @private
   */
  _promptForRelink(acctEmail) {
    let [continueLabel, title, heading, description] =
      lazy.l10n.formatValuesSync([
        { id: "sync-setup-verify-continue" },
        { id: "sync-setup-verify-title" },
        { id: "sync-setup-verify-heading" },
        {
          id: "sync-setup-verify-description",
          args: {
            email: acctEmail,
          },
        },
      ]);
    let body = heading + "\n\n" + description;
    let ps = Services.prompt;
    let buttonFlags =
      ps.BUTTON_POS_0 * ps.BUTTON_TITLE_IS_STRING +
      ps.BUTTON_POS_1 * ps.BUTTON_TITLE_CANCEL +
      ps.BUTTON_POS_1_DEFAULT;

    // If running in context of the browser chrome, window does not exist.
    let pressed = Services.prompt.confirmEx(
      null,
      title,
      body,
      buttonFlags,
      continueLabel,
      null,
      null,
      null,
      {}
    );
    this.emitSyncWarningDialogTelemetry(
      { 0: "continue", 1: "cancel" },
      pressed,
      false // old dialog doesn't have other profiles
    );
    return pressed === 0; // 0 is the "continue" button
  },

  /**
   * Similar to _promptForRelink but more offers more contextual warnings
   * to the user to support browser profiles.
   *
   * @returns {string} - The corresponding option the user pressed. Can be either:
   * cancel, continue, switch-profile, or create-profile
   */
  _promptForProfileSyncWarning(acctEmail, profileLinkedWithAcct) {
    let currentProfile = this._getCurrentProfileName();
    let title, heading, description, mergeLabel, switchLabel;
    if (profileLinkedWithAcct) {
      [title, heading, description, mergeLabel, switchLabel] =
        lazy.l10n.formatValuesSync([
          { id: "sync-account-in-use-header" },
          {
            id: lazy.allowSyncMerge
              ? "sync-account-already-signed-in-header"
              : "sync-account-in-use-header-merge",
            args: {
              acctEmail,
              otherProfile: profileLinkedWithAcct.name,
            },
          },
          {
            id: lazy.allowSyncMerge
              ? "sync-account-in-use-description-merge"
              : "sync-account-in-use-description",
            args: {
              acctEmail,
              currentProfile,
              otherProfile: profileLinkedWithAcct.name,
            },
          },
          {
            id: "sync-button-sync-profile",
            args: { profileName: currentProfile },
          },
          {
            id: "sync-button-switch-profile",
            args: { profileName: profileLinkedWithAcct.name },
          },
        ]);
    } else {
      // This current profile was previously associated with a different account
      [title, heading, description, mergeLabel, switchLabel] =
        lazy.l10n.formatValuesSync([
          {
            id: lazy.allowSyncMerge
              ? "sync-profile-different-account-title-merge"
              : "sync-profile-different-account-title",
          },
          {
            id: "sync-profile-different-account-header",
          },
          {
            id: lazy.allowSyncMerge
              ? "sync-profile-different-account-description-merge"
              : "sync-profile-different-account-description",
            args: {
              acctEmail,
              profileName: currentProfile,
            },
          },
          { id: "sync-button-sync-and-merge" },
          { id: "sync-button-create-profile" },
        ]);
    }
    let result = this.showWarningPrompt({
      title,
      body: `${heading}\n\n${description}`,
      btnLabel1: lazy.allowSyncMerge ? mergeLabel : switchLabel,
      btnLabel2: lazy.allowSyncMerge ? switchLabel : null,
      isAccountLoggedIntoAnotherProfile: !!profileLinkedWithAcct,
    });

    // If the user chose to switch profiles, return the associated profile as well.
    if (result === "switch-profile") {
      return { action: result, data: profileLinkedWithAcct };
    }

    // For all other actions, just return the action name.
    return { action: result };
  },

  /**
   * Shows the user a warning prompt.
   *
   * @returns {string} - The corresponding option the user pressed. Can be either:
   * cancel, continue, switch-profile, or create-profile
   */
  showWarningPrompt({
    title,
    body,
    btnLabel1,
    btnLabel2,
    isAccountLoggedIntoAnotherProfile,
  }) {
    let ps = Services.prompt;
    let buttonFlags;
    let pressed;
    let actionMap = {};

    if (lazy.allowSyncMerge) {
      // Merge allowed: two options + cancel
      buttonFlags =
        ps.BUTTON_POS_0 * ps.BUTTON_TITLE_IS_STRING +
        ps.BUTTON_POS_1 * ps.BUTTON_TITLE_IS_STRING +
        ps.BUTTON_POS_2 * ps.BUTTON_TITLE_CANCEL +
        ps.BUTTON_POS_2_DEFAULT;

      // Define action map based on context
      if (isAccountLoggedIntoAnotherProfile) {
        // Account is associated with another profile
        actionMap = {
          0: "continue", // merge option
          1: "switch-profile",
          2: "cancel",
        };
      } else {
        // Profile was previously logged in with another account
        actionMap = {
          0: "continue", // merge option
          1: "create-profile",
          2: "cancel",
        };
      }

      // Show the prompt
      pressed = ps.confirmEx(
        null,
        title,
        body,
        buttonFlags,
        btnLabel1,
        btnLabel2,
        null,
        null,
        {}
      );
    } else {
      // Merge not allowed: one option + cancel
      buttonFlags =
        ps.BUTTON_POS_0 * ps.BUTTON_TITLE_IS_STRING +
        ps.BUTTON_POS_1 * ps.BUTTON_TITLE_CANCEL +
        ps.BUTTON_POS_1_DEFAULT;

      // Define action map based on context
      if (isAccountLoggedIntoAnotherProfile) {
        // Account is associated with another profile
        actionMap = {
          0: "switch-profile",
          1: "cancel",
        };
      } else {
        // Profile was previously logged in with another account
        actionMap = {
          0: "create-profile",
          1: "cancel",
        };
      }

      // Show the prompt
      pressed = ps.confirmEx(
        null,
        title,
        body,
        buttonFlags,
        btnLabel1,
        null,
        null,
        null,
        {}
      );
    }

    this.emitSyncWarningDialogTelemetry(
      actionMap,
      pressed,
      isAccountLoggedIntoAnotherProfile
    );
    return actionMap[pressed] || "unknown";
  },

  emitSyncWarningDialogTelemetry(
    actionMap,
    pressed,
    isAccountLoggedIntoAnotherProfile
  ) {
    let variant;

    if (!this._selectableProfilesEnabled()) {
      // Old merge dialog
      variant = "old-merge";
    } else if (isAccountLoggedIntoAnotherProfile) {
      // Sync warning dialog for profile already associated
      variant = lazy.allowSyncMerge
        ? "sync-warning-allow-merge"
        : "sync-warning";
    } else {
      // Sync warning dialog for a different account previously logged in
      variant = lazy.allowSyncMerge
        ? "merge-warning-allow-merge"
        : "merge-warning";
    }

    // Telemetry extra options
    let extraOptions = {
      variant_shown: variant,
      option_clicked: actionMap[pressed] || "unknown",
    };

    // Record telemetry
    Glean.syncMergeDialog?.clicked?.record(extraOptions);
  },
};

var singleton;

// The entry-point for this module, which ensures only one of our channels is
// ever created - we require this because the WebChannel is global in scope
// (eg, it uses the observer service to tell interested parties of interesting
// things) and allowing multiple channels would cause such notifications to be
// sent multiple times.
export var EnsureFxAccountsWebChannel = () => {
  let contentUri = Services.urlFormatter.formatURLPref(
    "identity.fxaccounts.remote.root"
  );
  if (singleton && singleton._contentUri !== contentUri) {
    singleton.tearDown();
    singleton = null;
  }
  if (!singleton) {
    try {
      if (contentUri) {
        // The FxAccountsWebChannel listens for events and updates
        // the state machine accordingly.
        singleton = new FxAccountsWebChannel({
          content_uri: contentUri,
          channel_id: WEBCHANNEL_ID,
        });
      } else {
        log.warn("FxA WebChannel functionaly is disabled due to no URI pref.");
      }
    } catch (ex) {
      log.error("Failed to create FxA WebChannel", ex);
    }
  }
};
