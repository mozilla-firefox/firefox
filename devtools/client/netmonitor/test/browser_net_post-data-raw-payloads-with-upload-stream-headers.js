/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if the POST requests display the correct information in the UI,
 * for raw payloads with content-type headers attached to the upload stream.
 */
add_task(async function () {
  const {
    L10N,
  } = require("resource://devtools/client/netmonitor/src/utils/l10n.js");

  const { tab, monitor } = await initNetMonitor(POST_RAW_WITH_HEADERS_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  // Execute requests.
  await performRequests(monitor, tab, 3);

  const expectedRequestsContent = [
    {
      headersFromUploadSectionTitle:
        "Request headers from upload stream (47 B)",
      uploadSectionHeaders: [
        { label: "content-type", value: "application/x-www-form-urlencoded" },
      ],
      uploadSectionRawText: "content-type: application/x-www-form-urlencoded",
      requestPanelFormData: [
        { label: "foo", value: '"bar"' },
        { label: "baz", value: '"123"' },
      ],
      requestPanelPayload: [
        "content-type: application/x-www-form-urlencoded",
        "foo=bar&baz=123",
      ],
    },
    {
      headersFromUploadSectionTitle:
        "Request headers from upload stream (47 B)",
      uploadSectionHeaders: [
        { label: "content-type", value: "application/x-www-form-urlencoded" },
      ],
      uploadSectionRawText: "content-type: application/x-www-form-urlencoded",
      requestPanelPayload: ["content-type: application/x-www-form-urlencoded"],
    },
    {
      headersFromUploadSectionTitle:
        "Request headers from upload stream (74 B)",
      uploadSectionHeaders: [
        { label: "content-type", value: "application/x-www-form-urlencoded" },
        { label: "custom-header", value: "hello world!" },
      ],
      uploadSectionRawText:
        "content-type: application/x-www-form-urlencoded\r\ncustom-header: hello world!",
      requestPanelFormData: [
        { label: "foo", value: '"bar"' },
        { label: "baz", value: '"123"' },
      ],
      requestPanelPayload: [
        "content-type: application/x-www-form-urlencoded",
        "custom-header: hello world!",
        "foo=bar&baz=123",
      ],
    },
  ];

  const requests = document.querySelectorAll(".request-list-item");
  store.dispatch(Actions.toggleNetworkDetails());

  for (let i = 0; i < expectedRequestsContent.length; i++) {
    clickOnRequestRow(requests[i]);
    await assertRequestContentInHeaderAndRequestSidePanels(
      expectedRequestsContent[i]
    );
  }

  async function assertRequestContentInHeaderAndRequestSidePanels(expected) {
    // Wait for all 3 headers sections to load (Response Headers, Request Headers, Request headers from upload stream)
    let onDomUpdated = waitForDOM(
      document,
      "#headers-panel .accordion-item",
      3
    );
    clickOnSidebarTab(document, "headers");
    await onDomUpdated;

    let tabpanel = document.querySelector("#headers-panel");
    is(
      tabpanel.querySelectorAll(".accordion-item").length,
      3,
      "There should be 3 header sections displayed in this tabpanel."
    );

    info("Check that the Headers in the upload stream section are correct.");
    is(
      tabpanel.querySelectorAll(".accordion-item .accordion-header-label")[2]
        .textContent,
      expected.headersFromUploadSectionTitle,
      "The request headers from upload section doesn't have the correct title."
    );

    let labels = tabpanel.querySelectorAll(
      ".accordion-item:last-child .accordion-content tr .treeLabelCell .treeLabel"
    );
    let values = tabpanel.querySelectorAll(
      ".accordion-item:last-child .accordion-content tr .treeValueCell .objectBox"
    );

    for (let i = 0; i < labels.length; i++) {
      is(
        labels[i].textContent,
        expected.uploadSectionHeaders[i].label,
        "The request header name was incorrect."
      );
      is(
        values[i].textContent,
        expected.uploadSectionHeaders[i].value,
        "The request header value was incorrect."
      );
    }

    info(
      "Toggle to open the raw view for the request headers from upload stream"
    );

    onDomUpdated = waitForDOM(
      tabpanel,
      ".accordion-item:last-child .accordion-content .raw-headers-container"
    );

    tabpanel
      .querySelector("#raw-requestHeadersFromUploadStream-checkbox")
      .click();
    await onDomUpdated;

    const rawTextArea = tabpanel.querySelector(
      ".accordion-item:last-child .accordion-content .raw-headers"
    );
    is(
      rawTextArea.textContent,
      expected.uploadSectionRawText,
      "The raw text for the request headers from upload section is correct"
    );

    info("Switch to the Request panel");

    onDomUpdated = waitForDOM(document, "#request-panel .panel-container");
    clickOnSidebarTab(document, "request");
    await onDomUpdated;

    tabpanel = document.querySelector("#request-panel");
    if (expected.requestPanelFormData) {
      await waitUntil(
        () =>
          tabpanel.querySelector(".data-label").textContent ==
          L10N.getStr("paramsFormData")
      );

      labels = tabpanel.querySelectorAll("tr .treeLabelCell .treeLabel");
      values = tabpanel.querySelectorAll("tr .treeValueCell .objectBox");

      for (let i = 0; i < labels.length; i++) {
        is(
          labels[i].textContent,
          expected.requestPanelFormData[i].label,
          "The form data param name was incorrect."
        );
        is(
          values[i].textContent,
          expected.requestPanelFormData[i].value,
          "The form data param value was incorrect."
        );
      }

      info("Toggle open the the request payload raw view");

      tabpanel.querySelector("#raw-request-checkbox").click();
    }
    await waitUntil(
      () =>
        tabpanel.querySelector(".data-label").textContent ==
          L10N.getStr("paramsPostPayload") &&
        tabpanel.querySelector(
          ".panel-container .editor-row-container .cm-content"
        )
    );

    // Check that the expected header lines are included in the codemirror
    // text.
    const actualText = tabpanel.querySelector(
      ".panel-container .editor-row-container .cm-content"
    ).textContent;
    const requestPayloadIsCorrect = expected.requestPanelPayload.every(
      content => actualText.includes(content)
    );

    is(requestPayloadIsCorrect, true, "The request payload is not correct");
  }

  return teardown(monitor);
});
