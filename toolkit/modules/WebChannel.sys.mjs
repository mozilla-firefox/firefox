/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * WebChannel is an abstraction that uses the Message Manager and Custom Events
 * to create a two-way communication channel between chrome and content code.
 */

const ERRNO_UNKNOWN_ERROR = 999;
const ERROR_UNKNOWN = "UNKNOWN_ERROR";

/**
 * WebChannelBroker is a global object that helps manage WebChannel objects.
 * This object handles channel registration, origin validation and message multiplexing.
 */

export var WebChannelBroker = Object.create({
  /**
   * Register a new channel that callbacks messages
   * based on proper origin and channel name
   *
   * @param channel {WebChannel}
   */
  registerChannel(channel) {
    if (!this._channelMap.has(channel)) {
      this._channelMap.set(channel);
    } else {
      console.error("Failed to register the channel. Channel already exists.");
    }
  },

  /**
   * Unregister a channel
   *
   * @param channelToRemove {WebChannel}
   *        WebChannel to remove from the channel map
   *
   * Removes the specified channel from the channel map
   */
  unregisterChannel(channelToRemove) {
    if (!this._channelMap.delete(channelToRemove)) {
      console.error("Failed to unregister the channel. Channel not found.");
    }
  },

  /**
   * Object to store pairs of message origins and callback functions
   */
  _channelMap: new Map(),

  /**
   * Deliver a message to a registered channel.
   *
   * @returns bool whether we managed to find a registered channel.
   */
  tryToDeliver(data, sendingContext) {
    let validChannelFound = false;
    data.message = data.message || {};

    for (var channel of this._channelMap.keys()) {
      if (
        channel.id === data.id &&
        channel._originCheckCallback(sendingContext.principal)
      ) {
        validChannelFound = true;
        channel.deliver(data, sendingContext);
      }
    }
    return validChannelFound;
  },
});

/**
 * Creates a new WebChannel that listens and sends messages over some channel id
 *
 * @param id {String}
 *        WebChannel id
 * @param originOrPermission {nsIURI/string}
 *        If an nsIURI, incoming events will be accepted from any origin matching
 *        that URI's origin.
 *        If a string, it names a permission, and incoming events will be accepted
 *        from any https:// origin that has been granted that permission by the
 *        permission manager.
 * @class
 */
export var WebChannel = function (id, originOrPermission) {
  if (!id || !originOrPermission) {
    throw new Error("WebChannel id and originOrPermission are required.");
  }

  this.id = id;
  // originOrPermission can be either an nsIURI or a string representing a
  // permission name.
  if (typeof originOrPermission == "string") {
    this._originCheckCallback = requestPrincipal => {
      // Accept events from any secure origin having the named permission.
      // The permission manager operates on domain names rather than true
      // origins (bug 1066517).  To mitigate that, we explicitly check that
      // the scheme is https://.
      if (requestPrincipal.scheme != "https") {
        return false;
      }

      // Only restrict based on the site's origin, not on other origin attributes
      let nonAttributePrincipal =
        Services.scriptSecurityManager.principalWithOA(requestPrincipal, {});

      // OK - we have https - now we can check the permission.
      let perm = Services.perms.testExactPermissionFromPrincipal(
        nonAttributePrincipal,
        originOrPermission
      );
      return perm == Ci.nsIPermissionManager.ALLOW_ACTION;
    };
  } else {
    // Accept events from any origin matching the given URI.
    // We deliberately use `originNoSuffix` here because we only want to
    // restrict based on the site's origin, not on other origin attributes
    // such as containers or private browsing.
    this._originCheckCallback = requestPrincipal => {
      return originOrPermission.prePath === requestPrincipal.originNoSuffix;
    };
  }
  this._originOrPermission = originOrPermission;
};

WebChannel.prototype = {
  /**
   * WebChannel id
   */
  id: null,

  /**
   * The originOrPermission value passed to the constructor, mainly for
   * debugging and tests.
   */
  _originOrPermission: null,

  /**
   * Callback that will be called with the principal of an incoming message
   * to check if the request should be dispatched to the listeners.
   */
  _originCheckCallback: null,

  /**
   * WebChannelBroker that manages WebChannels
   */
  _broker: WebChannelBroker,

  /**
   * Callback that will be called with the contents of an incoming message
   */
  _deliverCallback: null,

  /**
   * Registers the callback for messages on this channel
   * Registers the channel itself with the WebChannelBroker
   *
   * @param callback {Function}
   *        Callback that will be called when there is a message
   *        @param {string} id
   *        The WebChannel id that was used for this message
   *        @param {object} message
   *        The message itself
   *        @param sendingContext {Object}
   *        The sending context of the source of the message. Can be passed to
   *        `send` to respond to a message.
   *               @param sendingContext.browser {browser}
   *                      The <browser> object that captured the
   *                      WebChannelMessageToChrome.
   *               @param sendingContext.eventTarget {EventTarget}
   *                      The <EventTarget> where the message was sent.
   *               @param sendingContext.principal {Principal}
   *                      The <Principal> of the EventTarget where the
   *                      message was sent.
   *               @param sendingContext.remoteType {String}
   *                      The remoteType from which the
   *                      WebChannelMessageToChrome was sent.
   */
  listen(callback) {
    if (this._deliverCallback) {
      throw new Error("Failed to listen. Listener already attached.");
    } else if (!callback) {
      throw new Error("Failed to listen. Callback argument missing.");
    } else {
      this._deliverCallback = callback;
      this._broker.registerChannel(this);
    }
  },

  /**
   * Resets the callback for messages on this channel
   * Removes the channel from the WebChannelBroker
   */
  stopListening() {
    this._broker.unregisterChannel(this);
    this._deliverCallback = null;
  },

  /**
   * Sends messages over the WebChannel id using the "WebChannelMessageToContent" event
   *
   * @param message {Object}
   *        The message object that will be sent
   * @param target {Object}
   *        A <target> with the information of where to send the message.
   *        @param target.browsingContext {BrowsingContext}
   *               The browsingContext we should send the message to.
   *        @param target.principal {Principal}
   *               Principal of the target. Prevents messages from
   *               being dispatched to unexpected origins. The system principal
   *               can be specified to send to any target.
   *        @param [target.eventTarget] {EventTarget}
   *               Optional eventTarget within the browser, use to send to a
   *               specific element. Can be null; if not null, should be
   *               a ContentDOMReference.
   */
  send(message, target) {
    let { browsingContext, principal, eventTarget } = target;

    if (message && browsingContext && principal) {
      let { currentWindowGlobal } = browsingContext;
      if (!currentWindowGlobal) {
        console.error(
          "Failed to send a WebChannel message. No currentWindowGlobal."
        );
        return;
      }
      currentWindowGlobal
        .getActor("WebChannel")
        .sendAsyncMessage("WebChannelMessageToContent", {
          id: this.id,
          message,
          eventTarget,
          principal,
        });
    } else if (!message) {
      console.error("Failed to send a WebChannel message. Message not set.");
    } else {
      console.error("Failed to send a WebChannel message. Target invalid.");
    }
  },

  /**
   * Deliver WebChannel messages to the set "_channelCallback"
   *
   * @param data {Object}
   *        Message data
   * @param sendingContext {Object}
   *        Message sending context.
   *        @param sendingContext.browsingContext {BrowsingContext}
   *               The browsingcontext from which the
   *               WebChannelMessageToChrome was sent.
   *        @param sendingContext.eventTarget {EventTarget}
   *               The <EventTarget> where the message was sent.
   *               Can be null; if not null, should be a ContentDOMReference.
   *        @param sendingContext.principal {Principal}
   *               The <Principal> of the EventTarget where the message was sent.
   */
  deliver(data, sendingContext) {
    if (this._deliverCallback) {
      try {
        this._deliverCallback(data.id, data.message, sendingContext);
      } catch (ex) {
        this.send(
          {
            errno: ERRNO_UNKNOWN_ERROR,
            error: ex.message ? ex.message : ERROR_UNKNOWN,
          },
          sendingContext
        );
        console.error("Failed to execute WebChannel callback:");
        console.error(ex);
      }
    } else {
      console.error("No callback set for this channel.");
    }
  },
};
