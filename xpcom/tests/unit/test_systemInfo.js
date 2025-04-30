/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

add_task(function properties() {
  const PROPERTIES = [
    "name",
    "arch",
    "version",
    "pagesize",
    "pageshift",
    "memmapalign",
    "memsize",
  ];
  let sysInfo = Services.sysinfo;

  PROPERTIES.forEach(function (aPropertyName) {
    print("Testing property: " + aPropertyName);
    let value = sysInfo.getProperty(aPropertyName);
    Assert.ok(!!value);
  });

  // This property must exist, but its value might be zero.
  print("Testing property: umask");
  Assert.equal(typeof sysInfo.getProperty("umask"), "number");
});

add_task(async function gleanSystemCpu() {
  do_get_profile();
  Services.fog.initializeFOG();
  let sysInfo = Services.sysinfo;

  const gleanToProcessInfoNames = {
    name: "name",
    vendor: "vendor",
    logicalCores: "count",
    physicalCores: "cores",
    bigCores: "pcount",
    mediumCores: "mcount",
    littleCores: "ecount",
    family: "family",
    model: "model",
    stepping: "stepping",
    l2Cache: "l2cacheKB",
    l3Cache: "l3cacheKB",
    speed: "speedMHz",
  };

  for (let metric in gleanToProcessInfoNames) {
    Assert.equal(
      Glean.systemCpu[metric].testGetValue(),
      null,
      metric + " should not be set yet"
    );
  }
  let processInfo = await sysInfo.processInfo;

  for (let name in gleanToProcessInfoNames) {
    let gleanValue = Glean.systemCpu[name].testGetValue();
    if (gleanValue) {
      Assert.equal(
        gleanValue,
        processInfo[gleanToProcessInfoNames[name]],
        `Glean.systemCpu.${name} should match Services.sysinfo.processInfo.${gleanToProcessInfoNames[name]}`
      );
    } else {
      Assert.ok(
        !processInfo[gleanToProcessInfoNames[name]],
        `Services.sysinfo.processInfo.${gleanToProcessInfoNames[name]} should be falsy when Glean.systemCpu.${name} is`
      );
    }
  }
});
