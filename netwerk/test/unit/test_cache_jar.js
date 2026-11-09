"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

ChromeUtils.defineLazyGetter(this, "URL", function () {
  return "http://localhost:" + httpserv.identity.primaryPort + "/cached";
});

var httpserv = null;
var handlers_called = 0;

function cached_handler(metadata, response) {
  response.setHeader("Content-Type", "text/plain", false);
  response.setHeader("Cache-Control", "max-age=10000", false);
  response.setStatusLine(metadata.httpVersion, 200, "OK");
  var body = "0123456789";
  response.bodyOutputStream.write(body, body.length);
  handlers_called++;
}

function makeChan(url, userContextId) {
  var chan = NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.loadInfo.originAttributes = { userContextId };
  return chan;
}

// [userContextId, expected_handlers_called]
var firstTests = [
  [0, 1],
  [1, 1],
];
var secondTests = [
  [0, 0],
  [1, 1],
  [1, 0],
];

async function run_all_tests() {
  for (let test of firstTests) {
    handlers_called = 0;
    await test_channel(...test);
  }

  // We can't easily cause webapp data to be cleared from the child process, so skip
  // the rest of these tests.
  let procType = Services.appinfo.processType;
  if (procType != Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT) {
    return;
  }

  await new Promise(resolve => {
    Services.clearData.deleteDataFromOriginAttributesPattern(
      { userContextId: 1 },
      resolve
    );
  });

  for (let test of secondTests) {
    handlers_called = 0;
    await test_channel(...test);
  }
}

function run_test() {
  do_get_profile();

  do_test_pending();

  httpserv = new HttpServer();
  httpserv.registerPathHandler("/cached", cached_handler);
  httpserv.start(-1);
  run_all_tests().then(() => {
    do_test_finished();
  });
}

function test_channel(userContextId, expected) {
  return new Promise(resolve => {
    var chan = makeChan(URL, userContextId);
    chan.asyncOpen(
      new ChannelListener(doneFirstLoad.bind(null, resolve), expected)
    );
  });
}

function doneFirstLoad(resolve, req, buffer, expected) {
  // Load it again, make sure it hits the cache
  var oa = req.loadInfo.originAttributes;
  var chan = makeChan(URL, oa.userContextId);
  chan.asyncOpen(
    new ChannelListener(doneSecondLoad.bind(null, resolve), expected)
  );
}

function doneSecondLoad(resolve, req, buffer, expected) {
  Assert.equal(handlers_called, expected);
  resolve();
}
