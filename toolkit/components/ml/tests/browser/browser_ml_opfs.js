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

const ICON_URL = "chrome://global/content/ml/mozilla-logo.webp";

function savePathFor(fileName) {
  const saveDir = `opfsTests-${crypto.randomUUID()}`;
  registerCleanupFunction(() =>
    OPFS.remove(saveDir, { recursive: true, ignoreErrors: true })
  );
  return `${saveDir}/${fileName}`;
}

add_task(async function test_opfs_download_from_cache() {
  const savePath = savePathFor("icon.webp");

  const downloaded = await OPFS.download({
    source: ICON_URL,
    savePath,
    useCache: true,
  });
  Assert.greater(downloaded.size, 0, "The file was downloaded.");

  // second call will get it from the cache
  let spy = sinon.spy(Progress, "fetchUrl");
  const cached = await OPFS.download({
    source: ICON_URL,
    savePath,
    useCache: true,
  });
  Assert.equal(cached.size, downloaded.size, "The whole file was returned.");

  // check that it came from OPFS
  Assert.equal(spy.called, false);
  spy.restore();
});

add_task(async function test_opfs_download() {
  const savePath = savePathFor("icon.webp");

  let spy = sinon.spy(Progress, "fetchUrl");
  const downloaded = await OPFS.download({
    source: ICON_URL,
    savePath,
    useCache: true,
  });
  Assert.greater(downloaded.size, 0, "The file was downloaded.");

  // check that it didn't come from OPFS
  Assert.equal(spy.called, true);
  Assert.notEqual(await spy.lastCall, null);
  Assert.notEqual(await spy.lastCall.returnValue, null);
  spy.restore();
});
