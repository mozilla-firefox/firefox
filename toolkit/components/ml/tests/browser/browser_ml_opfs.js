/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { OPFS } = ChromeUtils.importESModule(
  "chrome://global/content/ml/OPFS.sys.mjs"
);

add_task(async function test_opfs_file() {
  const iconUrl =
    "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/data/mozilla-logo.webp";
  const icon = await new OPFS.File({
    urls: [iconUrl],
    localPath: "/icons/icon.webp",
  });

  let blobUrl = await icon.getAsObjectURL();

  Assert.notEqual(blobUrl, null, "we got a blob url");

  // second call will get it from the cache
  let spy = sinon.spy(OPFS.File.prototype, "getBlobFromOPFS");
  blobUrl = await icon.getAsObjectURL();
  Assert.notEqual(blobUrl, null);

  // check that it cames from OPFS
  Assert.notEqual(await spy.lastCall.returnValue, null);
  sinon.restore();

  await icon.delete();
});
