// Test the plaintext-or-binary sniffer

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
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

function makeChan(headerIdx, bodyIdx) {
  var chan = NetUtil.newChannel({
    uri:
      "http://localhost:" +
      httpserv.identity.primaryPort +
      "/" +
      headerIdx +
      "/" +
      bodyIdx,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);

  chan.loadFlags |= Ci.nsIChannel.LOAD_CALL_CONTENT_SNIFFERS;

  return chan;
}

function makeListener(headerIdx, bodyIdx) {
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

      request.cancel(Cr.NS_BINDING_ABORTED);
    },

    onDataAvailable: function test_ODA() {
      do_throw("Should not get any data!");
    },

    onStopRequest: function test_onStopR() {
      ++caseIdx;
      if (caseIdx == testCases.length) {
        do_test_pending();
        httpserv.stop(do_test_finished);
      } else {
        doTest(...testCases[caseIdx]);
      }

      do_test_finished();
    },
  };

  return listener;
}

function doTest(headerIdx, bodyIdx) {
  var chan = makeChan(headerIdx, bodyIdx);

  var listener = makeListener(headerIdx, bodyIdx);

  chan.asyncOpen(listener);

  do_test_pending();
}

function createResponse(headerIdx, bodyIdx, metadata, response) {
  response.setHeader(
    "Content-Type",
    contentTypeHeaderList[headerIdx][0],
    false
  );
  response.bodyOutputStream.write(
    bodyList[bodyIdx][0],
    bodyList[bodyIdx][0].length
  );
}

function makeHandler(headerIdx, bodyIdx) {
  var f = function handlerClosure(metadata, response) {
    return createResponse(headerIdx, bodyIdx, metadata, response);
  };
  return f;
}

// The expected result only depends on whether the header and the body each
// allow sniffing, so rather than testing every header with every body, test
// every body with one sniffable and one non-sniffable header, and every
// header with a few representative bodies.
var testCases = [];
var caseIdx = 0;
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

var httpserv;
function run_test() {
  // disable on Windows for now, because it seems to leak sockets and die.
  // Silly operating system!
  // This is a really nasty way to detect Windows.  I wish we could do better.
  if (mozinfo.os == "win") {
    //failing eslint no-empty test
  }

  httpserv = new HttpServer();

  buildTestCases();
  for (let [h, b] of testCases) {
    httpserv.registerPathHandler("/" + h + "/" + b, makeHandler(h, b));
  }

  httpserv.start(-1);

  doTest(...testCases[0]);
}
