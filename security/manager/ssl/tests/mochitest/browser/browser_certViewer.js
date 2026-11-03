/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const { getCertViewerUrl } = ChromeUtils.importESModule(
  "moz-src:///security/manager/pki/pippki.sys.mjs"
);

// Repeatedly opens the certificate viewer dialog with various certificates and
// determines that the viewer correctly identifies either what usages those
// certificates are valid for or what errors prevented the certificates from
// being verified.

add_task(async function testCAandTitle() {
  let cert = await readCertificate("ca.pem", "CTu,CTu,CTu");
  let url = await getCertViewerUrl(cert);
  await openCertViewerAndCheckTabName(url, "ca");
});

add_task(async function testSSLEndEntity() {
  let cert = await readCertificate("ssl-ee.pem", ",,");
  let url = await getCertViewerUrl(cert);
  await openCertViewerAndCheckTabName(url, "ssl-ee");
});

add_task(async function testEmailEndEntity() {
  let cert = await readCertificate("email-ee.pem", ",,");
  let url = await getCertViewerUrl(cert);
  await openCertViewerAndCheckTabName(url, "email-ee");
});

add_task(async function testCodeSignEndEntity() {
  let cert = await readCertificate("code-ee.pem", ",,");
  let url = await getCertViewerUrl(cert);
  await openCertViewerAndCheckTabName(url, "code-ee");
});

add_task(async function testExpired() {
  let cert = await readCertificate("expired-ca.pem", ",,");
  let url = await getCertViewerUrl(cert);
  await openCertViewerAndCheckTabName(url, "expired-ca");
});

add_task(async function testUntrusted() {
  let cert = await readCertificate("untrusted-ca.pem", "p,p,p");
  let url = await getCertViewerUrl(cert);
  await openCertViewerAndCheckTabName(url, "untrusted-ca");
});

add_task(async function testInvalid() {
  // This certificate has a keyUsage extension asserting cRLSign and
  // keyCertSign, but it doesn't have a basicConstraints extension. This
  // shouldn't be valid for any usage. Sadly, we give a pretty bad error
  // message in this case.
  let cert = await readCertificate("invalid.pem", ",,");
  let url = await getCertViewerUrl(cert);
  await openCertViewerAndCheckTabName(url, "invalid");
});

add_task(async function testLongOID() {
  // This certificate has a certificatePolicies extension with a policy with a
  // very long OID. This tests that we don't crash when looking at it.
  let cert = await readCertificate("longOID.pem", ",,");
  let url = await getCertViewerUrl(cert);
  await openCertViewerAndCheckTabName(url, "Long OID");
});

/**
 * Given an certificate URL, opens the new certificate viewer and check
 * if a certain element exists, with its expected result.
 *
 * @param {string} url
 *        The URL with the certificate info
 * @param {string} expectedTabName
 *        The expected name of the tab in the certificate viewer
 */
async function openCertViewerAndCheckTabName(url, expectedTabName) {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url },
    async function (browser) {
      await SpecialPowers.spawn(
        browser,
        [expectedTabName],
        async function (expectedTabName) {
          let certificateSection = await ContentTaskUtils.waitForCondition(
            () => {
              return content.document.querySelector("certificate-section");
            },
            "Certificate section found"
          );
          let tabName =
            certificateSection.shadowRoot.querySelector(
              ".tab[idnumber='0']"
            ).textContent;
          Assert.equal(tabName, expectedTabName);
        }
      );
    }
  );
}
