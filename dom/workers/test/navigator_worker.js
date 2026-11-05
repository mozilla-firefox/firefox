/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

importScripts("../../tests/mochitest/general/interface_exposure_checker.js");

// IMPORTANT: Do not change the list below without review from a DOM peer!
var supportedProps = [
  { name: "appCodeName", insecureContext: true },
  { name: "appName", insecureContext: true },
  { name: "appVersion", insecureContext: true },
  { name: "globalPrivacyControl", insecureContext: true },
  { name: "gpu", nightly: true },
  { name: "gpu", windows: true },
  { name: "gpu", mac: true, aarch64: true },
  { name: "platform", insecureContext: true },
  { name: "product", insecureContext: true },
  { name: "userAgent", insecureContext: true },
  { name: "onLine", insecureContext: true },
  { name: "language", insecureContext: true },
  { name: "languages", insecureContext: true },
  "locks",
  { name: "mediaCapabilities", insecureContext: true },
  { name: "hardwareConcurrency", insecureContext: true },
  "storage",
  { name: "connection", insecureContext: true },
  { name: "permissions", insecureContext: true },
  "serviceWorker",
  { name: "serial", android: false },
];

self.onmessage = function (event) {
  if (!event || !event.data) {
    return;
  }

  startTest(event.data);
};

function startTest(channelData) {
  // Prepare the interface map showing if a property should exist in this build.
  // For example, if interfaceMap[foo] = true means navigator.foo should exist.
  var interfaceMap = {};

  for (var prop of supportedProps) {
    if (typeof prop === "string") {
      interfaceMap[prop] = !channelData.isInsecureContext;
      continue;
    }

    interfaceMap[prop.name] ||= !entryDisabled(prop, channelData);
  }

  for (var prop in navigator) {
    // Make sure the list is current!
    if (!interfaceMap[prop]) {
      throw "Navigator has the '" + prop + "' property that isn't in the list!";
    }
  }

  var obj;

  for (var prop in interfaceMap) {
    // Skip the property that is not supposed to exist in this build.
    if (!interfaceMap[prop]) {
      continue;
    }

    if (typeof navigator[prop] == "undefined") {
      throw "Navigator has no '" + prop + "' property!";
    }

    obj = { name: prop };
    obj.value = navigator[prop];

    postMessage(JSON.stringify(obj));
  }

  obj = {
    name: "testFinished",
  };

  postMessage(JSON.stringify(obj));
}
