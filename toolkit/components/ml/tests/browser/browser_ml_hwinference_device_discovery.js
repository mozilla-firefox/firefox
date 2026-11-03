/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* global HWInference */

add_task(async function test_device_discovery() {
  const devices = await HWInference.listDevices();
  info(`devices: ${JSON.stringify(devices)}`);
  const cpu = devices.find(device => device.kind === "cpu");
  Assert.ok(cpu, "The CPU is always a device");
  Assert.ok(
    cpu.backends.includes("llama.cpp"),
    "llama.cpp serves the CPU device"
  );
  for (const device of devices) {
    Assert.notEqual(device.id, "", `${device.description} has an id`);
  }
});
