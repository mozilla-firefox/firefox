/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

let mockCA = makeMockContentAnalysis();

add_setup(async function test_setup() {
  mockCA = await mockContentAnalysisService(mockCA);
});

const testPage =
  "<body style='margin: 0'><input id='input' type='text'></body>";

const CLIPBOARD_TEXT_STRING = "Just some text";

function setClipboardData(clipboardString) {
  const trans = Cc["@mozilla.org/widget/transferable;1"].createInstance(
    Ci.nsITransferable
  );
  trans.init(null);
  trans.addDataFlavor("text/plain");
  const str = Cc["@mozilla.org/supports-string;1"].createInstance(
    Ci.nsISupportsString
  );
  str.data = clipboardString;
  trans.setTransferData("text/plain", str);

  // Write to clipboard.
  Services.clipboard.setData(trans, null, Ci.nsIClipboard.kGlobalClipboard);
}

async function getElementValue(browser, elementId) {
  return await SpecialPowers.spawn(browser, [elementId], async elementId => {
    return content.document.getElementById(elementId).value;
  });
}

const TEST_MODES = Object.freeze({
  NO_REQUEST_ACTIVE: {
    requestActive: false,
  },
  REQUEST_ACTIVE_AND_CANCEL: {
    requestActive: true,
    cancelQuit: true,
  },
  REQUEST_ACTIVE_AND_CONFIRM_QUIT: {
    requestActive: true,
    cancelQuit: false,
  },
});

async function testConfirmationDialog(testMode) {
  mockCA.setupForTest(true, /* waitForEvent */ true, /* showDialogs */ true);
  let tab = BrowserTestUtils.addTab(gBrowser);
  let browser = gBrowser.getBrowserForTab(tab);
  gBrowser.selectedTab = tab;
  await promiseTabLoadEvent(tab, "data:text/html," + escape(testPage));
  await SimpleTest.promiseFocus(browser);

  setClipboardData(CLIPBOARD_TEXT_STRING);

  let scanStartedPromise = new Promise(res => {
    mockCA.eventTarget.addEventListener(
      "inAnalyzeContentRequest",
      () => {
        res();
      },
      { once: true }
    );
  });

  // Paste into content
  await SpecialPowers.spawn(browser, [], () => {
    content.document.getElementById("input").value = "";
    content.document.getElementById("input").focus();
  });

  let doPastePromise = BrowserTestUtils.synthesizeKey(
    "v",
    { accelKey: true },
    browser
  );
  await scanStartedPromise;
  // Wait for busy dialog
  await BrowserTestUtils.promiseAlertDialogOpen();

  if (!testMode.requestActive) {
    // Make the request complete.
    mockCA.eventTarget.dispatchEvent(
      new CustomEvent("returnContentAnalysisResponse")
    );
    await doPastePromise;
  }

  let cancelQuit = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
    Ci.nsISupportsPRBool
  );
  let quitConfirmationDialogPromise;
  if (testMode.requestActive) {
    quitConfirmationDialogPromise = BrowserTestUtils.promiseAlertDialog(
      testMode.cancelQuit ? "cancel" : "accept"
    );
  }

  // If testMode.requestActive is false, we don't expect a confirmation dialog
  // to pop up. If it does, this test will timeout. (since we didn't call
  // promiseAlertDialog() above)
  Services.obs.notifyObservers(cancelQuit, "quit-application-requested");
  is(
    cancelQuit.data,
    testMode.requestActive && testMode.cancelQuit,
    "checking if CA should abort quit"
  );
  if (testMode.requestActive) {
    // Note that the dialog should have already been dismissed; this is just
    // to avoid an uncompleted Promise.
    await quitConfirmationDialogPromise;
    mockCA.eventTarget.dispatchEvent(
      new CustomEvent("returnContentAnalysisResponse")
    );

    await doPastePromise;
  }
  is(await getElementValue(browser, "input"), CLIPBOARD_TEXT_STRING);

  BrowserTestUtils.removeTab(tab);
}

add_task(
  async function testQuitConfirmationDialogNotShownWhenNoRequestActive() {
    await testConfirmationDialog(TEST_MODES.NO_REQUEST_ACTIVE);
  }
);

add_task(
  async function testQuitConfirmationDialogShownWhenRequestActiveAndCancel() {
    await testConfirmationDialog(TEST_MODES.REQUEST_ACTIVE_AND_CANCEL);
  }
);

add_task(
  async function testQuitConfirmationDialogShownWhenRequestActiveAndConfirmQuit() {
    await testConfirmationDialog(TEST_MODES.REQUEST_ACTIVE_AND_CONFIRM_QUIT);
  }
);
