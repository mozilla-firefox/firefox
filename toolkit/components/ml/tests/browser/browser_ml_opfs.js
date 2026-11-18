/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { OPFS } = ChromeUtils.importESModule(
  "chrome://global/content/ml/OPFS.sys.mjs"
);

const { Progress } = ChromeUtils.importESModule(
  "chrome://global/content/ml/Utils.sys.mjs"
);

add_task(async function test_opfs_file_from_cache() {
  const iconUrl = "chrome://global/content/ml/mozilla-logo.webp";
  const icon = await new OPFS.File({
    urls: [iconUrl],
    localPath: `/icons/icon_${crypto.randomUUID()}.webp`,
  });

  let blobUrl = await icon.getAsObjectURL();

  Assert.notEqual(blobUrl, null, "we got a blob url");

  // second call will get it from the cache
  let spy = sinon.spy(Progress, "fetchUrl");
  blobUrl = await icon.getAsObjectURL();
  Assert.notEqual(blobUrl, null);

  // check that it cames from OPFS
  Assert.equal(await spy.called, false);
  spy.restore();

  await icon.delete();
});

add_task(async function test_opfs_file_download() {
  const iconUrl = "chrome://global/content/ml/mozilla-logo.webp";
  let spy = sinon.spy(Progress, "fetchUrl");
  const icon = await new OPFS.File({
    urls: [iconUrl],
    localPath: `/icons/icon_${crypto.randomUUID()}.webp`,
  });

  let blobUrl = await icon.getAsObjectURL();

  Assert.notEqual(blobUrl, null, "we got a blob url");

  // check that it didn't come from OPFS
  Assert.equal(spy.called, true);
  Assert.notEqual(await spy.lastCall, null);
  Assert.notEqual(await spy.lastCall.returnValue, null);
  spy.restore();

  await icon.delete();
});

/**
 * Test toResponse function.
 */
add_task(async function test_ml_opfs_to_response() {
  const modelPath = `test_${crypto.randomUUID()}.txt`;
  await OPFS.getFileHandle(modelPath, { create: true });

  registerCleanupFunction(async () => {
    await OPFS.remove(modelPath);
  });

  const cases = [
    {
      model: modelPath,
      headers: null,
      expected: {},
      msg: "valid response with no headers",
    },
    {
      model: modelPath,
      headers: { some: "header" },
      expected: { some: "header" },
      msg: "valid response",
    },

    {
      model: modelPath,
      headers: { some: "header", int: 1234 },
      expected: { some: "header", int: "1234" },
      msg: "valid response with ints conversion",
    },
    {
      model: modelPath,
      headers: { some: null, int: 1234 },
      expected: { int: "1234" },
      msg: "valid response with null keys ignored",
    },
  ];

  for (const case_ of cases) {
    const response = await OPFS.toResponse(case_.model, case_.headers);
    for (const [key, value] of Object.entries(case_.expected)) {
      Assert.deepEqual(response.headers.get(key), value, case_.message);
    }
  }
});
