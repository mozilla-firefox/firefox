/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * A module to track device changes
 * Adapted from adb.js at
 * https://github.com/mozilla/adbhelper/tree/f44386c2d8cb7635a7d2c5a51191c89b886f8327
 */

"use strict";

const {
  AdbSocket,
} = require("resource://devtools/client/shared/remote-debugging/adb/adb-socket.js");
const {
  logger,
} = require("resource://devtools/client/shared/remote-debugging/adb/adb-logger.js");

const OKAY = 0x59414b4f;
const FAIL = 0x4c494146;

// Return buffer, which differs between Gecko versions
function getBuffer(packet) {
  return packet.buffer ? packet.buffer : packet;
}

/**
 * Decode an adb packet into a JS object with length (number) and data (string)
 * properties.
 *
 * @param packet
 *     The packet to get the content from.
 * @param
 *     ignoreResponse True if this packet has no OKAY/FAIL.
 * @return
 *     A js object with the following properties:
 *     - length (number): length of the decoded data
 *     - data (string): the decoded data as a string. Can be multiline (\n)
 */
function unpackPacket(packet, ignoreResponse) {
  const buffer = getBuffer(packet);
  logger.debug("Len buffer: " + buffer.byteLength);
  if (buffer.byteLength === 4 && !ignoreResponse) {
    logger.debug("Packet empty");
    return { length: 0, data: "" };
  }
  let index = 0;
  let totalLength = 0;
  const decodedText = [];

  // Prepare a decoder.
  const decoder = new TextDecoder();

  // Loop over all lines in the packet
  while (index < buffer.byteLength) {
    // Set the index to 4 if we need to skip the response bytes.
    index += ignoreResponse ? 0 : 4;

    // Read the packet line length.
    const lengthView = new Uint8Array(buffer, index, 4);
    const length = parseInt(decoder.decode(lengthView), 16);

    // Move the index after the last size byte.
    index += 4;

    // Read the packet line content and append it to the decodedText array.
    const text = new Uint8Array(buffer, index, length);
    decodedText.push(decoder.decode(text));

    // Move the index after the last read byte for this packet line.
    index += length;
    // Note: totalLength is only used for logging purposes.
    totalLength += length;
  }
  return { length: totalLength, data: decodedText.join("\n") };
}

// Checks if the response is expected (defaults to OKAY).
// @return true if response equals expected.
function checkResponse(packet, expected = OKAY) {
  const buffer = getBuffer(packet);
  const view = new Uint32Array(buffer, 0, 1);
  if (view[0] == FAIL) {
    logger.debug("Response: FAIL");
  }
  logger.debug("view[0] = " + view[0]);
  return view[0] == expected;
}

// @param aCommand A protocol-level command as described in
//  http://androidxref.com/4.0.4/xref/system/core/adb/OVERVIEW.TXT and
//  http://androidxref.com/4.0.4/xref/system/core/adb/SERVICES.TXT
// @return A 8 bit typed array.
function createRequest(command) {
  let length = command.length.toString(16).toUpperCase();
  while (length.length < 4) {
    length = "0" + length;
  }

  const encoder = new TextEncoder();
  logger.debug("Created request: " + length + command);
  return encoder.encode(length + command);
}

function connect() {
  return new AdbSocket();
}

const client = {
  getBuffer,
  unpackPacket,
  checkResponse,
  createRequest,
  connect,
};

module.exports = client;
