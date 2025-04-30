/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if different response content types are handled correctly.
 */

const httpServer = createTestHTTPServer();
httpServer.registerContentType("html", "text/html");

const BASE_URL = `http://localhost:${httpServer.identity.primaryPort}/`;

const REDIRECT_URL = BASE_URL + "redirect.html";

// In all content previewed as HTML we ensure using proper html, head and body in order to
// prevent having them added by the <browser> when loaded as a preview.
function addBaseHtmlElements(body) {
  return `<html><head></head><body>${body}</body></html>`;
}

const TEST_PAGES = {
  // This page asserts we can redirect to another URL, even if JS happen to be executed
  redirect: addBaseHtmlElements(
    `Fetch 1<script>window.parent.location.href = "${REDIRECT_URL}";</script>`
  ),

  // #1 This page asserts that JS is disabled
  js: addBaseHtmlElements(
    `Fetch 2<script>document.write("JS activated")</script>`
  ),

  // #2 This page asserts that links and forms are disabled
  forms: addBaseHtmlElements(
    `Fetch 3<a href="${REDIRECT_URL}">link</a> -- <form action="${REDIRECT_URL}"><input type="submit"></form>`
  ),

  // #3 This page asserts responses with line breaks work
  lineBreak: addBaseHtmlElements(`
  <a href="#" id="link1">link1</a>
  <a href="#" id="link2">link2</a>
  `),

  // #4 This page asserts that we apply inline styles
  styles: addBaseHtmlElements(`<p style="color: red;">Hello World</p>`),

  // #5 This page asserts that (multiple) Content-Security-Policy headers are applied
  csp: addBaseHtmlElements(`
  <base href="https://example.com/">

  <img src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIW2P4v5ThPwAG7wKklwQ/bwAAAABJRU5ErkJggg==">
  <iframe src="/foo.html"></iframe>
  `),
};

// Use fetch in order to prevent actually running this code in the test page
const TEST_HTML = addBaseHtmlElements(
  `<div id="to-copy">HTML</div><script>` +
    Object.keys(TEST_PAGES)
      .map(name => `fetch("${BASE_URL}fetch-${name}.html");`)
      .join("\n") +
    `</script>`
);
const TEST_URL = BASE_URL + "doc-html-preview.html";

httpServer.registerPathHandler(
  "/doc-html-preview.html",
  (request, response) => {
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.write(TEST_HTML);
  }
);

for (const [name, content] of Object.entries(TEST_PAGES)) {
  httpServer.registerPathHandler(`/fetch-${name}.html`, (request, response) => {
    response.setStatusLine(request.httpVersion, 200, "OK");

    if (name === "csp") {
      // Duplicate un-merged headers
      response.setHeaderNoCheck("Content-Security-Policy", "img-src 'none'");
      response.setHeaderNoCheck("Content-Security-Policy", "base-uri 'self'");
    }

    response.write(content);
  });
}

httpServer.registerPathHandler("/redirect.html", (request, response) => {
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.write("Redirected!");
});

add_task(async function () {
  // Enable async events so that clicks on preview iframe's links are correctly
  // going through the parent process which is meant to cancel any mousedown.
  await pushPref("test.events.async.enabled", true);

  const { monitor } = await initNetMonitor(TEST_URL, { requestCount: 3 });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvent = waitForNetworkEvents(
    monitor,
    1 + Object.keys(TEST_PAGES).length
  );
  await reloadBrowser();
  await onNetworkEvent;

  // The new lines are stripped when using outerHTML to retrieve HTML content of the preview iframe
  await selectIndexAndWaitForHtmlView(0, "initial-page", TEST_HTML);
  let index = 1;
  for (const [name, content] of Object.entries(TEST_PAGES)) {
    await selectIndexAndWaitForHtmlView(index, name, content);
    index++;
  }

  await teardown(monitor);

  async function selectIndexAndWaitForHtmlView(
    index_,
    name,
    expectedHtmlPreview
  ) {
    info(`Select the request "${name}" #${index_}`);
    const onResponseContent = monitor.panelWin.api.once(
      TEST_EVENTS.RECEIVED_RESPONSE_CONTENT
    );
    store.dispatch(Actions.selectRequestByIndex(index_));

    document.querySelector("#response-tab").click();

    const [browser] = await waitForDOM(
      document,
      "#response-panel .html-preview browser"
    );

    await BrowserTestUtils.browserLoaded(browser);

    info("Wait for response content to be loaded");
    await onResponseContent;

    is(
      browser.browsingContext.currentWindowGlobal.isInProcess,
      false,
      "The preview is loaded in a content process"
    );

    await SpecialPowers.spawn(
      browser.browsingContext,
      [expectedHtmlPreview],
      async function (expectedHtml) {
        is(
          content.document.documentElement.outerHTML,
          expectedHtml,
          "The text shown in the browser is incorrect for the html request."
        );
      }
    );

    if (name === "style") {
      await SpecialPowers.spawn(browser.browsingContext, [], async function () {
        const p = content.document.querySelector("p");
        const computed = content.window.getComputedStyle(p);
        is(
          computed.getPropertyValue("color"),
          "rgb(255, 0, 0)",
          "The inline style was not applied"
        );
      });
    }

    if (name == "csp") {
      await SpecialPowers.spawn(browser.browsingContext, [], async function () {
        is(
          content.document.querySelector("img").complete,
          false,
          "img was blocked"
        );
        is(
          content.document.querySelector("iframe").src,
          "/foo.html",
          "URL of iframe was not changed by <base>"
        );
      });
    }

    // Only assert copy to clipboard on the first test page
    if (name == "initial-page") {
      await waitForClipboardPromise(async function () {
        await SpecialPowers.spawn(
          browser.browsingContext,
          [],
          async function () {
            const elt = content.document.getElementById("to-copy");
            EventUtils.synthesizeMouseAtCenter(elt, { clickCount: 2 }, content);
            await new Promise(r =>
              elt.addEventListener("dblclick", r, { once: true })
            );
            EventUtils.synthesizeKey("c", { accelKey: true }, content);
          }
        );
      }, "HTML");
    }
  }
});
