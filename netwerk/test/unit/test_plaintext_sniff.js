// Test the plaintext-or-binary sniffer

"use strict";

const { NodeHTTPServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

// List of Content-Type headers to test.  For each header we have an array.
// The first element in the array is the Content-Type header string.  The
// second element in the array is a boolean indicating whether we allow
// sniffing for that type.
var contentTypeHeaderList = [
  ["text/plain", true],
  ["text/plain; charset=ISO-8859-1", true],
  ["text/plain; charset=iso-8859-1", true],
  ["text/plain; charset=UTF-8", true],
  ["text/plain; charset=unknown", false],
  ["text/plain; param", false],
  ["text/plain; charset=ISO-8859-1; param", false],
  ["text/plain; charset=iso-8859-1; param", false],
  ["text/plain; charset=UTF-8; param", false],
  ["text/plain; charset=utf-8", false],
  ["text/plain; charset=utf8", false],
  ["text/plain; charset=UTF8", false],
  ["text/plain; charset=iSo-8859-1", false],
];

// List of response bodies to test.  For each response we have an array. The
// first element in the array is the body string.  The second element in the
// array is a boolean indicating whether that string should sniff as binary.
var bodyList = [["Plaintext", false]];

// List of possible BOMs
var BOMList = [
  "\xFE\xFF", // UTF-16BE
  "\xFF\xFE", // UTF-16LE
  "\xEF\xBB\xBF", // UTF-8
  "\x00\x00\xFE\xFF", // UCS-4BE
  "\xFF\xFE\x00\x00", // UCS-4LE
];

// Build up bodyList.  The things we treat as binary are ASCII codes 0-8,
// 14-26, 28-31.  That is, the control char range, except for tab, newline,
// vertical tab, form feed, carriage return, and ESC (this last being used by
// Shift_JIS, apparently).
function isBinaryChar(ch) {
  return (
    (0 <= ch && ch <= 8) || (14 <= ch && ch <= 26) || (28 <= ch && ch <= 31)
  );
}

// Test chars on their own
var i;
for (i = 0; i <= 127; ++i) {
  bodyList.push([String.fromCharCode(i), isBinaryChar(i)]);
}

// Text chars sniff as text with or without a BOM, so only one of them is
// needed to cover the BOM cases.
var bomTestChars = [];
for (i = 0; i <= 127; ++i) {
  if (isBinaryChar(i)) {
    bomTestChars.push(i);
  }
}
bomTestChars.push("A".charCodeAt(0));

// Test that having a BOM prevents plaintext sniffing
var j;
for (i of bomTestChars) {
  for (j = 0; j < BOMList.length; ++j) {
    bodyList.push([BOMList[j] + String.fromCharCode(i, i), false]);
  }
}

// Test that having a BOM requires at least 4 chars to kick in
for (i of bomTestChars) {
  for (j = 0; j < BOMList.length; ++j) {
    bodyList.push([
      BOMList[j] + String.fromCharCode(i),
      BOMList[j].length == 2 && isBinaryChar(i),
    ]);
  }
}

function makeChan(server, headerIdx, bodyIdx) {
  var chan = NetUtil.newChannel({
    uri: `${server.origin()}/sniff?h=${headerIdx}&b=${bodyIdx}`,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);

  chan.loadFlags |= Ci.nsIChannel.LOAD_CALL_CONTENT_SNIFFERS;

  return chan;
}

function makeListener(headerIdx, bodyIdx, resolve) {
  var listener = {
    onStartRequest: function test_onStartR(request) {
      try {
        var chan = request.QueryInterface(Ci.nsIChannel);

        Assert.equal(chan.status, Cr.NS_OK);

        var type = chan.contentType;

        var expectedType =
          contentTypeHeaderList[headerIdx][1] && bodyList[bodyIdx][1]
            ? "application/x-vnd.mozilla.guess-from-ext"
            : "text/plain";
        if (expectedType != type) {
          do_throw(
            "Unexpected sniffed type '" +
              type +
              "'.  " +
              "Should be '" +
              expectedType +
              "'.  " +
              "Header is ['" +
              contentTypeHeaderList[headerIdx][0] +
              "', " +
              contentTypeHeaderList[headerIdx][1] +
              "].  " +
              "Body is ['" +
              bodyList[bodyIdx][0].toSource() +
              "', " +
              bodyList[bodyIdx][1] +
              "]."
          );
        }
        Assert.equal(expectedType, type);
      } catch (e) {
        do_throw("Unexpected exception: " + e);
      }
    },

    // Read the whole response rather than cancelling the request, so that the
    // connection can be reused for the next request.
    onDataAvailable: function test_ODA(request, stream, offset, count) {
      read_stream(stream, count);
    },

    onStopRequest: function test_onStopR() {
      resolve();
    },
  };

  return listener;
}

// The expected result only depends on whether the header and the body each
// allow sniffing, so rather than testing every header with every body, test
// every body with one sniffable and one non-sniffable header, and every
// header with a few representative bodies.
var testCases = [];
function buildTestCases() {
  const sniffableHeaderIdx = 0;
  const nonSniffableHeaderIdx = 4;
  for (let b = 0; b < bodyList.length; ++b) {
    testCases.push([sniffableHeaderIdx, b], [nonSniffableHeaderIdx, b]);
  }
  const representativeBodies = [
    "Plaintext",
    "\x00",
    "A",
    "\xFF\xFE\x00\x00",
    "\xFF\xFE\x00",
  ].map(body => {
    const idx = bodyList.findIndex(([b]) => b == body);
    Assert.notEqual(idx, -1, `${JSON.stringify(body)} should be in bodyList`);
    return idx;
  });
  for (let h = 0; h < contentTypeHeaderList.length; ++h) {
    if (h == sniffableHeaderIdx || h == nonSniffableHeaderIdx) {
      continue;
    }
    for (let b of representativeBodies) {
      testCases.push([h, b]);
    }
  }
}

// httpd.js closes the connection after every response, so each request used to
// leave a client port in TIME_WAIT, and running this test repeatedly ran out of
// ports on Windows. The node server keeps connections alive instead.
add_task(async function test_plaintext_sniff() {
  let server = new NodeHTTPServer();
  await server.start();
  registerCleanupFunction(async () => {
    await server.stop();
  });

  await server.execute(
    `global.sniffHeaders = ${JSON.stringify(
      contentTypeHeaderList.map(([header]) => header)
    )};
     global.sniffBodies = ${JSON.stringify(bodyList.map(([body]) => btoa(body)))};`
  );
  await server.registerPathHandler("/sniff", (req, resp) => {
    let params = new URL(req.url, "http://localhost").searchParams;
    let body = Buffer.from(global.sniffBodies[params.get("b")], "base64");
    resp.writeHead(200, {
      "Content-Type": global.sniffHeaders[params.get("h")],
      "Content-Length": body.length,
    });
    resp.end(body);
  });

  buildTestCases();
  for (let [h, b] of testCases) {
    await new Promise(resolve => {
      makeChan(server, h, b).asyncOpen(makeListener(h, b, resolve));
    });
  }
});
