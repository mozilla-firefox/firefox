/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const RELATIVE_DIR = "toolkit/components/pdfjs/test/";
const TESTROOT = "https://example.com/browser/" + RELATIVE_DIR;
const MULTIPART = `${TESTROOT}/pdf_multipart.sjs`;

add_task(async function test_pdf_in_multipart_response() {
  makePDFJSHandler();

  const previewTabPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    url => {
      const uri = NetUtil.newURI(url);
      return uri.scheme == "file" && uri.spec.endsWith(".pdf");
    },
    true, // wait for load
    true // any tab
  );

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: MULTIPART, waitForLoad: false },
    async function (browser) {
      info("Waiting for preview tab");
      let previewTab = await previewTabPromise;
      info("Waiting for text layer");
      await waitForSelector(
        previewTab.linkedBrowser,
        ".textLayer .endOfContent",
        "Wait for text layer."
      );

      ok(previewTab, "PDF opened in a new tab");
      await waitForPdfJSClose(previewTab.linkedBrowser, /* closeTab */ true);

      await cleanupDownloads();
      await closeDownloadPanel();

      Assert.strictEqual(browser.documentURI, null, "documentURI is null");
    }
  );
});
