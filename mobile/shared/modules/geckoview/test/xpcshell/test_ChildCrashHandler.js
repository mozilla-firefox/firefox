/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ChildCrashHandler: "resource://gre/modules/ChildCrashHandler.sys.mjs",
  EventDispatcher: "resource://gre/modules/Messaging.sys.mjs",
});

const { makeFakeAppDir } = ChromeUtils.importESModule(
  "resource://testing-common/AppData.sys.mjs"
);

add_setup(async function () {
  await makeFakeAppDir();
  // The test harness sets MOZ_CRASHREPORTER_NO_REPORT, which disables
  // reporting crashes to the user. This test needs them enabled.
  const noReport = Services.env.get("MOZ_CRASHREPORTER_NO_REPORT");
  Services.env.set("MOZ_CRASHREPORTER_NO_REPORT", "");

  registerCleanupFunction(function () {
    Services.env.set("MOZ_CRASHREPORTER_NO_REPORT", noReport);
  });
});

add_task(async function test_remoteType() {
  const childID = 123;
  const remoteType = "webIsolated=https://example.com";
  // Force-set a remote type for the process that we are going to "crash" next.
  lazy.ChildCrashHandler.childMap.set(childID, remoteType);

  // Mock a process crash being notified.
  const propertyBag = Cc["@mozilla.org/hash-property-bag;1"].createInstance(
    Ci.nsIWritablePropertyBag2
  );
  propertyBag.setPropertyAsBool("abnormal", true);
  propertyBag.setPropertyAsAString("dumpID", "a-dump-id");

  // Set up a listener to receive the crash report event emitted by the handler.
  let listener;
  const crashReportPromise = new Promise(resolve => {
    listener = {
      onEvent(aEvent, aData) {
        resolve([aEvent, aData]);
      },
    };
  });
  lazy.EventDispatcher.instance.registerListener(listener, [
    "GeckoView:ChildCrashReport",
  ]);

  // Simulate a crash.
  lazy.ChildCrashHandler.observe(propertyBag, "ipc:content-shutdown", childID);

  const [aEvent, aData] = await crashReportPromise;
  Assert.equal(
    "GeckoView:ChildCrashReport",
    aEvent,
    "expected a child crash report"
  );
  Assert.equal("webIsolated", aData?.remoteType, "expected remote type prefix");
});

add_task(async function test_extensions_process_crash() {
  const childID = 123;
  const remoteType = "extension";
  // Force-set a remote type for the process that we are going to "crash" next.
  lazy.ChildCrashHandler.childMap.set(childID, remoteType);

  // Mock a process crash being notified.
  const propertyBag = Cc["@mozilla.org/hash-property-bag;1"].createInstance(
    Ci.nsIWritablePropertyBag2
  );
  propertyBag.setPropertyAsBool("abnormal", true);
  propertyBag.setPropertyAsAString("dumpID", "a-dump-id");

  // Set up a listener to receive the crash report event emitted by the handler.
  let listener;
  const crashReportPromise = new Promise(resolve => {
    listener = {
      onEvent(aEvent, aData) {
        resolve([aEvent, aData]);
      },
    };
  });
  lazy.EventDispatcher.instance.registerListener(listener, [
    "GeckoView:ChildCrashReport",
  ]);

  // Simulate a crash.
  lazy.ChildCrashHandler.observe(propertyBag, "ipc:content-shutdown", childID);

  const [aEvent, aData] = await crashReportPromise;
  Assert.equal(
    "GeckoView:ChildCrashReport",
    aEvent,
    "expected a child crash report"
  );
  Assert.equal("extension", aData?.remoteType, "expected remote type");
  Assert.equal("BACKGROUND_CHILD", aData?.processType, "expected process type");
});
