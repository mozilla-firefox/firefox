/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockFilePicker } = SpecialPowers;

const PDF_CONTENTS = `%PDF-1.4
1 0 obj<</Pages 2 0 R>>endobj
2 0 obj<</Kids[3 0 R]/Count 1>>endobj
3 0 obj<</MediaBox[0 0 1 1]>>endobj
trailer<</Root 1 0 R/Size 4>>`;

add_setup(function () {
  MockFilePicker.init();
  registerCleanupFunction(() => {
    MockFilePicker.cleanup();
  });
});

function pickFile(actor, file) {
  MockFilePicker.setFiles(file ? [file] : []);
  MockFilePicker.returnValue = file
    ? MockFilePicker.returnOK
    : MockFilePicker.returnCancel;
  return actor.receiveMessage({ name: "AboutPDF:PickFile" });
}

add_task(async function testCanceledPicker() {
  const tab = await openAboutPDF();
  is(
    await pickFile(getAboutPDFActor(tab), null),
    "canceled",
    "a canceled picker opens nothing"
  );
  is(
    tab.linkedBrowser.currentURI.spec,
    "about:pdf",
    "the tab did not navigate"
  );
  BrowserTestUtils.removeTab(tab);
});

add_task(async function testRejectsNonPDFExtension() {
  const file = await createTempFile(PDF_CONTENTS, { suffix: ".txt" });
  const tab = await openAboutPDF();
  is(
    await pickFile(getAboutPDFActor(tab), file),
    "invalid",
    ".txt extension rejected"
  );
  BrowserTestUtils.removeTab(tab);
  await safeRemove(file);
});

add_task(async function testRejectsBadMagicBytes() {
  const file = await createTempFile("not actually a pdf");
  const tab = await openAboutPDF();
  is(
    await pickFile(getAboutPDFActor(tab), file),
    "invalid",
    "file without %PDF- header rejected"
  );
  BrowserTestUtils.removeTab(tab);
  await safeRemove(file);
});

add_task(async function testAcceptsValidPDFAndNavigates() {
  const file = await createTempFile(PDF_CONTENTS);
  const fileURL = Services.io.newFileURI(file).spec;

  const tab = await openAboutPDF();
  const navigated = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    url => url === fileURL
  );
  is(
    await pickFile(getAboutPDFActor(tab), file),
    "opened",
    "the picked PDF is opened"
  );
  await navigated;

  is(
    tab.linkedBrowser.currentURI.spec,
    fileURL,
    "tab navigated to the PDF file URL"
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    const viewer = content.wrappedJSObject.PDFViewerApplication;
    await viewer.testingClose();
  });

  BrowserTestUtils.removeTab(tab);
  await safeRemove(file);
});

add_task(async function testPickerNeedsUserActivation() {
  // Avoid navigation if this unexpectedly succeeds.
  MockFilePicker.setFiles([]);
  MockFilePicker.returnValue = MockFilePicker.returnCancel;

  const tab = await openAboutPDF();
  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    let threw = false;
    try {
      content.wrappedJSObject.RPMPickPDFFile();
    } catch {
      threw = true;
    }
    ok(threw, "a script can't open the picker without a user gesture");
  });
  BrowserTestUtils.removeTab(tab);
});

add_task(async function testBrowseButtonOpensPickedPDF() {
  const file = await createTempFile(PDF_CONTENTS);
  const fileURL = Services.io.newFileURI(file).spec;
  MockFilePicker.setFiles([file]);
  MockFilePicker.returnValue = MockFilePicker.returnOK;

  const tab = await openAboutPDF();
  const navigated = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    url => url === fileURL
  );
  const pickerShown = promisePickerShown();
  // A scripted click would not grant user activation.
  await clickInAboutPDF(tab, "#browse-files");
  await pickerShown;
  await navigated;

  is(
    tab.linkedBrowser.currentURI.spec,
    fileURL,
    "the browse button opens the picked PDF"
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    const viewer = content.wrappedJSObject.PDFViewerApplication;
    await viewer.testingClose();
  });

  BrowserTestUtils.removeTab(tab);
  await safeRemove(file);
});

add_task(async function testBrowseButtonReportsANonPDF() {
  const file = await createTempFile(PDF_CONTENTS, { suffix: ".txt" });
  MockFilePicker.setFiles([file]);
  MockFilePicker.returnValue = MockFilePicker.returnOK;

  const tab = await openAboutPDF();
  const pickerShown = promisePickerShown();
  await clickInAboutPDF(tab, "#browse-files");
  await pickerShown;
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    const error = content.document.getElementById("dropzone-error");
    await ContentTaskUtils.waitForCondition(
      () => !error.hidden,
      "the page reports the invalid file"
    );
    is(
      error.getAttribute("data-l10n-id"),
      "about-pdf-dropzone-invalid-file",
      "the invalid file error is shown"
    );
  });

  BrowserTestUtils.removeTab(tab);
  await safeRemove(file);
});

add_task(async function testSecondPickerIsIgnored() {
  const tab = await openAboutPDF();
  const actor = getAboutPDFActor(tab);

  let opens = 0;
  let releaseFirst;
  const firstShown = Promise.withResolvers();
  MockFilePicker.setFiles([]);
  MockFilePicker.returnValue = MockFilePicker.returnCancel;
  MockFilePicker.showCallback = () => {
    opens++;
    // Keep the first picker open; close an unexpected second.
    if (opens > 1) {
      return Promise.resolve();
    }
    firstShown.resolve();
    return new Promise(resolve => {
      releaseFirst = resolve;
    });
  };

  const showing = actor.receiveMessage({ name: "AboutPDF:PickFile" });
  await firstShown.promise;
  is(
    await actor.receiveMessage({ name: "AboutPDF:PickFile" }),
    "canceled",
    "the second request is refused"
  );
  is(opens, 1, "no second picker was opened while one was up");

  releaseFirst();
  await showing;
  MockFilePicker.showCallback = null;
  BrowserTestUtils.removeTab(tab);
});

// Ignore file URLs from content; the parent must obtain the path itself.
add_task(async function testIgnoresFileURLFromContent() {
  const file = await createTempFile(PDF_CONTENTS);
  const tab = await openAboutPDF();
  const actor = getAboutPDFActor(tab);

  is(
    await actor.receiveMessage({
      name: "AboutPDF:OpenFile",
      data: { fileURL: Services.io.newFileURI(file).spec },
    }),
    undefined,
    "a file URL coming from the content process is not handled"
  );
  await TestUtils.waitForTick();
  is(
    tab.linkedBrowser.currentURI.spec,
    "about:pdf",
    "the tab did not navigate"
  );

  BrowserTestUtils.removeTab(tab);
  await safeRemove(file);
});
