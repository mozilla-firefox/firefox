const PREF_GET_LANGPACKS = "extensions.getAddons.langpacks.url";

let server = AddonTestUtils.createHttpServer({ hosts: ["example.com"] });
Services.prefs.setStringPref(
  PREF_GET_LANGPACKS,
  "http://example.com/langpacks.json"
);

add_task(async function test_getlangpacks() {
  function setData(data) {
    if (typeof data != "string") {
      data = JSON.stringify(data);
    }

    server.registerPathHandler("/langpacks.json", (request, response) => {
      response.setHeader("content-type", "application/json");
      response.write(data);
    });
  }

  const EXPECTED = [
    {
      id: "langpack-kl@firefox.mozilla.org",
      target_locale: "kl",
      url: "http://example.com/langpack1.xpi",
      version: "1.0",
      hash: "sha256:0123456789abcdef",
    },
    {
      id: "langpack-fo@firefox.mozilla.org",
      target_locale: "fo",
      url: "http://example.com/langpack2.xpi",
      version: "2.0",
      hash: "sha256:fedcba9876543210",
    },
  ];

  setData({
    results: [
      // A simple entry
      {
        id: 12345,
        guid: EXPECTED[0].id,
        target_locale: EXPECTED[0].target_locale,
        current_compatible_version: {
          files: [
            {
              platform: "all",
              url: EXPECTED[0].url,
              hash: EXPECTED[0].hash,
            },
          ],
          version: EXPECTED[0].version,
        },
      },

      // An entry with multiple supported platforms
      {
        id: 67890,
        guid: EXPECTED[1].id,
        target_locale: EXPECTED[1].target_locale,
        current_compatible_version: {
          files: [
            {
              platform: "somethingelse",
              url: "http://example.com/bogus.xpi",
              hash: "sha256:abcd",
            },
            {
              platform: Services.appinfo.OS.toLowerCase(),
              url: EXPECTED[1].url,
              hash: EXPECTED[1].hash,
            },
          ],
          version: EXPECTED[1].version,
        },
      },

      // An entry with no matching platform
      {
        target_locale: "bla",
        current_compatible_version: {
          files: [
            {
              platform: "unsupportedplatform",
              url: "http://example.com/bogus2.xpi",
              hash: "sha256:1234",
            },
          ],
        },
      },
    ],
  });

  let result = await AddonRepository.getAvailableLangpacks();
  equal(result.length, 2, "Got 2 results");

  deepEqual(result[0], EXPECTED[0], "Got expected result for simple entry");
  deepEqual(
    result[1],
    EXPECTED[1],
    "Got expected result for multi-platform entry"
  );

  setData("not valid json");
  await Assert.rejects(
    AddonRepository.getAvailableLangpacks(),
    /SyntaxError/,
    "Got parse error on invalid JSON"
  );
});

// Tests that cookies are not sent with langpack requests.
add_task(async function test_cookies() {
  let lastRequest = null;
  server.registerPathHandler("/langpacks.json", (request, response) => {
    lastRequest = request;
    response.write(JSON.stringify({ results: [] }));
  });

  const COOKIE = "test";
  let expiration = Date.now() + 1000 * 60 * 60;
  const cv = Services.cookies.add(
    "example.com",
    "/",
    COOKIE,
    "testing",
    false,
    false,
    false,
    expiration,
    {},
    Ci.nsICookie.SAMESITE_UNSET,
    Ci.nsICookie.SCHEME_HTTP
  );
  Assert.equal(cv.result, Ci.nsICookieValidation.eOK, "Valid cookie");

  await AddonRepository.getAvailableLangpacks();

  notEqual(lastRequest, null, "Received langpack request");
  equal(
    lastRequest.hasHeader("Cookie"),
    false,
    "Langpack request has no cookies"
  );
});
