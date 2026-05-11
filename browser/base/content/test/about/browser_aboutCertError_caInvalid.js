/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const BAD_CERT_PAGE = "https://expired.example.com/";
const BAD_CERT_PATH =
  "../../../../../security/manager/ssl/tests/mochitest/browser/revoked.pem";

function pemToBase64(pem) {
  return pem
    .replace(/-----BEGIN CERTIFICATE-----/, "")
    .replace(/-----END CERTIFICATE-----/, "")
    .replace(/\s+/g, "");
}

add_task(async function checkCACertInvalidCopy() {
  const pem = await IOUtils.readUTF8(getTestFilePath(BAD_CERT_PATH));
  const certBase64 = pemToBase64(pem);
  const tab = await openErrorPage(BAD_CERT_PAGE);
  const browser = tab.linkedBrowser;

  await SpecialPowers.spawn(browser, [certBase64], async cert => {
    const mockErrorInfo = {
      errorCodeString: "SEC_ERROR_CA_CERT_INVALID",
      errorIsOverridable: true,
      channelStatus: 0,
      overridableErrorCategory: "trust-error",
      validNotBefore: Date.now() - 1000 * 1000,
      validNotAfter: Date.now() + 1000 * 1000,
      certValidityRangeNotBefore: Date.now() - 1000 * 1000,
      certValidityRangeNotAfter: Date.now() + 1000 * 2000,
      issuerCommonName: "Non-CA Issuer",
      errorMessage: "Issuer certificate is invalid.",
      hasHSTS: false,
      hasHPKP: false,
      certChainStrings: [cert],
    };
    content.document.getFailedCertSecurityInfo = () => mockErrorInfo;

    const netErrorCard =
      content.document.querySelector("net-error-card").wrappedJSObject;
    const info = Cu.cloneInto(mockErrorInfo, netErrorCard);
    netErrorCard.errorInfo = info;
    netErrorCard.resolvedErrorId = "SEC_ERROR_CA_CERT_INVALID";
    netErrorCard.hideExceptionButton = netErrorCard.shouldHideExceptionButton();
    netErrorCard.errorConfig = netErrorCard.getErrorConfig();
    await netErrorCard.getUpdateComplete();

    Assert.equal(
      netErrorCard.errorIntro.dataset.l10nId,
      "fp-certerror-intro",
      "Should use the standard cert error intro."
    );

    netErrorCard.advancedButton.scrollIntoView();
    EventUtils.synthesizeMouseAtCenter(
      netErrorCard.advancedButton,
      {},
      content
    );

    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.isVisible(netErrorCard.advancedContainer),
      "Advanced container is visible"
    );

    Assert.equal(
      netErrorCard.whyDangerous.dataset.l10nId,
      "fp-certerror-invalid-cert-why-dangerous",
      "Should use the invalid CA cert why-dangerous copy."
    );
    Assert.equal(
      netErrorCard.whatCanYouDo.dataset.l10nId,
      "fp-certerror-untrusted-issuer-what-can-you-do-body",
      "Should use the untrusted issuer what-can-you-do copy."
    );
    Assert.equal(
      netErrorCard.learnMoreLink.getAttribute("support-page"),
      "connection-not-secure",
      "'Learn more' link points to the insecure connection errors support page."
    );
    Assert.ok(
      netErrorCard.exceptionButton,
      "Exception button should be present for an overridable cert error."
    );
  });

  await BrowserTestUtils.removeTab(tab);
});
