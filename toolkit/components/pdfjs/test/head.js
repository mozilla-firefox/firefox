// A page render can be cancelled, for example by the scale change the sidebar
// triggers when it opens, and then no "textlayerrendered" is dispatched for it.
// So check the state of the DOM instead of pairing up the render events.
function waitForPdfJS(browser, url = null) {
  const loadPromise = BrowserTestUtils.waitForContentEvent(
    browser,
    "textlayerrendered",
    false,
    event => {
      const doc = event.target.ownerDocument || event.target;
      const pages = doc.querySelectorAll(".page[data-loaded='true']");
      return (
        !!pages.length &&
        Array.from(pages).every(page =>
          page.querySelector(".textLayer:not([hidden]) .endOfContent")
        )
      );
    },
    true
  );
  if (url) {
    BrowserTestUtils.startLoadingURIString(browser, url);
  }
  return loadPromise;
}

async function waitForPdfJSClose(browser, closeTab = false) {
  await SpecialPowers.spawn(browser, [], async () => {
    const viewer = content.wrappedJSObject.PDFViewerApplication;
    await viewer.testingClose();
  });
  if (closeTab) {
    const tab = gBrowser.getTabForBrowser(browser);
    if (tab) {
      BrowserTestUtils.removeTab(tab);
    }
  }
}

async function waitForPdfJSAnnotationLayer(browser, url) {
  let loadPromise = BrowserTestUtils.waitForContentEvent(
    browser,
    "annotationlayerrendered",
    false,
    null,
    true
  );
  let pagePromise = waitForPdfJS(browser, url);
  return Promise.all([pagePromise, loadPromise]);
}

async function waitForPdfJSAllLayers(browser, url, layers) {
  let loadPromise = BrowserTestUtils.waitForContentEvent(
    browser,
    "textlayerrendered",
    false,
    null,
    true
  );
  let annotationPromise = BrowserTestUtils.waitForContentEvent(
    browser,
    "annotationlayerrendered",
    false,
    null,
    true
  );
  let annotationEditorPromise = BrowserTestUtils.waitForContentEvent(
    browser,
    "annotationeditorlayerrendered",
    false,
    null,
    true
  );
  let pagePromise = waitForPdfJS(browser, url);

  await Promise.all([
    pagePromise,
    loadPromise,
    annotationPromise,
    annotationEditorPromise,
  ]);

  await SpecialPowers.spawn(browser, [layers], async function (layers) {
    const { ContentTaskUtils } = ChromeUtils.importESModule(
      "resource://testing-common/ContentTaskUtils.sys.mjs"
    );
    const { document } = content;

    for (let i = 0; i < layers.length; i++) {
      await ContentTaskUtils.waitForCondition(
        () =>
          layers[i].every(
            name =>
              !!document.querySelector(
                `.page[data-page-number='${i + 1}'] .${name}`
              )
          ),
        `All the layers must be displayed on page ${i}`
      );
    }
  });

  await TestUtils.waitForTick();
}

async function waitForPdfJSCanvas(browser, url) {
  let loadPromise = BrowserTestUtils.waitForContentEvent(
    browser,
    "pagerendered",
    false,
    null,
    true
  );
  const pagePromise = waitForPdfJS(browser, url);
  return Promise.all([pagePromise, loadPromise]);
}

async function waitForPdfJSSandbox(browser) {
  let loadPromise = BrowserTestUtils.waitForContentEvent(
    browser,
    "sandboxcreated",
    false,
    null,
    true
  );
  return loadPromise;
}

async function waitForSelector(
  browser,
  selector,
  message,
  checkVisibility = true
) {
  return SpecialPowers.spawn(
    browser,
    [selector, message, checkVisibility],
    async function (sel, msg, checkVis) {
      const { ContentTaskUtils } = ChromeUtils.importESModule(
        "resource://testing-common/ContentTaskUtils.sys.mjs"
      );
      const { document } = content;
      await ContentTaskUtils.waitForCondition(
        () => !!document.querySelector(sel),
        `${sel} must be displayed`
      );

      if (checkVis) {
        await ContentTaskUtils.waitForCondition(
          () => ContentTaskUtils.isVisible(document.querySelector(sel)),
          msg
        );
      }
    }
  );
}

async function click(browser, selector) {
  info(`Click on: ${selector}`);
  await waitForSelector(browser, selector);
  await SpecialPowers.spawn(browser, [selector], async function (sel) {
    const el = content.document.querySelector(sel);
    await new Promise(r => {
      el.addEventListener("click", r, { once: true });
      el.click();
    });
  });
}

async function waitForTelemetry(browser) {
  await BrowserTestUtils.waitForContentEvent(
    browser,
    "reporttelemetry",
    false,
    null,
    true
  );
  await TestUtils.waitForTick();
}

/**
 * Enable an editor (Ink, FreeText, ...).
 *
 * @param {object} browser
 * @param {string} name
 */
async function enableEditor(browser, name, expectedPageRendered) {
  info("Enable editor: " + name);
  let loadPromise;
  if (expectedPageRendered !== 0) {
    loadPromise = new Promise(resolve => {
      const removeEventListener = BrowserTestUtils.addContentEventListener(
        browser,
        "pagerendered",
        () => {
          expectedPageRendered -= 1;
          if (expectedPageRendered === 0) {
            removeEventListener();
            resolve();
          }
        },
        { capture: false, wantUntrusted: true }
      );
    });
  }

  const layerPromise = waitForSelector(
    browser,
    `.annotationEditorLayer.${name.toLowerCase()}Editing`,
    "Wait for the annotation editor layer",
    true
  );
  const editingModePromise = BrowserTestUtils.waitForContentEvent(
    browser,
    "annotationeditormodechanged",
    false,
    null,
    true
  );
  await clickOn(browser, `#editor${name}`);
  await editingModePromise;
  await layerPromise;
  if (loadPromise) {
    await loadPromise;
  }
  await TestUtils.waitForTick();
}

/**
 * The text layer contains some spans with the text of the pdf.
 *
 * @param {object} browser
 * @param {string} text
 * @returns {object} the bbox of the span containing the text.
 */
async function getSpanBox(browser, text, pageNumber = 1) {
  return SpecialPowers.spawn(
    browser,
    [text, pageNumber],
    async function (text, number) {
      const { ContentTaskUtils } = ChromeUtils.importESModule(
        "resource://testing-common/ContentTaskUtils.sys.mjs"
      );
      const { document } = content;

      await ContentTaskUtils.waitForCondition(
        () =>
          !!document.querySelector(
            `.page[data-page-number='${number}'] .textLayer .endOfContent`
          ),
        "The text layer must be displayed"
      );

      let targetSpan = null;
      for (const span of document.querySelectorAll(
        `.page[data-page-number='${number}'] .textLayer span`
      )) {
        if (span.innerText.includes(text)) {
          targetSpan = span;
          break;
        }
      }

      Assert.ok(!!targetSpan, `document must have a span containing '${text}'`);

      const { x, y, width, height } = targetSpan.getBoundingClientRect();
      return { x, y, width, height };
    }
  );
}

/**
 * Count the number of elements corresponding to the given selector.
 *
 * @param {object} browser
 * @param {string} selector
 * @returns
 */
async function countElements(browser, selector) {
  return SpecialPowers.spawn(browser, [selector], async function (selector) {
    return new Promise(resolve => {
      content.setTimeout(() => {
        resolve(content.document.querySelectorAll(selector).length);
      }, 0);
    });
  });
}

/**
 * Click at the given coordinates.
 *
 * @param {object} browser
 * @param {number} x
 * @param {number} y
 * @param {number} n
 */
async function clickAt(browser, x, y, n = 1) {
  info(`Click at: (${x}, ${y}), ${n} times`);
  await BrowserTestUtils.synthesizeMouseAtPoint(
    x,
    y,
    {
      type: "mousedown",
      button: 0,
      clickCount: n,
    },
    browser
  );
  await BrowserTestUtils.synthesizeMouseAtPoint(
    x,
    y,
    {
      type: "mouseup",
      button: 0,
      clickCount: n,
    },
    browser
  );
  await TestUtils.waitForTick();
}

/**
 * Click on the element corresponding to the given selector.
 *
 * @param {object} browser
 * @param {string} selector
 */
async function clickOn(browser, selector) {
  await waitForSelector(browser, selector);
  const [x, y] = await SpecialPowers.spawn(
    browser,
    [selector],
    async selector => {
      const element = content.document.querySelector(selector);
      Assert.ok(
        !!element,
        `Element "${selector}" must be available in order to be clicked`
      );
      const { x, y, width, height } = element.getBoundingClientRect();
      return [x + width / 2, y + height / 2];
    }
  );
  await clickAt(browser, x, y);
}

function focusEditorLayer(browser) {
  return focus(browser, ".annotationEditorLayer");
}

/**
 * Focus an element corresponding to the given selector.
 *
 * @param {object} browser
 * @param {string} selector
 * @returns
 */
async function focus(browser, selector) {
  info("Focus: " + selector);
  return SpecialPowers.spawn(
    browser,
    [selector],
    sel =>
      new Promise(resolve => {
        const el = content.document.querySelector(sel);
        if (el === content.document.activeElement) {
          resolve();
        } else {
          el.addEventListener("focus", () => resolve(), { once: true });
          el.focus();
        }
      })
  );
}

/**
 * Hit a key.
 */
async function hitKey(browser, char) {
  await SpecialPowers.spawn(browser, [char], async function (char) {
    const { ContentTaskUtils } = ChromeUtils.importESModule(
      "resource://testing-common/ContentTaskUtils.sys.mjs"
    );
    const EventUtils = ContentTaskUtils.getEventUtils(content);
    await EventUtils.synthesizeKey(char, {}, content);
  });
  await TestUtils.waitForTick();
}

/**
 * Write some text using the keyboard.
 *
 * @param {object} browser
 * @param {string} text
 */
async function write(browser, text) {
  info(`Write: ${text}`);
  for (const char of text.split("")) {
    await hitKey(browser, char);
  }
}

/**
 * Hit escape key.
 */
async function escape(browser) {
  info("Escape");
  await hitKey(browser, "KEY_Escape");
}

/**
 * Add a FreeText annotation and write some text inside.
 *
 * @param {object} browser
 * @param {string} text
 * @param {object} box
 */
async function addFreeText(browser, text, box) {
  info("Add FreeText: " + text);
  const { x, y, width, height } = box;
  const count = await countElements(browser, ".freeTextEditor");
  await focusEditorLayer(browser);
  await clickAt(browser, x + 0.1 * width, y + 0.5 * height);
  await waitForEditors(browser, ".freeTextEditor", count + 1);

  await write(browser, text);
  await escape(browser);
}

async function waitForEditors(browser, selector, count) {
  info(`Wait for ${count} editors`);
  await TestUtils.waitForCondition(
    async () => (await countElements(browser, selector)) === count
  );
}

function changeMimeHandler(preferredAction, alwaysAskBeforeHandling) {
  let handlerService = Cc[
    "@mozilla.org/uriloader/handler-service;1"
  ].getService(Ci.nsIHandlerService);
  let mimeService = Cc["@mozilla.org/mime;1"].getService(Ci.nsIMIMEService);
  let handlerInfo = mimeService.getFromTypeAndExtension(
    "application/pdf",
    "pdf"
  );
  var oldAction = [
    handlerInfo.preferredAction,
    handlerInfo.alwaysAskBeforeHandling,
  ];

  // Change and save mime handler settings
  handlerInfo.alwaysAskBeforeHandling = alwaysAskBeforeHandling;
  handlerInfo.preferredAction = preferredAction;
  handlerService.store(handlerInfo);

  // Refresh data
  handlerInfo = mimeService.getFromTypeAndExtension("application/pdf", "pdf");

  // Test: Mime handler was updated
  is(
    handlerInfo.alwaysAskBeforeHandling,
    alwaysAskBeforeHandling,
    "always-ask prompt change successful"
  );
  is(
    handlerInfo.preferredAction,
    preferredAction,
    "mime handler change successful"
  );

  return oldAction;
}

function createTemporarySaveDirectory(id = "") {
  var saveDir = Services.dirsvc.get("TmpD", Ci.nsIFile);
  saveDir.append(`testsavedir${id}`);
  if (!saveDir.exists()) {
    saveDir.create(Ci.nsIFile.DIRECTORY_TYPE, 0o755);
  }
  return saveDir;
}

async function cleanupDownloads(listId = Downloads.PUBLIC) {
  info("cleaning up downloads");
  let downloadList = await Downloads.getList(listId);
  for (let download of await downloadList.getAll()) {
    await download.finalize(true);
    try {
      if (Services.appinfo.OS === "WINNT") {
        // We need to make the file writable to delete it on Windows.
        await IOUtils.setPermissions(download.target.path, 0o600);
      }
      await IOUtils.remove(download.target.path);
    } catch (error) {
      info("The file " + download.target.path + " is not removed, " + error);
    }

    await downloadList.remove(download);
    await download.finalize();
  }
}

async function closeDownloadPanel() {
  if (DownloadsPanel.panel.state !== "closed") {
    const hiddenPromise = BrowserTestUtils.waitForEvent(
      DownloadsPanel.panel,
      "popuphidden"
    );
    DownloadsPanel.hidePanel();
    await hiddenPromise;
  }
  is(
    DownloadsPanel.panel.state,
    "closed",
    "Check that the download panel is closed"
  );
}

function makePDFJSHandler() {
  let mimeService = Cc["@mozilla.org/mime;1"].getService(Ci.nsIMIMEService);
  let handlerInfo = mimeService.getFromTypeAndExtension(
    "application/pdf",
    "pdf"
  );

  // Make sure pdf.js is the default handler.
  is(
    handlerInfo.alwaysAskBeforeHandling,
    false,
    "pdf handler defaults to always-ask is false"
  );
  is(
    handlerInfo.preferredAction,
    Ci.nsIHandlerInfo.handleInternally,
    "pdf handler defaults to internal"
  );

  info("Pref action: " + handlerInfo.preferredAction);
}

async function testTelemetryEventExtra(record, expected, clear = true) {
  await Services.fog.testFlushAllChildren();
  const values = record.testGetValue();
  Assert.equal(values.length, expected.length, "Should have the length");

  for (let i = 0; i < expected.length; i++) {
    const value = values[i];
    Assert.deepEqual(
      value?.extra ?? {},
      expected[i],
      `Should have the right value at index ${i}`
    );
  }
  if (clear) {
    Services.fog.testResetFOG();
  }
}

function waitForPreviewVisible() {
  return TestUtils.waitForCondition(function () {
    let preview = document.querySelector(".printPreviewBrowser");
    return preview && BrowserTestUtils.isVisible(preview);
  });
}

function closePreview() {
  EventUtils.synthesizeKey("KEY_Escape");
  return TestUtils.waitForCondition(
    () => !document.querySelector(".printPreviewBrowser")
  );
}

function waitForTimeout(browser, n) {
  return SpecialPowers.spawn(browser, [n], n => {
    const { promise, resolve } = Promise.withResolvers();
    content.setTimeout(resolve, n);
    return promise;
  });
}
