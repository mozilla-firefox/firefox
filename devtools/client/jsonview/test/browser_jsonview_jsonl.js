/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

function getRowsCount() {
  return getElementCount(".jsonPanelBox .treeTable .treeRow");
}

add_task(async function testContentTypeSniffing() {
  info("Test JSONL content-type sniffing started");

  for (const type of [
    "application/jsonl",
    "application/jsonlines",
    "application/x-ndjson",
    "text/jsonl",
  ]) {
    const TEST_URL = `data:${type},${encodeURIComponent('{"a":1}\n{"b":2}\n')}`;
    const tab = await addJsonViewTab(TEST_URL);

    await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
      ok(
        content.wrappedJSObject.JSONView.isJsonlines,
        "JSONView.isJsonlines is exposed and true"
      );
    });

    BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function testLineByLineParsing() {
  info("Test JSONL line-by-line parsing started");

  // All three parsed lines are primitives (no children), so the row
  // count is exactly the number of non-blank lines — no auto-expand
  // of nested children to account for.
  const content = ["1", "", '"three"', "not json", ""].join("\n");
  const TEST_URL = `data:application/jsonl,${encodeURIComponent(content)}`;
  const tab = await addJsonViewTab(TEST_URL);

  is(await getRowsCount(), 3, "One row per non-blank line");
  is(await getRowText(0), `0: 1`, "Line 1 parsed as a number");
  is(await getRowText(1), `1: "three"`, "Line 3 parsed as a string");
  // Line 4 ("not json") could not be parsed as JSON; it's the 3rd row
  // (blank lines 2 and 5 add no row).
  const row3Label = await getElementText(
    `.jsonPanelBox .treeTable .treeRow:nth-of-type(3) .treeLabelCell`
  );
  is(row3Label, "2", "Line 4 (invalid JSON) still gets its own row");

  const row3Error = await getElementText(
    `.jsonPanelBox .treeTable .treeRow:nth-of-type(3) .jsonlLineError`
  );
  ok(
    row3Error.includes("not json"),
    "The invalid line's raw text is shown in its error row"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function testNoPrettyPrintForJsonl() {
  info("Test 'Pretty Print' button is hidden for JSONL started");

  const jsonl = '{"a":1}\n{"b":2}\n';
  const TEST_URL = `data:application/jsonl,${encodeURIComponent(jsonl)}`;
  const tab = await addJsonViewTab(TEST_URL);

  await selectJsonViewContentTab("rawdata");
  is(
    await getElementCount(".textPanelBox .toolbar button.prettyprint"),
    0,
    "There must be no pretty-print button for JSONL documents"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function testSaveAsRestoresJsonlContentType() {
  info("Test JSONL Save As started");

  const useDownloadDir = SpecialPowers.getBoolPref(
    "browser.download.useDownloadDir"
  );
  SpecialPowers.setBoolPref("browser.download.useDownloadDir", false);
  const { MockFilePicker } = SpecialPowers;
  MockFilePicker.init();
  MockFilePicker.returnValue = MockFilePicker.returnCancel;

  const jsonl = '{"a":1}\n{"b":2}\n';
  // Whichever spelling the document was served with is restored as-is.
  for (const type of [
    "application/jsonl",
    "application/x-ndjson",
    "text/jsonl",
  ]) {
    const TEST_URL = `data:${type},${encodeURIComponent(jsonl)}`;
    const tab = await addJsonViewTab(TEST_URL);

    await SpecialPowers.spawn(tab.linkedBrowser, [type], expectedType => {
      is(
        content.document.contentType,
        expectedType,
        "Save-As sees the original content type restored, not the internal one"
      );
    });

    BrowserTestUtils.removeTab(tab);
  }

  MockFilePicker.cleanup();
  SpecialPowers.setBoolPref("browser.download.useDownloadDir", useDownloadDir);
});

add_task(async function testExtensionFallback() {
  info("Test .jsonl extension fallback started");

  const TEST_URL = URL_ROOT + "jsonl_no_contenttype.jsonl";
  const tab = await addJsonViewTab(TEST_URL);

  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    ok(
      content.wrappedJSObject.JSONView.isJsonlines,
      "JSONView.isJsonlines is exposed and true for a .jsonl file " +
        "served with a non-matching content type"
    );
    is(
      content.document.contentType,
      "application/jsonl",
      "Save-As falls back to the standard type when the original doesn't match"
    );
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function testExplicitContentTypeIsHonored() {
  info("Test .jsonl served with an explicit non-JSON content type started");

  const TEST_URL = URL_ROOT + "jsonl_html_contenttype.jsonl";
  const tab = await addTab(TEST_URL);

  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    is(
      content.document.contentType,
      "text/html",
      "A .jsonl URL served as text/html keeps its declared content type"
    );
    ok(
      !content.wrappedJSObject.JSONView,
      "The JSON Viewer did not take over the document"
    );
    is(
      content.document.querySelector("p")?.textContent,
      "This is an HTML document, not JSON Lines.",
      "The HTML document is rendered as a regular page"
    );
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function testLocalFile() {
  info("Test local .jsonl file started");

  // A file:// load has no server-provided type: the extension decides.
  const dir = getChromeDir(getResolvedURI(gTestPath));
  dir.append("jsonl_no_contenttype.jsonl");
  dir.normalize();
  const TEST_URL = Services.io.newFileURI(dir).spec;
  const tab = await addJsonViewTab(TEST_URL);

  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    ok(
      content.wrappedJSObject.JSONView.isJsonlines,
      "A local .jsonl file is opened by the JSON Viewer as JSON Lines"
    );
  });

  BrowserTestUtils.removeTab(tab);
});
