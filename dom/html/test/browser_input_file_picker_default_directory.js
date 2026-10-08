/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_URL =
  "https://example.org/document-builder.sjs?html=<input type='file'>";

const MockFilePicker = SpecialPowers.MockFilePicker;
const DIRS = [
  Services.dirsvc.get("TmpD", Ci.nsIFile),
  Services.dirsvc.get("Home", Ci.nsIFile),
];

add_setup(async function init() {
  MockFilePicker.init(window.browsingContext);
  registerCleanupFunction(() => {
    MockFilePicker.cleanup();
  });
});

DIRS.forEach(dir => {
  // The home directory may not be readable by the content process sandbox, but
  // it is readable by the parent process where the file picker runs.
  add_task(async function test_display_directory_not_readable_by_content() {
    info(`Default directory: ${dir.path}`);
    await SpecialPowers.pushPrefEnv({
      set: [["dom.input.fallbackUploadDir", dir.path]],
    });

    await BrowserTestUtils.withNewTab(TEST_URL, async browser => {
      const shown = new Promise(resolve => {
        MockFilePicker.showCallback = () => {
          resolve();
          return Ci.nsIFilePicker.returnCancel;
        };
      });

      await SpecialPowers.spawn(browser, [], () => {
        content.document.notifyUserGestureActivation();
        content.document.querySelector("input").click();
      });
      await shown;

      is(
        MockFilePicker.displayDirectory?.path,
        dir.path,
        "Display directory should be passed to the file picker"
      );
      is(
        MockFilePicker.displaySpecialDirectory,
        "",
        "Display special directory should not be set"
      );

      MockFilePicker.reset();
    });
  });
});
