function entryDisabled(
  entry,
  {
    isNightly,
    isRelease,
    isDesktop,
    isWindows,
    isMac,
    isLinux,
    isAndroid,
    isAarch64,
    isInsecureContext,
    isFennec,
    isCrossOriginIsolated,
  }
) {
  return (
    entry.nightly === !isNightly ||
    (entry.nightlyAndroid === !(isAndroid && isNightly) && isAndroid) ||
    entry.desktop === !isDesktop ||
    entry.windows === !isWindows ||
    entry.mac === !isMac ||
    entry.linux === !isLinux ||
    (entry.android === !isAndroid && !entry.nightlyAndroid) ||
    entry.aarch64 === !isAarch64 ||
    entry.fennecOrDesktop === (isAndroid && !isFennec) ||
    entry.fennec === !isFennec ||
    entry.release === !isRelease ||
    // The insecureContext test is very purposefully converting
    // entry.insecureContext to boolean, so undefined will convert to
    // false.  That way entries without an insecureContext annotation
    // will get treated as "insecureContext: false", which means exposed
    // only in secure contexts.
    (isInsecureContext && !entry.insecureContext) ||
    entry.crossOriginIsolated === !isCrossOriginIsolated ||
    entry.disabled
  );
}

function createInterfaceMap(data, interfaceGroups) {
  var interfaceMap = {};

  /** @param {any[]} interfaceGroup */
  function checkSorted(interfaceGroup) {
    /** @type {(entry) => string} */
    let getName = entry => (typeof entry === "string" ? entry : entry.name);

    // slice(1) to start from index 1 (index 0 has nothing to compare with)
    for (let [index, entry] of interfaceGroup.slice(1).entries()) {
      let x = getName(interfaceGroup[index]);
      let y = getName(entry);
      ok(
        x <= y,
        `The interface group is not sorted! ${y} must come before ${x}!`
      );
    }
  }

  function addInterfaces(interfaces) {
    for (var entry of interfaces) {
      if (typeof entry === "string") {
        interfaceMap[entry] = !data.isInsecureContext;
      } else {
        ok(!("pref" in entry), "Bogus pref annotation for " + entry.name);
        interfaceMap[entry.name] ||= !entryDisabled(entry, data);
      }
    }
  }

  for (let interfaceGroup of interfaceGroups) {
    checkSorted(interfaceGroup);
    addInterfaces(interfaceGroup);
  }

  return interfaceMap;
}

function runTest(
  parentName,
  parent,
  { data, interfaceGroups, testFunctions = [] }
) {
  var interfaceMap = createInterfaceMap(data, interfaceGroups);
  for (var name of Object.getOwnPropertyNames(parent)) {
    // Ignore functions on the global that are part of the test (harness).
    if (parent === self && testFunctions.includes(name)) {
      continue;
    }
    ok(
      interfaceMap[name],
      "If this is failing: DANGER, are you sure you want to expose the new interface " +
        name +
        " to all webpages as a property on '" +
        parentName +
        "'? Do not make a change to this file without a " +
        " review from a DOM peer, via the #webidl reviewer group, for that specific change!!! " +
        "(or a JS peer, via the #spidermonkey-reviewers reviewer group, for changes to ecmaGlobals)"
    );

    ok(
      name in parent,
      `${name} is exposed as an own property on '${parentName}' but tests false for "in" in the global scope`
    );
    ok(
      Object.getOwnPropertyDescriptor(parent, name),
      `${name} is exposed as an own property on '${parentName}' but has no property descriptor in the global scope`
    );

    delete interfaceMap[name];
  }
  for (var name of Object.keys(interfaceMap)) {
    const not = interfaceMap[name] ? "" : " NOT";
    ok(
      name in parent === interfaceMap[name],
      `${name} should${not} be defined on ${parentName}`
    );
    if (!interfaceMap[name]) {
      delete interfaceMap[name];
    }
  }
  is(
    Object.keys(interfaceMap).length,
    0,
    "The following interface(s) are not enumerated: " +
      Object.keys(interfaceMap).join(", ")
  );
}

if (typeof window !== "undefined") {
  window.getHelperData = () => {
    const { AppConstants } = SpecialPowers.ChromeUtils.importESModule(
      "resource://gre/modules/AppConstants.sys.mjs"
    );
    const sysinfo = SpecialPowers.Services.sysinfo;
    const appinfo = SpecialPowers.Services.appinfo;

    return {
      isNightly: AppConstants.NIGHTLY_BUILD,
      isRelease: AppConstants.RELEASE_OR_BETA,
      isDesktop: !/Mobile|Tablet/.test(navigator.userAgent),
      isMac: AppConstants.platform == "macosx",
      isWindows: AppConstants.platform == "win",
      isAndroid: AppConstants.platform == "android",
      isLinux: AppConstants.platform == "linux",
      isAarch64: sysinfo.get("arch") === "aarch64",
      isInsecureContext: !window.isSecureContext,
      // Currently, MOZ_APP_NAME is always "fennec" for all mobile builds, so we can't use AppConstants for this
      isFennec:
        AppConstants.platform == "android" &&
        SpecialPowers.Cc["@mozilla.org/android/bridge;1"].getService(
          SpecialPowers.Ci.nsIGeckoViewBridge
        ).isFennec,
      isCrossOriginIsolated: window.crossOriginIsolated,
    };
  };
}
