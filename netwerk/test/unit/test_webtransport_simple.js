//
//  Simple WebTransport test
//

/* import-globals-from head_webtransport.js */

"use strict";

var h3Port;
var host;

var { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref("network.dns.localDomains");
  Services.prefs.clearUserPref(
    "network.http.http3.alt-svc-mapping-for-testing"
  );
});

add_task(async function setup() {
  await http3_setup_tests("h3");

  h3Port = Services.env.get("MOZHTTP3_PORT");
  Assert.notEqual(h3Port, null);
  Assert.notEqual(h3Port, "");
  host = "foo.example.com:" + h3Port;
});

function makeChan(url) {
  let chan = NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
    contentPolicyType: Ci.nsIContentPolicy.TYPE_DOCUMENT,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  return chan;
}

function channelOpenPromise(chan, flags) {
  // eslint-disable-next-line no-async-promise-executor
  return new Promise(async resolve => {
    function finish(req, buffer) {
      resolve([req, buffer]);
    }
    chan.asyncOpen(new ChannelListener(finish, null, flags));
  });
}

function bytesFromString(str) {
  return new TextEncoder().encode(str);
}

add_task(async function test_wt_datagram() {
  let webTransport = NetUtil.newWebTransport();
  let listener = new WebTransportListener().QueryInterface(
    Ci.WebTransportSessionEventListener
  );

  let pReady = new Promise(resolve => {
    listener.ready = resolve;
  });
  let pSize = new Promise(resolve => {
    listener.onMaxDatagramSize = resolve;
  });
  let pOutcome = new Promise(resolve => {
    listener.onDatagramOutcome = resolve;
  });

  webTransport.asyncConnect(
    NetUtil.newURI(`https://${host}/success`),
    true,
    [],
    Services.scriptSecurityManager.getSystemPrincipal(),
    Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
    listener
  );

  await pReady;

  webTransport.getMaxDatagramSize();
  let size = await pSize;
  info("max size:" + size);

  let rawData = new Uint8Array(size);
  rawData.fill(42);

  webTransport.sendDatagram(rawData, 1, 0, 0);
  let { id, outcome } = await pOutcome;
  Assert.equal(id, 1);
  Assert.equal(outcome, Ci.WebTransportSessionEventListener.SENT);

  let chan = makeChan(`https://${host}/get_webtransport_datagram`);
  let [req, buffer] = await channelOpenPromise(chan);
  Assert.equal(req.protocolVersion, "h3");

  Assert.deepEqual(bytesFromString(buffer), rawData);

  webTransport.getMaxDatagramSize();
  size = await pSize;
  info("max size:" + size);

  rawData = new Uint8Array(size + 1);
  webTransport.sendDatagram(rawData, 2, 0, 0);

  pOutcome = new Promise(resolve => {
    listener.onDatagramOutcome = resolve;
  });
  ({ id, outcome } = await pOutcome);
  Assert.equal(id, 2);
  Assert.equal(
    outcome,
    Ci.WebTransportSessionEventListener.DROPPED_TOO_MUCH_DATA
  );

  webTransport.closeSession(0, "");
});

add_task(async function test_connect_wt() {
  let webTransport = NetUtil.newWebTransport();

  await new Promise(resolve => {
    let listener = new WebTransportListener().QueryInterface(
      Ci.WebTransportSessionEventListener
    );
    listener.ready = resolve;

    webTransport.asyncConnect(
      NetUtil.newURI(`https://${host}/success`),
      true,
      [],
      Services.scriptSecurityManager.getSystemPrincipal(),
      Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
      listener
    );
  });

  webTransport.closeSession(0, "");
});

add_task(async function test_redirect_wt() {
  let webTransport = NetUtil.newWebTransport();

  await new Promise(resolve => {
    let listener = new WebTransportListener().QueryInterface(
      Ci.WebTransportSessionEventListener
    );

    listener.closed = resolve;

    webTransport.asyncConnect(
      NetUtil.newURI(`https://${host}/redirect`),
      true,
      [],
      Services.scriptSecurityManager.getSystemPrincipal(),
      Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
      listener
    );
  });
});

add_task(async function test_reject() {
  let webTransport = NetUtil.newWebTransport();

  await new Promise(resolve => {
    let listener = new WebTransportListener().QueryInterface(
      Ci.WebTransportSessionEventListener
    );
    listener.closed = resolve;

    webTransport.asyncConnect(
      NetUtil.newURI(`https://${host}/reject`),
      true,
      [],
      Services.scriptSecurityManager.getSystemPrincipal(),
      Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
      listener
    );
  });
});

async function test_closed(path) {
  let webTransport = NetUtil.newWebTransport();

  let listener = new WebTransportListener().QueryInterface(
    Ci.WebTransportSessionEventListener
  );

  let pReady = new Promise(resolve => {
    listener.ready = resolve;
  });
  let pClose = new Promise(resolve => {
    listener.closed = resolve;
  });
  webTransport.asyncConnect(
    NetUtil.newURI(`https://${host}${path}`),
    true,
    [],
    Services.scriptSecurityManager.getSystemPrincipal(),
    Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
    listener
  );

  await pReady;
  await pClose;
}

add_task(async function test_closed_0ms() {
  await test_closed("/closeafter0ms");
});

add_task(async function test_closed_100ms() {
  await test_closed("/closeafter100ms");
});

add_task(async function test_wt_stream_create() {
  let webTransport = NetUtil.newWebTransport().QueryInterface(
    Ci.nsIWebTransport
  );

  await new Promise(resolve => {
    let listener = new WebTransportListener().QueryInterface(
      Ci.WebTransportSessionEventListener
    );
    listener.ready = resolve;

    webTransport.asyncConnect(
      NetUtil.newURI(`https://${host}/success`),
      true,
      [],
      Services.scriptSecurityManager.getSystemPrincipal(),
      Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
      listener
    );
  });

  await streamCreatePromise(webTransport, true);
  await streamCreatePromise(webTransport, false);

  webTransport.closeSession(0, "");
});

add_task(async function test_wt_stream_send_and_stats() {
  let webTransport = NetUtil.newWebTransport().QueryInterface(
    Ci.nsIWebTransport
  );

  await new Promise(resolve => {
    let listener = new WebTransportListener().QueryInterface(
      Ci.WebTransportSessionEventListener
    );
    listener.ready = resolve;

    webTransport.asyncConnect(
      NetUtil.newURI(`https://${host}/success`),
      true,
      [],
      Services.scriptSecurityManager.getSystemPrincipal(),
      Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
      listener
    );
  });

  let stream = await streamCreatePromise(webTransport, false);
  let outputStream = stream.outputStream;

  let data = "1234567890ABC";
  outputStream.write(data, data.length);

  // We need some time to send the packet out.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 2000));

  let stats = await sendStreamStatsPromise(stream);
  Assert.equal(stats.bytesSent, data.length);

  webTransport.closeSession(0, "");
});

add_task(async function test_wt_receive_stream_and_stats() {
  let webTransport = NetUtil.newWebTransport().QueryInterface(
    Ci.nsIWebTransport
  );

  let listener = new WebTransportListener().QueryInterface(
    Ci.WebTransportSessionEventListener
  );

  let pReady = new Promise(resolve => {
    listener.ready = resolve;
  });
  let pStreamReady = new Promise(resolve => {
    listener.streamAvailable = resolve;
  });
  webTransport.asyncConnect(
    NetUtil.newURI(`https://${host}/create_unidi_stream_and_hello`),
    true,
    [],
    Services.scriptSecurityManager.getSystemPrincipal(),
    Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
    listener
  );

  await pReady;
  let stream = await pStreamReady;

  let data = await new Promise(resolve => {
    let handler = new inputStreamReader().QueryInterface(
      Ci.nsIInputStreamCallback
    );
    handler.finish = resolve;
    let inputStream = stream.inputStream;
    inputStream.asyncWait(handler, 0, 0, Services.tm.currentThread);
  });

  info("data: " + data);
  Assert.equal(data, "qwerty");

  let stats = await receiveStreamStatsPromise(stream);
  Assert.equal(stats.bytesReceived, data.length);

  stream.sendStopSending(0);

  webTransport.closeSession(0, "");
});

add_task(async function test_wt_outgoing_bidi_stream() {
  let webTransport = NetUtil.newWebTransport().QueryInterface(
    Ci.nsIWebTransport
  );

  await new Promise(resolve => {
    let listener = new WebTransportListener().QueryInterface(
      Ci.WebTransportSessionEventListener
    );
    listener.ready = resolve;

    webTransport.asyncConnect(
      NetUtil.newURI(`https://${host}/success`),
      true,
      [],
      Services.scriptSecurityManager.getSystemPrincipal(),
      Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
      listener
    );
  });

  let stream = await streamCreatePromise(webTransport, true);
  let outputStream = stream.outputStream;

  let data = "1234567";
  outputStream.write(data, data.length);

  let received = await new Promise(resolve => {
    let handler = new inputStreamReader().QueryInterface(
      Ci.nsIInputStreamCallback
    );
    handler.finish = resolve;
    let inputStream = stream.inputStream;
    inputStream.asyncWait(handler, 0, 0, Services.tm.currentThread);
  });

  info("received: " + received);
  Assert.equal(received, data);

  let stats = await sendStreamStatsPromise(stream);
  Assert.equal(stats.bytesSent, data.length);

  stats = await receiveStreamStatsPromise(stream);
  Assert.equal(stats.bytesReceived, data.length);

  webTransport.closeSession(0, "");
});

add_task(async function test_wt_incoming_bidi_stream() {
  let webTransport = NetUtil.newWebTransport().QueryInterface(
    Ci.nsIWebTransport
  );

  let listener = new WebTransportListener().QueryInterface(
    Ci.WebTransportSessionEventListener
  );

  let pReady = new Promise(resolve => {
    listener.ready = resolve;
  });
  let pStreamReady = new Promise(resolve => {
    listener.streamAvailable = resolve;
  });
  webTransport.asyncConnect(
    NetUtil.newURI(`https://${host}/create_bidi_stream`),
    true,
    [],
    Services.scriptSecurityManager.getSystemPrincipal(),
    Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
    listener
  );

  await pReady;
  let stream = await pStreamReady;

  let outputStream = stream.outputStream;

  let data = "12345678";
  outputStream.write(data, data.length);

  let received = await new Promise(resolve => {
    let handler = new inputStreamReader().QueryInterface(
      Ci.nsIInputStreamCallback
    );
    handler.finish = resolve;
    let inputStream = stream.inputStream;
    inputStream.asyncWait(handler, 0, 0, Services.tm.currentThread);
  });

  info("received: " + received);
  Assert.equal(received, data);

  let stats = await sendStreamStatsPromise(stream);
  Assert.equal(stats.bytesSent, data.length);

  stats = await receiveStreamStatsPromise(stream);
  Assert.equal(stats.bytesReceived, data.length);

  webTransport.closeSession(0, "");
});

async function createWebTransportAndConnect() {
  let webTransport = NetUtil.newWebTransport();

  await new Promise(resolve => {
    let listener = new WebTransportListener().QueryInterface(
      Ci.WebTransportSessionEventListener
    );
    listener.ready = resolve;

    webTransport.asyncConnect(
      NetUtil.newURI(`https://${host}/success`),
      true,
      [],
      Services.scriptSecurityManager.getSystemPrincipal(),
      Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
      listener
    );
  });

  return webTransport;
}

add_task(async function test_multple_webtransport_connnection() {
  let webTransports = [];
  for (let i = 0; i < 3; i++) {
    let transport = await createWebTransportAndConnect();
    webTransports.push(transport);
  }

  let first = webTransports[0];
  await streamCreatePromise(first, true);

  for (let i = 0; i < 3; i++) {
    webTransports[i].closeSession(0, "");
  }
});

// WebTransport over HTTP/2 is not fully implemented, so it is refused unless
// network.webtransport.http2.enabled is set.
add_task(async function test_wt_over_http2_disabled() {
  let webTransport = NetUtil.newWebTransport();
  let listener = new WebTransportListener().QueryInterface(
    Ci.WebTransportSessionEventListener
  );
  let closed = new Promise(resolve => {
    listener.closed = resolve;
  });
  listener.ready = () => {
    Assert.ok(false, "The session must not be established");
  };

  Assert.throws(
    () =>
      webTransport.asyncConnect(
        NetUtil.newURI(`https://${host}/success`),
        true,
        [],
        Services.scriptSecurityManager.getSystemPrincipal(),
        Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
        listener,
        Ci.nsIWebTransport.h2
      ),
    /NS_ERROR_NOT_AVAILABLE/
  );
  await closed;
});
