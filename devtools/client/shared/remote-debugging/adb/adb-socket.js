/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  logger,
} = require("resource://devtools/client/shared/remote-debugging/adb/adb-logger.js");

function createTCPSocket(location, port, options) {
  const { TCPSocket } = Cu.getGlobalForObject(
    ChromeUtils.importESModule("resource://gre/modules/AppConstants.sys.mjs")
  );

  return new TCPSocket(location, port, options);
}

// Creates a socket connected to the adb instance.
// This instantiation is sync, and returns before we know if opening the
// connection succeeds. Callers must attach handlers to the s field.
class AdbSocket {
  constructor() {
    this.s = createTCPSocket("127.0.0.1", 5037, { binaryType: "arraybuffer" });
  }

  /**
   * Dump the first few bytes of the given array to the console.
   *
   * @param {TypedArray} inputArray
   *        the array to dump
   */
  _hexdump(inputArray) {
    const decoder = new TextDecoder("windows-1252");
    const array = new Uint8Array(inputArray.buffer);
    const s = decoder.decode(array);
    const len = array.length;
    let dbg = "len=" + len + " ";
    const l = len > 20 ? 20 : len;

    for (let i = 0; i < l; i++) {
      let c = array[i].toString(16);
      if (c.length == 1) {
        c = "0" + c;
      }
      dbg += c;
    }
    dbg += " ";
    for (let i = 0; i < l; i++) {
      const c = array[i];
      if (c < 32 || c > 127) {
        dbg += ".";
      } else {
        dbg += s[i];
      }
    }
    logger.debug(dbg);
  }

  // debugging version of tcpsocket.send()
  send(array) {
    this._hexdump(array);

    this.s.send(array.buffer, array.byteOffset, array.byteLength);
  }

  close() {
    if (this.s.readyState === "open" || this.s.readyState === "connecting") {
      this.s.close();
    }
  }
}

exports.AdbSocket = AdbSocket;
