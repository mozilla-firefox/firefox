/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const RELATIVE_DIR = "toolkit/components/pdfjs/test/";
const TESTROOT = "https://example.com/browser/" + RELATIVE_DIR;
const pdfUrl = TESTROOT + "file_pdfjs_test.pdf";

// Each task generates and reparses PDFs.
requestLongerTimeout(2);

/**
 * Dispatch `printToPDF` and return its PDF bytes or null response.
 *
 * @param {object} browser
 * @param {Array} printData - the payload for the printToPDF action.
 * @returns {Promise<ArrayBuffer|null>}
 */
async function dispatchPrintToPDF(browser, printData) {
  return SpecialPowers.spawn(browser, [printData], async data => {
    return new Promise(resolve => {
      const request = content.document.createTextNode("");
      request.addEventListener(
        "pdf.js.response",
        event => {
          request.remove();
          resolve(event.detail.response);
        },
        { once: true }
      );
      content.document.documentElement.append(request);
      const detail = Cu.cloneInto(
        { action: "printToPDF", data, responseExpected: true },
        content
      );
      request.dispatchEvent(
        new content.CustomEvent("pdf.js.message", {
          bubbles: true,
          cancelable: false,
          detail,
        })
      );
    });
  });
}

/**
 * Dispatch `printToPDF` and parse the generated PDF.
 *
 * @param {object} browser
 * @param {Array} printData - the payload for the printToPDF action.
 * @returns {Promise<Array<{text: string, items: Array}>>} one entry per page.
 *   `items` holds the position of each extracted text item.
 */
async function printToPDFAndParse(browser, printData) {
  const buffer = await dispatchPrintToPDF(browser, printData);
  Assert.ok(!!buffer, "printToPDF should return a non-null PDF buffer");

  return SpecialPowers.spawn(browser, [buffer], async buffer => {
    const win = Cu.waiveXrays(content);
    const { pdfjsLib } = win;

    const params = new win.Object();
    params.data = new win.Uint8Array(Cu.cloneInto(buffer, content));
    const loadingTask = pdfjsLib.getDocument(params);
    const pdf = Cu.waiveXrays(await loadingTask.promise);

    const pages = [];
    for (let i = 1; i <= pdf.numPages; i++) {
      const page = Cu.waiveXrays(await pdf.getPage(i));
      const textContent = Cu.waiveXrays(await page.getTextContent());

      let text = "";
      const items = [];
      for (let j = 0; j < textContent.items.length; j++) {
        const item = Cu.waiveXrays(textContent.items[j]);
        text += item.str;
        items.push({
          str: item.str,
          x: item.transform[4],
          y: item.transform[5],
          width: item.width,
        });
      }

      pages.push({ text, items });
    }

    await loadingTask.destroy();
    return pages;
  });
}

// Expected text after compatibility and whitespace normalization.
const TEXT_ENTRIES = [
  { text: "Hello", expected: ["Hello"] },
  { text: "World", expected: ["World"] },
  // "Hello World" in Arabic (right-to-left script).
  { text: "مرحبا بالعالم", expected: ["مرحبا", "بالعالم"] },
  { text: "First line\nSecond line", expected: ["First line", "Second line"] },
];

// Verify text extraction from one generated page per entry.
add_task(async function test_printToPDF_text() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:blank" },
    async browser => {
      await waitForPdfJS(browser, pdfUrl);

      const pages = await printToPDFAndParse(
        browser,
        TEXT_ENTRIES.map(({ text }) => ({
          data: {
            width: 200,
            height: 80,
            text,
            color: "#000000",
            fontSize: 24,
          },
        }))
      );

      Assert.equal(
        pages.length,
        TEXT_ENTRIES.length,
        `The generated PDF should have one page per entry`
      );

      // Avoid masking a page-count failure with an undefined access.
      const count = Math.min(pages.length, TEXT_ENTRIES.length);
      for (let i = 0; i < count; i++) {
        // Normalize compatibility forms and ignore extracted whitespace.
        const extracted = pages[i].text.normalize("NFKC").replace(/\s+/g, "");
        for (const expected of TEXT_ENTRIES[i].expected) {
          Assert.stringContains(
            extracted,
            expected.normalize("NFKC").replace(/\s+/g, ""),
            `Page ${i + 1} should contain the expected text`
          );
        }
      }

      await waitForPdfJSClose(browser);
    }
  );
});

// The vertical alignment must move the text in the appearance box.
add_task(async function test_printToPDF_vertical_alignment() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:blank" },
    async browser => {
      await waitForPdfJS(browser, pdfUrl);

      // A box much taller than the text, so that both alignments are distinct.
      const entry = overrides => ({
        data: {
          width: 300,
          height: 200,
          text: "Hello",
          color: "#000000",
          fontSize: 12,
          ...overrides,
        },
      });

      const pages = await printToPDFAndParse(browser, [
        entry({}),
        entry({ verticalAlign: "top" }),
      ]);

      Assert.equal(
        pages.length,
        2,
        `The generated PDF should have one page per entry`
      );

      // The first non-blank item gives where the line starts.
      const [center, top] = pages.map(
        ({ items }) => items.find(item => item.str.trim() !== "") ?? {}
      );
      info(`Positions: ` + JSON.stringify({ center, top }));

      Assert.less(
        center.y,
        top.y,
        `Top-aligned text should sit above centered text`
      );

      await waitForPdfJSClose(browser);
    }
  );
});

// Check font selection with a fractional size.
add_task(async function test_printToPDF_font_family() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:blank" },
    async browser => {
      await waitForPdfJS(browser, pdfUrl);

      const entry = fontFamily => ({
        data: {
          width: 300,
          height: 100,
          text: "Hello",
          color: "#000000",
          fontSize: 12.3456789,
          fontFamily,
        },
      });
      const pages = await printToPDFAndParse(browser, [
        entry("monospace"),
        entry("sans-serif"),
      ]);

      Assert.equal(pages.length, 2, `The generated PDF should have two pages`);

      const [monospace, proportional] = pages.map(
        ({ items }) => items.find(item => item.str.trim() !== "") ?? {}
      );
      info(`Widths: ` + JSON.stringify({ monospace, proportional }));

      Assert.notEqual(
        monospace.width,
        proportional.width,
        `Monospace and proportional text should not have the same advance`
      );

      await waitForPdfJSClose(browser);
    }
  );
});

// Build malformed cases by overriding a valid text entry.
function textEntry(overrides) {
  return {
    data: {
      width: 100,
      height: 50,
      text: "Hello",
      color: "#000000",
      fontSize: 12,
      ...overrides,
    },
  };
}

// Payloads that #validatePrintToPDFData must reject.
const INVALID_CASES = [
  { name: "not an array", printData: {} },
  { name: "empty array", printData: [] },
  { name: "entry without data", printData: [{}] },
  { name: "size missing", printData: [textEntry({ width: undefined })] },
  { name: "size not a number", printData: [textEntry({ height: "50" })] },
  { name: "size non-finite", printData: [textEntry({ width: Infinity })] },
  { name: "size zero", printData: [textEntry({ height: 0 })] },
  { name: "size negative", printData: [textEntry({ width: -100 })] },
  {
    name: "size too large",
    printData: [textEntry({ width: 200 * 72 + 1 })],
  },
  { name: "text missing", printData: [textEntry({ text: undefined })] },
  { name: "text empty", printData: [textEntry({ text: "" })] },
  { name: "text not a string", printData: [textEntry({ text: 1 })] },
  { name: "color not #rrggbb", printData: [textEntry({ color: "red" })] },
  { name: "color without hash", printData: [textEntry({ color: "000000" })] },
  { name: "fontSize non-positive", printData: [textEntry({ fontSize: 0 })] },
  { name: "fontSize too large", printData: [textEntry({ fontSize: 1001 })] },
  {
    name: "fontFamily not a string",
    printData: [textEntry({ fontFamily: 42 })],
  },
  {
    name: "verticalAlign not supported",
    printData: [textEntry({ verticalAlign: "bottom" })],
  },
];

// Each listed payload must receive a null response.
add_task(async function test_printToPDF_invalid_input() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:blank" },
    async browser => {
      await waitForPdfJS(browser, pdfUrl);

      for (const { name, printData } of INVALID_CASES) {
        const response = await dispatchPrintToPDF(browser, printData);
        Assert.strictEqual(
          response,
          null,
          `Invalid input (${name}) should yield a null response`
        );
      }

      await waitForPdfJSClose(browser);
    }
  );
});
