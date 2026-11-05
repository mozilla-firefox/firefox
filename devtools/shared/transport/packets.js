/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Packets contain read / write functionality for the different packet types
 * supported by the debugging protocol, so that a transport can focus on
 * delivery and queue management without worrying too much about the specific
 * packet types.
 *
 * They are intended to be "one use only", so a new packet should be
 * instantiated for each incoming or outgoing packet.
 *
 * A complete Packet type should expose at least the following:
 *   * read(stream, scriptableStream)
 *     Called when the input stream has data to read
 *   * write(stream)
 *     Called when the output stream is ready to write
 *   * get done()
 *     Returns true once the packet is done being read / written
 *   * destroy()
 *     Called to clean up at the end of use
 */

const DevToolsUtils = require("resource://devtools/shared/DevToolsUtils.js");
const StreamUtils = require("resource://devtools/shared/transport/stream-utils.js");

DevToolsUtils.defineLazyGetter(this, "unicodeConverter", () => {
  // eslint-disable-next-line no-shadow
  const unicodeConverter = Cc[
    "@mozilla.org/intl/scriptableunicodeconverter"
  ].createInstance(Ci.nsIScriptableUnicodeConverter);
  unicodeConverter.charset = "UTF-8";
  return unicodeConverter;
});

const logger = console.createInstance({
  logModule: "devtools_rdp",
  maxLogLevel: "Warn",
});

// The transport's previous check ensured the header length did not exceed 20
// characters.  Here, we opt for the somewhat smaller, but still large limit of
// 1 TiB.
const PACKET_LENGTH_MAX = Math.pow(2, 40);

/**
 * A generic Packet processing object (extended by two subtypes below).
 */
class Packet {
  constructor(transport) {
    this._transport = transport;
    this._length = 0;
  }
  /**
   * Attempt to initialize a new Packet based on the incoming packet header we've
   * received so far.  We try each of the types in succession, trying JSON packets
   * first since they are much more common.
   *
   * @param {string} header
   *        The packet header string to attempt parsing.
   * @param {DebuggerTransport} transport
   *        The transport instance that will own the packet.
   * @return Packet
   *         The parsed packet of the matching type, or null if no types matched.
   */
  static fromHeader(header, transport) {
    return (
      JSONPacket.fromHeader(header, transport) ||
      BulkPacket.fromHeader(header, transport)
    );
  }
  get length() {
    return this._length;
  }

  set length(length) {
    if (length > PACKET_LENGTH_MAX) {
      throw Error(
        "Packet length " +
          length +
          " exceeds the max length of " +
          PACKET_LENGTH_MAX
      );
    }
    this._length = length;
  }

  destroy() {
    this._transport = null;
  }
}

exports.Packet = Packet;

/**
 * With a JSON packet (the typical packet type sent via the transport), data is
 * transferred as a JSON packet serialized into a string, with the string length
 * prepended to the packet, followed by a colon ([length]:[packet]). The
 * contents of the JSON packet are specified in the Remote Debugging Protocol
 * specification.
 *
 */
class JSONPacket extends Packet {
  /**
   * @param {DebuggerTransport} transport
   *        The transport instance that will own the packet.
   */
  constructor(transport) {
    super(transport);

    this._data = "";
    this._done = false;
  }
  /**
   * Attempt to initialize a new JSONPacket based on the incoming packet header
   * we've received so far.
   *
   * @param {string} header
   *        The packet header string to attempt parsing.
   * @param {DebuggerTransport} transport
   *        The transport instance that will own the packet.
   * @return {JSONPacket}
   *         The parsed packet, or null if it's not a match.
   */
  static fromHeader(header, transport) {
    const match = this.HEADER_PATTERN.exec(header);

    if (!match) {
      return null;
    }

    logger.debug("Header matches JSON packet");
    const packet = new JSONPacket(transport);
    packet.length = +match[1];
    return packet;
  }

  static HEADER_PATTERN = /^(\d+):$/;

  /**
   * Gets the object (not the serialized string) being read or written.
   */
  get object() {
    return this._object;
  }
  /**
   * Sets the object to be sent when write() is called.
   */
  set object(object) {
    this._object = object;
    const data = JSON.stringify(object);
    this._data = unicodeConverter.ConvertFromUnicode(data);
    this.length = this._data.length;
  }

  read(stream, scriptableStream) {
    logger.debug("Reading JSON packet");

    // Read in more packet data.
    this._readData(stream, scriptableStream);

    if (!this.done) {
      // Don't have a complete packet yet.
      return;
    }

    let json = this._data;
    try {
      json = unicodeConverter.ConvertToUnicode(json);
      this._object = JSON.parse(json);
    } catch (e) {
      const msg =
        "Error parsing incoming packet: " +
        json +
        " (" +
        e +
        " - " +
        e.stack +
        ")";
      logger.error(msg);
      return;
    }

    this._transport._onJSONObjectReady(this._object);
  }

  _readData(stream, scriptableStream) {
    if (logger.shouldLog("Debug")) {
      logger.debug(
        "Reading JSON data: _l: " +
          this.length +
          " dL: " +
          this._data.length +
          " sA: " +
          stream.available()
      );
    }
    const bytesToRead = Math.min(
      this.length - this._data.length,
      stream.available()
    );
    this._data += scriptableStream.readBytes(bytesToRead);
    this._done = this._data.length === this.length;
  }

  write(stream) {
    logger.debug("Writing JSON packet");

    if (this._outgoing === undefined) {
      // Format the serialized packet to a buffer
      this._outgoing = this.length + ":" + this._data;
    }

    const written = stream.write(this._outgoing, this._outgoing.length);
    this._outgoing = this._outgoing.slice(written);
    this._done = !this._outgoing.length;
  }

  get done() {
    return this._done;
  }

  toString() {
    return JSON.stringify(this._object, null, 2);
  }
}

exports.JSONPacket = JSONPacket;

/**
 * With a bulk packet, data is transferred by temporarily handing over the
 * transport's input or output stream to the application layer for writing data
 * directly.  This can be much faster for large data sets, and avoids various
 * stages of copies and data duplication inherent in the JSON packet type.  The
 * bulk packet looks like:
 *
 * bulk [actor] [type] [length]:[data]
 *
 * The interpretation of the data portion depends on the kind of actor and the
 * packet's type.  See the Remote Debugging Protocol Stream Transport spec for
 * more details.
 *
 */
class BulkPacket extends Packet {
  /**
   * @param {DebuggerTransport} transport
   *        The transport instance that will own the packet.
   */
  constructor(transport) {
    super(transport);

    this._done = false;
    let _resolve;
    this._readyForWriting = new Promise(resolve => {
      _resolve = resolve;
    });
    this._readyForWriting.resolve = _resolve;
  }

  /**
   * Attempt to initialize a new BulkPacket based on the incoming packet header
   * we've received so far.
   *
   * @param {string} header
   *        The packet header string to attempt parsing.
   * @param {DebuggerTransport} transport
   *        The transport instance that will own the packet.
   * @return {BulkPacket}
   *         The parsed packet, or null if it's not a match.
   */
  static fromHeader = function (header, transport) {
    const match = this.HEADER_PATTERN.exec(header);

    if (!match) {
      return null;
    }

    logger.debug("Header matches bulk packet");
    const packet = new BulkPacket(transport);
    packet.header = {
      actor: match[1],
      type: match[2],
      length: +match[3],
    };
    return packet;
  };

  static HEADER_PATTERN = /^bulk ([^: ]+) ([^: ]+) (\d+):$/;

  read(stream) {
    logger.debug("Reading bulk packet, handing off input stream");

    // Temporarily pause monitoring of the input stream
    this._transport.pauseIncoming();

    new Promise(resolve => {
      this._transport._onBulkReadReady({
        actor: this.actor,
        type: this.type,
        length: this.length,
        copyTo: output => {
          logger.debug("CT length: " + this.length);
          const copying = StreamUtils.copyStream(stream, output, this.length);
          resolve(copying);
          return copying;
        },
        copyToBuffer: outputBuffer => {
          if (outputBuffer.byteLength !== this.length) {
            throw new Error(
              `In copyToBuffer, the output buffer needs to have the same length as the data to read. ${outputBuffer.byteLength} !== ${this.length}`
            );
          }
          logger.debug("CT length: " + this.length);
          const copying = StreamUtils.copyAsyncStreamToArrayBuffer(
            stream,
            outputBuffer
          );
          resolve(copying);
          return copying;
        },
        stream,
        done: resolve,
      });
      // Await the result of reading from the stream
    }).then(() => {
      logger.debug("onReadDone called, ending bulk mode");
      this._done = true;
      this._transport.resumeIncoming();
    }, this._transport.close);

    // Ensure this is only done once
    this.read = () => {
      throw new Error("Tried to read() a BulkPacket's stream multiple times.");
    };
  }

  write(stream) {
    logger.debug("Writing bulk packet");

    if (this._outgoingHeader === undefined) {
      logger.debug("Serializing bulk packet header");
      // Format the serialized packet header to a buffer
      this._outgoingHeader =
        "bulk " + this.actor + " " + this.type + " " + this.length + ":";
    }

    // Write the header, or whatever's left of it to write.
    if (this._outgoingHeader.length) {
      logger.debug("Writing bulk packet header");
      const written = stream.write(
        this._outgoingHeader,
        this._outgoingHeader.length
      );
      this._outgoingHeader = this._outgoingHeader.slice(written);
      return;
    }

    logger.debug("Handing off output stream");

    // Temporarily pause the monitoring of the output stream
    this._transport.pauseOutgoing();

    new Promise(resolve => {
      this._readyForWriting.resolve({
        copyFrom: input => {
          logger.debug("CF length: " + this.length);
          const copying = StreamUtils.copyStream(input, stream, this.length);
          resolve(copying);
          return copying;
        },
        copyFromBuffer: inputBuffer => {
          if (inputBuffer.byteLength !== this.length) {
            throw new Error(
              `In copyFromBuffer, the input buffer needs to have the same length as the data to write. ${inputBuffer.byteLength} !== ${this.length}`
            );
          }
          logger.debug("CF length: " + this.length);
          const copying = StreamUtils.copyArrayBufferToAsyncStream(
            inputBuffer,
            stream
          );
          resolve(copying);
          return copying;
        },
        stream,
        done: resolve,
      });
      // Await the result of writing to the stream
    }).then(() => {
      logger.debug("onWriteDone called, ending bulk mode");
      this._done = true;
      this._transport.resumeOutgoing();
    }, this._transport.close);

    // Ensure this is only done once
    this.write = () => {
      throw new Error("Tried to write() a BulkPacket's stream multiple times.");
    };
  }

  get streamReadyForWriting() {
    return this._readyForWriting;
  }

  get header() {
    return {
      actor: this.actor,
      type: this.type,
      length: this.length,
    };
  }

  set header(header) {
    this.actor = header.actor;
    this.type = header.type;
    this.length = header.length;
  }

  get done() {
    return this._done;
  }

  toString() {
    return "Bulk: " + JSON.stringify(this.header, null, 2);
  }
}

exports.BulkPacket = BulkPacket;

/**
 * RawPacket is used to test the transport's error handling of malformed
 * packets, by writing data directly onto the stream.
 */
class RawPacket extends Packet {
  /**
   * @param {DebuggerTransport} transport
   *        The transport instance that will own the packet.
   * @param {string} data
   *        The raw string to send out onto the stream.
   */
  constructor(transport, data) {
    super(transport);
    this._data = data;
    this.length = data.length;
    this._done = false;
  }
  read() {
    // This hasn't yet been needed for testing.
    throw Error("Not implmented.");
  }
  write(stream) {
    const written = stream.write(this._data, this._data.length);
    this._data = this._data.slice(written);
    this._done = !this._data.length;
  }
  get done() {
    return this._done;
  }
}

exports.RawPacket = RawPacket;
