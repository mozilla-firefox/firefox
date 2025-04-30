var worker = new Worker("navigator_worker.js");

var is = window.parent.is;
var ok = window.parent.ok;
var SimpleTest = window.parent.SimpleTest;

worker.onmessage = function (event) {
  var args = JSON.parse(event.data);

  if (args.name == "testFinished") {
    SimpleTest.finish();
    return;
  }

  if (typeof navigator[args.name] == "undefined") {
    ok(false, "Navigator has no '" + args.name + "' property!");
    return;
  }

  if (args.name === "languages") {
    is(
      navigator.languages.toString(),
      args.value.toString(),
      "languages matches"
    );
    return;
  }

  const objectProperties = [
    "connection",
    "gpu",
    "locks",
    "mediaCapabilities",
    "permissions",
    "serviceWorker",
    "storage",
  ];

  if (objectProperties.includes(args.name)) {
    is(
      typeof navigator[args.name],
      typeof args.value,
      `${args.name} type matches`
    );
    return;
  }

  is(
    navigator[args.name],
    args.value,
    "Mismatched navigator string for " + args.name + "!"
  );
};

worker.onerror = function (event) {
  ok(false, "Worker had an error: " + event.message);
  SimpleTest.finish();
};

var { AppConstants } = SpecialPowers.ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
var isNightly = AppConstants.NIGHTLY_BUILD;
var isEarlyBetaOrEarlier = AppConstants.EARLY_BETA_OR_EARLIER;
var isRelease = AppConstants.RELEASE_OR_BETA;
var isAndroid = AppConstants.platform == "android";

worker.postMessage({ isNightly, isEarlyBetaOrEarlier, isRelease, isAndroid });
