// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

import {
  log,
  PREF_REMOTE_PAIRING_URI,
  COMMAND_PAIR_SUPP_METADATA,
  COMMAND_PAIR_AUTHORIZE,
  COMMAND_PAIR_DECLINE,
  COMMAND_PAIR_HEARTBEAT,
  COMMAND_PAIR_COMPLETE,
} from "resource://gre/modules/FxAccountsCommon.sys.mjs";

import {
  getFxAccountsSingleton,
  FxAccounts,
} from "resource://gre/modules/FxAccounts.sys.mjs";

const fxAccounts = getFxAccountsSingleton();
import { setTimeout, clearTimeout } from "resource://gre/modules/Timer.sys.mjs";

ChromeUtils.importESModule("resource://services-common/utils.sys.mjs");
const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  FxAccountsPairingChannel:
    "resource://gre/modules/FxAccountsPairingChannel.sys.mjs",

  Weave: "resource://services-sync/main.sys.mjs",
});

const PAIRING_REDIRECT_URI = "urn:ietf:wg:oauth:2.0:oob:pair-auth-webchannel";
// A pairing flow is not tied to a specific browser window, can also finish in
// various ways and subsequently might leak a Web Socket, so just in case we
// time out and free-up the resources after a specified amount of time.
const FLOW_TIMEOUT_MS = 15 * 60 * 1000; // 15 minutes.

class PairingStateMachine {
  constructor(emitter) {
    this._emitter = emitter;
    this._transition(SuppConnectionPending);
  }

  get currentState() {
    return this._currentState;
  }

  _transition(StateCtor, ...args) {
    const state = new StateCtor(this, ...args);
    this._currentState = state;
  }

  assertState(RequiredStates, messagePrefix = null) {
    if (!(RequiredStates instanceof Array)) {
      RequiredStates = [RequiredStates];
    }
    if (
      !RequiredStates.some(
        RequiredState => this._currentState instanceof RequiredState
      )
    ) {
      const msg = `${
        messagePrefix ? `${messagePrefix}. ` : ""
      }Valid expected states: ${RequiredStates.map(({ name }) => name).join(
        ", "
      )}. Current state: ${this._currentState.label}.`;
      throw new Error(msg);
    }
  }
}

/**
 * The pairing flow can be modeled by a finite state machine:
 * We start by connecting to a WebSocket channel (SuppConnectionPending).
 * Then the other party connects and requests some metadata from us (PendingConfirmations).
 * A confirmation happens locally first (PendingRemoteConfirmation)
 * or the oppposite (PendingLocalConfirmation).
 * Any side can decline this confirmation (Aborted).
 * Once both sides have confirmed, the pairing flow is finished (Completed).
 * During this flow errors can happen and should be handled (Errored).
 */
class State {
  constructor(stateMachine, ...args) {
    this._transition = (...args) => stateMachine._transition(...args);
    this._notify = (...args) => stateMachine._emitter.emit(...args);
    this.init(...args);
  }

  init() {
    /* Does nothing by default but can be re-implemented. */
  }

  get label() {
    return this.constructor.name;
  }

  hasErrored(error) {
    this._notify("view:Error", error);
    this._transition(Errored, error);
  }

  hasAborted() {
    this._transition(Aborted);
  }
}
class SuppConnectionPending extends State {
  suppConnected(sender, oauthOptions) {
    this._transition(PendingConfirmations, sender, oauthOptions);
  }
}
class PendingConfirmationsState extends State {
  localConfirmed() {
    throw new Error("Subclasses must implement this method.");
  }
  remoteConfirmed() {
    throw new Error("Subclasses must implement this method.");
  }
}
class PendingConfirmations extends PendingConfirmationsState {
  init(sender, oauthOptions) {
    this.sender = sender;
    this.oauthOptions = oauthOptions;
  }

  localConfirmed() {
    this._transition(PendingRemoteConfirmation);
  }

  remoteConfirmed() {
    this._transition(PendingLocalConfirmation, this.sender, this.oauthOptions);
  }
}
class PendingLocalConfirmation extends PendingConfirmationsState {
  init(sender, oauthOptions) {
    this.sender = sender;
    this.oauthOptions = oauthOptions;
  }

  localConfirmed() {
    this._transition(Completed);
  }

  remoteConfirmed() {
    throw new Error(
      "Insane state! Remote has already been confirmed at this point."
    );
  }
}
class PendingRemoteConfirmation extends PendingConfirmationsState {
  localConfirmed() {
    throw new Error(
      "Insane state! Local has already been confirmed at this point."
    );
  }

  remoteConfirmed() {
    this._transition(Completed);
  }
}
class Completed extends State {}
class Aborted extends State {}
class Errored extends State {
  init(error) {
    this.error = error;
  }
}

const flows = new Map();

export class FxAccountsPairingFlow {
  static get(channelId) {
    return flows.get(channelId);
  }

  static finalizeAll() {
    for (const flow of flows) {
      flow.finalize();
    }
  }

  static async start(options) {
    const { emitter } = options;
    const fxaConfig = options.fxaConfig || FxAccounts.config;
    const fxa = options.fxAccounts || fxAccounts;
    const weave = options.weave || lazy.Weave;
    const flowTimeout = options.flowTimeout || FLOW_TIMEOUT_MS;

    const contentPairingURI = await fxaConfig.promisePairingURI();
    const wsUri = Services.urlFormatter.formatURLPref(PREF_REMOTE_PAIRING_URI);
    const pairingChannel =
      options.pairingChannel ||
      (await lazy.FxAccountsPairingChannel.create(wsUri));
    const { channelId, channelKey } = pairingChannel;
    const channelKeyB64 = ChromeUtils.base64URLEncode(channelKey, {
      pad: false,
    });
    const pairingFlow = new FxAccountsPairingFlow({
      channelId,
      pairingChannel,
      emitter,
      fxa,
      fxaConfig,
      flowTimeout,
      weave,
    });
    flows.set(channelId, pairingFlow);

    return `${contentPairingURI}#channel_id=${channelId}&channel_key=${channelKeyB64}`;
  }

  constructor(options) {
    this._channelId = options.channelId;
    this._pairingChannel = options.pairingChannel;
    this._emitter = options.emitter;
    this._fxa = options.fxa;
    this._fxai = options.fxai || this._fxa._internal;
    this._fxaConfig = options.fxaConfig;
    this._weave = options.weave;
    this._stateMachine = new PairingStateMachine(this._emitter);
    this._setupListeners();
    this._flowTimeoutId = setTimeout(
      () => this._onFlowTimeout(),
      options.flowTimeout
    );
  }

  _onFlowTimeout() {
    log.warn(`The pairing flow ${this._channelId} timed out.`);
    this._onError(new Error("Timeout"));
    this.finalize();
  }

  _closeChannel() {
    if (!this._closed && !this._pairingChannel.closed) {
      this._pairingChannel.close();
      this._closed = true;
    }
  }

  finalize() {
    this._closeChannel();
    clearTimeout(this._flowTimeoutId);
    // Free up resources and let the GC do its thing.
    flows.delete(this._channelId);
  }

  _setupListeners() {
    this._pairingChannel.addEventListener(
      "message",
      ({ detail: { sender, data } }) =>
        this.onPairingChannelMessage(sender, data)
    );
    this._pairingChannel.addEventListener("error", event =>
      this._onPairingChannelError(event.detail.error)
    );
    this._emitter.on("view:Closed", () => this.onPrefViewClosed());
  }

  _onAbort() {
    this._stateMachine.currentState.hasAborted();
    this.finalize();
  }

  _onError(error) {
    this._stateMachine.currentState.hasErrored(error);
    this._closeChannel();
  }

  _onPairingChannelError(error) {
    log.error("Pairing channel error", error);
    this._onError(error);
  }

  // Any non-falsy returned value is sent back through WebChannel.
  async onWebChannelMessage(command) {
    const stateMachine = this._stateMachine;
    const curState = stateMachine.currentState;
    try {
      switch (command) {
        case COMMAND_PAIR_SUPP_METADATA: {
          stateMachine.assertState(
            [PendingConfirmations, PendingLocalConfirmation],
            `Wrong state for ${command}`
          );
          const {
            ua,
            city,
            region,
            country,
            remote: ipAddress,
          } = curState.sender;
          return { ua, city, region, country, ipAddress };
        }
        case COMMAND_PAIR_AUTHORIZE: {
          stateMachine.assertState(
            [PendingConfirmations, PendingLocalConfirmation],
            `Wrong state for ${command}`
          );
          const {
            client_id,
            state,
            scope,
            code_challenge,
            code_challenge_method,
            keys_jwk,
          } = curState.oauthOptions;
          const authorizeParams = {
            client_id,
            access_type: "offline",
            state,
            scope,
            code_challenge,
            code_challenge_method,
            keys_jwk,
          };
          const codeAndState = await this._authorizeOAuthCode(authorizeParams);
          if (codeAndState.state != state) {
            throw new Error(`OAuth state mismatch`);
          }
          await this._pairingChannel.send({
            message: "pair:auth:authorize",
            data: {
              ...codeAndState,
            },
          });
          curState.localConfirmed();
          break;
        }
        case COMMAND_PAIR_DECLINE:
          this._onAbort();
          break;
        case COMMAND_PAIR_HEARTBEAT: {
          if (curState instanceof Errored || this._pairingChannel.closed) {
            return { err: curState.error.message || "Pairing channel closed" };
          }
          const suppAuthorized = !(
            curState instanceof PendingConfirmations ||
            curState instanceof PendingRemoteConfirmation
          );
          return { suppAuthorized };
        }
        case COMMAND_PAIR_COMPLETE:
          this.finalize();
          break;
        default:
          throw new Error(`Received unknown WebChannel command: ${command}`);
      }
    } catch (e) {
      log.error(e);
      curState.hasErrored(e);
    }
    return {};
  }

  async onPairingChannelMessage(sender, payload) {
    const { message } = payload;
    const stateMachine = this._stateMachine;
    const curState = stateMachine.currentState;
    try {
      switch (message) {
        case "pair:supp:request": {
          stateMachine.assertState(
            SuppConnectionPending,
            `Wrong state for ${message}`
          );
          const oauthUri = await this._fxaConfig.promiseOAuthURI();
          const { uid, email, avatar, displayName } =
            await this._fxa.getSignedInUser();
          const deviceName = this._weave.Service.clientsEngine.localName;
          await this._pairingChannel.send({
            message: "pair:auth:metadata",
            data: {
              email,
              avatar,
              displayName,
              deviceName,
            },
          });
          const {
            client_id,
            state,
            scope,
            code_challenge,
            code_challenge_method,
            keys_jwk,
          } = payload.data;
          const url = new URL(oauthUri);
          url.searchParams.append("client_id", client_id);
          url.searchParams.append("scope", scope);
          url.searchParams.append("email", email);
          url.searchParams.append("uid", uid);
          url.searchParams.append("channel_id", this._channelId);
          url.searchParams.append("redirect_uri", PAIRING_REDIRECT_URI);
          this._emitter.emit("view:SwitchToWebContent", url.href);
          curState.suppConnected(sender, {
            client_id,
            state,
            scope,
            code_challenge,
            code_challenge_method,
            keys_jwk,
          });
          break;
        }
        case "pair:supp:authorize":
          stateMachine.assertState(
            [PendingConfirmations, PendingRemoteConfirmation],
            `Wrong state for ${message}`
          );
          curState.remoteConfirmed();
          break;
        default:
          throw new Error(
            `Received unknown Pairing Channel message: ${message}`
          );
      }
    } catch (e) {
      log.error(e);
      curState.hasErrored(e);
    }
  }

  onPrefViewClosed() {
    const curState = this._stateMachine.currentState;
    // We don't want to stop the pairing process in the later stages.
    if (
      curState instanceof SuppConnectionPending ||
      curState instanceof Aborted ||
      curState instanceof Errored
    ) {
      this.finalize();
    }
  }

  /**
   * Grant an OAuth authorization code for the connecting client.
   *
   * @param {object} options see `FxAccountsOAuth.authorizeOAuthCode`.
   * @returns {Promise<object>} Object containing "code" and "state" properties.
   */
  _authorizeOAuthCode(options) {
    return this._fxai.authorizeOAuthCode(options);
  }
}
