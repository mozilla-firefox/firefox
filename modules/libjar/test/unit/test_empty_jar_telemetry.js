/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

const { NetUtil } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);

const nsIBinaryInputStream = Components.Constructor(
  "@mozilla.org/binaryinputstream;1",
  "nsIBinaryInputStream",
  "setInputStream"
);

do_get_profile();
Services.fog.initializeFOG();

Services.prefs.setBoolPref("network.jar.record_failure_reason", true);

const fileBase = "test_empty_file.zip";
const file = do_get_file("data/" + fileBase);
const tmpDir = Services.dirsvc.get("TmpD", Ci.nsIFile);
var copy;

function setup() {
  copy = tmpDir.clone();
  copy.append("zzdxphd909dbc6r2bxtqss2m000017");
  copy.append("zzdxphd909dbc6r2bxtqss2m000017");
  copy.append(fileBase);
  file.copyTo(copy.parent, copy.leafName);
}

setup();

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref("network.jar.record_failure_reason");
  Services.fog.testResetFOG();
  try {
    copy.remove(false);
  } catch (e) {}
});

function Listener(callback) {
  this._callback = callback;
}
Listener.prototype = {
  gotStartRequest: false,
  available: -1,
  gotStopRequest: false,
  QueryInterface: ChromeUtils.generateQI(["nsIRequestObserver"]),
  onDataAvailable(request, stream, offset, count) {
    try {
      this.available = stream.available();
      Assert.equal(this.available, count);
      // Need to consume stream to avoid assertion
      new nsIBinaryInputStream(stream).readBytes(count);
    } catch (ex) {
      do_throw(ex);
    }
  },
  onStartRequest() {
    this.gotStartRequest = true;
  },
  onStopRequest(request, status) {
    this.gotStopRequest = true;
    Assert.equal(status, 0);
    if (this._callback) {
      this._callback.call(null, this);
    }
  },
};

function assertLoadOthersEvent(sync) {
  let events = Glean.zeroByteLoad.loadOthers.testGetValue();
  Assert.equal(events.length, 1);
  Assert.equal(events[0].category, "zero_byte_load");
  Assert.equal(events[0].name, "load_others");
  Assert.deepEqual(events[0].extra, {
    sync: `${sync}`,
    file_name: `${fileBase}!/test.txt`,
    status: "NS_OK",
    cancelled: "false",
    cancel_reason: "",
  });
}

function makeChan() {
  var uri = "jar:" + Services.io.newFileURI(copy).spec + "!/test.txt";
  var chan = NetUtil.newChannel({ uri, loadUsingSystemPrincipal: true });
  return chan;
}

add_task(async function test_empty_jar_file_async() {
  var chan = makeChan();

  Services.fog.testResetFOG();

  await new Promise(resolve => {
    chan.asyncOpen(
      new Listener(function () {
        Assert.equal(chan.contentLength, 0);
        resolve();
      })
    );
  });

  assertLoadOthersEvent(false);
});

add_task(async function test_empty_jar_file_sync() {
  var chan = makeChan();

  Services.fog.testResetFOG();

  await new Promise(resolve => {
    let stream = chan.open();
    Assert.equal(stream.available(), 0);
    resolve();
  });

  assertLoadOthersEvent(true);
});
