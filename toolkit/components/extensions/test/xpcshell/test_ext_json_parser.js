/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

add_task(async function test_json_parser() {
  const ID = "json@test.web.extension";

  let xpi = AddonTestUtils.createTempWebExtensionFile({
    files: {
      "manifest.json": String.raw`{
        // This is a manifest.
        "manifest_version": 2,
        "browser_specific_settings": {"gecko": {"id": "${ID}"}},
        "homepage_url": "https://example.com/not/confused/by/slashes",
        "test_long_string": "${"x".repeat(1e7)}",
        "name": "This \" is // not a comment",
        "version": "0.1\\" // , "description": "This is not a description"
      }`,
      "test_edge_cases.json": String.raw`// Comment at start[
{
        /// Still a comment, preceded by /.
        //// Still a comment, preceded by //.
        "test_empty_string": "",
        // comment
"test_start_of_line_after_comment": true,
        "test_string_with_trailing_escaped_quote": "\"",
        "test_string_with_many_escaped_slashes": "odd:\\\"even:\\\\\\"
      }//`, // <-- Do not put anything after // - verifies trailing //.
      "test_error_invalid_json.json": "{where_are_my_quotes",
      "test_error_unterminated_string.json": String.raw`{"\":":"}`,
      "test_error_unexpected_slash.json": "/",
    },
  });

  let expectedManifest = {
    manifest_version: 2,
    browser_specific_settings: { gecko: { id: ID } },
    homepage_url: "https://example.com/not/confused/by/slashes",
    name: 'This " is // not a comment',
    version: "0.1\\",
  };

  let expectedJsonEdgeCases = {
    test_empty_string: "",
    test_start_of_line_after_comment: true,
    test_string_with_trailing_escaped_quote: '"',
    test_string_with_many_escaped_slashes: 'odd:\\"even:\\\\\\',
  };

  let fileURI = Services.io.newFileURI(xpi);
  let uri = NetUtil.newURI(`jar:${fileURI.spec}!/`);

  let extension = new ExtensionData(uri, false);

  await extension.parseManifest();

  const { test_long_string, ...rawManifest } = extension.rawManifest;

  Assert.equal(test_long_string.length, 1e7, "test1 length");

  Assert.deepEqual(
    rawManifest,
    expectedManifest,
    "Manifest with correctly-filtered comments"
  );

  Assert.deepEqual(
    await extension.readJSON("test_edge_cases.json"),
    expectedJsonEdgeCases,
    "Edge cases in JSON file are parsed correctly"
  );

  await Assert.rejects(
    extension.readJSON("test_error_invalid_json.json"),
    /^SyntaxError: JSON.parse: expected property name or '}' at line 1 column 2 of the JSON data$/,
    "Error for invalid JSON"
  );
  await Assert.rejects(
    extension.readJSON("test_error_unterminated_string.json"),
    /^Error: Invalid JSON: Unterminated string literal$/,
    "Error for escaped quote without matching closing quote"
  );
  await Assert.rejects(
    extension.readJSON("test_error_unexpected_slash.json"),
    /^Error: Invalid JSON: Unexpected \/$/,
    "Error for / that is not a start of comment"
  );

  Services.obs.notifyObservers(xpi, "flush-cache-entry");
});

add_task(async function test_getExtensionVersionWithoutValidation() {
  let xpi = AddonTestUtils.createTempWebExtensionFile({
    files: {
      "manifest.json": String.raw`{
        // This is valid JSON but not a valid manifest.
        "version": ["This is not a valid version"]
      }`,
    },
  });
  let fileURI = Services.io.newFileURI(xpi);
  let uri = NetUtil.newURI(`jar:${fileURI.spec}!/`);
  let extension = new ExtensionData(uri, false);

  let rawVersion = await extension.getExtensionVersionWithoutValidation();
  Assert.deepEqual(
    rawVersion,
    ["This is not a valid version"],
    "Got the raw value of the 'version' key from an (invalid) manifest file"
  );

  // The manifest lacks several required properties and manifest_version is
  // invalid. The exact error here doesn't matter, as long as it shows that the
  // manifest is invalid.
  await Assert.rejects(
    extension.parseManifest(),
    /Unexpected params.manifestVersion value: undefined/,
    "parseManifest() should reject an invalid manifest"
  );

  Services.obs.notifyObservers(xpi, "flush-cache-entry");
});

add_task(
  {
    pref_set: [
      ["extensions.manifestV3.enabled", true],
      ["extensions.webextensions.warnings-as-errors", false],
    ],
  },
  async function test_applications_no_longer_valid_in_mv3() {
    let id = "some@id";
    let xpi = AddonTestUtils.createTempWebExtensionFile({
      files: {
        "manifest.json": JSON.stringify({
          manifest_version: 3,
          name: "some name",
          version: "0.1",
          applications: { gecko: { id } },
        }),
      },
    });

    let fileURI = Services.io.newFileURI(xpi);
    let uri = NetUtil.newURI(`jar:${fileURI.spec}!/`);

    let extension = new ExtensionData(uri, false);

    const { manifest } = await extension.parseManifest();
    ok(
      !Object.keys(manifest).includes("applications"),
      "expected no applications key in manifest"
    );

    Services.obs.notifyObservers(xpi, "flush-cache-entry");
  }
);
