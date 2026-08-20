/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const RELATIVE_DIR = "toolkit/components/pdfjs/test/";
const TESTROOT = "http://example.com/browser/" + RELATIVE_DIR;

// Test js in pdf file.
add_task(async function test_js_sandbox() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:blank" },
    async function (browser) {
      await SpecialPowers.pushPrefEnv({
        set: [["pdfjs.enableScripting", true]],
      });

      await Promise.all([
        waitForPdfJSAnnotationLayer(browser, TESTROOT + "file_pdfjs_js.pdf"),
        waitForPdfJSSandbox(browser),
      ]);

      await SpecialPowers.spawn(browser, [], async () => {
        const { PdfSandbox } = ChromeUtils.importESModule(
          "resource://pdf.js/PdfSandbox.sys.mjs"
        );

        let sandboxDestroyCount = 0;
        const originalDestroy = PdfSandbox.prototype.destroy;
        PdfSandbox.prototype.destroy = function () {
          const obj = this.sandbox.eval("({})");
          originalDestroy.apply(this, arguments);
          sandboxDestroyCount++;
          ok(Cu.isDeadWrapper(obj), "Sandbox must have been nuked");
        };

        const document = content.document;
        const button = document.querySelector("[data-annotation-id='16R'] a");
        button.dispatchEvent(new content.Event("click"));

        const text = document.querySelector(`[data-element-id="15R"]`);

        is(text.value, "test", "Text field must contain 'test' string");

        content.addEventListener(
          "unload",
          () => {
            is(sandboxDestroyCount, 1, "Sandbox must have been destroyed");
          },
          { once: true }
        );
      });

      await waitForPdfJSClose(browser);
      await SpecialPowers.popPrefEnv();
    }
  );
});
