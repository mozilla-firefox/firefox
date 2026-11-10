/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

add_task(async function test_getSelectionDetails_input() {
  // Mostly a regression test for bug 1420560
  const url = kFixtureBaseURL + "file_getSelectionDetails_inputs.html";
  await BrowserTestUtils.withNewTab({ gBrowser, url }, async browser => {
    await SpecialPowers.spawn(browser, [], () => {
      function checkSelection({ id, text, linkURL }) {
        const { SelectionUtils } = ChromeUtils.importESModule(
          "resource://gre/modules/SelectionUtils.sys.mjs"
        );
        content.document.getElementById(id).select();
        // It seems that when running as a test, the previous line will set
        // the both the window's selection and the input's selection to contain
        // the input's text. Outside of tests, only the input's selection seems
        // to be updated, so we explicitly clear the window's selection to
        // ensure we're doing the right thing in the case that only the input's
        // selection is present.
        content.getSelection().removeAllRanges();
        let info = SelectionUtils.getSelectionDetails(content);
        Assert.equal(text, info.text);
        Assert.strictEqual(info.docSelectionIsCollapsed, false);
        Assert.equal(linkURL, info.linkURL);
      }

      checkSelection({
        id: "url-no-scheme",
        text: "test.example.com",
        linkURL: "http://test.example.com/",
      });
      checkSelection({
        id: "url-with-scheme",
        text: "https://test.example.com",
        linkURL: "https://test.example.com/",
      });
      checkSelection({
        id: "not-url",
        text: "foo. bar",
        linkURL: null,
      });
      checkSelection({
        id: "not-url-number",
        text: "3.5",
        linkURL: null,
      });
    });
  });
});

add_task(async function test_getSelectionDetails_shadow_selection() {
  const url = kFixtureBaseURL + "file_getSelectionDetails_inputs.html";
  await BrowserTestUtils.withNewTab({ gBrowser, url }, async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      function checkSelection() {
        const { SelectionUtils } = ChromeUtils.importESModule(
          "resource://gre/modules/SelectionUtils.sys.mjs"
        );

        const text = content.document.getElementById("outer");
        const host = content.document.getElementById("host");
        content
          .getSelection()
          .setBaseAndExtent(
            text,
            0,
            host.shadowRoot.getElementById("inner").firstChild,
            3
          );
        let info = SelectionUtils.getSelectionDetails(content);
        // TODO(sefeng): verify info.text after bug 1881095 is fixed
        Assert.strictEqual(info.docSelectionIsCollapsed, false);
      }
      checkSelection();
    });
  });
});

add_task(async function test_copyLinks_html() {
  const url = `https://example.com/?a=1&b="2"&c='3'&d=<4>`;
  const title = `Q&A <"Quotes" and 'apostrophes'>`;
  BrowserUtils.copyLinks([
    { url, title },
    { url: "https://example.org/", title: null },
  ]);

  let xferable = Cc["@mozilla.org/widget/transferable;1"].createInstance(
    Ci.nsITransferable
  );
  xferable.init(null);
  xferable.addDataFlavor("text/html");
  Services.clipboard.getData(xferable, Ci.nsIClipboard.kGlobalClipboard);
  let data = {};
  xferable.getTransferData("text/html", data);
  let html = data.value.QueryInterface(Ci.nsISupportsString).data;

  Assert.stringContains(
    html,
    `<A HREF="https://example.com/?a=1&amp;b=&quot;2&quot;&amp;c=&#39;3&#39;&amp;d=&lt;4&gt;">` +
      `Q&amp;A &lt;&quot;Quotes&quot; and &#39;apostrophes&#39;&gt;</A><BR>\n` +
      `<A HREF="https://example.org/"></A>`,
    "Should contain the expected HTML anchors"
  );
});

add_task(async function test_getExposableURI() {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.query_stripping.strip_on_share.enabled", true]],
  });
  await Cc["@mozilla.org/query-stripping-list-service;1"]
    .getService(Ci.nsIURLQueryStrippingListService)
    .testWaitForInit();

  const trackedURL =
    "https://example.com/?utm_source=copy&keep=value&utm_medium=share#fragment";
  const cleanURL = "https://example.com/?keep=value#fragment";
  for (const url of [
    trackedURL,
    trackedURL.replace("https://", "https://user:password@"),
    `about:reader?url=${encodeURIComponent(trackedURL)}`,
  ]) {
    Assert.equal(
      BrowserUtils.getExposableURI(Services.io.newURI(url)).displaySpec,
      trackedURL,
      "Keep tracking parameters but remove credentials and Reader Mode wrapping by default"
    );
  }

  for (const [url, expected] of [
    [trackedURL, cleanURL],
    [
      "https://user:password@example.com/?utm_source=copy&keep=value#fragment",
      cleanURL,
    ],
    [`about:reader?url=${encodeURIComponent(trackedURL)}`, cleanURL],
    [cleanURL, cleanURL],
    ["about:robots", "about:robots"],
    ["about:robots?utm_source=copy", "about:robots?utm_source=copy"],
  ]) {
    Assert.equal(
      BrowserUtils.getExposableURI(Services.io.newURI(url), {
        stripTracking: true,
      }).displaySpec,
      expected,
      `Prepare ${url} for copying`
    );
  }

  await SpecialPowers.pushPrefEnv({
    set: [["privacy.query_stripping.strip_on_share.enabled", false]],
  });
  for (const url of [
    trackedURL,
    trackedURL.replace("https://", "https://user:password@"),
    `about:reader?url=${encodeURIComponent(trackedURL)}`,
  ]) {
    Assert.equal(
      BrowserUtils.getExposableURI(Services.io.newURI(url), {
        stripTracking: true,
      }).displaySpec,
      trackedURL,
      "Keep tracking parameters but remove credentials and Reader Mode wrapping when stripping is disabled"
    );
  }
});
