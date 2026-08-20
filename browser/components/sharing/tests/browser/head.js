ChromeUtils.defineESModuleGetters(this, {
  ContentSharingUtils:
    "moz-src:///browser/components/sharing/ContentSharingUtils.sys.mjs",
  makeShareResult:
    "moz-src:///browser/components/sharing/ContentSharingUtils.sys.mjs",
  ERRORS: "moz-src:///browser/components/sharing/ContentSharingUtils.sys.mjs",
  WARNINGS: "moz-src:///browser/components/sharing/ContentSharingUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(this, "ContentSharingMockServer", () => {
  const { ContentSharingMockServer: server } = ChromeUtils.importESModule(
    "resource://testing-common/ContentSharingMockServer.sys.mjs"
  );
  return server;
});

const SHARE_BUTTON_ID = "share-button";

/**
 * Sets a cookie for test purposes.
 *
 * @param {string} name Name of the cookie (ours will usually be "auth")
 * @param {string} value Value of the cookie
 * @param {number} [expiry] Optional, Cookie expiry time in milliseconds in
 *                          the future (or past), defaults to 5 minutes.
 * @param {string} [host] Optional, defaults to "localhost".
 */
function setCookie(name, value, expiry = 1000 * 60 * 5, host = "localhost") {
  Services.cookies.add(
    host,
    "/",
    name,
    value,
    true, // isSecure
    false, // isHttpOnly
    false, // isSession
    Date.now() + expiry,
    {}, // originAttributes
    Ci.nsICookie.SAMESITE_LAX,
    Ci.nsICookie.SCHEME_HTTPS
  );
}

function clearCookies() {
  Services.cookies.removeAll();
}

/**
 * Starts the mock content sharing server, runs task, then stops it.
 * The server is stopped in a finally block so cleanup always runs.
 * Now with auth cookie support.
 *
 * @param {Function} task - Async function receiving the mock server instance.
 */
async function withContentSharingMockServer(task) {
  setCookie("auth", "1");
  await ContentSharingMockServer.start();
  try {
    await task(ContentSharingMockServer);
  } finally {
    clearCookies();
    await ContentSharingMockServer.stop();
  }
}

/**
 * Asserts on the contents of the sharing modal.
 * If leaveOpen is true, returns the sharing modal el.
 *
 * @param {Window} window - Chrome window in which to open the modal.
 * @param {object} expected - expected result object with shape
 *                            { share, url, isSignedIn }.
 * @param {boolean} leaveOpen - If true, the modal element is returned and
 *                              the dialog is left open. Otherwise the dialog
 *                              is closed when the assert is finished.
 */
async function assertContentSharingModal(window, expected, leaveOpen = false) {
  // Wait for the modal to be fully rendered
  const modalEl = await TestUtils.waitForCondition(() =>
    window.gDialogBox.dialog.frameContentWindow.document.querySelector(
      "content-sharing-modal"
    )
  );
  await TestUtils.waitForCondition(() => BrowserTestUtils.isVisible(modalEl));

  // If the modal is still loading, wait for the loadingPromise to resolve
  // before asserting on the final shareResult state.
  if (modalEl.loading) {
    await TestUtils.waitForCondition(() => !modalEl.loading);
  }

  await TestUtils.waitForCondition(() => modalEl.getUpdateComplete);
  await modalEl.getUpdateComplete();

  Assert.ok(window.gDialogBox.isOpen, "Content sharing modal should be open");

  const laodedShareResult = modalEl.shareResult;
  Assert.deepEqual(
    laodedShareResult,
    expected,
    "The window has the expected arguments"
  );

  Assert.deepEqual(
    modalEl.shareResult,
    expected,
    "Modal has the expected share result"
  );
  await TestUtils.waitForCondition(
    () => modalEl.links?.length === Math.min(expected.share.links.length, 3)
  );

  Assert.equal(
    modalEl.title.innerText,
    expected.share.title,
    "Modal has the correct share title"
  );

  if (expected.share.type !== "tabs") {
    Assert.equal(
      modalEl.linkCount.innerText,
      `${expected.share.links.length}`,
      "Modal has the correct link count"
    );
  }

  Assert.equal(
    modalEl.links.length,
    Math.min(expected.share.links.length, 3),
    "Modal has the expected number of links. Max of 3 links"
  );

  if (expected.error) {
    Assert.ok(
      BrowserTestUtils.isVisible(modalEl.errorMessageBar),
      "Error message is visible"
    );
  } else if (expected.isSignedIn) {
    Assert.ok(
      BrowserTestUtils.isVisible(modalEl.copyButton),
      "Copy button is visible"
    );
  } else {
    Assert.ok(
      BrowserTestUtils.isVisible(modalEl.signInButton),
      "Sign in button is visible"
    );
  }

  if (expected.share.links.length > 3) {
    if (expected.warning === WARNINGS.TOO_MANY_LINKS) {
      Assert.ok(
        BrowserTestUtils.isVisible(modalEl.tooManyLinks),
        "Too many links warning is visible"
      );
    } else {
      await TestUtils.waitForCondition(() =>
        modalEl.moreLinks.innerText.startsWith(
          `+${expected.share.links.length - 3}`
        )
      );
      Assert.ok(
        modalEl.moreLinks.innerText.startsWith(
          `+${expected.share.links.length - 3}`
        ),
        `Modal has +${expected.share.links.length - 3} more links text`
      );
    }
  }

  if (leaveOpen) {
    return modalEl;
  }
  window.gDialogBox.dialog.close();
  return null;
}

async function createFolderWithBookmarks(
  folderName,
  parentGuid = PlacesUtils.bookmarks.toolbarGuid
) {
  const folder = await PlacesUtils.bookmarks.insert({
    index: -1,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    parentGuid,
    title: folderName,
  });

  for (let i of [1, 2, 3, 4, 5]) {
    await PlacesUtils.bookmarks.insert({
      index: -1,
      type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
      parentGuid: folder.guid,
      url: `https://example.com/${i}`,
      title: `Example ${i}`,
    });
  }
  return folder;
}

async function openSharePanel(win) {
  const button = win.document.getElementById(SHARE_BUTTON_ID);

  await TestUtils.waitForCondition(
    () => BrowserTestUtils.isVisible(button),
    "Wait for button to be visible"
  );

  const panelShown = BrowserTestUtils.waitForEvent(
    win.document,
    "popupshown",
    true,
    e => e.target.id === "share-panel"
  );

  EventUtils.synthesizeMouseAtCenter(button, {});

  await panelShown;

  return PanelMultiView.getViewNode(win.document, "share-panel");
}

async function closeSharePanel(win) {
  const panel = PanelMultiView.getViewNode(win.document, "share-panel");
  if (panel.state === "closed") {
    return;
  }

  let hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
  panel.hidePopup();
  await hidden;
}
