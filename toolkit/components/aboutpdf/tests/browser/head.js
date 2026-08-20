/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

async function openAboutPDF() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "about:pdf",
    waitForLoad: true,
  });
  // The load event can fire before painting is unsuppressed, making synthesized
  // clicks miss their target.
  await BrowserTestUtils.waitForPaintingUnsuppressed(
    tab.linkedBrowser.browsingContext
  );
  return tab;
}

// Wait for a layout box before synthesizing the click.
async function clickInAboutPDF(tab, selector) {
  await SpecialPowers.spawn(tab.linkedBrowser, [selector], async sel => {
    const element = content.document.querySelector(sel);
    await ContentTaskUtils.waitForCondition(
      () => element.checkVisibility(),
      `${sel} has a layout box`
    );
  });
  await BrowserTestUtils.synthesizeMouseAtCenter(
    selector,
    {},
    tab.linkedBrowser
  );
}

// Arm before opening the picker.
function promisePickerShown() {
  const { MockFilePicker } = SpecialPowers;
  return new Promise(resolve => {
    MockFilePicker.showCallback = () => {
      MockFilePicker.showCallback = null;
      resolve();
    };
  });
}

function getAboutPDFActor(tab) {
  return tab.linkedBrowser.browsingContext.currentWindowGlobal.getActor(
    "AboutPDF"
  );
}

async function createTempFile(contents, { suffix = ".pdf" } = {}) {
  const file = Services.dirsvc.get("TmpD", Ci.nsIFile);
  file.append(`aboutPDF-test${suffix}`);
  file.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, 0o600);
  const bytes =
    typeof contents === "string"
      ? new TextEncoder().encode(contents)
      : contents;
  await IOUtils.write(file.path, bytes);
  return file;
}

async function safeRemove(file) {
  try {
    await IOUtils.remove(file.path, { ignoreAbsent: true });
  } catch {}
}
