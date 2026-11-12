/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Check if the JIT frames inside marker stacks contain source information.
add_task(async () => {
  const entries = 10000;
  const interval = 1;
  const threads = ["GeckoMain"];
  // We need the jssources feature for sourceId to be included in the profile.
  const features = ["js", "nostacksampling", "jssources"];

  await Services.profiler.StartProfiler(entries, interval, features, threads);

  const markerName = "TestMarkerWithJITFrames";

  function testFunction(last) {
    if (last) {
      ChromeUtils.addProfilerMarker(markerName, { captureStack: true });
    }
    return Math.sqrt(Math.random() * 100);
  }

  // Make sure that it gets JIT compiled.
  const iterCount = 1000;
  for (let i = 0; i < iterCount; i++) {
    testFunction(i === iterCount - 1);
  }

  const profile = await ProfilerTestUtils.stopNowAndGetProfile();
  const mainThread = profile.threads.find(({ name }) => name === "GeckoMain");

  const markers = ProfilerTestUtils.getInflatedMarkerData(mainThread).filter(
    m => m.name === markerName
  );

  Assert.equal(
    markers.length,
    1,
    "Should find exactly one marker with the test name"
  );

  const marker = markers[0];
  Assert.ok(marker.data, "Marker should have data");
  Assert.ok(marker.data.stack, "Marker should have a stack");

  const stack = marker.data.stack;
  Assert.ok(stack.samples, "Marker stack should have samples");
  Assert.ok(stack.samples.data, "Marker stack samples should have data");

  const stackIndex = stack.samples.data[0][stack.samples.schema.stack];
  Assert.notEqual(stackIndex, null, "Should have a valid stack index");

  const stackPrefixCol = mainThread.stackTable.schema.prefix;
  const stackFrameCol = mainThread.stackTable.schema.frame;
  const frameLocationCol = mainThread.frameTable.schema.location;
  const sourceIdCol = profile.sources.schema.id;
  const sourceMapURLCol = profile.sources.schema.sourceMapURL;

  // Verify the sources schema has the expected fields
  Assert.ok("id" in profile.sources.schema, "Sources schema has 'id' field");
  Assert.ok(
    "filename" in profile.sources.schema,
    "Sources schema has 'filename' field"
  );
  Assert.ok(
    "sourceMapURL" in profile.sources.schema,
    "Sources schema has 'sourceMapURL' field"
  );

  let foundJITFrame = false;
  let currentStackIndex = stackIndex;

  while (currentStackIndex !== null) {
    const stack = mainThread.stackTable.data[currentStackIndex];
    const frameIndex = stack[stackFrameCol];
    const frame = mainThread.frameTable.data[frameIndex];
    const location = mainThread.stringTable[frame[frameLocationCol]];

    if (location.includes("testFunction")) {
      foundJITFrame = true;

      // sourceIndex is included in the location string, inside brackets.
      const sourceIndexMatch = location.match(/\[(\d+)\]/);
      Assert.ok(sourceIndexMatch, "JIT frame should have a sourceIndex");

      const sourceIndex = Number(sourceIndexMatch[1]);
      const source = profile.sources.data[sourceIndex];
      Assert.ok(
        source,
        "Source index should correspond to a valid source entry"
      );

      const sourceId = source[sourceIdCol];
      Assert.ok(sourceId, "JIT frame sourceId should be non-empty");
      const sourceMapURL = source[sourceMapURLCol];
      Assert.ok(
        sourceMapURL === null ||
          sourceMapURL === undefined ||
          typeof sourceMapURL === "string",
        "Source entry has a valid nullable sourceMapURL"
      );
      info(`Found JIT frame with sourceId: ${sourceId}, location: ${location}`);
      break;
    }

    currentStackIndex = stack[stackPrefixCol];
  }

  Assert.ok(
    foundJITFrame,
    "Should find at least one JIT frame with 'testFunction' in the marker stack"
  );
});
