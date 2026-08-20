/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  PeerConnectionIdp: "resource://gre/modules/media/PeerConnectionIdp.sys.mjs",
});

const PC_CONTRACT = "@mozilla.org/dom/peerconnection;1";
const PC_OBS_CONTRACT = "@mozilla.org/dom/peerconnectionobserver;1";
const PC_SESSION_CONTRACT = "@mozilla.org/dom/rtcsessiondescription;1";
const PC_COREQUEST_CONTRACT = "@mozilla.org/dom/createofferrequest;1";

const PC_CID = Components.ID("{bdc2e533-b308-4708-ac8e-a8bfade6d851}");
const PC_OBS_CID = Components.ID("{d1748d4c-7f6a-4dc5-add6-d55b7678537e}");
const PC_SESSION_CID = Components.ID("{1775081b-b62d-4954-8ffe-a067bbf508a7}");
const PC_MANAGER_CID = Components.ID("{7293e901-2be3-4c02-b4bd-cbef6fc24f78}");
const PC_COREQUEST_CID = Components.ID(
  "{74b2122d-65a8-4824-aa9e-3d664cb75dc2}"
);

function logWebRTCMsg(msg, file, line, flag, win) {
  let scriptErrorClass = Cc["@mozilla.org/scripterror;1"];
  let scriptError = scriptErrorClass.createInstance(Ci.nsIScriptError);
  scriptError.initWithWindowID(
    `WebRTC: ${msg}`,
    file,
    line,
    0,
    flag,
    "content javascript",
    win.windowGlobalChild.innerWindowId
  );
  Services.console.logMessage(scriptError);
  if (
    Services.prefs.getBoolPref("media.peerconnection.treat_warnings_as_errors")
  ) {
    throw new win.TypeError(msg);
  }
}

let setupPrototype = (_class, dict) => {
  _class.prototype.classDescription = _class.name;
  Object.assign(_class.prototype, dict);
};

// Global list of PeerConnection objects, so they can be cleaned up when
// a page is torn down. (Maps inner window ID to an array of PC objects).
export class GlobalPCList {
  constructor() {
    this._list = {};
    this._networkdown = false; // XXX Need to query current state somehow
    this._nextId = 1;
    Services.obs.addObserver(this, "inner-window-destroyed");
    Services.obs.addObserver(this, "profile-change-net-teardown");
    Services.obs.addObserver(this, "network:offline-about-to-go-offline");
    Services.obs.addObserver(this, "network:offline-status-changed");
    Services.obs.addObserver(this, "gmp-plugin-crash");
    Services.obs.addObserver(this, "PeerConnection:response:allow");
    Services.obs.addObserver(this, "PeerConnection:response:deny");
    if (Services.cpmm) {
      Services.cpmm.addMessageListener("gmp-plugin-crash", this);
    }
  }

  addPC(pc) {
    let winID = pc._winID;
    if (this._list[winID]) {
      this._list[winID].push(Cu.getWeakReference(pc));
    } else {
      this._list[winID] = [Cu.getWeakReference(pc)];
    }
    pc._globalPCListId = this._nextId++;
    this.removeNullRefs(winID);
  }

  findPC(globalPCListId) {
    for (let winId in this._list) {
      if (this._list.hasOwnProperty(winId)) {
        for (let pcref of this._list[winId]) {
          let pc = pcref.get();
          if (pc && pc._globalPCListId == globalPCListId) {
            return pc;
          }
        }
      }
    }
    return null;
  }

  removeNullRefs(winID) {
    if (this._list[winID] === undefined) {
      return;
    }
    this._list[winID] = this._list[winID].filter(function (e) {
      return e.get() !== null;
    });

    if (this._list[winID].length === 0) {
      delete this._list[winID];
    }
  }

  handleGMPCrash(data) {
    let broadcastPluginCrash = function (list, winID, pluginID, pluginName) {
      if (list.hasOwnProperty(winID)) {
        list[winID].forEach(function (pcref) {
          let pc = pcref.get();
          if (pc) {
            pc._pc.pluginCrash(pluginID, pluginName);
          }
        });
      }
    };

    // a plugin crashed; if it's associated with any of our PCs, fire an
    // event to the DOM window
    for (let winId in this._list) {
      broadcastPluginCrash(this._list, winId, data.pluginID, data.pluginName);
    }
  }

  receiveMessage({ name, data }) {
    if (name == "gmp-plugin-crash") {
      this.handleGMPCrash(data);
    }
  }

  observe(subject, topic, data) {
    let cleanupPcRef = function (pcref) {
      let pc = pcref.get();
      if (pc) {
        pc._suppressEvents = true;
        pc.close();
      }
    };

    let cleanupWinId = function (list, winID) {
      if (list.hasOwnProperty(winID)) {
        list[winID].forEach(cleanupPcRef);
        delete list[winID];
      }
    };

    if (topic == "inner-window-destroyed") {
      let winID = subject.QueryInterface(Ci.nsISupportsPRUint64).data;
      cleanupWinId(this._list, winID);
    } else if (
      topic == "profile-change-net-teardown" ||
      topic == "network:offline-about-to-go-offline"
    ) {
      // As Necko doesn't prevent us from accessing the network we still need to
      // monitor the network offline/online state here. See bug 1326483
      this._networkdown = true;
    } else if (topic == "network:offline-status-changed") {
      if (data == "offline") {
        this._networkdown = true;
      } else if (data == "online") {
        this._networkdown = false;
      }
    } else if (topic == "gmp-plugin-crash") {
      if (subject instanceof Ci.nsIWritablePropertyBag2) {
        let pluginID = subject.getPropertyAsUint32("pluginID");
        let pluginName = subject.getPropertyAsAString("pluginName");
        this.handleGMPCrash({ pluginID, pluginName });
      }
    } else if (
      topic == "PeerConnection:response:allow" ||
      topic == "PeerConnection:response:deny"
    ) {
      var pc = this.findPC(data);
      if (pc) {
        if (topic == "PeerConnection:response:allow") {
          pc._settlePermission.allow();
        } else {
          let err = new pc._win.DOMException(
            "The request is not allowed by " +
              "the user agent or the platform in the current context.",
            "NotAllowedError"
          );
          pc._settlePermission.deny(err);
        }
      }
    }
  }
}

setupPrototype(GlobalPCList, {
  QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),
  classID: PC_MANAGER_CID,
});

var _globalPCList = new GlobalPCList();

// eslint-disable-next-line no-shadow
export class RTCSessionDescription {
  init(win) {
    this._win = win;
    this._winID = this._win.windowGlobalChild.innerWindowId;
    this._legacyPref = Services.prefs.getBoolPref(
      "media.peerconnection.description.legacy.enabled"
    );
  }

  __init({ type, sdp }) {
    Object.assign(this, { _type: type, _sdp: sdp });
  }

  get type() {
    return this._type;
  }
  set type(type) {
    if (!this._legacyPref) {
      // TODO: this throws even in sloppy mode. Remove in bug 1883992
      throw new this._win.TypeError("setting getter-only property type");
    }
    this.warn();
    this._type = type;
  }

  get sdp() {
    return this._sdp;
  }
  set sdp(sdp) {
    if (!this._legacyPref) {
      // TODO: this throws even in sloppy mode. Remove in bug 1883992
      throw new this._win.TypeError("setting getter-only property sdp");
    }
    this.warn();
    this._sdp = sdp;
  }

  warn() {
    if (!this._warned) {
      // Warn once per RTCSessionDescription about deprecated writable usage.
      if (this._legacyPref) {
        this.logMsg(
          "RTCSessionDescription's members are readonly! " +
            "Writing to them is deprecated and will break soon!",
          Ci.nsIScriptError.warningFlag
        );
      } else {
        this.logMsg(
          "RTCSessionDescription's members are readonly! " +
            "Writing to them no longer works!",
          Ci.nsIScriptError.errorFlag
        );
      }
      this._warned = true;
    }
  }

  logMsg(msg, flag) {
    let err = this._win.Error();
    logWebRTCMsg(msg, err.fileName, err.lineNumber, flag, this._win);
  }
}

setupPrototype(RTCSessionDescription, {
  classID: PC_SESSION_CID,
  contractID: PC_SESSION_CONTRACT,
  QueryInterface: ChromeUtils.generateQI(["nsIDOMGlobalPropertyInitializer"]),
});

// eslint-disable-next-line no-shadow
export class RTCPeerConnection {
  constructor() {
    this._pc = null;
    this._closed = false;
    this._pendingLocalDescription = null;
    this._pendingRemoteDescription = null;
    this._currentLocalDescription = null;
    this._currentRemoteDescription = null;
    this._legacyPref = Services.prefs.getBoolPref(
      "media.peerconnection.description.legacy.enabled"
    );

    // http://rtcweb-wg.github.io/jsep/#rfc.section.4.1.9
    // canTrickle == null means unknown; when a remote description is received it
    // is set to true or false based on the presence of the "trickle" ice-option
    this._canTrickle = null;

    this._hasStunServer = this._hasTurnServer = false;
    this._iceGatheredRelayCandidates = false;
  }

  init(win) {
    this._win = win;
  }

  // Pref-based overrides; will _not_ be reflected in getConfiguration
  _applyPrefsToConfig(rtcConfig) {
    if (
      rtcConfig.iceTransportPolicy == "all" &&
      Services.prefs.getBoolPref("media.peerconnection.ice.relay_only")
    ) {
      rtcConfig.iceTransportPolicy = "relay";
    }

    if (
      !rtcConfig.iceServers ||
      !Services.prefs.getBoolPref(
        "media.peerconnection.use_document_iceservers"
      )
    ) {
      try {
        rtcConfig.iceServers = JSON.parse(
          Services.prefs.getCharPref(
            "media.peerconnection.default_iceservers"
          ) || "[]"
        );
      } catch (e) {
        this.logWarning(
          "Ignoring invalid media.peerconnection.default_iceservers in about:config"
        );
        rtcConfig.iceServers = [];
      }
      try {
        this._reviewIceServers(rtcConfig.iceServers);
      } catch (e) {
        this.logWarning(e.message);
        rtcConfig.iceServers = [];
      }
    }
  }

  _validateConfig(rtcConfig) {
    if ("sdpSemantics" in rtcConfig) {
      if (rtcConfig.sdpSemantics == "plan-b") {
        this.logWarning(
          `Outdated and non-standard {sdpSemantics: "plan-b"} is not ` +
            `supported! WebRTC may be unreliable. Please update code to ` +
            `follow standard "unified-plan".`
        );
      }
      // Don't let it show up in getConfiguration.
      delete rtcConfig.sdpSemantics;
    }

    if (this._config) {
      // certificates must match
      if (rtcConfig.certificates.length != this._config.certificates.length) {
        throw new this._win.DOMException(
          "Cannot change certificates with setConfiguration (length differs)",
          "InvalidModificationError"
        );
      }
      for (let i = 0; i < rtcConfig.certificates.length; i++) {
        if (rtcConfig.certificates[i] != this._config.certificates[i]) {
          throw new this._win.DOMException(
            `Cannot change certificates with setConfiguration ` +
              `(cert at index ${i} differs)`,
            "InvalidModificationError"
          );
        }
      }

      // bundlePolicy must match
      if (rtcConfig.bundlePolicy != this._config.bundlePolicy) {
        throw new this._win.DOMException(
          "Cannot change bundlePolicy with setConfiguration",
          "InvalidModificationError"
        );
      }

      // alwaysNegotiateDataChannels must match
      if (
        rtcConfig.alwaysNegotiateDataChannels !=
        this._config.alwaysNegotiateDataChannels
      ) {
        throw new this._win.DOMException(
          "Cannot change alwaysNegotiateDataChannels with setConfiguration",
          "InvalidModificationError"
        );
      }

      // peerIdentity must match
      if (
        rtcConfig.peerIdentity &&
        rtcConfig.peerIdentity != this._config.peerIdentity
      ) {
        throw new this._win.DOMException(
          "Cannot change peerIdentity with setConfiguration",
          "InvalidModificationError"
        );
      }

      // TODO (bug 1529398): iceCandidatePoolSize must match if sLD has ever
      // been called.
    }

    if (rtcConfig.rtcpMuxPolicy == "negotiate") {
      this.logWarning(
        `{rtcpMuxPolicy: "negotiate"} is deprecated and will be removed ` +
          `in a future release. Please use {rtcpMuxPolicy: "require"} instead.`
      );
    }

    // This gets executed in the typical case when iceServers
    // are passed in through the web page.
    this._reviewIceServers(rtcConfig.iceServers);
  }

  _checkIfIceRestartRequired(rtcConfig) {
    if (this._config) {
      if (rtcConfig.iceTransportPolicy != this._config.iceTransportPolicy) {
        this._pc.restartIceNoRenegotiationNeeded();
        return;
      }
      if (
        JSON.stringify(this._config.iceServers) !=
        JSON.stringify(rtcConfig.iceServers)
      ) {
        this._pc.restartIceNoRenegotiationNeeded();
      }
    }
  }

  __init(rtcConfig) {
    this._winID = this._win.windowGlobalChild.innerWindowId;
    let certificates = rtcConfig.certificates || [];

    if (certificates.some(c => c.expires <= Date.now())) {
      throw new this._win.DOMException(
        "Unable to create RTCPeerConnection with an expired certificate",
        "InvalidAccessError"
      );
    }

    // TODO(bug 1531875): Check origin of certs

    // TODO(bug 1176518): Remove this code once we support multiple certs
    let certificate;
    if (certificates.length == 1) {
      certificate = certificates[0];
    } else if (certificates.length) {
      throw new this._win.DOMException(
        "RTCPeerConnection does not currently support multiple certificates",
        "NotSupportedError"
      );
    }

    this._documentPrincipal = Cu.getWebIDLCallerPrincipal();

    if (_globalPCList._networkdown) {
      throw new this._win.DOMException(
        "Can't create RTCPeerConnections when the network is down",
        "InvalidStateError"
      );
    }

    this.makeGetterSetterEH("ontrack");
    this.makeLegacyGetterSetterEH(
      "onaddstream",
      "Use peerConnection.ontrack instead."
    );
    this.makeLegacyGetterSetterEH(
      "onaddtrack",
      "Use peerConnection.ontrack instead."
    );
    this.makeGetterSetterEH("onicecandidate");
    this.makeGetterSetterEH("onicecandidateerror");
    this.makeGetterSetterEH("onnegotiationneeded");
    this.makeGetterSetterEH("onsignalingstatechange");
    this.makeGetterSetterEH("ondatachannel");
    this.makeGetterSetterEH("oniceconnectionstatechange");
    this.makeGetterSetterEH("onicegatheringstatechange");
    this.makeGetterSetterEH("onconnectionstatechange");
    this.makeGetterSetterEH("onidentityresult");
    this.makeGetterSetterEH("onpeeridentity");
    this.makeGetterSetterEH("onidpassertionerror");
    this.makeGetterSetterEH("onidpvalidationerror");

    this._pc = new this._win.PeerConnectionImpl();

    this.__DOM_IMPL__._innerObject = this;
    const observer = new this._win.PeerConnectionObserver(this.__DOM_IMPL__);

    // Add a reference to the PeerConnection to global list (before init).
    _globalPCList.addPC(this);

    this._pc.initialize(observer, this._win);

    this.setConfiguration(rtcConfig);

    this._certificateReady = this._initCertificate(certificate);
    this._initIdp();
  }

  getConfiguration() {
    const config = Object.assign({}, this._config);
    delete config.sdpSemantics;
    return config;
  }

  setConfiguration(rtcConfig) {
    this._checkClosed();
    this._validateConfig(rtcConfig);
    this._checkIfIceRestartRequired(rtcConfig);

    // Allow prefs to tweak these settings before passing to c++, but hide all
    // of that from JS.
    const configWithPrefTweaks = Object.assign({}, rtcConfig);
    this._applyPrefsToConfig(configWithPrefTweaks);
    try {
      this._pc.setConfiguration(configWithPrefTweaks);
    } catch (e) {
      throw this._cloneExceptionInto(e, this._win);
    }

    this._config = Object.assign({}, rtcConfig);
  }

  async _initCertificate(certificate) {
    if (!certificate) {
      certificate = await this._win.RTCPeerConnection.generateCertificate({
        name: "ECDSA",
        namedCurve: "P-256",
      });
    }
    this._pc.certificate = certificate;
  }

  _resetPeerIdentityPromise() {
    this._peerIdentity = new this._win.Promise((resolve, reject) => {
      this._resolvePeerIdentity = resolve;
      this._rejectPeerIdentity = reject;
    });
  }

  _initIdp() {
    this._resetPeerIdentityPromise();
    this._lastIdentityValidation = this._win.Promise.resolve();

    let prefName = "media.peerconnection.identity.timeout";
    let idpTimeout = Services.prefs.getIntPref(prefName);
    this._localIdp = new lazy.PeerConnectionIdp(this._win, idpTimeout);
    this._remoteIdp = new lazy.PeerConnectionIdp(this._win, idpTimeout);
  }

  // Add a function to the internal operations chain.

  _chain(operation) {
    return this._pc.chain(operation);
  }

  // It's basically impossible to use async directly in JSImplemented code,
  // because the implicit promise must be wrapped to the right type for content.
  //
  // The _async wrapper takes care of this. The _legacy wrapper implements
  // legacy callbacks in a manner that produces correct line-numbers in errors,
  // provided that methods validate their inputs before putting themselves on
  // the pc's operations chain.
  //
  // These wrappers also serve as guards against settling promises past close().

  _async(func) {
    return this._win.Promise.resolve(this._closeWrapper(func));
  }

  _legacy(...args) {
    return this._win.Promise.resolve(this._legacyCloseWrapper(...args));
  }

  _auto(onSucc, onErr, func) {
    return typeof onSucc == "function"
      ? this._legacy(onSucc, onErr, func)
      : this._async(func);
  }

  async _closeWrapper(func) {
    let closed = this._closed;
    try {
      let result = await func();
      if (!closed && this._closed) {
        await new Promise(() => {});
      }
      return result;
    } catch (e) {
      if (!closed && this._closed) {
        await new Promise(() => {});
      }
      throw e;
    }
  }

  async _legacyCloseWrapper(onSucc, onErr, func) {
    let wrapCallback = cb => result => {
      try {
        cb && cb(result);
      } catch (e) {
        this.logErrorAndCallOnError(e);
      }
    };

    try {
      wrapCallback(onSucc)(await func());
    } catch (e) {
      wrapCallback(onErr)(e);
    }
  }

  // This implements the fairly common "Queue a task" logic
  async _queueTaskWithClosedCheck(func) {
    const pc = this;
    return new this._win.Promise((resolve, reject) => {
      Services.tm.dispatchToMainThread({
        run() {
          try {
            if (!pc._closed) {
              func();
              resolve();
            }
          } catch (e) {
            reject(e);
          }
        },
      });
    });
  }

  /**
   * An RTCConfiguration may look like this:
   *
   * { "iceServers": [ { urls: "stun:stun.example.org", },
   *                   { url: "stun:stun.example.org", }, // deprecated version
   *                   { urls: ["turn:turn1.x.org", "turn:turn2.x.org"],
   *                     username:"jib", credential:"mypass"} ] }
   *
   * This function normalizes the structure of the input for rtcConfig.iceServers,
   * sets internal flags, and logs warnings. URI syntax validation and credential
   * checks are handled in C++ (PeerConnectionImpl::ParseIceServers).
   */
  _reviewIceServers(iceServers) {
    // Normalize iceServers input
    iceServers.forEach(server => {
      if (typeof server.urls === "string") {
        server.urls = [server.urls];
      } else if (!server.urls && server.url) {
        // TODO: Remove support for legacy iceServer.url eventually (Bug 1116766)
        server.urls = [server.url];
        this.logWarning("RTCIceServer.url is deprecated! Use urls instead.");
      }
    });

    let stunServers = 0;

    iceServers.forEach(({ urls, credentialType }) => {
      if (!urls) {
        // TODO: Remove once url is deprecated (Bug 1369563)
        throw new this._win.TypeError(
          "Missing required 'urls' member of RTCIceServer"
        );
      }
      urls.forEach(url => {
        const colonIdx = url.indexOf(":");
        if (colonIdx < 0) {
          return;
        }
        const scheme = url.substring(0, colonIdx).toLowerCase();

        if (scheme === "turn" || scheme === "turns") {
          if (credentialType != "password") {
            this.logWarning(
              `RTCConfiguration TURN credentialType \"${credentialType}\"` +
                " is not yet implemented. Treating as password." +
                " https://bugzil.la/1247616"
            );
          }
          this._hasTurnServer = true;
          const qIdx = url.indexOf("?");
          if (qIdx < 0 || !url.substring(qIdx).includes("transport=tcp")) {
            // The piggyback srflx candidate
            this._hasStunServer = true;
          }
          stunServers += 1;
        } else if (scheme === "stun") {
          this._hasStunServer = true;
          stunServers += 1;
        }
      });
    });

    if (stunServers >= 5) {
      this.logError(
        "Using five or more STUN/TURN servers slows down discovery"
      );
    }
  }

  // Ideally, this should be of the form _checkState(state),
  // where the state is taken from an enumeration containing
  // the valid peer connection states defined in the WebRTC
  // spec. See Bug 831756.
  _checkClosed() {
    if (this._closed) {
      throw new this._win.DOMException(
        "Peer connection is closed",
        "InvalidStateError"
      );
    }
  }

  // Exceptions thrown by C++ do not propagate across the JSImpl privilege
  // boundary as proper DOMExceptions. Use StructuredCloneHolder to clone
  // into the web content's realm. Promise-based calls use Cu.cloneInto, but
  // cloneInto does not work with exceptions.
  _cloneExceptionInto(e, win) {
    const holder = new StructuredCloneHolder("", "", new ClonedErrorHolder(e));
    return holder.deserialize(win);
  }

  dispatchEvent(event) {
    // PC can close while events are firing if there is an async dispatch
    // in c++ land. But let through "closed" signaling and ice connection events.
    if (!this._suppressEvents) {
      this.__DOM_IMPL__.dispatchEvent(event);
    }
  }

  // Log error message to web console and window.onerror, if present.
  logErrorAndCallOnError(e) {
    this.logMsg(
      e.message,
      e.fileName,
      e.lineNumber,
      Ci.nsIScriptError.errorFlag
    );

    // Safely call onerror directly if present (necessary for testing)
    try {
      if (typeof this._win.onerror === "function") {
        this._win.onerror(e.message, e.fileName, e.lineNumber);
      }
    } catch (err) {
      // If onerror itself throws, service it.
      try {
        this.logMsg(
          err.message,
          err.fileName,
          err.lineNumber,
          Ci.nsIScriptError.errorFlag
        );
      } catch {}
    }
  }

  logError(msg) {
    this.logStackMsg(msg, Ci.nsIScriptError.errorFlag);
  }

  logWarning(msg) {
    this.logStackMsg(msg, Ci.nsIScriptError.warningFlag);
  }

  logStackMsg(msg, flag) {
    let err = this._win.Error();
    this.logMsg(msg, err.fileName, err.lineNumber, flag);
  }

  logMsg(msg, file, line, flag) {
    return logWebRTCMsg(msg, file, line, flag, this._win);
  }

  getEH(type) {
    return this.__DOM_IMPL__.getEventHandler(type);
  }

  setEH(type, handler) {
    this.__DOM_IMPL__.setEventHandler(type, handler);
  }

  makeGetterSetterEH(name) {
    Object.defineProperty(this, name, {
      get() {
        return this.getEH(name);
      },
      set(h) {
        this.setEH(name, h);
      },
    });
  }

  makeLegacyGetterSetterEH(name, msg) {
    Object.defineProperty(this, name, {
      get() {
        return this.getEH(name);
      },
      set(h) {
        this.logWarning(name + " is deprecated! " + msg);
        this.setEH(name, h);
      },
    });
  }

  createOffer(optionsOrOnSucc, onErr, options) {
    let onSuccess = null;
    if (typeof optionsOrOnSucc == "function") {
      onSuccess = optionsOrOnSucc;
    } else {
      options = optionsOrOnSucc;
    }
    // This entry-point handles both new and legacy call sig. Decipher which one
    if (onSuccess) {
      return this._legacy(onSuccess, onErr, () => this._createOffer(options));
    }
    return this._async(() => this._createOffer(options));
  }

  // Ensures that we have at least one transceiver of |kind| that is
  // configured to receive. It will create one if necessary.
  _ensureOfferToReceive(kind) {
    let hasRecv = this.getTransceivers().some(
      transceiver =>
        transceiver.getKind() == kind &&
        (transceiver.direction == "sendrecv" ||
          transceiver.direction == "recvonly") &&
        !transceiver.stopped
    );

    if (!hasRecv) {
      this._addTransceiverNoEvents(kind, { direction: "recvonly" });
    }
  }

  // Handles offerToReceiveAudio/Video
  _ensureTransceiversForOfferToReceive(options) {
    if (options.offerToReceiveAudio) {
      this._ensureOfferToReceive("audio");
    }

    if (options.offerToReceiveVideo) {
      this._ensureOfferToReceive("video");
    }

    this.getTransceivers()
      .filter(transceiver => {
        return (
          (options.offerToReceiveVideo === false &&
            transceiver.receiver.track.kind == "video") ||
          (options.offerToReceiveAudio === false &&
            transceiver.receiver.track.kind == "audio")
        );
      })
      .forEach(transceiver => {
        if (transceiver.direction == "sendrecv") {
          transceiver.setDirectionInternal("sendonly");
        } else if (transceiver.direction == "recvonly") {
          transceiver.setDirectionInternal("inactive");
        }
      });
  }

  _maybeDuplicateFingerprints(sdp) {
    if (this._pc.duplicateFingerprintQuirk) {
      // hack to put a=fingerprint under all m= lines
      const lines = sdp.split("\n");
      const fingerprint = lines.find(line => line.startsWith("a=fingerprint"));
      if (fingerprint) {
        for (let i = 0; i < lines.length; i++) {
          if (lines[i].startsWith("m=")) {
            let j = i + 1;
            while (j < lines.length && lines[j].startsWith("c=")) {
              j++;
            }
            lines.splice(j, 0, fingerprint);
          }
        }
        sdp = lines.join("\n");
      }
    }
    return sdp;
  }

  _createOffer(options) {
    this._checkClosed();
    this._ensureTransceiversForOfferToReceive(options);
    return this._chain(async () =>
      Cu.cloneInto(await this._createAnOffer(options), this._win)
    );
  }

  async _createAnOffer(options = {}) {
    switch (this.signalingState) {
      case "stable":
      case "have-local-offer":
        break;
      default:
        throw new this._win.DOMException(
          `Cannot create offer in ${this.signalingState}`,
          "InvalidStateError"
        );
    }
    let haveAssertion;
    if (this._localIdp.enabled) {
      haveAssertion = this._getIdentityAssertion();
    }
    await this._getPermission();
    await this._certificateReady;
    let sdp = await new Promise((resolve, reject) => {
      this._onCreateOfferSuccess = resolve;
      this._onCreateOfferFailure = reject;
      this._pc.createOffer(options);
    });
    sdp = this._maybeDuplicateFingerprints(sdp);
    if (haveAssertion) {
      await haveAssertion;
      sdp = this._localIdp.addIdentityAttribute(sdp);
    }
    return { type: "offer", sdp };
  }

  createAnswer(optionsOrOnSucc, onErr) {
    // This entry-point handles both new and legacy call sig. Decipher which one
    if (typeof optionsOrOnSucc == "function") {
      return this._legacy(optionsOrOnSucc, onErr, () => this._createAnswer({}));
    }
    return this._async(() => this._createAnswer(optionsOrOnSucc));
  }

  _createAnswer() {
    this._checkClosed();
    return this._chain(async () =>
      Cu.cloneInto(await this._createAnAnswer(), this._win)
    );
  }

  async _createAnAnswer() {
    if (this.signalingState != "have-remote-offer") {
      throw new this._win.DOMException(
        `Cannot create answer in ${this.signalingState}`,
        "InvalidStateError"
      );
    }
    let haveAssertion;
    if (this._localIdp.enabled) {
      haveAssertion = this._getIdentityAssertion();
    }
    await this._getPermission();
    await this._certificateReady;
    let sdp = await new Promise((resolve, reject) => {
      this._onCreateAnswerSuccess = resolve;
      this._onCreateAnswerFailure = reject;
      this._pc.createAnswer();
    });
    sdp = this._maybeDuplicateFingerprints(sdp);
    if (haveAssertion) {
      await haveAssertion;
      sdp = this._localIdp.addIdentityAttribute(sdp);
    }
    return { type: "answer", sdp };
  }

  async _getPermission() {
    if (!this._havePermission) {
      const privileged =
        this._documentPrincipal.isSystemPrincipal ||
        Services.prefs.getBoolPref("media.navigator.permission.disabled");

      if (privileged) {
        this._havePermission = Promise.resolve();
      } else {
        this._havePermission = new Promise((resolve, reject) => {
          this._settlePermission = { allow: resolve, deny: reject };
          let outerId = this._win.docShell.outerWindowID;

          let chrome = new CreateOfferRequest(
            outerId,
            this._winID,
            this._globalPCListId,
            false
          );
          let request = this._win.CreateOfferRequest._create(this._win, chrome);
          Services.obs.notifyObservers(request, "PeerConnection:request");
        });
      }
    }
    return this._havePermission;
  }

  _sanityCheckSdp(sdp) {
    // The fippo butter finger filter AKA non-ASCII chars
    // Note: SDP allows non-ASCII character in the subject (who cares?)
    // eslint-disable-next-line no-control-regex
    let pos = sdp.search(/[^\u0000-\u007f]/);
    if (pos != -1) {
      throw new this._win.DOMException(
        "SDP contains non ASCII characters at position " + pos,
        "InvalidParameterError"
      );
    }
  }

  setLocalDescription(desc, onSucc, onErr) {
    return this._auto(onSucc, onErr, () => this._setLocalDescription(desc));
  }

  _setLocalDescription({ type, sdp }) {
    if (type == "pranswer") {
      throw new this._win.DOMException(
        "pranswer not yet implemented",
        "NotSupportedError"
      );
    }
    this._checkClosed();
    return this._chain(async () => {
      // Avoid Promise.all ahead of synchronous part of spec algorithm, since it
      // defers. NOTE: The spec says to return an already-rejected promise in
      // some cases, which is difficult to achieve in practice from JS (would
      // require avoiding await and then() entirely), but we want to come as
      // close as we reasonably can.
      const p = this._getPermission();
      if (!type) {
        switch (this.signalingState) {
          case "stable":
          case "have-local-offer":
          case "have-remote-pranswer":
            type = "offer";
            break;
          default:
            type = "answer";
            break;
        }
      }
      if (!sdp) {
        if (type == "offer") {
          sdp = (await this._createAnOffer()).sdp;
        } else if (type == "answer") {
          sdp = (await this._createAnAnswer()).sdp;
        }
      } else {
        this._sanityCheckSdp(sdp);
      }

      try {
        await new Promise((resolve, reject) => {
          this._onSetDescriptionSuccess = resolve;
          this._onSetDescriptionFailure = reject;
          this._pc.setLocalDescription(this._actions[type], sdp);
        });
        await p;
      } catch (e) {
        this._pc.onSetDescriptionError();
        throw e;
      }
      await this._pc.onSetDescriptionSuccess(type, false);
    });
  }

  async _validateIdentity(sdp, origin) {
    // Only run a single identity verification at a time.  We have to do this to
    // avoid problems with the fact that identity validation doesn't block the
    // resolution of setRemoteDescription().
    const validate = async () => {
      // Access this._pc synchronously in case pc is closed later
      const identity = this._pc.peerIdentity;
      await this._lastIdentityValidation;
      const msg = await this._remoteIdp.verifyIdentityFromSDP(sdp, origin);
      // If this pc has an identity already, then the identity in sdp must match
      if (identity && (!msg || msg.identity !== identity)) {
        throw new this._win.DOMException(
          "Peer Identity mismatch, expected: " + identity,
          "OperationError"
        );
      }
      if (this._closed) {
        return;
      }
      if (msg) {
        // Set new identity and generate an event.
        this._pc.peerIdentity = msg.identity;
        this._resolvePeerIdentity(
          Cu.cloneInto(
            {
              idp: this._remoteIdp.provider,
              name: msg.identity,
            },
            this._win
          )
        );
      }
    };

    const haveValidation = validate();

    // Always eat errors on this chain
    this._lastIdentityValidation = haveValidation.catch(() => {});

    // If validation fails, we have some work to do. Fork it so it cannot
    // interfere with the validation chain itself, even if the catch function
    // throws.
    haveValidation.catch(e => {
      if (this._closed) {
        return;
      }
      this._rejectPeerIdentity(e);

      // If we don't expect a specific peer identity, failure to get a valid
      // peer identity is not a terminal state, so replace the promise to
      // allow another attempt.
      if (!this._pc.peerIdentity) {
        this._resetPeerIdentityPromise();
      }
    });

    if (this._closed) {
      return;
    }
    // Only wait for IdP validation if we need identity matching
    if (this._pc.peerIdentity) {
      await haveValidation;
    }
  }

  setRemoteDescription(desc, onSucc, onErr) {
    return this._auto(onSucc, onErr, () => this._setRemoteDescription(desc));
  }

  _setRemoteDescription({ type, sdp }) {
    if (type == "pranswer") {
      throw new this._win.DOMException(
        "pranswer not yet implemented",
        "NotSupportedError"
      );
    }
    this._checkClosed();
    return this._chain(async () => {
      try {
        if (type == "offer" && this.signalingState == "have-local-offer") {
          await new Promise((resolve, reject) => {
            this._onSetDescriptionSuccess = resolve;
            this._onSetDescriptionFailure = reject;
            this._pc.setLocalDescription(
              Ci.IPeerConnection.kActionRollback,
              ""
            );
          });
          await this._pc.onSetDescriptionSuccess("rollback", false);
          this._updateCanTrickle();
        }

        if (this._closed) {
          return;
        }

        this._sanityCheckSdp(sdp);

        const p = this._getPermission();

        const haveSetRemote = new Promise((resolve, reject) => {
          this._onSetDescriptionSuccess = resolve;
          this._onSetDescriptionFailure = reject;
          this._pc.setRemoteDescription(this._actions[type], sdp);
        });

        if (type != "rollback") {
          // Do setRemoteDescription and identity validation in parallel
          await this._validateIdentity(sdp);
        }
        await p;
        await haveSetRemote;
      } catch (e) {
        this._pc.onSetDescriptionError();
        throw e;
      }

      await this._pc.onSetDescriptionSuccess(type, true);
      this._updateCanTrickle();
    });
  }

  setIdentityProvider(provider, { protocol, usernameHint, peerIdentity } = {}) {
    this._checkClosed();
    peerIdentity = peerIdentity || this._pc.peerIdentity;
    this._localIdp.setIdentityProvider(
      provider,
      protocol,
      usernameHint,
      peerIdentity
    );
  }

  async _getIdentityAssertion() {
    await this._certificateReady;
    return this._localIdp.getIdentityAssertion(
      this._pc.fingerprint,
      this._documentPrincipal.origin
    );
  }

  getIdentityAssertion() {
    this._checkClosed();
    return this._win.Promise.resolve(
      this._chain(() => this._getIdentityAssertion())
    );
  }

  get canTrickleIceCandidates() {
    return this._canTrickle;
  }

  _updateCanTrickle() {
    let containsTrickle = section => {
      let lines = section.toLowerCase().split(/(?:\r\n?|\n)/);
      return lines.some(line => {
        let prefix = "a=ice-options:";
        if (line.substring(0, prefix.length) !== prefix) {
          return false;
        }
        let tokens = line.substring(prefix.length).split(" ");
        return tokens.some(x => x === "trickle");
      });
    };

    let desc = null;
    try {
      // The getter for remoteDescription can throw if the pc is closed.
      desc = this.remoteDescription;
    } catch (e) {}
    if (!desc) {
      this._canTrickle = null;
      return;
    }

    let sections = desc.sdp.split(/(?:\r\n?|\n)m=/);
    let topSection = sections.shift();
    this._canTrickle =
      containsTrickle(topSection) || sections.every(containsTrickle);
  }

  addIceCandidate(cand, onSucc, onErr) {
    // Spec requires us to return an already-rejected (with TypeError) promise
    // if this fails, without any chaining or using RTCIceCandidate's
    // constructor to catch the error.
    if (
      cand.candidate != "" &&
      cand.sdpMid == null &&
      cand.sdpMLineIndex == null
    ) {
      throw new this._win.TypeError(
        "Cannot add a candidate without specifying either sdpMid or sdpMLineIndex"
      );
    }
    return this._auto(onSucc, onErr, () => this._addIceCandidate(cand));
  }

  async _addIceCandidate({
    candidate,
    sdpMid,
    sdpMLineIndex,
    usernameFragment,
  }) {
    this._checkClosed();
    return this._chain(async () => {
      if (
        !this._pc.pendingRemoteDescription.length &&
        !this._pc.currentRemoteDescription.length
      ) {
        throw new this._win.DOMException(
          "No remoteDescription.",
          "InvalidStateError"
        );
      }
      return new Promise((resolve, reject) => {
        this._onAddIceCandidateSuccess = resolve;
        this._onAddIceCandidateError = reject;
        this._pc.addIceCandidate(
          candidate,
          sdpMid || "",
          usernameFragment || "",
          sdpMLineIndex
        );
      });
    });
  }

  restartIce() {
    this._pc.restartIce();
  }

  addStream(stream) {
    stream.getTracks().forEach(track => this.addTrack(track, stream));
  }

  addTrack(track, ...streams) {
    this._checkClosed();

    if (
      this.getTransceivers().some(
        transceiver => transceiver.sender.track == track
      )
    ) {
      throw new this._win.DOMException(
        "This track is already set on a sender.",
        "InvalidAccessError"
      );
    }

    let transceiver = this.getTransceivers().find(
      t =>
        t.sender.track == null &&
        t.getKind() == track.kind &&
        !t.stopped &&
        !t.hasBeenUsedToSend()
    );

    if (transceiver) {
      transceiver.sender.setTrack(track);
      transceiver.sender.setStreamsImpl(...streams);
      if (transceiver.direction == "recvonly") {
        transceiver.setDirectionInternal("sendrecv");
      } else if (transceiver.direction == "inactive") {
        transceiver.setDirectionInternal("sendonly");
      }
    } else {
      transceiver = this._addTransceiverNoEvents(
        track,
        {
          streams,
          direction: "sendrecv",
        },
        true
      );
    }

    this.updateNegotiationNeeded();
    return transceiver.sender;
  }

  removeTrack(sender) {
    this._checkClosed();

    if (!this._pc.createdSender(sender)) {
      throw new this._win.DOMException(
        "This sender was not created by this PeerConnection",
        "InvalidAccessError"
      );
    }

    let transceiver = this.getTransceivers().find(
      t => !t.stopped && t.sender == sender
    );

    // If the transceiver was removed due to rollback, let it slide.
    if (!transceiver || !sender.track) {
      return;
    }

    sender.setTrack(null);
    if (transceiver.direction == "sendrecv") {
      transceiver.setDirectionInternal("recvonly");
    } else if (transceiver.direction == "sendonly") {
      transceiver.setDirectionInternal("inactive");
    }

    this.updateNegotiationNeeded();
  }

  _addTransceiverNoEvents(sendTrackOrKind, init, addTrackMagic) {
    let sendTrack = null;
    let kind;
    if (typeof sendTrackOrKind == "string") {
      kind = sendTrackOrKind;
      switch (kind) {
        case "audio":
        case "video":
          break;
        default:
          throw new this._win.TypeError("Invalid media kind");
      }
    } else {
      sendTrack = sendTrackOrKind;
      kind = sendTrack.kind;
    }

    try {
      return this._pc.addTransceiver(init, kind, sendTrack, addTrackMagic);
    } catch (e) {
      throw this._cloneExceptionInto(e, this._win);
    }
  }

  addTransceiver(sendTrackOrKind, init) {
    this._checkClosed();
    let transceiver = this._addTransceiverNoEvents(sendTrackOrKind, init);
    this.updateNegotiationNeeded();
    return transceiver;
  }

  updateNegotiationNeeded() {
    this._pc.updateNegotiationNeeded();
  }

  close() {
    if (this._closed) {
      return;
    }
    this._closed = true;
    if (this._localIdp) {
      this._localIdp.close();
    }
    if (this._remoteIdp) {
      this._remoteIdp.close();
    }
    this._pc.close();
    this._suppressEvents = true;
  }

  getLocalStreams() {
    this._checkClosed();
    let localStreams = new Set();
    this.getTransceivers().forEach(transceiver => {
      transceiver.sender.getStreams().forEach(stream => {
        localStreams.add(stream);
      });
    });
    return [...localStreams.values()];
  }

  getRemoteStreams() {
    this._checkClosed();
    return this._pc.getRemoteStreams();
  }

  getSenders() {
    return this.getTransceivers()
      .filter(transceiver => !transceiver.stopped)
      .map(transceiver => transceiver.sender);
  }

  getReceivers() {
    return this.getTransceivers()
      .filter(transceiver => !transceiver.stopped)
      .map(transceiver => transceiver.receiver);
  }

  mozSetPacketCallback(callback) {
    this._onPacket = callback;
  }

  mozEnablePacketDump(level, type, sending) {
    this._pc.enablePacketDump(level, type, sending);
  }

  mozDisablePacketDump(level, type, sending) {
    this._pc.disablePacketDump(level, type, sending);
  }

  getTransceivers() {
    return this._pc.getTransceivers();
  }

  get localDescription() {
    return this.pendingLocalDescription || this.currentLocalDescription;
  }

  cacheDescription(name, type, sdp) {
    if (
      !this[name] ||
      this[name].type != type ||
      this[name].sdp != sdp ||
      this._legacyPref
    ) {
      this[name] = sdp.length
        ? new this._win.RTCSessionDescription({ type, sdp })
        : null;
    }
    return this[name];
  }

  get currentLocalDescription() {
    this._checkClosed();
    return this.cacheDescription(
      "_currentLocalDescription",
      this._pc.currentOfferer ? "offer" : "answer",
      this._pc.currentLocalDescription
    );
  }

  get pendingLocalDescription() {
    this._checkClosed();
    return this.cacheDescription(
      "_pendingLocalDescription",
      this._pc.pendingOfferer ? "offer" : "answer",
      this._pc.pendingLocalDescription
    );
  }

  get remoteDescription() {
    return this.pendingRemoteDescription || this.currentRemoteDescription;
  }

  get currentRemoteDescription() {
    this._checkClosed();
    return this.cacheDescription(
      "_currentRemoteDescription",
      this._pc.currentOfferer ? "answer" : "offer",
      this._pc.currentRemoteDescription
    );
  }

  get pendingRemoteDescription() {
    this._checkClosed();
    return this.cacheDescription(
      "_pendingRemoteDescription",
      this._pc.pendingOfferer ? "answer" : "offer",
      this._pc.pendingRemoteDescription
    );
  }

  get peerIdentity() {
    return this._peerIdentity;
  }
  get idpLoginUrl() {
    return this._localIdp.idpLoginUrl;
  }
  get id() {
    return this._pc.id;
  }
  set id(s) {
    this._pc.id = s;
  }
  get iceGatheringState() {
    return this._pc.iceGatheringState;
  }
  get iceConnectionState() {
    return this._pc.iceConnectionState;
  }
  get connectionState() {
    return this._pc.connectionState;
  }

  get signalingState() {
    // checking for our local pc closed indication
    // before invoking the pc methods.
    if (this._closed) {
      return "closed";
    }
    return this._pc.signalingState;
  }

  handleIceGatheringStateChange() {
    this.dispatchEvent(new this._win.Event("icegatheringstatechange"));
    if (this.iceGatheringState === "complete") {
      this.dispatchEvent(
        new this._win.RTCPeerConnectionIceEvent("icecandidate", {
          candidate: null,
        })
      );
    }
  }

  getStats(selector, onSucc, onErr) {
    if (selector !== null) {
      let matchingSenders = this.getSenders().filter(s => s.track === selector);
      let matchingReceivers = this.getReceivers().filter(
        r => r.track === selector
      );

      if (matchingSenders.length + matchingReceivers.length != 1) {
        throw new this._win.DOMException(
          "track must be associated with a unique sender or receiver, but " +
            " is associated with " +
            matchingSenders.length +
            " senders and " +
            matchingReceivers.length +
            " receivers.",
          "InvalidAccessError"
        );
      }
    }

    return this._auto(onSucc, onErr, () => this._pc.getStats(selector));
  }

  get sctp() {
    return this._pc.sctp;
  }

  createDataChannel(
    label,
    {
      maxRetransmits,
      ordered,
      negotiated,
      id = null,
      maxRetransmitTime,
      maxPacketLifeTime,
      protocol,
    } = {}
  ) {
    this._checkClosed();

    if (maxPacketLifeTime === undefined) {
      maxPacketLifeTime = maxRetransmitTime;
    }

    if (maxRetransmitTime !== undefined) {
      this.logWarning(
        "Use maxPacketLifeTime instead of deprecated maxRetransmitTime which will stop working soon in createDataChannel!"
      );
    }

    if (protocol.length > 32767) {
      // At least 65536/2 UTF-16 characters. UTF-8 might be too long.
      // Spec says to check how long |protocol| and |label| are in _bytes_. This
      // is a little ambiguous. For now, examine the length of the utf-8 encoding.
      const byteCounter = new TextEncoder();

      if (byteCounter.encode(protocol).length > 65535) {
        throw new this._win.TypeError(
          "protocol cannot be longer than 65535 bytes"
        );
      }
    }

    if (label.length > 32767) {
      const byteCounter = new TextEncoder();
      if (byteCounter.encode(label).length > 65535) {
        throw new this._win.TypeError(
          "label cannot be longer than 65535 bytes"
        );
      }
    }

    if (!negotiated) {
      id = null;
    } else if (id === null) {
      throw new this._win.TypeError("id is required when negotiated is true");
    }
    if (maxPacketLifeTime !== undefined && maxRetransmits !== undefined) {
      throw new this._win.TypeError(
        "Both maxPacketLifeTime and maxRetransmits cannot be provided"
      );
    }
    if (id == 65535) {
      throw new this._win.TypeError("id cannot be 65535");
    }
    // Must determine the type where we still know if entries are undefined.
    let type;
    if (maxPacketLifeTime !== undefined) {
      type = Ci.IPeerConnection.kDataChannelPartialReliableTimed;
    } else if (maxRetransmits !== undefined) {
      type = Ci.IPeerConnection.kDataChannelPartialReliableRexmit;
    } else {
      type = Ci.IPeerConnection.kDataChannelReliable;
    }
    // Synchronous since it doesn't block.
    let dataChannel;
    try {
      dataChannel = this._pc.createDataChannel(
        label,
        protocol,
        type,
        ordered,
        maxPacketLifeTime,
        maxRetransmits,
        negotiated,
        id
      );
    } catch (e) {
      if (e.result != Cr.NS_ERROR_NOT_AVAILABLE) {
        throw e;
      }

      const msg =
        id === null ? "No available id could be generated" : "Id is in use";
      throw new this._win.DOMException(msg, "OperationError");
    }

    // Spec says to only do this if this is the first DataChannel created,
    // but the c++ code that does the "is negotiation needed" checking will
    // only ever return true on the first one.
    this.updateNegotiationNeeded();

    return dataChannel;
  }
}

setupPrototype(RTCPeerConnection, {
  classID: PC_CID,
  contractID: PC_CONTRACT,
  QueryInterface: ChromeUtils.generateQI(["nsIDOMGlobalPropertyInitializer"]),
  _actions: {
    offer: Ci.IPeerConnection.kActionOffer,
    answer: Ci.IPeerConnection.kActionAnswer,
    pranswer: Ci.IPeerConnection.kActionPRAnswer,
    rollback: Ci.IPeerConnection.kActionRollback,
  },
});

// This is a separate class because we don't want to expose it to DOM.

// eslint-disable-next-line no-shadow
export class PeerConnectionObserver {
  init(win) {
    this._win = win;
  }

  __init(dompc) {
    this._dompc = dompc._innerObject;
  }

  newError({ message, name, errorDetail, sdpLineNumber }) {
    if (errorDetail !== undefined) {
      return new this._dompc._win.RTCError(
        { errorDetail, sdpLineNumber },
        message
      );
    }
    return new this._dompc._win.DOMException(message, name);
  }

  dispatchEvent(event) {
    this._dompc.dispatchEvent(event);
  }

  onCreateOfferSuccess(sdp) {
    this._dompc._onCreateOfferSuccess(sdp);
  }

  onCreateOfferError(error) {
    this._dompc._onCreateOfferFailure(this.newError(error));
  }

  onCreateAnswerSuccess(sdp) {
    this._dompc._onCreateAnswerSuccess(sdp);
  }

  onCreateAnswerError(error) {
    this._dompc._onCreateAnswerFailure(this.newError(error));
  }

  onSetDescriptionSuccess() {
    this._dompc._onSetDescriptionSuccess();
  }

  onSetDescriptionError(error) {
    this._dompc._onSetDescriptionFailure(this.newError(error));
  }

  onAddIceCandidateSuccess() {
    this._dompc._onAddIceCandidateSuccess();
  }

  onAddIceCandidateError(error) {
    this._dompc._onAddIceCandidateError(this.newError(error));
  }

  onIceCandidate(sdpMLineIndex, sdpMid, candidate, usernameFragment) {
    let win = this._dompc._win;
    if (candidate || sdpMid || usernameFragment) {
      if (candidate.includes(" typ relay ")) {
        this._dompc._iceGatheredRelayCandidates = true;
      }
      candidate = new win.RTCIceCandidate({
        candidate,
        sdpMid,
        sdpMLineIndex,
        usernameFragment,
      });
    }
    this.dispatchEvent(
      new win.RTCPeerConnectionIceEvent("icecandidate", { candidate })
    );
  }

  onIceCandidateError(address, port, url, errorCode, errorText) {
    let win = this._dompc._win;
    this.dispatchEvent(
      new win.RTCPeerConnectionIceErrorEvent("icecandidateerror", {
        address: address !== "" ? address : null,
        port: port !== 0 ? port : null,
        url,
        errorCode,
        errorText,
      })
    );
  }

  logIceConnectionStateChange(iceConnectionState) {
    let pc = this._dompc;

    if (iceConnectionState === "failed") {
      if (!pc._hasStunServer) {
        pc.logError(
          "ICE failed, add a STUN server and see about:webrtc for more details"
        );
      } else if (!pc._hasTurnServer) {
        pc.logError(
          "ICE failed, add a TURN server and see about:webrtc for more details"
        );
      } else if (pc._hasTurnServer && !pc._iceGatheredRelayCandidates) {
        pc.logError(
          "ICE failed, your TURN server appears to be broken, see about:webrtc for more details"
        );
      } else {
        pc.logError("ICE failed, see about:webrtc for more details");
      }
    }
  }

  onStateChange(state) {
    if (!this._dompc) {
      return;
    }

    if (state == "SignalingState") {
      this.dispatchEvent(new this._win.Event("signalingstatechange"));
      return;
    }

    if (!this._dompc._pc) {
      return;
    }

    switch (state) {
      case "IceConnectionState":
        this.logIceConnectionStateChange(this._dompc.iceConnectionState);
        this.dispatchEvent(new this._win.Event("iceconnectionstatechange"));
        break;

      case "IceGatheringState":
        this._dompc.handleIceGatheringStateChange();
        break;

      case "ConnectionState":
        this.dispatchEvent(new this._win.Event("connectionstatechange"));
        break;

      default:
        this._dompc.logWarning("Unhandled state type: " + state);
        break;
    }
  }

  onTransceiverNeeded(kind, transceiverImpl) {
    this._dompc._onTransceiverNeeded(kind, transceiverImpl);
  }

  notifyDataChannel(channel) {
    this.dispatchEvent(
      new this._dompc._win.RTCDataChannelEvent("datachannel", { channel })
    );
  }

  fireTrackEvent(receiver, streams) {
    const pc = this._dompc;
    const transceiver = pc.getTransceivers().find(t => t.receiver == receiver);
    if (!transceiver) {
      return;
    }
    const track = receiver.track;
    this.dispatchEvent(
      new this._win.RTCTrackEvent("track", {
        transceiver,
        receiver,
        track,
        streams,
      })
    );
    // Fire legacy event as well for a little bit.
    this.dispatchEvent(
      new this._win.MediaStreamTrackEvent("addtrack", { track })
    );
  }

  fireStreamEvent(stream) {
    const ev = new this._win.MediaStreamEvent("addstream", { stream });
    this.dispatchEvent(ev);
  }

  fireNegotiationNeededEvent() {
    this.dispatchEvent(new this._win.Event("negotiationneeded"));
  }

  onPacket(level, type, sending, packet) {
    var pc = this._dompc;
    if (pc._onPacket) {
      pc._onPacket(level, type, sending, packet);
    }
  }
}

setupPrototype(PeerConnectionObserver, {
  classID: PC_OBS_CID,
  contractID: PC_OBS_CONTRACT,
  QueryInterface: ChromeUtils.generateQI(["nsIDOMGlobalPropertyInitializer"]),
});

// eslint-disable-next-line no-shadow
export class CreateOfferRequest {
  constructor(windowID, innerWindowID, callID, isSecure) {
    Object.assign(this, { windowID, innerWindowID, callID, isSecure });
  }
}

setupPrototype(CreateOfferRequest, {
  classID: PC_COREQUEST_CID,
  contractID: PC_COREQUEST_CONTRACT,
  QueryInterface: ChromeUtils.generateQI([]),
});
