/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";

const { DefaultMap } = ExtensionUtils;

const lazy = {};

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "mimeHeader",
  "@mozilla.org/network/mime-hdrparam;1",
  Ci.nsIMIMEHeaderParam
);

const BinaryInputStream = Components.Constructor(
  "@mozilla.org/binaryinputstream;1",
  "nsIBinaryInputStream",
  "setInputStream"
);
const ConverterInputStream = Components.Constructor(
  "@mozilla.org/intl/converter-input-stream;1",
  "nsIConverterInputStream",
  "init"
);

export var WebRequestUpload;

/**
 * Parses the given raw header block, and stores the value of each
 * lower-cased header name in the resulting map.
 */
class Headers extends Map {
  constructor(headerText) {
    super();

    if (headerText) {
      this.parseHeaders(headerText);
    }
  }

  parseHeaders(headerText) {
    let lines = headerText.split("\r\n");

    let lastHeader;
    for (let line of lines) {
      // The first empty line indicates the end of the header block.
      if (line === "") {
        return;
      }

      // Lines starting with whitespace are appended to the previous
      // header.
      if (/^\s/.test(line)) {
        if (lastHeader) {
          let val = this.get(lastHeader);
          this.set(lastHeader, `${val}\r\n${line}`);
        }
        continue;
      }

      let match = /^(.*?)\s*:\s+(.*)/.exec(line);
      if (match) {
        lastHeader = match[1].toLowerCase();
        this.set(lastHeader, match[2]);
      }
    }
  }

  /**
   * If the given header exists, and contains the given parameter,
   * returns the value of that parameter.
   *
   * @param {string} name
   *        The lower-cased header name.
   * @param {string} paramName
   *        The name of the parameter to retrieve, or empty to retrieve
   *        the first (possibly unnamed) parameter.
   * @returns {string | null}
   */
  getParam(name, paramName) {
    return Headers.getParam(this.get(name), paramName);
  }

  /**
   * If the given header value is non-null, and contains the given
   * parameter, returns the value of that parameter.
   *
   * @param {string | null} header
   *        The text of the header from which to retrieve the param.
   * @param {string} paramName
   *        The name of the parameter to retrieve, or empty to retrieve
   *        the first (possibly unnamed) parameter.
   * @returns {string | null}
   */
  static getParam(header, paramName) {
    if (header) {
      // The service expects this to be a raw byte string, so convert to
      // UTF-8.
      let bytes = new TextEncoder().encode(header);
      let binHeader = String.fromCharCode(...bytes);

      return lazy.mimeHeader.getParameterHTTP(
        binHeader,
        paramName,
        null,
        false,
        {}
      );
    }

    return null;
  }
}

/**
 * Rewinds the given seekable input stream to its beginning, and catches
 * any resulting errors.
 *
 * @param {nsISeekableStream} stream
 *        The stream to rewind.
 */
function rewind(stream) {
  // Do this outside the try-catch so that we throw if the stream is not
  // actually seekable.
  stream.QueryInterface(Ci.nsISeekableStream);

  try {
    stream.seek(0, 0);
  } catch (e) {
    // It might be already closed, e.g. because of a previous error.
    Cu.reportError(e);
  }
}

/**
 * Iterates over all of the sub-streams that make up the given stream,
 * or yields the stream itself if it is not a multi-part stream.
 *
 * @param {nsIIMultiplexInputStream|nsIStreamBufferAccess<nsIMultiplexInputStream>|nsIInputStream} outerStream
 *        The outer stream over which to iterate.
 */
function* getStreams(outerStream) {
  // If this is a multi-part stream, we need to iterate over its sub-streams,
  // rather than treating it as a simple input stream. Since it may be wrapped
  // in a buffered input stream, unwrap it before we do any checks.
  let unbuffered = outerStream;
  if (outerStream instanceof Ci.nsIStreamBufferAccess) {
    unbuffered = outerStream.unbufferedStream;
  }

  if (unbuffered instanceof Ci.nsIMultiplexInputStream) {
    let count = unbuffered.count;
    for (let i = 0; i < count; i++) {
      yield unbuffered.getStream(i);
    }
  } else {
    yield outerStream;
  }
}

/**
 * Parses the form data of the given stream as either multipart/form-data or
 * x-www-form-urlencoded, and returns a map of its fields.
 *
 * @param {nsIInputStream} stream
 *        The input stream from which to parse the form data.
 * @param {nsIHttpChannel} channel
 *        The channel to which the stream belongs.
 * @param {boolean} [lenient = false]
 *        If true, the operation will succeed even if there are UTF-8
 *        decoding errors.
 *
 * @returns {Map<string, Array<string>> | null}
 */
function parseFormData(stream, channel, lenient = false) {
  const BUFFER_SIZE = 8192;

  let touchedStreams = new Set();
  let converterStreams = [];

  /**
   * Creates a converter input stream from the given raw input stream,
   * and adds it to the list of streams to be rewound at the end of
   * parsing.
   *
   * Returns null if the given raw stream cannot be rewound.
   *
   * @param {nsIInputStream} stream
   *        The base stream from which to create a converter.
   * @returns {ConverterInputStream | null}
   */
  function createTextStream(stream) {
    if (!(stream instanceof Ci.nsISeekableStream)) {
      return null;
    }

    touchedStreams.add(stream);
    let converterStream = ConverterInputStream(
      stream,
      "UTF-8",
      0,
      lenient ? Ci.nsIConverterInputStream.DEFAULT_REPLACEMENT_CHARACTER : 0
    );
    converterStreams.push(converterStream);
    return converterStream;
  }

  /**
   * Reads a string of no more than the given length from the given text
   * stream.
   *
   * @param {ConverterInputStream} stream
   *        The stream to read.
   * @param {integer} [length = BUFFER_SIZE]
   *        The maximum length of data to read.
   * @returns {string}
   */
  function readString(stream, length = BUFFER_SIZE) {
    let data = {};
    stream.readString(length, data);
    return data.value;
  }

  /**
   * Iterates over all of the sub-streams of the given (possibly multi-part)
   * input stream, and yields a ConverterInputStream for each
   * nsIStringInputStream among them.
   *
   * @param {nsIInputStream|nsIMultiplexInputStream} outerStream
   *        The multi-part stream over which to iterate.
   */
  function* getTextStreams(outerStream) {
    for (let stream of getStreams(outerStream)) {
      if (stream instanceof Ci.nsIStringInputStream) {
        touchedStreams.add(outerStream);
        yield createTextStream(stream);
      }
    }
  }

  /**
   * Iterates over all of the string streams of the given (possibly
   * multi-part) input stream, and yields all of the available data in each as
   * chunked strings, each no more than BUFFER_SIZE in length.
   *
   * @param {nsIInputStream|nsIMultiplexInputStream} outerStream
   *        The multi-part stream over which to iterate.
   */
  function* readAllStrings(outerStream) {
    for (let textStream of getTextStreams(outerStream)) {
      let str;
      while ((str = readString(textStream))) {
        yield str;
      }
    }
  }

  /**
   * Iterates over the text contents of all of the string streams in the given
   * (possibly multi-part) input stream, splits them at occurrences of the
   * given boundary string, and yields each part.
   *
   * @param {nsIInputStream|nsIMultiplexInputStream} stream
   *        The multi-part stream over which to iterate.
   * @param {string} boundary
   *        The boundary at which to split the parts.
   * @param {string} [tail = ""]
   *        Any initial data to prepend to the start of the stream data.
   */
  function* getParts(stream, boundary, tail = "") {
    for (let chunk of readAllStrings(stream)) {
      chunk = tail + chunk;

      let parts = chunk.split(boundary);
      tail = parts.pop();

      yield* parts;
    }

    if (tail) {
      yield tail;
    }
  }

  /**
   * Parses the given stream as multipart/form-data and returns a map of its fields.
   *
   * @param {nsIMultiplexInputStream|nsIInputStream} stream
   *        The (possibly multi-part) stream to parse.
   * @param {string} boundary
   *        The boundary at which to split the parts.
   * @returns {Map<string, Array<string>>}
   */
  function parseMultiPart(stream, boundary) {
    let formData = new DefaultMap(() => []);

    for (let part of getParts(stream, boundary, "\r\n")) {
      if (part === "") {
        // The first part will always be empty.
        continue;
      }
      if (part === "--\r\n") {
        // This indicates the end of the stream.
        break;
      }

      let end = part.indexOf("\r\n\r\n");

      // All valid parts must begin with \r\n, and we can't process form
      // fields without any header block.
      if (!part.startsWith("\r\n") || end <= 0) {
        throw new Error("Invalid MIME stream");
      }

      let content = part.slice(end + 4);
      let headerText = part.slice(2, end);
      let headers = new Headers(headerText);

      let name = headers.getParam("content-disposition", "name");
      if (
        !name ||
        headers.getParam("content-disposition", "") !== "form-data"
      ) {
        throw new Error(
          "Invalid MIME stream: No valid Content-Disposition header"
        );
      }

      // Decode the percent-escapes in the name. Unlike with decodeURIComponent,
      // partial percent-escapes are passed through as is rather than throwing
      // exceptions.
      name = name.replace(/(%[0-9A-Fa-f]{2})+/g, match => {
        const bytes = new Uint8Array(match.length / 3);
        for (let i = 0; i < match.length / 3; i++) {
          bytes[i] = parseInt(match.substring(i * 3 + 1, (i + 1) * 3), 16);
        }
        return new TextDecoder("utf-8").decode(bytes);
      });

      if (headers.has("content-type")) {
        // For file upload fields, we return the filename, rather than the
        // file data. We're following Chrome in not percent-decoding the
        // filename.
        let filename = headers.getParam("content-disposition", "filename");
        content = filename || "";
      }
      formData.get(name).push(content);
    }

    return formData;
  }

  /**
   * Parses the given stream as x-www-form-urlencoded, and returns a map of its fields.
   *
   * @param {nsIInputStream} stream
   *        The stream to parse.
   * @returns {Map<string, Array<string>>}
   */
  function parseUrlEncoded(stream) {
    let formData = new DefaultMap(() => []);

    for (let part of getParts(stream, "&")) {
      let [name, value] = part
        .replace(/\+/g, " ")
        .split("=")
        .map(decodeURIComponent);
      formData.get(name).push(value);
    }

    return formData;
  }

  try {
    if (stream instanceof Ci.nsIMIMEInputStream && stream.data) {
      stream = stream.data;
    }

    channel.QueryInterface(Ci.nsIHttpChannel);
    let contentType;
    try {
      contentType = channel.getRequestHeader("Content-Type");
    } catch (error) {
      if (error.result != Cr.NS_ERROR_NOT_AVAILABLE) {
        throw error;
      }
      return null;
    }

    switch (Headers.getParam(contentType, "")) {
      case "multipart/form-data": {
        let boundary = Headers.getParam(contentType, "boundary");
        return parseMultiPart(stream, `\r\n--${boundary}`);
      }
      case "application/x-www-form-urlencoded":
        return parseUrlEncoded(stream);
    }
  } finally {
    for (let stream of touchedStreams) {
      rewind(stream);
    }
    for (let converterStream of converterStreams) {
      // Release the reference to the underlying input stream, to prevent the
      // destructor of nsConverterInputStream from closing the stream, which
      // would cause uploads to break.
      converterStream.init(null, null, 0, 0);
    }
  }

  return null;
}

/**
 * Parses the form data of the given stream as either multipart/form-data or
 * x-www-form-urlencoded, and returns a map of its fields.
 *
 * Returns null if the stream is not seekable.
 *
 * @param {nsIMultiplexInputStream|nsIInputStream} stream
 *        The (possibly multi-part) stream from which to create the form data.
 * @param {nsIChannel} channel
 *        The channel to which the stream belongs.
 * @param {boolean} [lenient = false]
 *        If true, the operation will succeed even if there are UTF-8
 *        decoding errors.
 * @returns {Map<string, Array<string>> | null}
 */
function createFormData(stream, channel, lenient) {
  if (!(stream instanceof Ci.nsISeekableStream)) {
    return null;
  }

  try {
    let formData = parseFormData(stream, channel, lenient);
    if (formData) {
      return Object.fromEntries(formData);
    }
  } catch (e) {
    Cu.reportError(e);
  } finally {
    rewind(stream);
  }
  return null;
}

/**
 * Iterates over all of the sub-streams of the given (possibly multi-part)
 * input stream, and yields an object containing the data for each chunk, up
 * to a total of `maxRead` bytes.
 *
 * @param {nsIMultiplexInputStream|nsIInputStream} outerStream
 *        The stream for which to return data.
 * @param {integer} [maxRead = WebRequestUpload.MAX_RAW_BYTES]
 *        The maximum total bytes to read.
 */
function* getRawDataChunked(
  outerStream,
  maxRead = WebRequestUpload.MAX_RAW_BYTES
) {
  for (let stream of getStreams(outerStream)) {
    // We need to inspect the stream to make sure it's not a file input
    // stream. If it's wrapped in a buffered input stream, unwrap it first,
    // so we can inspect the inner stream directly.
    let unbuffered = stream;
    if (stream instanceof Ci.nsIStreamBufferAccess) {
      unbuffered = stream.unbufferedStream;
    }

    // For file fields, we return an object containing the full path of
    // the file, rather than its data.
    if (
      unbuffered instanceof Ci.nsIFileInputStream ||
      unbuffered instanceof Ci.mozIRemoteLazyInputStream
    ) {
      // But this is not actually supported yet.
      yield { file: "<file>" };
      continue;
    }

    try {
      let binaryStream = BinaryInputStream(stream);
      let available;
      while ((available = binaryStream.available())) {
        let buffer = new ArrayBuffer(Math.min(maxRead, available));
        binaryStream.readArrayBuffer(buffer.byteLength, buffer);

        maxRead -= buffer.byteLength;

        let chunk = { bytes: buffer };

        if (buffer.byteLength < available) {
          chunk.truncated = true;
          chunk.originalSize = available;
        }

        yield chunk;

        if (maxRead <= 0) {
          return;
        }
      }
    } finally {
      rewind(stream);
    }
  }
}

WebRequestUpload = {
  createRequestBody(channel) {
    if (!(channel instanceof Ci.nsIUploadChannel) || !channel.uploadStream) {
      return null;
    }

    try {
      let stream = channel.uploadStream;

      let formData = createFormData(stream, channel);
      if (formData) {
        return { formData };
      }

      // If we failed to parse the stream as form data, return it as a
      // sequence of raw data chunks, along with a leniently-parsed form
      // data object, which ignores encoding errors.
      return {
        raw: Array.from(getRawDataChunked(stream)),
        lenientFormData: createFormData(stream, channel, true),
      };
    } catch (e) {
      Cu.reportError(e);
      return { error: e.message || String(e) };
    }
  },
};

XPCOMUtils.defineLazyPreferenceGetter(
  WebRequestUpload,
  "MAX_RAW_BYTES",
  "webextensions.webRequest.requestBodyMaxRawBytes"
);
