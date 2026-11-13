"use strict";

const { Manifests } = ChromeUtils.importESModule(
  "moz-src:///dom/manifest/Manifest.sys.mjs"
);

const defaultURL = new URL(
  "http://example.org/browser/dom/manifest/test/resource.sjs"
);
defaultURL.searchParams.set("Content-Type", "application/manifest+json");

const manifestMock = JSON.stringify({
  short_name: "hello World",
  scope: "/browser/",
});
const manifestUrl = `${defaultURL}&body=${manifestMock}`;

function makeTestURL() {
  const url = new URL(defaultURL);
  const body = `<link rel="manifest" href='${manifestUrl}'>`;
  url.searchParams.set("Content-Type", "text/html; charset=utf-8");
  url.searchParams.set("body", encodeURIComponent(body));
  return url.href;
}

function generateHash(aString, hashAlg) {
  const cryptoHash = Cc["@mozilla.org/security/hash;1"].createInstance(
    Ci.nsICryptoHash
  );
  cryptoHash.init(hashAlg);
  const stringStream = Cc[
    "@mozilla.org/io/string-input-stream;1"
  ].createInstance(Ci.nsIStringInputStream);
  stringStream.setByteStringData(aString);
  cryptoHash.updateFromStream(stringStream, -1);
  // base64 allows the '/' char, but we can't use it for filenames.
  return cryptoHash.finish(true).replace(/\//g, "-");
}

const MANIFESTS_DIR = PathUtils.join(PathUtils.profileDir, "manifests");

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.security.https_first", false]],
  });
  registerCleanupFunction(async () => {
    const manifest = Manifests.manifestObjs.get(manifestUrl);
    delete Manifests._store.data.scopes[manifest.scope];
    await manifest._store.finalize();
    Manifests.manifestObjs.delete(manifestUrl);
    await IOUtils.remove(manifest.path);
    await Manifests._store._save();
  });
});

add_task(async function () {
  const tabOptions = { gBrowser, url: makeTestURL() };

  const filenameMD5 = generateHash(manifestUrl, Ci.nsICryptoHash.MD5) + ".json";
  const filenameSHA =
    generateHash(manifestUrl, Ci.nsICryptoHash.SHA256) + ".json";
  const manifestMD5Path = PathUtils.join(MANIFESTS_DIR, filenameMD5);
  const manifestSHAPath = PathUtils.join(MANIFESTS_DIR, filenameSHA);

  await BrowserTestUtils.withNewTab(tabOptions, async function (browser) {
    let tmpManifest = await Manifests.getManifest(browser, manifestUrl);
    is(tmpManifest.installed, false, "We haven't installed this manifest yet");

    await tmpManifest.install();

    // making sure the manifest is actually installed before proceeding, and
    // that its pending save doesn't rewrite the file after cleanup
    await tmpManifest._store.finalize();
    await IOUtils.move(tmpManifest.path, manifestMD5Path);

    let exists = await IOUtils.exists(tmpManifest.path);
    is(
      exists,
      false,
      "Manually moved manifest from SHA256 based path to MD5 based path"
    );
    Manifests.manifestObjs.delete(manifestUrl);

    let manifest = await Manifests.getManifest(browser, manifestUrl);
    await manifest.install(browser);
    is(manifest.name, "hello World", "Manifest has correct name");
    is(manifest.installed, true, "Manifest is installed");
    is(manifest.url, manifestUrl, "has correct url");
    is(manifest.browser, browser, "has correct browser");
    is(manifest.path, manifestSHAPath, "has correct path");

    exists = await IOUtils.exists(manifestMD5Path);
    is(exists, false, "MD5 based manifest removed");

    manifest = await Manifests.getManifest(browser, manifestUrl);
    is(manifest.installed, true, "New instances are installed");

    manifest = await Manifests.getManifest(browser);
    is(manifest.installed, true, "Will find manifest without being given url");

    let foundManifest = Manifests.findManifestUrl(
      "http://example.org/browser/dom/"
    );
    is(foundManifest, manifestUrl, "Finds manifests within scope");

    foundManifest = Manifests.findManifestUrl("http://example.org/");
    is(foundManifest, null, "Does not find manifests outside scope");
  });
  // Get the cached one now
  await BrowserTestUtils.withNewTab(tabOptions, async browser => {
    const manifest = await Manifests.getManifest(browser, manifestUrl);
    is(manifest.browser, browser, "has updated browser object");
  });
});
