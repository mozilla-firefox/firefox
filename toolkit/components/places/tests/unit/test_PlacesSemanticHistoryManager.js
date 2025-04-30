/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { PlacesSemanticHistoryManager } = ChromeUtils.importESModule(
  "resource://gre/modules/PlacesSemanticHistoryManager.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

function approxEqual(a, b, tolerance = 1e-6) {
  return Math.abs(a - b) < tolerance;
}

function createPlacesSemanticHistoryManager() {
  return new PlacesSemanticHistoryManager({
    embeddingSize: 4,
    rowLimit: 10,
    samplingAttrib: "frecency",
    changeThresholdCount: 3,
    distanceThreshold: 0.75,
    testFlag: true,
  });
}

add_task(async function test_tensorToBindable() {
  let manager = new createPlacesSemanticHistoryManager();
  let tensor = [0.3, 0.3, 0.3, 0.3];
  let bindable = manager.tensorToBindable(tensor);
  Assert.equal(
    Object.prototype.toString.call(bindable),
    "[object Uint8ClampedArray]",
    "tensorToBindable should return a Uint8ClampedArray"
  );
  let floatArray = new Float32Array(bindable.buffer);
  Assert.equal(
    floatArray.length,
    4,
    "Float32Array should have the same length as tensor"
  );
  for (let i = 0; i < 4; i++) {
    Assert.ok(
      approxEqual(floatArray[i], tensor[i]),
      "Element " +
        i +
        " matches expected value within tolerance. " +
        "Expected: " +
        tensor[i] +
        ", got: " +
        floatArray[i]
    );
  }
});

add_task(async function test_shutdown_no_error() {
  let manager = new createPlacesSemanticHistoryManager();

  const fakeConn = {
    close: sinon.stub().resolves(),
  };

  manager.setConnection(fakeConn);

  await manager.shutdown();

  Assert.ok(
    fakeConn.close.calledOnce,
    "DB connection close() should be called once"
  );
});
