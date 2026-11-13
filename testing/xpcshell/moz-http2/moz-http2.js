/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This module is the stateful server side of test_http2.js and is meant
// to have node be restarted in between each invocation

var node_http2_root = "../node-http2";
if (process.env.NODE_HTTP2_ROOT) {
  node_http2_root = process.env.NODE_HTTP2_ROOT;
}
var http2 = require(node_http2_root);
var fs = require("fs");
var url = require("url");
var crypto = require("crypto");
const ip = require(`${node_http2_root}/../node_ip`);
const { fork } = require("child_process");
const { spawn } = require("child_process");
const path = require("path");

// Hook into the decompression code to log the decompressed name-value pairs
var compression_module = node_http2_root + "/lib/protocol/compressor";
var http2_compression = require(compression_module);
var HeaderSetDecompressor = http2_compression.HeaderSetDecompressor;
var originalRead = HeaderSetDecompressor.prototype.read;
var lastDecompressor;
var decompressedPairs;
HeaderSetDecompressor.prototype.read = function () {
  if (this != lastDecompressor) {
    lastDecompressor = this;
    decompressedPairs = [];
  }
  var pair = originalRead.apply(this, arguments);
  if (pair) {
    decompressedPairs.push(pair);
  }
  return pair;
};

var connection_module = node_http2_root + "/lib/protocol/connection";
var http2_connection = require(connection_module);
var Connection = http2_connection.Connection;
var originalClose = Connection.prototype.close;
Connection.prototype.close = function (error, lastId) {
  if (lastId !== undefined) {
    this._lastIncomingStream = lastId;
  }

  originalClose.apply(this, arguments);
};

var framer_module = node_http2_root + "/lib/protocol/framer";
var http2_framer = require(framer_module);
var Serializer = http2_framer.Serializer;
var originalTransform = Serializer.prototype._transform;
var newTransform = function (frame) {
  if (frame.type == "DATA") {
    // Insert our empty DATA frame
    const emptyFrame = {};
    emptyFrame.type = "DATA";
    emptyFrame.data = Buffer.alloc(0);
    emptyFrame.flags = [];
    emptyFrame.stream = frame.stream;
    var buffers = [];
    Serializer.DATA(emptyFrame, buffers);
    Serializer.commonHeader(emptyFrame, buffers);
    for (var i = 0; i < buffers.length; i++) {
      this.push(buffers[i]);
    }

    // Reset to the original version for later uses
    Serializer.prototype._transform = originalTransform;
  }
  originalTransform.apply(this, arguments);
};

// Injects a raw CONTINUATION frame with stream ID 0 before the first HEADERS
// frame. Since there is no pending HEADERS or PUSH_PROMISE, mExpectedHeaderID
// and mExpectedPushPromiseID are both 0, so the pre-dispatch checks are
// skipped and RecvContinuation is called with mInputFrameID = 0.
var newTransformContinuationStreamZero = function (frame) {
  if (frame.type == "HEADERS") {
    const contFrame = Buffer.alloc(9);
    contFrame[0] = 0x00; // length high
    contFrame[1] = 0x00; // length mid
    contFrame[2] = 0x00; // length low (no payload)
    contFrame[3] = 0x09; // type = CONTINUATION
    contFrame[4] = 0x04; // flags = END_HEADERS
    // stream ID bytes 5-8 remain 0x00 — stream ID = 0 (protocol error)
    this.push(contFrame);

    Serializer.prototype._transform = originalTransform;
  }
  originalTransform.apply(this, arguments);
};

function getHttpContent(pathName) {
  var content =
    "<!doctype html>" +
    "<html>" +
    "<head><title>HOORAY!</title></head>" +
    // 'You Win!' used in tests to check we reached this server
    "<body>You Win! (by requesting" +
    pathName +
    ")</body>" +
    "</html>";
  return content;
}

function generateContent(size) {
  var content = "";
  for (var i = 0; i < size; i++) {
    content += "0";
  }
  return content;
}

/* This takes care of responding to the multiplexed request for us */
var m = {
  mp1res: null,
  mp2res: null,
  buf: null,
  mp1start: 0,
  mp2start: 0,

  checkReady() {
    if (this.mp1res != null && this.mp2res != null) {
      this.buf = generateContent(30 * 1024);
      this.mp1start = 0;
      this.mp2start = 0;
      this.send(this.mp1res, 0);
      setTimeout(this.send.bind(this, this.mp2res, 0), 5);
    }
  },

  send(res, start) {
    var end = Math.min(start + 1024, this.buf.length);
    var content = this.buf.substring(start, end);
    res.write(content);
    if (end < this.buf.length) {
      setTimeout(this.send.bind(this, res, end), 10);
    } else {
      // Clear these variables so we can run the test again with --verify
      if (res == this.mp1res) {
        this.mp1res = null;
      } else {
        this.mp2res = null;
      }
      res.end();
    }
  },
};

var runlater = function () {};
runlater.prototype = {
  req: null,
  resp: null,
  fin: true,

  onTimeout: function onTimeout() {
    this.resp.writeHead(200);
    if (this.fin) {
      this.resp.end("It's all good 750ms.");
    }
  },
};

var runConnectLater = function () {};
runConnectLater.prototype = {
  req: null,
  resp: null,
  connect: false,

  onTimeout: function onTimeout() {
    if (this.connect) {
      this.resp.writeHead(200);
      this.connect = true;
      setTimeout(executeRunLaterCatchError, 50, this);
    } else {
      this.resp.end("HTTP/1.1 200\n\r\n\r");
    }
  },
};

var moreData = function () {};
moreData.prototype = {
  req: null,
  resp: null,
  iter: 3,

  onTimeout: function onTimeout() {
    // 1mb of data
    const content = generateContent(1024 * 1024);
    this.resp.write(content); // 1mb chunk
    this.iter--;
    if (!this.iter) {
      this.resp.end();
    } else {
      setTimeout(executeRunLater, 1, this);
    }
  },
};

function executeRunLater(arg) {
  arg.onTimeout();
}

function executeRunLaterCatchError(arg) {
  arg.onTimeout();
}

var h11required_conn = null;
var h11required_header = "yes";
var didRst = false;
var rstConnection = null;
var didUnknownRst = false;
var illegalheader_conn = null;

// eslint-disable-next-line complexity
function handleRequestImpl(req, res) {
  var u = "";
  if (req.url != undefined) {
    u = url.parse(req.url, true);
  }
  var content = getHttpContent(u.pathname);
  var push;

  if (req.httpVersionMajor === 2) {
    res.setHeader("X-Connection-Http2", "yes");
    res.setHeader("X-Http2-StreamId", "" + req.stream.id);
  } else {
    res.setHeader("X-Connection-Http2", "no");
  }

  if (u.pathname === "/exit") {
    res.setHeader("Content-Type", "text/plain");
    res.setHeader("Connection", "close");
    res.writeHead(200);
    res.end("ok");
    process.exit();
  }

  if (u.pathname === "/exception-test") {
    // Intentionally throws to test that handleRequest isolates exceptions.
    // Without the try/catch wrapper this would crash the server process.
    throw new Error("Intentional test exception from /exception-test");
  }

  if (req.method == "CONNECT") {
    if (req.headers.host == "illegalhpacksoft.example.com:80") {
      illegalheader_conn = req.stream.connection;
      res.setHeader("Content-Type", "text/html");
      res.setHeader("x-softillegalhpack", "true");
      res.writeHead(200);
      res.end(content);
      return;
    } else if (req.headers.host == "illegalhpackhard.example.com:80") {
      res.setHeader("Content-Type", "text/html");
      res.setHeader("x-hardillegalhpack", "true");
      res.writeHead(200);
      res.end(content);
      return;
    } else if (req.headers.host == "750.example.com:80") {
      // This response will mock a response through a proxy to a HTTP server.
      // After 750ms , a 200 response for the proxy will be sent then
      // after additional 50ms a 200 response for the HTTP GET request.
      let rl = new runConnectLater();
      rl.req = req;
      rl.resp = res;
      setTimeout(executeRunLaterCatchError, 750, rl);
      return;
    } else if (req.headers.host == "h11required.com:80") {
      if (req.httpVersionMajor === 2) {
        res.stream.reset("HTTP_1_1_REQUIRED");
      }
      return;
    }
  } else if (u.pathname === "/750ms") {
    let rl = new runlater();
    rl.req = req;
    rl.resp = res;
    setTimeout(executeRunLater, 750, rl);
    return;
  } else if (u.pathname === "/750msNoData") {
    let rl = new runlater();
    rl.req = req;
    rl.resp = res;
    rl.fin = false;
    setTimeout(executeRunLater, 750, rl);
    return;
  } else if (u.pathname === "/multiplex1" && req.httpVersionMajor === 2) {
    res.setHeader("Content-Type", "text/plain");
    res.writeHead(200);
    m.mp1res = res;
    m.checkReady();
    return;
  } else if (u.pathname === "/multiplex2" && req.httpVersionMajor === 2) {
    res.setHeader("Content-Type", "text/plain");
    res.writeHead(200);
    m.mp2res = res;
    m.checkReady();
    return;
  } else if (u.pathname === "/header") {
    var val = req.headers["x-test-header"];
    if (val) {
      res.setHeader("X-Received-Test-Header", val);
    }
  } else if (u.pathname === "/doubleheader") {
    res.setHeader("Content-Type", "text/html");
    res.writeHead(200);
    res.write(content);
    res.writeHead(200);
    res.end();
    return;
  } else if (u.pathname === "/cookie_crumbling") {
    res.setHeader("X-Received-Header-Pairs", JSON.stringify(decompressedPairs));
  } else if (u.pathname === "/big") {
    content = generateContent(128 * 1024);
    var hash = crypto.createHash("md5");
    hash.update(content);
    let md5 = hash.digest("hex");
    res.setHeader("X-Expected-MD5", md5);
  } else if (u.pathname === "/huge") {
    content = generateContent(1024);
    res.setHeader("Content-Type", "text/plain");
    res.writeHead(200);
    // 1mb of data
    for (let i = 0; i < 1024 * 1; i++) {
      res.write(content); // 1kb chunk
    }
    res.end();
    return;
  } else if (u.pathname === "/post" || u.pathname === "/patch") {
    if (req.method != "POST" && req.method != "PATCH") {
      res.writeHead(405);
      res.end("Unexpected method: " + req.method);
      return;
    }

    var post_hash = crypto.createHash("md5");
    var received_data = false;
    req.on("data", function receivePostData(chunk) {
      received_data = true;
      post_hash.update(chunk.toString());
    });
    req.on("end", function finishPost() {
      let md5 = received_data ? post_hash.digest("hex") : "0";
      res.setHeader("X-Calculated-MD5", md5);
      res.writeHead(200);
      res.end(content);
    });

    return;
  } else if (u.pathname === "/750msPost") {
    if (req.method != "POST") {
      res.writeHead(405);
      res.end("Unexpected method: " + req.method);
      return;
    }

    var accum = 0;
    req.on("data", function receivePostData(chunk) {
      accum += chunk.length;
    });
    req.on("end", function finishPost() {
      res.setHeader("X-Recvd", accum);
      let rl = new runlater();
      rl.req = req;
      rl.resp = res;
      setTimeout(executeRunLater, 750, rl);
    });

    return;
  } else if (u.pathname === "/h11required_stream") {
    if (req.httpVersionMajor === 2) {
      h11required_conn = req.stream.connection;
      res.stream.reset("HTTP_1_1_REQUIRED");
      return;
    }
  } else if (u.pathname === "/bigdownload") {
    res.setHeader("Content-Type", "text/html");
    res.writeHead(200);

    let rl = new moreData();
    rl.req = req;
    rl.resp = res;
    setTimeout(executeRunLater, 1, rl);
    return;
  } else if (u.pathname === "/h11required_session") {
    if (req.httpVersionMajor === 2) {
      if (h11required_conn !== req.stream.connection) {
        h11required_header = "no";
      }
      res.stream.connection.close("HTTP_1_1_REQUIRED", res.stream.id - 2);
      return;
    }
    res.setHeader("X-H11Required-Stream-Ok", h11required_header);
  } else if (u.pathname === "/rstonce") {
    if (!didRst && req.httpVersionMajor === 2) {
      didRst = true;
      rstConnection = req.stream.connection;
      req.stream.reset("REFUSED_STREAM");
      return;
    }

    if (rstConnection === null || rstConnection !== req.stream.connection) {
      if (req.httpVersionMajor != 2) {
        res.setHeader("Connection", "close");
      }
      res.writeHead(400);
      res.end("WRONG CONNECTION, HOMIE!");
      return;
    }

    // Clear these variables so we can run the test again with --verify
    didRst = false;
    rstConnection = null;

    if (req.httpVersionMajor != 2) {
      res.setHeader("Connection", "close");
    }
    res.writeHead(200);
    res.end("It's all good.");
    return;
  } else if (u.pathname === "/unknown_rst_once") {
    // First H2 request gets RST_STREAM with an unrecognized error code; the
    // retry succeeds.
    if (!didUnknownRst && req.httpVersionMajor === 2) {
      didUnknownRst = true;
      req.stream.reset(0xfe);
      return;
    }
    // Clear so the test can be re-run with --verify
    didUnknownRst = false;
    res.setHeader("Content-Type", "text/html");
    res.writeHead(200);
    res.end("It's all good.");
    return;
  } else if (u.pathname === "/continuedheaders") {
    var pushRequestHeaders = { "x-pushed-request": "true" };
    var pushResponseHeaders = {
      "content-type": "text/plain",
      "content-length": "2",
      "X-Connection-Http2": "yes",
    };
    var pushHdrTxt =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    var pullHdrTxt = pushHdrTxt.split("").reverse().join("");
    for (let i = 0; i < 265; i++) {
      pushRequestHeaders["X-Push-Test-Header-" + i] = pushHdrTxt;
      res.setHeader("X-Pull-Test-Header-" + i, pullHdrTxt);
    }
    push = res.push({
      hostname: "localhost:" + serverPort,
      port: serverPort,
      path: "/continuedheaders/push",
      method: "GET",
      headers: pushRequestHeaders,
    });
    push.writeHead(200, pushResponseHeaders);
    push.end("ok");
  } else if (u.pathname === "/hugecontinuedheaders") {
    for (let i = 0; i < u.query.size; i++) {
      res.setHeader(
        "X-Test-Header-" + i,
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789".repeat(1024)
      );
    }
    res.writeHead(200);
    res.end(content);
    return;
  } else if (u.pathname === "/altsvc1") {
    if (
      req.httpVersionMajor != 2 ||
      req.scheme != "http" ||
      req.headers["alt-used"] != "foo.example.com:" + serverPort
    ) {
      res.writeHead(400);
      res.end("WHAT?");
      return;
    }
    // test the alt svc frame for use with altsvc2
    res.altsvc(
      "foo.example.com",
      serverPort,
      "h2",
      3600,
      req.headers["x-redirect-origin"]
    );
  } else if (u.pathname === "/altsvc2") {
    if (
      req.httpVersionMajor != 2 ||
      req.scheme != "http" ||
      req.headers["alt-used"] != "foo.example.com:" + serverPort
    ) {
      res.writeHead(400);
      res.end("WHAT?");
      return;
    }
  }

  // for use with test_altsvc.js
  else if (u.pathname === "/altsvc-test") {
    res.setHeader("Cache-Control", "no-cache");
    res.setHeader("Alt-Svc", "h2=" + req.headers["x-altsvc"]);
  }
  // for use with test_http3.js
  else if (u.pathname === "/http3-test") {
    res.setHeader("Cache-Control", "no-cache");
    res.setHeader("Alt-Svc", "h3=" + req.headers["x-altsvc"]);
  }
  // for use with test_http3.js
  else if (u.pathname === "/http3-test2") {
    res.setHeader("Cache-Control", "no-cache");
    res.setHeader(
      "Alt-Svc",
      "h2=foo2.example.com:8000,h3=" + req.headers["x-altsvc"]
    );
  } else if (u.pathname === "/http3-test3") {
    res.setHeader("Cache-Control", "no-cache");
    res.setHeader(
      "Alt-Svc",
      "h3-29=" + req.headers["x-altsvc"] + ",h3=" + req.headers["x-altsvc"]
    );
  } else if (u.pathname === "/websocket") {
    res.setHeader("Upgrade", "websocket");
    res.setHeader("Connection", "Upgrade");
    var wshash = crypto.createHash("sha1");
    wshash.update(req.headers["sec-websocket-key"]);
    wshash.update("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    let key = wshash.digest("base64");
    res.setHeader("Sec-WebSocket-Accept", key);
    res.writeHead(101);
    res.end("something....");
    return;
  } else if (u.pathname === "/.well-known/http-opportunistic") {
    res.setHeader("Cache-Control", "no-cache");
    res.setHeader("Content-Type", "application/json");
    res.writeHead(200, "OK");
    res.end('["http://' + req.headers.host + '"]');
    return;
  } else if (u.pathname === "/illegalhpacksoft") {
    // This will cause the compressor to compress a header that is not legal,
    // but only affects the stream, not the session.
    illegalheader_conn = req.stream.connection;
    res.setHeader("Content-Type", "text/html");
    res.setHeader("x-softillegalhpack", "true");
    res.writeHead(200);
    res.end(content);
    return;
  } else if (u.pathname === "/illegalhpackhard") {
    // This will cause the compressor to insert an HPACK instruction that will
    // cause a session failure.
    res.setHeader("Content-Type", "text/html");
    res.setHeader("x-hardillegalhpack", "true");
    res.writeHead(200);
    res.end(content);
    return;
  } else if (u.pathname === "/illegalhpack_validate") {
    if (req.stream.connection === illegalheader_conn) {
      res.setHeader("X-Did-Goaway", "no");
    } else {
      res.setHeader("X-Did-Goaway", "yes");
    }
    // Fall through to the default response behavior
  } else if (u.pathname === "/foldedheader") {
    res.setHeader("X-Folded-Header", "this is\n folded");
    // Fall through to the default response behavior
  } else if (u.pathname === "/emptydata") {
    // Overwrite the original transform with our version that will insert an
    // empty DATA frame at the beginning of the stream response, then fall
    // through to the default response behavior.
    Serializer.prototype._transform = newTransform;
  } else if (u.pathname === "/continuation_stream_zero") {
    Serializer.prototype._transform = newTransformContinuationStreamZero;
  }

  // for use with test_immutable.js
  else if (u.pathname === "/immutable-test-without-attribute") {
    res.setHeader("Cache-Control", "max-age=100000");
    res.setHeader("Etag", "1");
    if (req.headers["if-none-match"]) {
      res.setHeader("x-conditional", "true");
    }
    // default response from here
  } else if (u.pathname === "/immutable-test-with-attribute") {
    res.setHeader("Cache-Control", "max-age=100000, immutable");
    res.setHeader("Etag", "2");
    if (req.headers["if-none-match"]) {
      res.setHeader("x-conditional", "true");
    }
    // default response from here
  } else if (u.pathname === "/immutable-test-expired-with-Expires-header") {
    res.setHeader("Cache-Control", "immutable");
    res.setHeader("Expires", "Mon, 01 Jan 1990 00:00:00 GMT");
    res.setHeader("Etag", "3");

    if (req.headers["if-none-match"]) {
      res.setHeader("x-conditional", "true");
    }
  } else if (
    u.pathname === "/immutable-test-expired-with-last-modified-header"
  ) {
    res.setHeader("Cache-Control", "public, max-age=3600, immutable");
    res.setHeader("Date", "Mon, 01 Jan 1990 00:00:00 GMT");
    res.setHeader("Last-modified", "Mon, 01 Jan 1990 00:00:00 GMT");
    res.setHeader("Etag", "4");

    if (req.headers["if-none-match"]) {
      res.setHeader("x-conditional", "true");
    }
  } else if (u.pathname === "/statusphrase") {
    // Fortunately, the node-http2 API is dumb enough to allow this right on
    // through, so we can easily test rejecting this on gecko's end.
    res.writeHead("200 OK");
    res.end(content);
    return;
  } else if (u.pathname === "/doublypushed") {
    content = "not pushed";
  } else if (u.pathname === "/diskcache") {
    content = "this was pulled via h2";
  }

  // For test_header_Server_Timing.js
  else if (u.pathname === "/server-timing") {
    res.setHeader("Content-Type", "text/plain");
    res.setHeader("Content-Length", "12");
    res.setHeader("Trailer", "Server-Timing");
    res.setHeader(
      "Server-Timing",
      "metric; dur=123.4; desc=description, metric2; dur=456.78; desc=description1"
    );
    res.write("data reached");
    res.addTrailers({
      "Server-Timing":
        "metric3; dur=789.11; desc=description2, metric4; dur=1112.13; desc=description3",
    });
    res.end();
    return;
  } else if (u.pathname === "/103_response") {
    let link_val = req.headers["link-to-set"];
    if (link_val) {
      res.setHeader("link", link_val);
    }
    res.setHeader("something", "something");
    res.writeHead(103);

    res.setHeader("Content-Type", "text/plain");
    res.setHeader("Content-Length", "12");
    res.writeHead(200);
    res.write("data reached");
    res.end();
    return;
  } else if (u.pathname.startsWith("/invalid_response_header/")) {
    // response headers with invalid characters in the name / value (RFC7540 Sec 10.3)
    let kind = u.pathname.slice("/invalid_response_header/".length);
    if (kind === "name_spaces") {
      res.setHeader("With Spaces", "Hello");
    } else if (kind === "value_line_feed") {
      res.setHeader("invalid-header", "line\nfeed");
    } else if (kind === "value_carriage_return") {
      res.setHeader("invalid-header", "carriage\rreturn");
    } else if (kind === "value_null") {
      res.setHeader("invalid-header", "null\0");
    }

    res.writeHead(200);
    res.end("");
    return;
  }

  res.setHeader("Content-Type", "text/html");
  if (req.httpVersionMajor != 2) {
    res.setHeader("Connection", "close");
  }
  res.writeHead(200);
  res.end(content);
}

/**
 * Wrapper that isolates per-request exceptions from the server process.
 *
 * Without this wrapper, a throw inside handleRequestImpl propagates as an
 * uncaught exception and kills the entire Node.js process, causing every
 * subsequent test that depends on the shared moz-http2 server to fail.
 *
 * With this wrapper, the exception is caught, a 500 response is written,
 * and the server continues running for the next test.
 */
function handleRequest(req, res) {
  try {
    handleRequestImpl(req, res);
  } catch (e) {
    console.error("moz-http2: Unhandled exception in request handler:", e);
    try {
      // Attempt to send a 500 so the client gets a response rather than a
      // connection reset, which makes test failures easier to diagnose.
      res.writeHead(500, "Internal Server Error", {
        "content-type": "text/plain",
      });
      res.end("moz-http2 handler exception: " + e.message);
    } catch (_) {
      // The response object may already be in an unusable state; ignore.
    }
  }
}

/**
 * Last-resort safety net for exceptions thrown by asynchronous callbacks
 * (e.g., a setTimeout inside a handler) that cannot be caught by the
 * synchronous try/catch in handleRequest above.
 */
process.on("uncaughtException", function (err) {
  console.error("moz-http2: Uncaught exception (server continuing):", err);
});

// Set up the SSL certs for our server - this server has a cert for foo.example.com
// signed by netwerk/tests/unit/http2-ca.pem
var options = {
  key: fs.readFileSync(__dirname + "/http2-cert.key"),
  cert: fs.readFileSync(__dirname + "/http2-cert.pem"),
};

if (process.env.HTTP2_LOG !== undefined) {
  var log_module = node_http2_root + "/test/util";
  options.log = require(log_module).createLogger("server");
}

var server = http2.createServer(options, handleRequest);

server.on("connection", function (socket) {
  socket.on("error", function () {
    // Ignoring SSL socket errors, since they usually represent a connection that was tore down
    // by the browser because of an untrusted certificate. And this happens at least once, when
    // the first test case if done.
  });
});

server.on("connect", function (req, clientSocket) {
  clientSocket.write(
    "HTTP/1.1 404 Not Found\r\nProxy-agent: Node.js-Proxy\r\n\r\n"
  );
  clientSocket.destroy();
});

function makeid(length) {
  var result = "";
  var characters =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  var charactersLength = characters.length;
  for (var i = 0; i < length; i++) {
    result += characters.charAt(Math.floor(Math.random() * charactersLength));
  }
  return result;
}

let globalObjects = {};
var serverPort;

const listen = (serv, envport) => {
  if (!serv) {
    return Promise.resolve(0);
  }

  let portSelection = 0;
  if (envport !== undefined) {
    try {
      portSelection = parseInt(envport, 10);
    } catch (e) {
      portSelection = -1;
    }
  }
  return new Promise(resolve => {
    serv.listen(portSelection, "127.0.0.1", 2000, () => {
      resolve(serv.address().port);
    });
  });
};

const http = require("http");
let httpServer = http.createServer((req, res) => {
  if (req.method != "POST") {
    let u = url.parse(req.url, true);
    if (u.pathname == "/test") {
      // This path is used to test that the server is working properly
      res.writeHead(200);
      res.end("OK");
      return;
    }
    res.writeHead(405);
    res.end("Unexpected method: " + req.method);
    return;
  }

  let code = "";
  req.on("data", function receivePostData(chunk) {
    code += chunk;
  });
  req.on("end", function finishPost() {
    let u = url.parse(req.url, true);
    if (u.pathname == "/fork") {
      let id = forkProcess();
      computeAndSendBackResponse(id);
      return;
    }

    if (u.pathname == "/forkH3Server") {
      forkH3Server(u.query.path, u.query.dbPath)
        .then(result => {
          computeAndSendBackResponse(result);
        })
        .catch(error => {
          computeAndSendBackResponse(error);
        });
      return;
    }

    if (u.pathname.startsWith("/kill/")) {
      let id = u.pathname.slice(6);
      let forked = globalObjects[id];
      if (!forked) {
        computeAndSendBackResponse(undefined, new Error("could not find id"));
        return;
      }

      new Promise((resolve, reject) => {
        forked.resolve = resolve;
        forked.reject = reject;
        forked.kill();
      })
        .then(x =>
          computeAndSendBackResponse(
            undefined,
            new Error(`incorrectly resolved ${x}`)
          )
        )
        .catch(e => {
          // We indicate a proper shutdown by resolving with undefined.
          if (e && e.toString().match(/child process exit closing code/)) {
            e = undefined;
          }
          computeAndSendBackResponse(undefined, e);
        });
      return;
    }

    if (u.pathname.startsWith("/execute/")) {
      let id = u.pathname.slice(9);
      let forked = globalObjects[id];
      if (!forked) {
        computeAndSendBackResponse(undefined, new Error("could not find id"));
        return;
      }

      let messageId = makeid(6);
      new Promise((resolve, reject) => {
        forked.messageHandlers[messageId] = { resolve, reject };
        forked.send({ code, messageId });
      })
        .then(x => sendBackResponse(x))
        .catch(e => computeAndSendBackResponse(undefined, e));
    }

    function computeAndSendBackResponse(evalResult, e) {
      let output = { result: evalResult, error: "", errorStack: "" };
      if (e) {
        output.error = e.toString();
        output.errorStack = e.stack;
      }
      sendBackResponse(output);
    }

    function sendBackResponse(output) {
      output = JSON.stringify(output);

      res.setHeader("Content-Length", output.length);
      res.setHeader("Content-Type", "application/json");
      res.writeHead(200);
      res.write(output);
      res.end("");
    }
  });
});

function forkH3Server(serverPath, dbPath) {
  const args = [dbPath];
  const env = Object.assign({}, process.env, {
    RUST_BACKTRACE: "full",
  });
  let child = spawn(serverPath, args, { env });
  let id = forkProcessInternal(child);
  // Return a promise that resolves when we receive data from stdout
  return new Promise((resolve, _) => {
    child.stdout.on("data", data => {
      console.log(data.toString());
      resolve({ id, output: data.toString().trim() });
    });

    child.stderr.on("data", chunk => {
      const s = chunk.toString();
      console.log(`[child stderr] ${s}`);
    });
  });
}

function forkProcess() {
  let scriptPath = path.resolve(__dirname, "moz-http2-child.js");
  let forked = fork(scriptPath);
  return forkProcessInternal(forked);
}

function forkProcessInternal(forked) {
  let id = makeid(6);
  forked.errors = "";
  forked.messageHandlers = {};
  globalObjects[id] = forked;
  forked.on("message", msg => {
    if (msg.messageId && forked.messageHandlers[msg.messageId]) {
      let handler = forked.messageHandlers[msg.messageId];
      delete forked.messageHandlers[msg.messageId];
      handler.resolve(msg);
    } else {
      console.log(
        `forked process without handler sent: ${JSON.stringify(msg)}`
      );
      forked.errors += `forked process without handler sent: ${JSON.stringify(
        msg
      )}\n`;
    }
  });

  let exitFunction = (code, signal) => {
    if (globalObjects[id]) {
      delete globalObjects[id];
    } else {
      // already called
      return;
    }

    let errorMsg = `child process exit closing code: ${code} signal: ${signal}`;
    if (forked.errors != "") {
      errorMsg = forked.errors;
      forked.errors = "";
    }

    // Handle /kill/ case where forked.reject is set
    if (forked.reject) {
      forked.reject(errorMsg);
      forked.reject = null;
      forked.resolve = null;
    }

    if (Object.keys(forked.messageHandlers).length === 0) {
      return;
    }

    for (let messageId in forked.messageHandlers) {
      forked.messageHandlers[messageId].reject(errorMsg);
    }
    forked.messageHandlers = {};
  };

  forked.on("error", exitFunction);
  forked.on("close", exitFunction);
  forked.on("exit", exitFunction);

  return id;
}

Promise.all([
  listen(server, process.env.MOZHTTP2_PORT).then(port => (serverPort = port)),
  listen(httpServer, process.env.MOZNODE_EXEC_PORT),
]).then(([sPort, nodeExecPort]) => {
  console.log(`HTTP2 server listening on ports ${sPort},${nodeExecPort}`);
});
