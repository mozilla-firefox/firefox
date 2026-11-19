/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests the New Tab APIs in merino client.

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  MerinoClient: "moz-src:///browser/components/urlbar/MerinoClient.sys.mjs",
});

add_setup(async () => {
  await MerinoTestUtils.server.start();
});

add_task(async function fetchPictureOfTheDay() {
  MerinoTestUtils.server.reset();
  MerinoTestUtils.server.response.body = { title: "Picture" };

  let merino = new MerinoClient("test");
  let result = await merino.fetchPictureOfTheDay({
    source: "newtab",
    endpointUrl: MerinoTestUtils.server.url.toString(),
    acceptLanguage: "fr",
  });

  Assert.deepEqual(result, { data: { title: "Picture" }, error: null });
  let [request] = MerinoTestUtils.server.requests;
  let params = new URLSearchParams(request.queryString);
  Assert.equal(params.get("source"), "newtab");
  Assert.equal(request.getHeader("Accept-Language"), "fr");
});

add_task(async function fetchFramedWidgets() {
  MerinoTestUtils.server.reset();
  MerinoTestUtils.server.response.body = { widgets: [] };

  let merino = new MerinoClient("test");
  let result = await merino.fetchFramedWidgets({
    source: "newtab",
    endpointUrl: MerinoTestUtils.server.url.toString(),
    locale: "fr",
  });

  Assert.deepEqual(result, { data: { widgets: [] }, error: null });
  let [request] = MerinoTestUtils.server.requests;
  let params = new URLSearchParams(request.queryString);
  Assert.equal(params.get("source"), "newtab");
  Assert.equal(params.get("locale"), "fr");
  Assert.equal(request.getHeader("Accept-Language"), "fr");
});

add_task(async function errors() {
  let merino = new MerinoClient("test");
  let endpointUrl = MerinoTestUtils.server.url.toString();

  for (let method of ["fetchPictureOfTheDay", "fetchFramedWidgets"]) {
    Assert.deepEqual(
      await merino[method]({ endpointUrl: "" }),
      { data: null, error: null },
      `${method}: no endpoint`
    );
    Assert.deepEqual(
      await merino[method]({ endpointUrl: "not a url" }),
      { data: null, error: "invalid_url" },
      `${method}: invalid endpoint`
    );

    MerinoTestUtils.server.response = { status: 500 };
    Assert.deepEqual(
      await merino[method]({ endpointUrl }),
      { data: null, error: "load_error" },
      `${method}: HTTP error`
    );

    MerinoTestUtils.server.response = {
      status: 200,
      contentType: "text/plain",
      body: "not json",
    };
    Assert.deepEqual(
      await merino[method]({ endpointUrl }),
      { data: null, error: "load_error" },
      `${method}: unreadable response`
    );
  }

  MerinoTestUtils.server.reset();
});
