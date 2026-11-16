/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
/* eslint-disable mozilla/no-arbitrary-setTimeout */

"use strict";

const { BasePromiseWorker } = ChromeUtils.importESModule(
  "resource://gre/modules/PromiseWorker.sys.mjs"
);
const { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

// Worker must be loaded from a chrome:// uri, not a file://
// uri, so we first need to load it.

var WORKER_SOURCE_URI = "chrome://promiseworker/content/worker.js";
do_load_manifest("data/chrome.manifest");

const UUID = crypto.randomUUID();

const SOME_ARRAY = new Uint8Array(4);
for (let i = 0; i < 4; ++i) {
  SOME_ARRAY[i] = i;
}

async function echo(message) {
  return new BasePromiseWorker.Meta([message, UUID, SOME_ARRAY.buffer], {
    transfers: [SOME_ARRAY.buffer],
  });
}

var worker = new BasePromiseWorker(WORKER_SOURCE_URI, {}, { echo });

worker.log = function (...args) {
  info("Controller: " + args.join(" "));
};

add_setup(() => {
  // Ensures that when the test ends, our worker doesn't have any deferred
  // jobs remaining that might be leaking memory.
  registerCleanupFunction(() => {
    Assert.equal(
      worker._deferredJobs.size,
      0,
      "Deferred jobs list should be empty."
    );
  });
});

// Test that simple messages work
add_task(async function test_simple_args() {
  let message = ["test_simple_args", Math.random()];
  let result = await worker.post("bounce", message);
  Assert.equal(JSON.stringify(result), JSON.stringify(message));
});

// Test that it works when we don't provide a message
add_task(async function test_no_args() {
  let result = await worker.post("bounce");
  Assert.equal(JSON.stringify(result), JSON.stringify([]));
});

// Test that messages with promise work
add_task(async function test_promise_args() {
  let message = ["test_promise_args", Promise.resolve(Math.random())];
  let stringified = JSON.stringify(await Promise.resolve(Promise.all(message)));
  let result = await worker.post("bounce", message);
  Assert.equal(JSON.stringify(result), stringified);
});

// Test that messages with delayed promise work
add_task(async function test_delayed_promise_args() {
  let promise = new Promise(resolve =>
    setTimeout(() => resolve(Math.random()), 10)
  );
  let message = ["test_delayed_promise_args", promise];
  let stringified = JSON.stringify(await Promise.resolve(Promise.all(message)));
  let result = await worker.post("bounce", message);
  Assert.equal(JSON.stringify(result), stringified);
});

// Test that messages with rejected promise cause appropriate errors
add_task(async function test_rejected_promise_args() {
  let error = new Error();
  let message = ["test_promise_args", Promise.reject(error)];
  try {
    await worker.post("bounce", message);
    do_throw("I shound have thrown an error by now");
  } catch (ex) {
    if (ex != error) {
      throw ex;
    }
    info("I threw the right error");
  }
});

// Test that we can transfer to the worker using argument `transfer`
add_task(async function test_transfer_args() {
  let array = new Uint8Array(4);
  for (let i = 0; i < 4; ++i) {
    array[i] = i;
  }
  Assert.equal(array.buffer.byteLength, 4, "The buffer is not detached yet");

  let result = (
    await worker.post("bounce", [array.buffer], [], [array.buffer])
  )[0];

  // Check that the buffer has been sent
  Assert.equal(array.buffer.byteLength, 0, "The buffer has been detached");

  // Check that the result is correct
  Assert.equal(result.byteLength, 4, "The result has the right size");
  let array2 = new Uint8Array(result);
  for (let i = 0; i < 4; ++i) {
    Assert.equal(array2[i], i);
  }
});

// Test that we can transfer to the worker using an instance of `Meta`
add_task(async function test_transfer_with_meta() {
  let array = new Uint8Array(4);
  for (let i = 0; i < 4; ++i) {
    array[i] = i;
  }
  Assert.equal(array.buffer.byteLength, 4, "The buffer is not detached yet");

  let message = new BasePromiseWorker.Meta(array, {
    transfers: [array.buffer],
  });
  let result = (await worker.post("bounce", [message]))[0];

  // Check that the buffer has been sent
  Assert.equal(array.buffer.byteLength, 0, "The buffer has been detached");

  // Check that the result is correct
  Assert.equal(
    Object.prototype.toString.call(result),
    "[object Uint8Array]",
    "The result appears to be a Typed Array"
  );
  Assert.equal(result.byteLength, 4, "The result has the right size");

  for (let i = 0; i < 4; ++i) {
    Assert.equal(result[i], i);
  }
});

add_task(async function test_throw_error() {
  try {
    await worker.post("throwError", ["error message"]);
    Assert.ok(false, "should have thrown");
  } catch (ex) {
    Assert.equal(ex.message, "Error: error message");
  }
});

add_task(async function test_throw_dom_exception() {
  try {
    await worker.post("throwDOMException", ["error message", "NotFoundError"]);
    Assert.ok(false, "should have thrown");
  } catch (ex) {
    Assert.ok(DOMException.isInstance(ex), "should be a DOMException");
    Assert.equal(ex.name, "NotFoundError");
    Assert.equal(ex.message, "error message");
  }
});

add_task(async function test_terminate() {
  let previousWorker = worker._worker;

  // Send two messages that we'll expect to be rejected.
  let message = ["test_simple_args", Math.random()];
  let promise1 = worker.post("bounce", message);
  let promise2 = worker.post("throwError", ["error message"]);
  // Skip a few beats so we can be sure that the two messages are in the queue.
  await Promise.resolve();
  await Promise.resolve();

  worker.terminate();

  await Assert.rejects(
    promise1,
    /worker terminated/,
    "Pending promise should be rejected"
  );
  await Assert.rejects(
    promise2,
    /worker terminated/,
    "Pending promise should be rejected"
  );

  // Unfortunately, there's no real way to check whether a terminate worked from
  // the JS API. We'll just have to assume it worked.

  // Post and test a simple message to ensure that the worker has been re-instantiated.
  message = ["test_simple_args", Math.random()];
  let result = await worker.post("bounce", message);
  Assert.equal(JSON.stringify(result), JSON.stringify(message));
  Assert.notEqual(
    worker._worker,
    previousWorker,
    "ChromeWorker instances should differ"
  );
});

function cloneArrayBuffer(original) {
  const clone = new ArrayBuffer(original.byteLength);
  const originalView = new Uint8Array(original);
  const cloneView = new Uint8Array(clone);
  cloneView.set(originalView);
  return clone;
}

add_task(async function test_bidirectional() {
  // Before we transfer the array, we clone it
  const arrayCopy = cloneArrayBuffer(SOME_ARRAY.buffer);

  let message = ["test_simple_args", Math.random()];

  // Checking the array buffer size
  Assert.equal(
    SOME_ARRAY.buffer.byteLength,
    4,
    "The buffer is not detached yet"
  );
  let result = await worker.post("bounceWithExtraCalls", message);

  // After the post call, the array was transferred and SOME_ARRAY should be empty
  Assert.equal(SOME_ARRAY.buffer.byteLength, 0, "The buffer has been detached");

  // The echo() function in the worker adds to the message a string, an uuid and has the transferred array
  message.push(["Posting something unrelated", UUID, arrayCopy]);

  Assert.deepEqual(result, message);
});
