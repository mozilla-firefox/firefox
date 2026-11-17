/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

add_task(async function test_ml_modelhub_listModels_on_mostrecent_pbwindow() {
  let winPB = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  const FAKE_HUB =
    "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/data";
  const FAKE_URL_TEMPLATE = "{model}/resolve/{revision}";
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });

  const models = await hub.listModels();
  Assert.deepEqual(models, [], "expected empty list");

  await BrowserTestUtils.closeWindow(winPB);
});
