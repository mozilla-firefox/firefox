/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const DevToolsUtils = require("resource://devtools/shared/DevToolsUtils.js");
const StreamUtils = require("resource://devtools/shared/transport/stream-utils.js");
const {
  Packet,
  JSONPacket,
  BulkPacket,
} = require("resource://devtools/shared/transport/packets.js");

loader.lazyGetter(this, "ScriptableInputStream", () => {
  return Components.Constructor(
    "@mozilla.org/scriptableinputstream;1",
    "nsIScriptableInputStream",
    "init"
  );
});

const logger = console.createInstance({
  logModule: "devtools_rdp",
  maxLogLevel: "Warn",
});

const PACKET_HEADER_MAX = 200;

/**
 * An adapter that handles data transfers between the devtools client and
 * server. It can work with both nsIPipe and nsIServerSocket transports so
 * long as the properly created input and output streams are specified.
 * (However, for intra-process connections, LocalDebuggerTransport, below,
 * is more efficient than using an nsIPipe pair with DebuggerTransport.)
 *
 * Given a DebuggerTransport instance dt:
 * 1) Set dt.hooks to a packet handler object (described below).
 * 2) Call dt.ready() to begin watching for input packets.
 * 3) Call dt.send() / dt.startBulkSend() to send packets.
 * 4) Call dt.close() to close the connection, and disengage from the event
 *    loop.
 *
 * A packet handler is an object with the following methods:
 *
 * - onPacket(packet) - called when we have received a complete packet.
 *   |packet| is the parsed form of the packet --- a JavaScript value, not
 *   a JSON-syntax string.
 *
 * - onBulkPacket(packet) - called when we have switched to bulk packet
 *   receiving mode. |packet| is an object containing:
 *   * actor:        Name of actor that will receive the packet
 *   * type:         Name of actor's method that should be called on receipt
 *   * length:       Size of the data to be read
 *   * stream:       This input stream should only be used directly if you can ensure
 *                   that you will read exactly |length| bytes and will not close the
 *                   stream when reading is complete
 *   * done:         If you use the stream directly (instead of |copyTo| or
 *                   |copyToBuffer| below), you must signal completion by
 *                   resolving / rejecting this deferred. If it's rejected, the
 *                   transport will be closed.  If an Error is supplied as a
 *                   rejection value, it will be logged via MOZ_LOG. If you do
 *                   use |copyTo| or |copyToBuffer|, resolving is taken care of
 *                   for you when copying completes.
 *   * copyTo:       A helper function for getting your data out of the stream that
 *                   meets the stream handling requirements above, and has the
 *                   following signature:
 *
 *     @param  output nsIAsyncOutputStream
 *                   The stream to copy to.
 *     @return Promise
 *                   The promise is resolved when copying completes or rejected if any
 *                   (unexpected) error occurs.
 *                   This object also emits "progress" events for each chunk that is
 *                   copied.  See stream-utils.js.
 *   * copyToBuffer: a helper function for getting your data out of the stream
 *                   that meets the stream handling requirements above, and has
 *                   the following signature:
 *     @param output ArrayBuffer
 *                   The buffer to copy to. It needs to be the same length as the data
 *                   to be transfered.
 *     @return Promise
 *                   The promise is resolved when copying completes or rejected if any
 *                   (unexpected) error occurs.
 *
 * - onTransportClosed(reason) - called when the connection is closed. |reason| is
 *   an optional nsresult or object, typically passed when the transport is
 *   closed due to some error in a underlying stream.
 *
 * See ./packets.js and the Remote Debugging Protocol specification for more
 * details on the format of these packets.
 */
class DebuggerTransport {
  /**
   * @param {nsIAsyncInputStream} input
   *        The input stream.
   * @param {nsIAsyncOutputStream} output
   *        The output stream.
   */
  constructor(input, output) {
    this._input = input;
    this._scriptableInput = new ScriptableInputStream(input);
    this._output = output;

    // The current incoming (possibly partial) header, which will determine which
    // type of Packet |_incoming| below will become.
    this._incomingHeader = "";
    // The current incoming Packet object
    this._incoming = null;
    // A queue of outgoing Packet objects
    this._outgoing = [];

    this.hooks = null;
    this.active = false;

    this._incomingEnabled = true;
    this._outgoingEnabled = true;

    this.close = this.close.bind(this);
  }

  /**
   * Transmit an object as a JSON packet.
   *
   * This method returns immediately, without waiting for the entire
   * packet to be transmitted, registering event handlers as needed to
   * transmit the entire packet. Packets are transmitted in the order
   * they are passed to this method.
   */
  send(object) {
    const packet = new JSONPacket(this);
    packet.object = object;
    this._outgoing.push(packet);
    this._flushOutgoing();
  }

  /**
   * Transmit streaming data via a bulk packet.
   *
   * This method initiates the bulk send process by queuing up the header data.
   * The caller receives eventual access to a stream for writing.
   *
   * N.B.: Do *not* attempt to close the stream handed to you, as it will
   * continue to be used by this transport afterwards.  Most users should
   * instead use the provided |copyFrom| or |copyFromBuffer| functions instead.
   *
   * @param {object} header
   *        This is modeled after the format of JSON packets above, but does not
   *        actually contain the data, but is instead just a routing header:
   *          * actor:  Name of actor that will receive the packet
   *          * type:   Name of actor's method that should be called on receipt
   *          * length: Size of the data to be sent
   * @return {Promise}
   *         The promise will be resolved when you are allowed to write to the
   *         stream with an object containing:
   *           * stream:         This output stream should only be used directly if
   *                             you can ensure that you will write exactly |length|
   *                             bytes and will not close the stream when writing is
   *                             complete
   *           * done:           If you use the stream directly (instead of |copyFrom|
   *                             or |copyFromBuffer| below), you must signal
   *                             completion by resolving / rejecting this
   *                             deferred.  If it's rejected, the transport will
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
   */
  startBulkSend(header) {
    const packet = new BulkPacket(this);
    packet.header = header;
    this._outgoing.push(packet);
    this._flushOutgoing();
    return packet.streamReadyForWriting;
  }

  /**
   * Close the transport.
   *
   * @param reason nsresult / object (optional)
   *        The status code or error message that corresponds to the reason for
   *        closing the transport (likely because a stream closed or failed).
   */
  close(reason) {
    this.active = false;
    this._input.close();
    this._scriptableInput.close();
    this._output.close();
    this._destroyIncoming();
    this._destroyAllOutgoing();
    if (this.hooks) {
      this.hooks.onTransportClosed(reason);
      this.hooks = null;
    }
    if (reason) {
      logger.debug(
        "Transport closed: " + DevToolsUtils.safeErrorString(reason)
      );
    } else {
      logger.debug("Transport closed.");
    }
  }

  /**
   * The currently outgoing packet (at the top of the queue).
   */
  get _currentOutgoing() {
    return this._outgoing[0];
  }

  /**
   * Flush data to the outgoing stream.  Waits until the output stream notifies
   * us that it is ready to be written to (via onOutputStreamReady).
   */
  _flushOutgoing() {
    if (!this._outgoingEnabled || this._outgoing.length === 0) {
      return;
    }

    // If the top of the packet queue has nothing more to send, remove it.
    if (this._currentOutgoing.done) {
      this._finishCurrentOutgoing();
    }

    if (this._outgoing.length) {
      const threadManager = Cc["@mozilla.org/thread-manager;1"].getService();
      this._output.asyncWait(this, 0, 0, threadManager.currentThread);
    }
  }

  /**
   * Pause this transport's attempts to write to the output stream.  This is
   * used when we've temporarily handed off our output stream for writing bulk
   * data.
   */
  pauseOutgoing() {
    this._outgoingEnabled = false;
  }

  /**
   * Resume this transport's attempts to write to the output stream.
   */
  resumeOutgoing() {
    this._outgoingEnabled = true;
    this._flushOutgoing();
  }

  // nsIOutputStreamCallback
  /**
   * This is called when the output stream is ready for more data to be written.
   * The current outgoing packet will attempt to write some amount of data, but
   * may not complete.
   */
  onOutputStreamReady = DevToolsUtils.makeInfallible(function (stream) {
    if (!this._outgoingEnabled || this._outgoing.length === 0) {
      return;
    }

    try {
      this._currentOutgoing.write(stream);
    } catch (e) {
      if (e.result != Cr.NS_BASE_STREAM_WOULD_BLOCK) {
        this.close(e.result);
        return;
      }
      throw e;
    }

    this._flushOutgoing();
  }, "DebuggerTransport.prototype.onOutputStreamReady");

  /**
   * Remove the current outgoing packet from the queue upon completion.
   */
  _finishCurrentOutgoing() {
    if (this._currentOutgoing) {
      this._currentOutgoing.destroy();
      this._outgoing.shift();
    }
  }

  /**
   * Clear the entire outgoing queue.
   */
  _destroyAllOutgoing() {
    for (const packet of this._outgoing) {
      packet.destroy();
    }
    this._outgoing = [];
  }

  /**
   * Initialize the input stream for reading. Once this method has been called,
   * we watch for packets on the input stream, and pass them to the appropriate
   * handlers via this.hooks.
   */
  ready() {
    this.active = true;
    this._waitForIncoming();
  }

  /**
   * Asks the input stream to notify us (via onInputStreamReady) when it is
   * ready for reading.
   */
  _waitForIncoming() {
    if (this._incomingEnabled) {
      const threadManager = Cc["@mozilla.org/thread-manager;1"].getService();
      this._input.asyncWait(this, 0, 0, threadManager.currentThread);
    }
  }

  /**
   * Pause this transport's attempts to read from the input stream.  This is
   * used when we've temporarily handed off our input stream for reading bulk
   * data.
   */
  pauseIncoming() {
    this._incomingEnabled = false;
  }

  /**
   * Resume this transport's attempts to read from the input stream.
   */
  resumeIncoming() {
    this._incomingEnabled = true;
    this._flushIncoming();
    this._waitForIncoming();
  }

  // nsIInputStreamCallback
  /**
   * Called when the stream is either readable or closed.
   */
  onInputStreamReady = DevToolsUtils.makeInfallible(function (stream) {
    try {
      while (
        stream.available() &&
        this._incomingEnabled &&
        this._processIncoming(stream, stream.available())
      ) {
        // Loop until there is nothing more to process
      }
      this._waitForIncoming();
    } catch (e) {
      if (e.result != Cr.NS_BASE_STREAM_WOULD_BLOCK) {
        this.close(e.result);
      } else {
        throw e;
      }
    }
  }, "DebuggerTransport.prototype.onInputStreamReady");

  /**
   * Process the incoming data.  Will create a new currently incoming Packet if
   * needed.  Tells the incoming Packet to read as much data as it can, but
   * reading may not complete.  The Packet signals that its data is ready for
   * delivery by calling one of this transport's _on*Ready methods (see
   * ./packets.js and the _on*Ready methods below).
   *
   * @return {boolean}
   *         Whether incoming stream processing should continue for any
   *         remaining data.
   */
  _processIncoming(stream, count) {
    logger.debug("Data available: " + count);

    if (!count) {
      logger.debug("Nothing to read, skipping");
      return false;
    }

    try {
      if (!this._incoming) {
        logger.debug("Creating a new packet from incoming");

        if (!this._readHeader(stream)) {
          // Not enough data to read packet type
          return false;
        }

        // Attempt to create a new Packet by trying to parse each possible
        // header pattern.
        this._incoming = Packet.fromHeader(this._incomingHeader, this);
        if (!this._incoming) {
          throw new Error(
            "No packet types for header: " + this._incomingHeader
          );
        }
      }

      if (!this._incoming.done) {
        // We have an incomplete packet, keep reading it.
        logger.debug("Existing packet incomplete, keep reading");
        this._incoming.read(stream, this._scriptableInput);
      }
    } catch (e) {
      const msg =
        "Error reading incoming packet: (" + e + " - " + e.stack + ")";
      logger.error(msg);

      // Now in an invalid state, shut down the transport.
      this.close();
      return false;
    }

    if (!this._incoming.done) {
      // Still not complete, we'll wait for more data.
      logger.debug("Packet not done, wait for more");
      return true;
    }

    // Ready for next packet
    this._flushIncoming();
    return true;
  }

  /**
   * Read as far as we can into the incoming data, attempting to build up a
   * complete packet header (which terminates with ":").  We'll only read up to
   * PACKET_HEADER_MAX characters.
   *
   * @return boolean
   *         True if we now have a complete header.
   */
  _readHeader() {
    const amountToRead = PACKET_HEADER_MAX - this._incomingHeader.length;
    this._incomingHeader += StreamUtils.delimitedRead(
      this._scriptableInput,
      ":",
      amountToRead
    );
    logger.debug("Header read: " + this._incomingHeader);

    if (this._incomingHeader.endsWith(":")) {
      logger.debug("Found packet header successfully: " + this._incomingHeader);
      return true;
    }

    if (this._incomingHeader.length >= PACKET_HEADER_MAX) {
      throw new Error("Failed to parse packet header!");
    }

    // Not enough data yet.
    return false;
  }

  /**
   * If the incoming packet is done, log it as needed and clear the buffer.
   */
  _flushIncoming() {
    if (!this._incoming.done) {
      return;
    }
    // `_incoming` is the RDP packet as a string, so it can be very large.
    if (logger.shouldLog("Debug")) {
      logger.debug("Got: ", this._incoming);
    }
    this._destroyIncoming();
  }

  /**
   * Handler triggered by an incoming JSONPacket completing it's |read| method.
   * Delivers the packet to this.hooks.onPacket.
   */
  _onJSONObjectReady(object) {
    DevToolsUtils.executeSoon(
      DevToolsUtils.makeInfallible(() => {
        // Ensure the transport is still alive by the time this runs.
        if (this.active) {
          this.hooks.onPacket(object);
        }
      }, "DebuggerTransport instance's this.hooks.onPacket")
    );
  }

  /**
   * Handler triggered by an incoming BulkPacket entering the |read| phase for
   * the stream portion of the packet.  Delivers info about the incoming
   * streaming data to this.hooks.onBulkPacket.  See the main comment on the
   * transport at the top of this file for more details.
   */
  _onBulkReadReady(...args) {
    DevToolsUtils.executeSoon(
      DevToolsUtils.makeInfallible(() => {
        // Ensure the transport is still alive by the time this runs.
        if (this.active) {
          this.hooks.onBulkPacket(...args);
        }
      }, "DebuggerTransport instance's this.hooks.onBulkPacket")
    );
  }

  /**
   * Remove all handlers and references related to the current incoming packet,
   * either because it is now complete or because the transport is closing.
   */
  _destroyIncoming() {
    if (this._incoming) {
      this._incoming.destroy();
    }
    this._incomingHeader = "";
    this._incoming = null;
  }
}

exports.DebuggerTransport = DebuggerTransport;
