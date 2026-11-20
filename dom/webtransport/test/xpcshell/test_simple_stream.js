/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Using multiple files to reduce racing
// This file tests reading/writing to incoming/outgoing streams (uni & bidi)
//
"use strict";

var h3Port;
var host;

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref("network.dns.localDomains");
  Services.prefs.clearUserPref("network.webtransport.redirect.enabled");
});

var { NetUtil } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);

function readFile(file) {
  let fstream = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(
    Ci.nsIFileInputStream
  );
  fstream.init(file, -1, 0, 0);
  let data = NetUtil.readInputStreamToString(fstream, fstream.available());
  fstream.close();
  return data;
}

function addCertFromFile(certdb, filename, trustString) {
  let certFile = do_get_file(filename, false);
  let pem = readFile(certFile)
    .replace(/-----BEGIN CERTIFICATE-----/, "")
    .replace(/-----END CERTIFICATE-----/, "")
    .replace(/[\r\n]/g, "");
  certdb.addCertFromBase64(pem, trustString);
}

add_setup(async function setup() {
  Services.prefs.setCharPref("network.dns.localDomains", "foo.example.com");
  Services.prefs.setBoolPref("network.webtransport.redirect.enabled", true);

  h3Port = Services.env.get("MOZHTTP3_PORT");
  Assert.notEqual(h3Port, null);
  Assert.notEqual(h3Port, "");
  host = "foo.example.com:" + h3Port;
  do_get_profile();

  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  // `../unit/` so that unit_ipc tests can use as well
  addCertFromFile(
    certdb,
    "../../../../netwerk/test/unit/http2-ca.pem",
    "CTu,u,u"
  );
});

// Read all chunks from |readable_stream|, decode chunks to a utf-8 string, then
// return the string. (borrowed from wpt tests)
async function read_stream_as_string(readable_stream) {
  const decoder = new (webTransportWindow().TextDecoderStream)();
  const decode_stream = readable_stream.pipeThrough(decoder);
  const reader = decode_stream.getReader();

  let chunks = "";
  while (true) {
    const { value: chunk, done } = await reader.read();
    if (done) {
      break;
    }
    chunks += chunk;
  }
  reader.releaseLock();
  return chunks;
}

add_task(async function test_wt_incoming_unidi_stream() {
  // trigger stream creation server side and default echo
  let wt = newWebTransport(
    "https://" + host + "/create_unidi_stream_and_hello"
  );
  await wt.ready;

  const streams = await wt.incomingUnidirectionalStreams;
  const stream_reader = streams.getReader();
  const { value: recv_stream } = await stream_reader.read();
  let str = await read_stream_as_string(recv_stream);
  stream_reader.releaseLock();
  Assert.equal(str, "qwerty");

  wt.close();
});

add_task(async function test_wt_incoming_and_outgoing_unidi_stream() {
  // create the client's incoming stream from the server side
  // we need it to listen to the echo back
  let wt = newWebTransport("https://" + host + "/create_unidi_stream");
  await wt.ready;

  // send hello to server
  let expected = "uni_hello";
  let writableStream = await wt.createUnidirectionalStream(); // only triggers NewStream OnWrite
  let wsDefaultWriter = writableStream.getWriter();
  await wsDefaultWriter.ready;
  let data = new (webTransportWindow().TextEncoder)().encode(expected);
  await wsDefaultWriter.write(data); // triggers Http3ServerEvent::Data
  await wsDefaultWriter.close();
  wsDefaultWriter.releaseLock();

  // read the echo
  const streams = await wt.incomingUnidirectionalStreams;
  const stream_reader = streams.getReader();
  const { value: recv_stream } = await stream_reader.read();
  let str = await read_stream_as_string(recv_stream);
  Assert.equal(str, expected);
  stream_reader.releaseLock();
  await recv_stream.closed;

  wt.close();
});

add_task(async function test_wt_outgoing_bidi_stream() {
  let wt = newWebTransport("https://" + host + "/success");
  await wt.ready;

  // write to server
  let wtbds = await wt.createBidirectionalStream();
  let writableStream = wtbds.writable;
  let wsDefaultWriter = writableStream.getWriter();
  await wsDefaultWriter.ready;
  let expected = "xyzhello";
  let data = new (webTransportWindow().TextEncoder)().encode(expected);
  await wsDefaultWriter.write(data);
  await wsDefaultWriter.close();
  wsDefaultWriter.releaseLock();

  // string goes through server and is echoed back here
  const str = await read_stream_as_string(wtbds.readable);
  Assert.equal(str, expected);

  wt.close();
});

add_task(async function test_wt_incoming_bidi_stream() {
  let wt = newWebTransport("https://" + host + "/create_bidi_stream_and_hello");
  // await wt.ready; // causes occasional hang on release --verify

  const stream_reader = wt.incomingBidirectionalStreams.getReader();
  const { value: bidi_stream } = await stream_reader.read();
  stream_reader.releaseLock();

  const str = await read_stream_as_string(bidi_stream.readable);
  Assert.equal(str, "asdfg");

  wt.close();
});

add_task(async function test_wt_incoming_bidi_stream_huge_data() {
  let wt = newWebTransport(
    "https://" + host + "/create_bidi_stream_and_large_data"
  );
  // await wt.ready; // causes occasional hang on release --verify

  const stream_reader = wt.incomingBidirectionalStreams.getReader();
  const { value: bidi_stream } = await stream_reader.read();
  stream_reader.releaseLock();

  const str = await read_stream_as_string(bidi_stream.readable);
  Assert.equal(str.length, 32 * 1024 * 1024);
  wt.close();
});
