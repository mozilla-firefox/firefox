/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const DevToolsUtils = require("resource://devtools/shared/DevToolsUtils.js");
const EventEmitter = require("resource://devtools/shared/event-emitter.js");

DevToolsUtils.defineLazyGetter(this, "IOUtil", () => {
  return Cc["@mozilla.org/io-util;1"].getService(Ci.nsIIOUtil);
});

DevToolsUtils.defineLazyGetter(this, "ScriptableInputStream", () => {
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

const BUFFER_SIZE = 0x8000;

/**
 * This helper function (and its companion object) are used by bulk senders and
 * receivers to read and write data in and out of other streams.  Functions that
 * make use of this tool are passed to callers when it is time to read or write
 * bulk data.  It is highly recommended to use these copier functions instead of
 * the stream directly because the copier enforces the agreed upon length.
 * Since bulk mode reuses an existing stream, the sender and receiver must write
 * and read exactly the agreed upon amount of data, or else the entire transport
 * will be left in a invalid state.  Additionally, other methods of stream
 * copying (such as NetUtil.asyncCopy) close the streams involved, which would
 * terminate the debugging transport, and so it is avoided here.
 *
 * Overall, this *works*, but clearly the optimal solution would be able to just
 * use the streams directly.  If it were possible to fully implement
 * nsIInputStream / nsIOutputStream in JS, wrapper streams could be created to
 * enforce the length and avoid closing, and consumers could use familiar stream
 * utilities like NetUtil.asyncCopy.
 *
 * The function takes two async streams and copies a precise number of bytes
 * from one to the other.  Copying begins immediately, but may complete at some
 * future time depending on data size.  Use the returned promise to know when
 * it's complete.
 *
 * @param input nsIAsyncInputStream
 *        The stream to copy from.
 * @param output nsIAsyncOutputStream
 *        The stream to copy to.
 * @param length Integer
 *        The amount of data that needs to be copied.
 * @return Promise
 *         The promise is resolved when copying completes or rejected if any
 *         (unexpected) errors occur.
 */
function copyStream(input, output, length) {
  const copier = new StreamCopier(input, output, length);
  return copier.copy();
}

class StreamCopier extends EventEmitter {
  static _nextId = 0;

  constructor(input, output, length) {
    super();
    this._id = StreamCopier._nextId++;
    this.input = input;
    // Save off the base output stream, since we know it's async as we've required
    this.baseAsyncOutput = output;
    if (IOUtil.outputStreamIsBuffered(output)) {
      this.output = output;
    } else {
      this.output = Cc[
        "@mozilla.org/network/buffered-output-stream;1"
      ].createInstance(Ci.nsIBufferedOutputStream);
      this.output.init(output, BUFFER_SIZE);
    }
    this._length = length;
    this._amountLeft = length;
    let _resolve;
    let _reject;
    this._deferred = new Promise((resolve, reject) => {
      _resolve = resolve;
      _reject = reject;
    });
    this._deferred.resolve = _resolve;
    this._deferred.reject = _reject;

    this._copy = this._copy.bind(this);
    this._flush = this._flush.bind(this);
    this._destroy = this._destroy.bind(this);

    // Copy promise's then method up to this object.
    // Allows the copier to offer a promise interface for the simple succeed or
    // fail scenarios, but also emit events (due to the EventEmitter) for other
    // states, like progress.
    this.then = this._deferred.then.bind(this._deferred);
    this.then(this._destroy, this._destroy);

    // Stream ready callback starts as |_copy|, but may switch to |_flush| at end
    // if flushing would block the output stream.
    this._streamReadyCallback = this._copy;
  }
  copy() {
    // Dispatch to the next tick so that it's possible to attach a progress
    // event listener, even for extremely fast copies (like when testing).
    Services.tm.dispatchToMainThread(() => {
      try {
        this._copy();
      } catch (e) {
        this._deferred.reject(e);
      }
    });
    return this;
  }

  _copy() {
    const bytesAvailable = this.input.available();
    const amountToCopy = Math.min(bytesAvailable, this._amountLeft);
    this._debug("Trying to copy: " + amountToCopy);

    let bytesCopied;
    try {
      bytesCopied = this.output.writeFrom(this.input, amountToCopy);
    } catch (e) {
      if (e.result == Cr.NS_BASE_STREAM_WOULD_BLOCK) {
        this._debug("Base stream would block, will retry");
        this._debug("Waiting for output stream");
        this.baseAsyncOutput.asyncWait(this, 0, 0, Services.tm.currentThread);
        return;
      }
      throw e;
    }

    this._amountLeft -= bytesCopied;
    this._debug("Copied: " + bytesCopied + ", Left: " + this._amountLeft);
    this._emitProgress();

    if (this._amountLeft === 0) {
      this._debug("Copy done!");
      this._flush();
      return;
    }

    this._debug("Waiting for input stream");
    this.input.asyncWait(this, 0, 0, Services.tm.currentThread);
  }

  _emitProgress() {
    this.emit("progress", {
      bytesSent: this._length - this._amountLeft,
      totalBytes: this._length,
    });
  }

  _flush() {
    try {
      this.output.flush();
    } catch (e) {
      if (
        e.result == Cr.NS_BASE_STREAM_WOULD_BLOCK ||
        e.result == Cr.NS_ERROR_FAILURE
      ) {
        this._debug("Flush would block, will retry");
        this._streamReadyCallback = this._flush;
        this._debug("Waiting for output stream");
        this.baseAsyncOutput.asyncWait(this, 0, 0, Services.tm.currentThread);
        return;
      }
      throw e;
    }
    this._deferred.resolve();
  }

  _destroy() {
    this._destroy = null;
    this._copy = null;
    this._flush = null;
    this.input = null;
    this.output = null;
  }

  // nsIInputStreamCallback
  onInputStreamReady() {
    this._streamReadyCallback();
  }

  // nsIOutputStreamCallback
  onOutputStreamReady() {
    this._streamReadyCallback();
  }

  _debug(msg) {
    // Prefix logs with the copier ID, which makes logs much easier to
    // understand when several copiers are running simultaneously
    logger.debug("Copier: " + this._id + " " + msg);
  }
}

/**
 * Read from a stream, one byte at a time, up to the next |delimiter|
 * character, but stopping if we've read |count| without finding it.  Reading
 * also terminates early if there are less than |count| bytes available on the
 * stream.  In that case, we only read as many bytes as the stream currently has
 * to offer.
 * TODO: This implementation could be removed if bug 984651 is fixed, which
 * provides a native version of the same idea.
 *
 * @param stream nsIInputStream
 *        The input stream to read from.
 * @param delimiter string
 *        The character we're trying to find.
 * @param count integer
 *        The max number of characters to read while searching.
 * @return string
 *         The data collected.  If the delimiter was found, this string will
 *         end with it.
 */
function delimitedRead(stream, delimiter, count) {
  logger.debug(
    "Starting delimited read for " + delimiter + " up to " + count + " bytes"
  );

  let scriptableStream;
  if (stream instanceof Ci.nsIScriptableInputStream) {
    scriptableStream = stream;
  } else {
    scriptableStream = new ScriptableInputStream(stream);
  }

  let data = "";

  // Don't exceed what's available on the stream
  count = Math.min(count, stream.available());

  if (count <= 0) {
    return data;
  }

  let char;
  while (char !== delimiter && count > 0) {
    char = scriptableStream.readBytes(1);
    count--;
    data += char;
  }

  return data;
}

/**
 * This function efficiently copies an async stream to an array buffer.
 * Usage:
 *   // The buffer length is used to define the length of data to copy from the stream.
 *   const buffer = new ArrayBuffer(length);
 *   await copyAsyncStreamToArrayBuffer(inputStream, buffer);
 *
 * @param {nsIAsyncStream} asyncInputStream
 * @param {ArrayBuffer} buffer The byteLength of this buffer will be used to define the length of data to copy from the stream.
 */
async function copyAsyncStreamToArrayBuffer(asyncInputStream, buffer) {
  const reader = new AsyncStreamToArrayBufferCopier(asyncInputStream, buffer);
  await reader.asyncRead();
}

class AsyncStreamToArrayBufferCopier {
  #BUFFER_SIZE = 16 * 1024; // A 16k buffer

  /**
   * @typedef {nsIAsyncInputStream}
   */
  #originalStream;

  /**
   * This is a wrapper on top of #originalStream, to be able to read buffers
   * easily.
   *
   * @typedef {nsIBinaryInputStream}
   */
  #binaryStream;

  /**
   * This is the output buffer, accessed as an UInt8Array.
   *
   * @typedef {Uint8Array}
   */
  #outputArray;

  /**
   * How many bytes have been read already. This is also the next index to write
   * in #outputArray.
   *
   * @typedef {number}
   */
  #pointer = 0;

  /**
   * The count of bytes to be transfered. It is infered from the byteLength of
   * of the output buffer.
   *
   * @typedef {number}
   */
  #count;

  /**
   * This temporary buffer is used when reading from #binaryStream.
   *
   * @typedef {ArrayBuffer}
   */
  #tempBuffer;

  /**
   * @typedef {Uint8Array}
   */
  #tempBufferAsArray;

  /**
   * @param {nsIAsyncStream} stream
   * @param {ArrayBuffer} arrayBuffer The byteLength of this buffer will be used to define the length of data to copy from the stream.
   */
  constructor(stream, arrayBuffer) {
    this.#originalStream = stream;
    this.#binaryStream = Cc["@mozilla.org/binaryinputstream;1"].createInstance(
      Ci.nsIBinaryInputStream
    );
    this.#binaryStream.setInputStream(stream);

    this.#outputArray = new Uint8Array(arrayBuffer);
    this.#count = arrayBuffer.byteLength;
    this.#tempBuffer = new ArrayBuffer(this.#BUFFER_SIZE);
    this.#tempBufferAsArray = new Uint8Array(this.#tempBuffer);
  }

  /**
   * @returns {Promise<void>} Resolves when the reading has finished.
   */
  async asyncRead() {
    do {
      await this.#waitForStreamAvailability();
      this.#syncRead();
    } while (this.#pointer < this.#count);
    logger.debug(`Successfully read ${this.#count} bytes!`);
  }

  /**
   * @returns {Promise<void>} Resolves when the stream is available.
   */
  async #waitForStreamAvailability() {
    return new Promise(resolve => {
      this.#originalStream.asyncWait(
        () => resolve(),
        0,
        0,
        Services.tm.currentThread
      );
    });
  }

  /**
   * @returns {void}
   */
  #syncRead() {
    const amountLeft = this.#count - this.#pointer;
    const count = Math.min(this.#binaryStream.available(), amountLeft);
    if (count <= 0) {
      return;
    }

    logger.debug(
      `Will read synchronously ${count} bytes out of ${amountLeft} bytes left.`
    );

    let remaining = count;
    while (remaining) {
      // TODO readArrayBuffer doesn't know how to write to an offset in the buffer,
      // see bug 1962705.
      const willRead = Math.min(remaining, this.#BUFFER_SIZE);
      const hasRead = this.#binaryStream.readArrayBuffer(
        willRead,
        this.#tempBuffer
      );

      if (hasRead < willRead) {
        console.error(
          `[devtools perf front] We were expecting ${willRead} bytes, but received ${hasRead} bytes instead.`
        );
      }
      const toCopyArray = this.#tempBufferAsArray.subarray(0, hasRead);
      this.#outputArray.set(toCopyArray, this.#pointer);
      this.#pointer += hasRead;
      remaining -= hasRead;
    }
    logger.debug(
      `${count} bytes have been successfully read. Total: ${this.#pointer} / ${this.#count}`
    );
  }
}

/**
 * This function efficiently copies the content of an array buffer to an async stream.
 * Usage:
 *   // The buffer length is used to define the length of data to copy to the stream.
 *   await copyArrayBufferToAsyncStream(buffer, asyncOutputStream);
 *
 * @param {ArrayBuffer} buffer The byteLength of this buffer will be used to define the length of data to copy to the stream.
 * @param {nsIAsyncStream} asyncOutputStream
 */
async function copyArrayBufferToAsyncStream(buffer, asyncOutputStream) {
  const writer = new ArrayBufferToAsyncStreamCopier(buffer, asyncOutputStream);
  await writer.asyncWrite();
}

class ArrayBufferToAsyncStreamCopier {
  #BUFFER_SIZE = 16 * 1024; // A 16k buffer

  /**
   * @typedef {nsIAsyncOutputStream}
   */
  #originalStream;

  /**
   * This is a wrapper on top of #originalStream, to be able to write buffers
   * easily.
   *
   * @typedef {nsIBinaryOutputStream}
   */
  #binaryStream;

  /**
   * This is the input buffer, accessed as an UInt8Array.
   *
   * @typedef {Uint8Array}
   */
  #inputArray;

  /**
   * How many bytes have been read already. This is also the next index to read
   * in #outputArray.
   *
   * @typedef {number}
   */
  #pointer = 0;

  /**
   * The count of bytes to be transfered. It is infered from the byteLength of
   * of the input buffer.
   *
   * @typedef {number}
   */
  #count;

  /**
   * @param {ArrayBuffer} arrayBuffer The byteLength of this buffer will be used to define the length of data to copy to the stream.
   * @param {nsIAsyncStream} stream
   */
  constructor(arrayBuffer, stream) {
    this.#originalStream = stream;
    this.#binaryStream = Cc["@mozilla.org/binaryoutputstream;1"].createInstance(
      Ci.nsIBinaryOutputStream
    );
    this.#binaryStream.setOutputStream(stream);

    this.#inputArray = new Uint8Array(arrayBuffer);
    this.#count = arrayBuffer.byteLength;
  }

  /**
   * @returns {Promise<void>} Resolves when the reading has finished.
   */
  async asyncWrite() {
    do {
      await this.#waitForStreamAvailability();
      this.#syncWrite();
    } while (this.#pointer < this.#count);
    logger.debug(`Successfully wrote ${this.#count} bytes!`);
  }

  /**
   * @returns {Promise<void>} Resolves when the stream is available.
   */
  async #waitForStreamAvailability() {
    return new Promise(resolve => {
      this.#originalStream.asyncWait(
        () => resolve(),
        0,
        0,
        Services.tm.currentThread
      );
    });
  }

  /**
   * @returns {void}
   */
  #syncWrite() {
    const amountLeft = this.#count - this.#pointer;
    if (amountLeft <= 0) {
      return;
    }

    let remaining = amountLeft;
    while (remaining) {
      const willWrite = Math.min(remaining, this.#BUFFER_SIZE);
      const subarray = this.#inputArray.subarray(
        this.#pointer,
        this.#pointer + willWrite
      );
      try {
        // Bug 1962705: writeByteArray does a copy in
        // https://searchfox.org/mozilla-central/rev/3d294b119bf2add880f615a0fc61a5d54bcd6264/js/xpconnect/src/XPCConvert.cpp#1440
        // modify BinaryOutputStream so that it can read directly from the buffer.
        this.#binaryStream.writeByteArray(subarray);
      } catch (e) {
        if (e.result == Cr.NS_BASE_STREAM_WOULD_BLOCK) {
          logger.debug(
            `Base stream would block, will retry. ${amountLeft - remaining} bytes have been successfully written. Total: ${this.#pointer} / ${this.#count}`
          );
          return;
        }
        throw e;
      }

      this.#pointer += willWrite;
      remaining -= willWrite;
    }
    logger.debug(
      `${amountLeft - remaining} bytes have been successfully written. Total: ${this.#pointer} / ${this.#count}`
    );
  }
}

module.exports = {
  copyStream,
  delimitedRead,
  copyAsyncStreamToArrayBuffer,
  copyArrayBufferToAsyncStream,
};
