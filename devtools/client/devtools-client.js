/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const DevToolsUtils = require("resource://devtools/shared/DevToolsUtils.js");
const {
  getStack,
  callFunctionWithAsyncStack,
} = require("resource://devtools/shared/platform/stack.js");
const EventEmitter = require("resource://devtools/shared/event-emitter.js");
const {
  UnsolicitedNotifications,
} = require("resource://devtools/client/constants.js");
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

loader.lazyRequireGetter(
  this,
  "Authentication",
  "resource://devtools/shared/security/auth.js"
);
loader.lazyRequireGetter(
  this,
  "DebuggerSocket",
  "resource://devtools/shared/security/socket.js",
  true
);
loader.lazyRequireGetter(
  this,
  "EventEmitter",
  "resource://devtools/shared/event-emitter.js"
);

loader.lazyRequireGetter(
  this,
  ["createRootFront", "Front"],
  "resource://devtools/shared/protocol.js",
  true
);

loader.lazyRequireGetter(
  this,
  "ObjectFront",
  "resource://devtools/client/fronts/object.js",
  true
);

const logger = console.createInstance({
  logModule: "devtools_rdp",
  maxLogLevel: "Warn",
});

// Hack MOZ_LOG/Console.cpp usage of ToSource logic
// to be able to write raw strings to stdout.
// This prevent being wrapped with quotes, and use color characters.
const SEND_MOZ_LOG_STRING = {
  toSource() {
    return "\x1b[2m->\x1b[0m";
  },
};
const RECEIVE_MOZ_LOG_STRING = {
  toSource() {
    return "\x1b[2m<-\x1b[0m";
  },
};

/**
 * Creates a client for the remote debugging protocol server. This client
 * provides the means to communicate with the server and exchange the messages
 * required by the protocol in a traditional JavaScript API.
 */
class DevToolsClient extends EventEmitter {
  constructor(transport) {
    super();

    this._transport = transport;
    this._transport.hooks = this;

    this._pendingRequests = new Map();
    this._activeRequests = new Map();
    this._eventsEnabled = true;

    this.traits = {};

    this.request = this.request.bind(this);

    /*
     * As the first thing on the connection, expect a greeting packet from
     * the connection's root actor.
     */
    this.mainRoot = null;
    this.expectReply("root", async packet => {
      if (packet.error) {
        console.error("Error when waiting for root actor", packet);
        return;
      }

      this.mainRoot = createRootFront(this, packet);

      // Once the root actor has been communicated by the server,
      // emit a request to it to also push informations down to the server.
      //
      // This request has been added in Firefox 133.
      try {
        await this.mainRoot.connect({
          frontendVersion: AppConstants.MOZ_APP_VERSION,
        });
      } catch (e) {
        // Ignore errors of unsupported packet as the server may not yet support this request.
        // The request may also fail to complete in tests when closing DevTools quickly after opening.
        if (!e.message.includes("unrecognizedPacketType")) {
          throw e;
        }
      }

      this.emit("connected", packet.applicationType, packet.traits);
    });
  }

  // Expose these to save callers the trouble of importing DebuggerSocket
  static socketConnect(options) {
    // Defined here instead of just copying the function to allow lazy-load
    return DebuggerSocket.connect(options);
  }

  static get Authenticators() {
    return Authentication.Authenticators;
  }

  static get AuthenticationResult() {
    return Authentication.AuthenticationResult;
  }

  /**
   * Connect to the server and start exchanging protocol messages.
   *
   * @return Promise
   *         Resolves once connected with an array whose first element
   *         is the application type, by default "browser", and the second
   *         element is the traits object (help figure out the features
   *         and behaviors of the server we connect to. See RootActor).
   */
  connect() {
    return new Promise(resolve => {
      this.once("connected", (applicationType, traits) => {
        this.traits = traits;

        resolve([applicationType, traits]);
      });

      this._transport.ready();
    });
  }

  /**
   * Shut down communication with the debugging server.
   *
   * @return Promise
   *         Resolves after the underlying transport is closed.
   */
  close() {
    if (this._transportClosed) {
      return Promise.resolve();
    }
    if (this._closePromise) {
      return this._closePromise;
    }
    // Immediately set the destroy promise,
    // as the following code is fully synchronous and can be reentrant.
    this._closePromise = this.once("closed");

    // Disable detach event notifications, because event handlers will be in a
    // cleared scope by the time they run.
    this._eventsEnabled = false;

    if (this._transport) {
      this._transport.close();
      this._transport = null;
    }

    return this._closePromise;
  }

  /**
   * Send a request to the debugging server.
   *
   * @param packet object
   *        A JSON packet to send to the debugging server.
   * @return Request
   *         This object emits a number of events to allow you to respond to
   *         different parts of the request lifecycle.
   *         It is also a Promise object, with a `then` method, that is resolved
   *         whenever a JSON or a Bulk response is received; and is rejected
   *         if the response is an error.
   *
   *         Events emitted:
   *         * json-reply: The server replied with a JSON packet, which is
   *           passed as event data.
   *         * bulk-reply: The server replied with bulk data, which you can read
   *           using the event data object containing:
   *           * actor:        Name of actor that received the packet
   *           * type:         Name of actor's method that was called on receipt
   *           * length:       Size of the data to be read
   *           * stream:       This input stream should only be used directly if you
   *                           can ensure that you will read exactly |length| bytes
   *                           and will not close the stream when reading is complete
   *           * done:         If you use the stream directly (instead of |copyTo|
   *                           or |copyToBuffer| below), you must signal
   *                           completion by resolving / rejecting this promise.
   *                           If it's rejected, the transport will be closed.
   *                           If an Error is supplied as a rejection value, it
   *                           will be logged via MOZ_LOG.  If you do use
   *                           |copyTo| or |copyToBuffer|, resolving is taken
   *                           care of for you when copying completes.
   *           * copyTo:       A helper function for getting your data out of the
   *                           stream that meets the stream handling requirements
   *                           above, and has the following signature:
   *             @param  output nsIAsyncOutputStream
   *                           The stream to copy to.
   *             @return Promise
   *                           The promise is resolved when copying completes or
   *                           rejected if any (unexpected) errors occur.
   *                           This object also emits "progress" events for each chunk
   *                           that is copied.  See stream-utils.js.
   *           * copyToBuffer: a helper function for getting your data out of the stream
   *                           that meets the stream handling requirements above, and has
   *                           the following signature:
   *             @param output ArrayBuffer
   *                           The buffer to copy to. It needs to be the same length as the data
   *                           to be transfered.
   *             @return Promise
   *                           The promise is resolved when copying completes or rejected if any
   *                           (unexpected) error occurs.
   */
  request(packet) {
    if (!this.mainRoot) {
      throw Error("Have not yet received a hello packet from the server.");
    }
    const type = packet.type || "";
    if (!packet.to) {
      throw Error("'" + type + "' request packet has no destination.");
    }

    if (this._transportClosed) {
      const msg =
        "'" +
        type +
        "' request packet to " +
        "'" +
        packet.to +
        "' " +
        "can't be sent as the connection is closed.";
      return Promise.reject({ error: "connectionClosed", message: msg });
    }

    const request = new Request(packet);
    request.format = "json";
    request.stack = getStack();

    // Implement a Promise like API on the returned object
    // that resolves/rejects on request response
    const promise = new Promise((resolve, reject) => {
      function listenerJson(resp) {
        removeRequestListeners();
        if (resp.error) {
          reject(resp);
        } else {
          resolve(resp);
        }
      }
      function listenerBulk(resp) {
        removeRequestListeners();
        resolve(resp);
      }

      const removeRequestListeners = () => {
        request.off("json-reply", listenerJson);
        request.off("bulk-reply", listenerBulk);
      };

      request.on("json-reply", listenerJson);
      request.on("bulk-reply", listenerBulk);
    });

    this._sendOrQueueRequest(request);
    request.then = promise.then.bind(promise);
    request.catch = promise.catch.bind(promise);

    return request;
  }

  /**
   * Transmit streaming data via a bulk request.
   *
   * This method initiates the bulk send process by queuing up the header data.
   * The caller receives eventual access to a stream for writing.
   *
   * Since this opens up more options for how the server might respond (it could
   * send back either JSON or bulk data), and the returned Request object emits
   * events for different stages of the request process that you may want to
   * react to.
   *
   * @param request Object
   *        This is modeled after the format of JSON packets above, but does not
   *        actually contain the data, but is instead just a routing header:
   *          * actor:  Name of actor that will receive the packet
   *          * type:   Name of actor's method that should be called on receipt
   *          * length: Size of the data to be sent
   * @return Request
   *         This object emits a number of events to allow you to respond to
   *         different parts of the request lifecycle.
   *
   *         Events emitted:
   *         * bulk-send-ready: Ready to send bulk data to the server, using the
   *           event data object containing:
   *           * stream:         This output stream should only be used directly if
   *                             you can ensure that you will write exactly |length|
   *                             bytes and will not close the stream when writing is
   *                             complete
   *           * done:           If you use the stream directly (instead of |copyFrom|
   *                             or |copyFromBuffer| below), you must signal
   *                             completion by resolving / rejecting this
   *                             promise.  If it's rejected, the transport will
   *                             be closed.  If an Error is supplied as a
   *                             rejection value, it will be logged via MOZ_LOG.
   *                             If you do use |copyFrom| or |copyFromBuffer|,
   *                             resolving is taken care of for you when copying
   *                             completes.
   *           * copyFrom:       A helper function for getting your data onto the
   *                             stream that meets the stream handling requirements
   *                             above, and has the following signature:
   *             @param  input nsIAsyncInputStream
   *                     The stream to copy from.
   *             @return Promise
   *                     The promise is resolved when copying completes or
   *                     rejected if any (unexpected) errors occur.
   *                     This object also emits "progress" events for each chunk
   *                     that is copied.  See stream-utils.js.
   *           * copyFromBuffer: A helper function for getting your data onto
   *                             the stream that meets the stream handling
   *                             requirements above, and has the following
   *                             signature:
   *             @param  input ArrayBuffer
   *                     The buffer to read from. It needs to be the same length
   *                     as the data to write.
   *             @return Promise
   *                     The promise is resolved when copying completes or
   *                     rejected if any (unexpected) errors occur.
   *         * json-reply: The server replied with a JSON packet, which is
   *           passed as event data.
   *         * bulk-reply: The server replied with bulk data, which you can read
   *           using the event data object containing:
   *           * actor:        Name of actor that received the packet
   *           * type:         Name of actor's method that was called on receipt
   *           * length:       Size of the data to be read
   *           * stream:       This input stream should only be used directly if you
   *                           can ensure that you will read exactly |length| bytes
   *                           and will not close the stream when reading is complete
   *           * done:         If you use the stream directly (instead of |copyTo|
   *                           |copyToBuffer| below), you must signal completion
   *                           by resolving / rejecting this promise.  If it's
   *                           rejected, the transport will be closed.  If an
   *                           Error is supplied as a rejection value, it will
   *                           be logged via MOZ_LOG.  If you do use |copyTo| or
   *                           |copyToBuffer|, resolving is taken care of for
   *                           you when copying completes.
   *           * copyTo:       A helper function for getting your data out of the
   *                           stream that meets the stream handling requirements
   *                           above, and has the following signature:
   *             @param  output nsIAsyncOutputStream
   *                           The stream to copy to.
   *             @return Promise
   *                           The promise is resolved when copying completes or
   *                           rejected if any (unexpected) errors occur.
   *                           This object also emits "progress" events for each chunk
   *                           that is copied.  See stream-utils.js.
   *
   *           * copyToBuffer: a helper function for getting your data out of the stream
   *                           that meets the stream handling requirements above, and has
   *                           the following signature:
   *             @param output ArrayBuffer
   *                           The buffer to copy to. It needs to be the same length as the data
   *                           to be transfered.
   *             @return Promise
   *                           The promise is resolved when copying completes or rejected if any
   *                           (unexpected) error occurs.
   */
  startBulkRequest(request) {
    if (!this.mainRoot) {
      throw Error("Have not yet received a hello packet from the server.");
    }
    if (!request.type) {
      throw Error("Bulk packet is missing the required 'type' field.");
    }
    if (!request.actor) {
      throw Error("'" + request.type + "' bulk packet has no destination.");
    }
    if (!request.length) {
      throw Error("'" + request.type + "' bulk packet has no length.");
    }

    request = new Request(request);
    request.format = "bulk";

    this._sendOrQueueRequest(request);

    return request;
  }

  /**
   * If a new request can be sent immediately, do so.  Otherwise, queue it.
   */
  _sendOrQueueRequest(request) {
    const actor = request.actor;
    if (!this._activeRequests.has(actor)) {
      this._sendRequest(request);
    } else {
      this._queueRequest(request);
    }
  }

  /**
   * Send a request.
   *
   * @throws Error if there is already an active request in flight for the same
   *         actor.
   */
  _sendRequest(request) {
    const actor = request.actor;
    this.expectReply(actor, request);

    if (request.format === "json") {
      // Log outgoing RDP packet being sent via DevToolsClient.
      // (packet sent via protocol.js will be logged from protocol.js codebase)
      logger.log(SEND_MOZ_LOG_STRING, request.request);

      this._transport.send(request.request);
      return;
    }

    this._transport.startBulkSend(request.request).then((...args) => {
      request.emit("bulk-send-ready", ...args);
    });
  }

  /**
   * Queue a request to be sent later.  Queues are only drained when an in
   * flight request to a given actor completes.
   */
  _queueRequest(request) {
    const actor = request.actor;
    const queue = this._pendingRequests.get(actor) || [];
    queue.push(request);
    this._pendingRequests.set(actor, queue);
  }

  /**
   * Attempt the next request to a given actor (if any).
   */
  _attemptNextRequest(actor) {
    if (this._activeRequests.has(actor)) {
      return;
    }
    const queue = this._pendingRequests.get(actor);
    if (!queue) {
      return;
    }
    const request = queue.shift();
    if (queue.length === 0) {
      this._pendingRequests.delete(actor);
    }
    this._sendRequest(request);
  }

  /**
   * Arrange to hand the next reply from |actor| to the handler bound to
   * |request|.
   *
   * DevToolsClient.prototype.request / startBulkRequest usually takes care of
   * establishing the handler for a given request, but in rare cases (well,
   * greetings from new root actors, is the only case at the moment) we must be
   * prepared for a "reply" that doesn't correspond to any request we sent.
   */
  expectReply(actor, request) {
    if (this._activeRequests.has(actor)) {
      throw Error("clashing handlers for next reply from " + actor);
    }

    // If a handler is passed directly (as it is with the handler for the root
    // actor greeting), create a dummy request to bind this to.
    if (typeof request === "function") {
      const handler = request;
      request = new Request();
      request.on("json-reply", handler);
    }

    this._activeRequests.set(actor, request);
  }

  // Transport hooks.

  /**
   * Called by DebuggerTransport to dispatch incoming packets as appropriate.
   *
   * @param packet object
   *        The incoming packet.
   */
  onPacket(packet) {
    if (!packet.from) {
      logger.log(RECEIVE_MOZ_LOG_STRING, packet);
      DevToolsUtils.reportException(
        "onPacket",
        new Error(
          "Server did not specify an actor, dropping packet: " +
            JSON.stringify(packet)
        )
      );
      return;
    }

    // Check for "forwardingCancelled" here instead of using a front to handle it.
    // This is necessary because we might receive this event while the client is closing,
    // and the fronts have already been removed by that point.
    if (
      this.mainRoot &&
      packet.from == this.mainRoot.actorID &&
      packet.type == "forwardingCancelled"
    ) {
      logger.log(RECEIVE_MOZ_LOG_STRING, packet);
      this.purgeRequests(packet.prefix);
      return;
    }

    // If we have a registered Front for this actor, let it handle the packet
    // and skip all the rest of this unpleasantness.
    const front = this.getFrontByID(packet.from);
    if (front) {
      front.onPacket(packet);
      return;
    }

    // Log incoming RDP packet being sent via DevToolsClient.
    // (packet received via protocol.js will be logged from protocol.js codebase)
    logger.log(RECEIVE_MOZ_LOG_STRING, packet);

    let activeRequest;
    // See if we have a handler function waiting for a reply from this
    // actor. (Don't count unsolicited notifications or pauses as
    // replies.)
    if (
      this._activeRequests.has(packet.from) &&
      !(packet.type in UnsolicitedNotifications)
    ) {
      activeRequest = this._activeRequests.get(packet.from);
      this._activeRequests.delete(packet.from);
    }

    // If there is a subsequent request for the same actor, hand it off to the
    // transport.  Delivery of packets on the other end is always async, even
    // in the local transport case.
    this._attemptNextRequest(packet.from);

    // Only try to notify listeners on events, not responses to requests
    // that lack a packet type.
    if (packet.type) {
      this.emit(packet.type, packet);
    }

    if (activeRequest) {
      const emitReply = () => activeRequest.emit("json-reply", packet);
      if (activeRequest.stack) {
        callFunctionWithAsyncStack(
          emitReply,
          activeRequest.stack,
          "DevTools RDP"
        );
      } else {
        emitReply();
      }
    }
  }

  /**
   * Called by the DebuggerTransport to dispatch incoming bulk packets as
   * appropriate.
   *
   * @param packet object
   *        The incoming packet, which contains:
   *        * actor:        Name of actor that will receive the packet
   *        * type:         Name of actor's method that should be called on receipt
   *        * length:       Size of the data to be read
   *        * stream:       This input stream should only be used directly if you can
   *                        ensure that you will read exactly |length| bytes and will
   *                        not close the stream when reading is complete
   *        * done:         If you use the stream directly (instead of |copyTo|
   *                        or |copyToBuffer| below), you must signal completion
   *                        by resolving / rejecting this promise.  If it's
   *                        rejected, the transport will be closed.  If an Error
   *                        is supplied as a rejection value, it will be logged
   *                        via MOZ_LOG.  If you do use |copyTo| or
   *                        |copyToBuffer|, resolving is taken care of for you
   *                        when copying completes.
   *        * copyTo:       A helper function for getting your data out of the stream
   *                        that meets the stream handling requirements above, and has
   *                        the following signature:
   *          @param  output nsIAsyncOutputStream
   *                        The stream to copy to.
   *          @return Promise
   *                        The promise is resolved when copying completes or rejected
   *                        if any (unexpected) errors occur.
   *                        This object also emits "progress" events for each chunk
   *                        that is copied.  See stream-utils.js.
   *        * copyToBuffer: a helper function for getting your data out of the stream
   *                        that meets the stream handling requirements above, and has
   *                        the following signature:
   *          @param output ArrayBuffer
   *                        The buffer to copy to. It needs to be the same length as the data
   *                        to be transfered.
   *          @return Promise
   *                        The promise is resolved when copying completes or rejected if any
   *                        (unexpected) error occurs.
   */
  onBulkPacket(packet) {
    const { actor } = packet;

    if (!actor) {
      DevToolsUtils.reportException(
        "onBulkPacket",
        new Error(
          "Server did not specify an actor, dropping bulk packet: " +
            JSON.stringify(packet)
        )
      );
      return;
    }

    const front = this.getFrontByID(actor);
    if (front) {
      front.onBulkPacket(packet);
      return;
    }

    // See if we have a handler function waiting for a reply from this
    // actor.
    if (!this._activeRequests.has(actor)) {
      return;
    }

    const activeRequest = this._activeRequests.get(actor);
    this._activeRequests.delete(actor);

    // If there is a subsequent request for the same actor, hand it off to the
    // transport.  Delivery of packets on the other end is always async, even
    // in the local transport case.
    this._attemptNextRequest(actor);

    activeRequest.emit("bulk-reply", packet);
  }

  /**
   * Called by DebuggerTransport when the underlying stream is closed.
   *
   * @param status nsresult
   *        The status code that corresponds to the reason for closing
   *        the stream.
   */
  onTransportClosed() {
    if (this._transportClosed) {
      return;
    }
    this._transportClosed = true;
    this.emit("closed");

    this.purgeRequests();

    // The |_pools| array on the client-side currently is used only by
    // protocol.js to store active fronts, mirroring the actor pools found in
    // the server.  So, read all usages of "pool" as "protocol.js front".
    //
    // In the normal case where we shutdown cleanly, the toolbox tells each tool
    // to close, and they each call |destroy| on any fronts they were using.
    // When |destroy| is called on a protocol.js front, it also
    // removes itself from the |_pools| array.  Once the toolbox has shutdown,
    // the connection is closed, and we reach here.  All fronts (should have
    // been) |destroy|ed, so |_pools| should empty.
    //
    // If the connection instead aborts unexpectedly, we may end up here with
    // all fronts used during the life of the connection.  So, we call |destroy|
    // on them clear their state, reject pending requests, and remove themselves
    // from |_pools|.  This saves the toolbox from hanging indefinitely, in case
    // it waits for some server response before shutdown that will now never
    // arrive.
    for (const pool of this._pools) {
      pool.destroy();
    }
  }

  /**
   * Purge pending and active requests in this client.
   *
   * @param prefix string (optional)
   *        If a prefix is given, only requests for actor IDs that start with the prefix
   *        will be cleaned up.  This is useful when forwarding of a portion of requests
   *        is cancelled on the server.
   */
  purgeRequests(prefix = "") {
    const reject = function (type, request) {
      // Server can send packets on its own and client only pass a callback
      // to expectReply, so that there is no request object.
      let msg;
      if (request.request) {
        msg =
          "'" +
          request.request.type +
          "' " +
          type +
          " request packet" +
          " to '" +
          request.actor +
          "' " +
          "can't be sent as the connection just closed.";
      } else {
        msg =
          "server side packet can't be received as the connection just closed.";
      }
      const packet = { error: "connectionClosed", message: msg };
      request.emit("json-reply", packet);
    };

    let pendingRequestsToReject = [];
    this._pendingRequests.forEach((requests, actor) => {
      if (!actor.startsWith(prefix)) {
        return;
      }
      this._pendingRequests.delete(actor);
      pendingRequestsToReject = pendingRequestsToReject.concat(requests);
    });
    pendingRequestsToReject.forEach(request => reject("pending", request));

    let activeRequestsToReject = [];
    this._activeRequests.forEach((request, actor) => {
      if (!actor.startsWith(prefix)) {
        return;
      }
      this._activeRequests.delete(actor);
      activeRequestsToReject = activeRequestsToReject.concat(request);
    });
    activeRequestsToReject.forEach(request => reject("active", request));

    // Also purge protocol.js requests
    const fronts = this.getAllFronts();

    for (const front of fronts) {
      if (!front.isDestroyed() && front.actorID.startsWith(prefix)) {
        // Call Front.baseFrontClassDestroy nstead of Front.destroy in order to flush requests
        // and nullify front.actorID immediately, even if Front.destroy is overloaded
        // by an async function which would otherwise be able to try emitting new request
        // after the purge.
        front.baseFrontClassDestroy();
      }
    }
  }

  /**
   * Search for all requests in process for this client, including those made via
   * protocol.js and wait all of them to complete.  Since the requests seen when this is
   * first called may in turn trigger more requests, we keep recursing through this
   * function until there is no more activity.
   *
   * This is a fairly heavy weight process, so it's only meant to be used in tests.
   *
   * @param {object=} options
   * @param {boolean=} options.ignoreOrphanedFronts
   *        Allow to ignore fronts which can no longer be retrieved via
   *        getFrontByID, as their requests can never be completed now.
   *        Ideally we should rather investigate and address those cases, but
   *        since this is a test helper, allow to bypass them here. Defaults to
   *        false.
   *
   * @return Promise
   *         Resolved when all requests have settled.
   */
  waitForRequestsToSettle({ ignoreOrphanedFronts = false } = {}) {
    let requests = [];

    // Gather all pending and active requests in this client
    // The request object supports a Promise API for completion (it has .then())
    this._pendingRequests.forEach(requestsForActor => {
      // Each value is an array of pending requests
      requests = requests.concat(requestsForActor);
    });
    this._activeRequests.forEach(requestForActor => {
      // Each value is a single active request
      requests = requests.concat(requestForActor);
    });

    // protocol.js
    const fronts = this.getAllFronts();

    // For each front, wait for its requests to settle
    for (const front of fronts) {
      if (front.hasRequests()) {
        if (ignoreOrphanedFronts && !this.getFrontByID(front.actorID)) {
          // If a front was stuck during its destroy but the pool managing it
          // has been already removed, ignore its pending requests, they can
          // never resolve.
          continue;
        }
        requests.push(front.waitForRequestsToSettle());
      }
    }

    // Abort early if there are no requests
    if (!requests.length) {
      return Promise.resolve();
    }

    return DevToolsUtils.settleAll(requests)
      .catch(() => {
        // One of the requests might have failed, but ignore that situation here and pipe
        // both success and failure through the same path.  The important part is just that
        // we waited.
      })
      .then(() => {
        // Repeat, more requests may have started in response to those we just waited for
        return this.waitForRequestsToSettle({ ignoreOrphanedFronts });
      });
  }

  getAllFronts() {
    // Use a Set because some fronts (like domwalker) seem to have multiple parents.
    const fronts = new Set();
    const poolsToVisit = [...this._pools];

    // With protocol.js, each front can potentially have its own pools containing child
    // fronts, forming a tree.  Descend through all the pools to locate all child fronts.
    while (poolsToVisit.length) {
      const pool = poolsToVisit.shift();
      // `_pools` contains either Fronts or Pools, we only want to collect Fronts here.
      // Front inherits from Pool which exposes `poolChildren`.
      if (pool instanceof Front) {
        fronts.add(pool);
      }
      for (const child of pool.poolChildren()) {
        poolsToVisit.push(child);
      }
    }
    return fronts;
  }

  /**
   * Actor lifetime management, echos the server's actor pools.
   */
  get _pools() {
    if (this.__pools) {
      return this.__pools;
    }
    this.__pools = new Set();
    return this.__pools;
  }

  addActorPool(pool) {
    this._pools.add(pool);
  }
  removeActorPool(pool) {
    this._pools.delete(pool);
  }

  /**
   * Return the Front for the Actor whose ID is the one passed in argument.
   *
   * @param {string} actorID: The actor ID to look for.
   */
  getFrontByID(actorID) {
    const pool = this.poolFor(actorID);
    return pool ? pool.getActorByID(actorID) : null;
  }

  poolFor(actorID) {
    for (const pool of this._pools) {
      if (pool.has(actorID)) {
        return pool;
      }
    }
    return null;
  }

  /**
   * Creates an object front for this DevToolsClient and the grip in parameter,
   *
   * @param {object} grip: The grip to create the ObjectFront for.
   * @param {ThreadFront} threadFront
   * @param {Front} parentFront: Optional front that will manage the object front.
   *                             Defaults to threadFront.
   * @returns {ObjectFront}
   */
  createObjectFront(grip, threadFront, parentFront) {
    if (!parentFront) {
      parentFront = threadFront;
    }

    return new ObjectFront(this, threadFront.targetFront, parentFront, grip);
  }

  get transport() {
    return this._transport;
  }

  /**
   * Boolean flag to help identify client connected to the current runtime,
   * via a LocalDevToolsTransport pipe.
   */
  get isLocalClient() {
    return !!this._transport.isLocalTransport;
  }

  dumpPools() {
    for (const pool of this._pools) {
      console.log(`%c${pool.actorID}`, "font-weight: bold;", [
        ...pool.__poolMap.keys(),
      ]);
    }
  }
}

class Request extends EventEmitter {
  constructor(request) {
    super();
    this.request = request;
  }

  get actor() {
    return this.request.to || this.request.actor;
  }
}

module.exports = {
  DevToolsClient,
};
