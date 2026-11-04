/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that Security details tab contains the expected data.
 */

add_task(async function () {
  await pushPref("security.pki.certificate_transparency.mode", 1);

  const { tab, monitor } = await initNetMonitor(CUSTOM_GET_URL, {
    requestCount: 1,
  });
  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  info("Performing a secure request.");
  const REQUESTS_URL = "https://example.com" + CORS_SJS_PATH;
  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [REQUESTS_URL],
    async function (url) {
      content.wrappedJSObject.performRequests(1, url);
    }
  );
  await onNetworkEvents;

  info("Wait until the Security Tab is visible");
  const waitForSecurityTab = waitForDOM(document, "#security-tab");
  store.dispatch(Actions.toggleNetworkDetails());
  await waitForSecurityTab;

  info("Selecting the Security Tab");
  clickOnSidebarTab(document, "security");
  await waitUntil(() =>
    document.querySelector("#security-panel .security-info-value")
  );

  const tabpanel = document.querySelector("#security-panel");
  const securityInfoNames = [
    ...tabpanel.querySelectorAll(".treeLabelCell .treeLabel"),
  ]
    // Filter out the titles
    .filter(el => {
      const ignoreList = [
        "Connection:",
        "Host example.com:",
        "Certificate:",
        "Issued To",
        "Issued By",
        "Period of Validity",
        "Fingerprints",
      ];
      return !ignoreList.includes(el.innerText);
    });

  const securtyInfoValues = [
    ...tabpanel.querySelectorAll(".security-info-value"),
  ];
  // The expected security values, this should match the other they are listed in the
  // DOM.
  const securityValues = [
    // Connection
    // The protocol will be TLS but the exact version depends on which protocol
    // the test server example.com supports.
    { name: "Protocol version:", startsWith: '"TLS' }, // e.g  TLSv1.3
    { name: "Cipher suite:", startsWith: '"TLS_' }, // e.g TLS_AES_128_GCM_SHA256"
    // These values can change. So only check they're not empty.
    { name: "Key Exchange Group:", checkIsNotEmpty: true }, // e.g mlkem768x25519
    { name: "Signature Scheme:", checkIsNotEmpty: true }, // e.g RSA-PSS-SHA256

    { name: "Used Encrypted Client Hello (ECH):", value: "false" },
    { name: "Used Delegated Credentials:", value: "false" },
    { name: "Used Online Certificate Status Protocol (OCSP):", value: "false" },
    { name: "Used Private DNS:", value: "false" },
    // ---------------------------------------------------------------
    // Host
    { name: "HTTP Strict Transport Security:", value: '\"Disabled\"' },
    { name: "Public Key Pinning:", value: '\"Disabled\"' },
    // ---------------------------------------------------------------
    // Certificate
    // > Issued To
    { name: "Common Name (CN):", value: '"example.com"' },
    { name: "Organization (O):", value: '"<Not Available>"' },
    { name: "Organizational Unit (OU):", value: '"<Not Available>"' },
    // > Issued By
    { name: "Common Name (CN):", value: '"Temporary Certificate Authority"' },
    { name: "Organization (O):", value: '"Mozilla Testing"' },
    {
      name: "Organizational Unit (OU):",
      value: '"Profile Guided Optimization"',
    },
    // > Period of Validity
    // Locale sensitive and varies between timezones. Can't compare equality or
    // the test fails depending on which part of the world the test is executed.
    { name: "Begins On:", checkIsNotEmpty: true }, // e.g "Wed, 27 Nov 2024 00:00:00 GMT"
    { name: "Expires On:", checkIsNotEmpty: true }, // e.g "Fri, 05 Feb 2027 00:00:00 GMT"
    // > Fingerprints
    // These values can change. So only check they're not empty.
    { name: "SHA-256 Fingerprint:", checkIsNotEmpty: true }, // e.g "B1:3E:BB:AF:DE:F5:CE:BC:53:44:F5:50:59:2D:30:DB:A8:7E:CF:82:20:7A:8D:44:65:EA:C5:1B:2D:EE:F8:CC"
    { name: "SHA1 Fingerprint:", checkIsNotEmpty: true }, // e.g "4F:64:97:E1:16:29:26:C8:88:0E:78:0F:DE:D0:9A:5B:71:58:65:C1"
    // > Transparency
    { name: "Transparency:", checkIsNotEmpty: true }, // e.g <Not Available>
    // ---------------------------------------------------------------
  ];

  for (const [index, item] of securityValues.entries()) {
    const actualName = securityInfoNames[index].textContent;
    const actualValue = securtyInfoValues[index].textContent;
    is(
      actualName,
      item.name,
      "The security property name `" + actualName + "` is correct"
    );
    if (item.checkIsNotEmpty) {
      isnot(
        actualValue,
        "",
        "The value of `" + actualValue + "` is not empty."
      );
    } else if (item.startsWith) {
      ok(
        actualValue.startsWith(item.startsWith),
        "The `" +
          actualValue +
          "` starts with the value `" +
          item.startsWith +
          "` which is valid."
      );
    } else if (item.value) {
      is(
        actualValue,
        item.value,
        "The " + actualName + " has the expected value."
      );
    }
  }

  // Check the host is correctly displayed
  is(
    tabpanel.querySelectorAll(".treeLabel.objectLabel")[1].textContent,
    "Host example.com:",
    "The 'Host' label has the expected value."
  );

  await teardown(monitor);
});
