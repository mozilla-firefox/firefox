/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* global HWInference */

// CI sets this on workers that have an accelerator llama.cpp can use.
const EXPECT_ACCELERATOR =
  Services.env.get("MOZ_ML_EXPECT_ACCELERATOR") === "1";

requestLongerTimeout(2);

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
  if (EXPECT_ACCELERATOR) {
    Assert.ok(
      devices.some(device => device.kind !== "cpu"),
      "This machine lists an accelerator"
    );
  }
});

add_task(async function test_placement() {
  const devices = await HWInference.listDevices();
  const cpu = devices.find(device => device.kind === "cpu");

  const byKind = await createTinyStoriesGenerator({
    placement: { kinds: ["cpu"] },
  });
  Assert.equal(byKind.device, cpu.id, "A kind placement loads on the CPU");
  Assert.equal(byKind.deviceKind, "cpu", "The generator reports its kind");
  byKind.terminate();

  const byId = await createTinyStoriesGenerator({
    placement: { devices: ["no-such-device", cpu.id] },
  });
  Assert.equal(byId.device, cpu.id, "An unknown id falls through");
  byId.terminate();

  await Assert.rejects(
    createTinyStoriesGenerator({
      placement: { devices: [cpu.id], kinds: ["cpu"] },
    }),
    /devices or kinds, not both/,
    "A placement with both devices and kinds rejects"
  );

  await Assert.rejects(
    createTinyStoriesGenerator({ placement: { devices: ["no-such-device"] } }),
    /placement/,
    "A placement no device matches rejects"
  );
});

add_task(async function test_generation_on_every_device() {
  for (const device of await HWInference.listDevices()) {
    const generator = await createTinyStoriesGenerator({
      placement: { devices: [device.id] },
    });
    try {
      Assert.equal(
        generator.device,
        device.id,
        `${device.description} loads the model`
      );
      const result = await generator.generate({
        messages: TINYSTORIES_STORYTELLER_PROMPT,
        maxTokens: 8,
        samplers: TINYSTORIES_GREEDY_SAMPLERS,
      });
      Assert.greater(
        result.content.length,
        0,
        `${device.description} generates text`
      );
    } finally {
      generator.terminate();
    }
  }
});
