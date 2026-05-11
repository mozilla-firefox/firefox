/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

do_get_profile();

let tempFile = new FileUtils.File(PathUtils.tempDir);
const TEST_LOCAL_FILE_NAME = "hello.txt";
tempFile.append(TEST_LOCAL_FILE_NAME);

const gL10n = new Localization(["toolkit/global/browser-utils.ftl"], true);
const DATA_URL_EXPECTED_STRING = gL10n.formatValueSync(
  "browser-utils-url-data"
);
const EXTENSION_NAME = "My Test Extension";
const EXTENSION_URL_EXPECTED_STRING = gL10n.formatValueSync(
  "browser-utils-url-extension",
  { extension: EXTENSION_NAME }
);

const FILE_URL_EXPECTED_STRING = gL10n.formatValueSync(
  "browser-utils-file-scheme"
);

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

const { ExtensionTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ExtensionXPCShellUtils.sys.mjs"
);

AddonTestUtils.init(this);
ExtensionTestUtils.init(this);

// Allow for unsigned addons.
AddonTestUtils.overrideCertDB();

AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "42",
  "42"
);

// Filled in in setup, shared state with the various test tasks so declared here.
let addonBaseURI;

// http/https tests first. These are split out from TESTS for the benefit of reuse by the
// blob: tests further down.
const HTTP_TESTS = [
  // Simple http/https examples with/without `www.`.
  {
    input: "https://example.com",
    output: "example.com",
  },
  {
    input: "https://example.com/path/?query#hash",
    output: "example.com",
  },
  {
    input: "https://www.example.com",
    output: "example.com",
  },
  {
    input: "https://www.example.com/path/?query#hash",
    output: "example.com",
  },
  {
    input: "http://example.com",
    output: "example.com",
  },
  {
    input: "http://www.example.com",
    output: "example.com",
  },

  // We shouldn't drop `www.` if that's the domain:
  {
    input: "https://www.com",
    output: "www.com",
  },

  // Multilevel TLDs should work:
  {
    input: "https://www.example.co.uk",
    output: "example.co.uk",
  },
  {
    input: "https://www.co.uk",
    output: "www.co.uk",
  },

  // Other subdomains should be kept:
  {
    input: "https://webmail.example.co.uk",
    output: "webmail.example.co.uk",
  },
  {
    input: "https://webmail.example.com",
    output: "webmail.example.com",
  },

  // IP addresses should work:
  {
    input: "http://[::1]/foo/bar?baz=bax#quux",
    output: "[::1]",
  },
  {
    input: "http://127.0.0.1/foo/bar?baz=bax#quux",
    output: "127.0.0.1",
  },

  // IDN domains should be displayed in unicode, not punycode (bug 2037875).
  {
    input: "https://bäcker.de/",
    output: "bäcker.de",
  },
  {
    input: "https://www.bäcker.de/",
    output: "bäcker.de",
  },
];

const TESTS = [
  ...HTTP_TESTS,

  // about URIs:
  {
    input: "about:config",
    output: "about:config",
  },
  {
    input: "about:config?foo#bar",
    output: "about:config",
  },

  // file URI directory:
  {
    input: Services.io.newFileURI(new FileUtils.File(PathUtils.tempDir)).spec,
    output: PathUtils.filename(PathUtils.tempDir),
  },
  // file URI directory that ends in slash:
  {
    input:
      Services.io.newFileURI(new FileUtils.File(PathUtils.tempDir)).spec + "/",
    output: PathUtils.filename(PathUtils.tempDir),
  },
  // file URI individual file:
  {
    input: Services.io.newFileURI(tempFile).spec,
    output: tempFile.leafName,
  },

  // As above but for chrome URIs:
  {
    input: "chrome://global/content/blah",
    output: "blah",
  },
  {
    input: "chrome://global/content/blah//",
    output: "blah",
  },
  {
    input: "chrome://global/content/foo.txt",
    output: "foo.txt",
  },

  // Also check data URIs:
  {
    input: "data:text/html,42",
    output: DATA_URL_EXPECTED_STRING,
  },
  {
    input: "data://google.com/text/html,Hello World",
    output: DATA_URL_EXPECTED_STRING,
  },

  {
    input: `moz-icon:${Services.io.newFileURI(tempFile).spec}`,
    output: tempFile.leafName,
  },
  {
    input: "moz-icon://.extension?size=16",
    output: "moz-icon://.extension?size=16",
  },
];

add_setup(async () => {
  const testExtensionData = {
    useAddonManager: "temporary",
    manifest: {
      browser_specific_settings: { gecko: { id: "myextension@example.com" } },
      name: EXTENSION_NAME,
    },
  };

  const testNoNameExtensionData = {
    useAddonManager: "temporary",
    manifest: {
      browser_specific_settings: { gecko: { id: "noname@example.com" } },
      name: "",
    },
  };

  await AddonTestUtils.promiseStartupManager();
  const extension = ExtensionTestUtils.loadExtension(testExtensionData);
  const noNameExtension = ExtensionTestUtils.loadExtension(
    testNoNameExtensionData
  );
  await extension.startup();
  await noNameExtension.startup();

  addonBaseURI = extension.extension.baseURI;
  let noNameAddonBaseURI = noNameExtension.extension.baseURI;

  TESTS.push(
    {
      input: addonBaseURI.spec,
      output: EXTENSION_URL_EXPECTED_STRING,
    },
    {
      input: "moz-extension://blah/",
      output: gL10n.formatValueSync("browser-utils-url-extension", {
        extension: "moz-extension://blah/",
      }),
    },
    {
      input: noNameAddonBaseURI.spec,
      output: gL10n.formatValueSync("browser-utils-url-extension", {
        extension: noNameAddonBaseURI.spec,
      }),
    }
  );

  registerCleanupFunction(async () => {
    await extension.unload();
    await noNameExtension.unload();
  });
});

const { BrowserUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/BrowserUtils.sys.mjs"
);

add_task(async function test_checkStringFormatting() {
  for (let { input, output } of TESTS) {
    Assert.equal(
      BrowserUtils.formatURIStringForDisplay(input, {
        showFilenameForLocalURIs: true,
      }),
      output,
      `String ${input} formatted for output should match`
    );
  }
});

add_task(async function test_checkURIFormatting() {
  for (let { input, output } of TESTS) {
    let uri = Services.io.newURI(input);
    Assert.equal(
      BrowserUtils.formatURIForDisplay(uri, {
        showFilenameForLocalURIs: true,
      }),
      output,
      `URI ${input} formatted for output should match`
    );
  }
});

add_task(async function test_checkOnlyBaseDomain() {
  for (let { input, output } of [
    { input: "https://subdomain.example.com/", output: "example.com" },
    {
      input: "http://www.city.mikasa.hokkaido.jp/",
      output: "city.mikasa.hokkaido.jp",
    },
    { input: "https://www.example.co.uk/", output: "example.co.uk" },
    {
      input: "mailto:example@subdomain.example.com",
      output: "mailto:example@subdomain.example.com",
    },
    // IDN domains should be displayed in unicode (bug 2037875):
    { input: "https://subdomain.bäcker.de/", output: "bäcker.de" },
    { input: "https://subdomain.bäcker.de:8443/", output: "bäcker.de:8443" },
  ]) {
    let uri = Services.io.newURI(input);
    Assert.equal(
      BrowserUtils.formatURIForDisplay(uri, { onlyBaseDomain: true }),
      output,
      `URI ${input} formatted for output should match`
    );
  }
});

add_task(async function test_checkLocalFileFormatting() {
  for (let { input } of TESTS) {
    let uri = Services.io.newURI(input);
    if (
      ["file", "chrome", "moz-icon", "resource", "jar"].includes(uri.scheme)
    ) {
      Assert.equal(
        BrowserUtils.formatURIForDisplay(uri, {
          showFilenameForLocalURIs: false,
        }),
        uri.scheme == "file"
          ? FILE_URL_EXPECTED_STRING
          : `${uri.scheme} resource`,
        `URI ${input} formatted for output should match`
      );
    }
  }
});

add_task(async function test_checkViewSourceFormatting() {
  for (let { input, output } of HTTP_TESTS) {
    Assert.equal(
      BrowserUtils.formatURIStringForDisplay("view-source:" + input),
      output,
      `String view-source:${input} formatted for output should match`
    );
    let uri = Services.io.newURI("view-source:" + input);
    Assert.equal(
      BrowserUtils.formatURIForDisplay(uri),
      output,
      `URI view-source:${input} formatted for output should match`
    );
  }
});

function createBlobURLWithSandbox(origin) {
  let sb = new Cu.Sandbox(origin, { wantGlobalProperties: ["Blob", "URL"] });
  // Need to pass 'false' for the validate filename param or this throws
  // exceptions. I'm not sure why...
  return Cu.evalInSandbox(
    'URL.createObjectURL(new Blob(["text"], { type: "text/plain" }))',
    sb,
    "",
    null,
    0,
    false
  );
}

add_task(async function test_checkBlobURIs() {
  // These don't just live in the TESTS array because creating a valid
  // blob URI is a bit more involved...
  let blob = new Blob(["test"], { type: "text/plain" });
  let url = URL.createObjectURL(blob);
  Assert.equal(
    BrowserUtils.formatURIStringForDisplay(url),
    DATA_URL_EXPECTED_STRING,
    `Blob url string without origin should be represented as (data)`
  );

  // Now with a null principal:
  url = createBlobURLWithSandbox(null);
  Assert.equal(
    BrowserUtils.formatURIStringForDisplay(url),
    DATA_URL_EXPECTED_STRING,
    `Blob url string with null principal origin should be represented as (data)`
  );

  // And some valid url principals:
  let BLOB_TESTS = [
    {
      input: addonBaseURI.spec,
      output: EXTENSION_URL_EXPECTED_STRING,
    },
  ].concat(HTTP_TESTS);
  for (let { input, output } of BLOB_TESTS) {
    url = createBlobURLWithSandbox(input);
    Assert.equal(
      BrowserUtils.formatURIStringForDisplay(url),
      output,
      `Blob url string with principal from ${input} should show principal URI`
    );
  }
});
