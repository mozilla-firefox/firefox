/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ExtensionTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ExtensionXPCShellUtils.sys.mjs"
);

ExtensionTestUtils.init(this);

// ID of the WebExtension that bundles the DLP wasm module, shared by the tests
// that install it. keep in sync with EXTENSION_ID in
// ContentAnalysisWasmRunner.sys.mjs
const EXTENSION_ID = "dlp-wasm-provider@mozilla.org";

// Name the wasm module has both as a test support-file and inside the extension
// package.
const WASM_PATH = "content_analysis_wasm.wasm";

// The ContentAnalysisWasm process actor is normally registered at browser
// startup (via ActorManagerParent/DesktopActorRegistry), which does not run in
// xpcshell, so register it here.
try {
  ChromeUtils.registerProcessActor("ContentAnalysisWasm", {
    parent: {
      esModuleURI:
        "moz-src:///toolkit/components/contentanalysis/ContentAnalysisWasmParent.sys.mjs",
    },
    child: {
      esModuleURI:
        "moz-src:///toolkit/components/contentanalysis/ContentAnalysisWasmChild.sys.mjs",
    },
    remoteTypes: ["privilegedabout"],
  });
} catch (e) {
  if (e.name !== "NotSupportedError") {
    throw e;
  }
}

// Install a WebExtension bundling the wasm module under WASM_PATH
async function installModuleExtension() {
  const extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: { gecko: { id: EXTENSION_ID } },
      web_accessible_resources: [WASM_PATH],
    },
    files: {
      // The XPI writer requires an ArrayBuffer for binary entries. do_get_file
      // resolves WASM_PATH against the test's directory (the wasm is a
      // support-file copied there) and IOUtils.read needs an absolute path.
      [WASM_PATH]: (await IOUtils.read(do_get_file(WASM_PATH).path)).buffer,
    },
  });
  await extension.startup();
  return extension;
}
