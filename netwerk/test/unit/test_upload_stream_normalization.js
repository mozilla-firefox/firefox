/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Upload streams are normalized in the parent process so that the channel can
// rewind them for a redirect or an auth retry and nsHttpTransaction can read a
// clone of them.  Wrappers such as nsMultiplexInputStream implement
// nsICloneableInputStream but refuse to clone once they have been read from;
// the channel must reject such a stream rather than accept it.

function stringStream(data) {
  let stream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(
    Ci.nsIStringInputStream
  );
  stream.setByteStringData(data);
  return stream;
}

function partiallyReadMultiplexStream() {
  let multiplex = Cc["@mozilla.org/io/multiplex-input-stream;1"].createInstance(
    Ci.nsIMultiplexInputStream
  );
  multiplex.appendStream(stringStream("hello world"));

  let scriptable = Cc["@mozilla.org/scriptableinputstream;1"].createInstance(
    Ci.nsIScriptableInputStream
  );
  scriptable.init(multiplex);
  Assert.equal(scriptable.read(5), "hello");
  Assert.ok(
    !multiplex.QueryInterface(Ci.nsICloneableInputStream).cloneable,
    "a multiplex stream that has been read from refuses to clone"
  );

  return multiplex;
}

function setUploadStream(stream) {
  let chan = NetUtil.newChannel({
    uri: "http://localhost:4444/",
    loadUsingSystemPrincipal: true,
  });
  chan
    .QueryInterface(Ci.nsIUploadChannel2)
    .explicitSetUploadStream(stream, "text/plain", -1, "POST");
  return chan.QueryInterface(Ci.nsIUploadChannel).uploadStream;
}

function assertRejected(stream, what) {
  Assert.throws(
    () => setUploadStream(stream),
    e => e.result == Cr.NS_ERROR_INVALID_ARG,
    `${what} is rejected`
  );
}

add_task(function test_unread_multiplex_upload_stream() {
  let multiplex = Cc["@mozilla.org/io/multiplex-input-stream;1"].createInstance(
    Ci.nsIMultiplexInputStream
  );
  multiplex.appendStream(stringStream("hello world"));

  let stream = setUploadStream(multiplex);
  Assert.ok(
    stream.QueryInterface(Ci.nsICloneableInputStream).cloneable,
    "unread multiplex upload stream is cloneable"
  );
  Assert.ok(
    stream instanceof Ci.nsISeekableStream,
    "unread multiplex upload stream is seekable"
  );
});

add_task(function test_multiplex_upload_stream() {
  assertRejected(
    partiallyReadMultiplexStream(),
    "partially read multiplex upload stream"
  );
});

add_task(function test_buffered_upload_stream() {
  let buffered = Cc[
    "@mozilla.org/network/buffered-input-stream;1"
  ].createInstance(Ci.nsIBufferedInputStream);
  buffered.init(partiallyReadMultiplexStream(), 4096);

  assertRejected(buffered, "buffered partially read upload stream");
});

add_task(function test_mime_upload_stream() {
  let mime = Cc["@mozilla.org/network/mime-input-stream;1"].createInstance(
    Ci.nsIMIMEInputStream
  );
  mime.addHeader("Content-Type", "text/plain");
  mime.setData(partiallyReadMultiplexStream());

  assertRejected(mime, "MIME partially read upload stream");
});
