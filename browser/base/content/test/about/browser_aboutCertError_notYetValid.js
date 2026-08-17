/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const BAD_CERT_PAGE = "https://expired.example.com";
const NOT_YET_VALID_CERT_PATH =
  "../../../../../security/manager/ssl/tests/mochitest/browser/ca.pem";

function pemToBase64(pem) {
  return pem
    .replace(/-----BEGIN CERTIFICATE-----/, "")
    .replace(/-----END CERTIFICATE-----/, "")
    .replace(/\s+/g, "");
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["test.wait300msAfterTabSwitch", true],
      ["security.certerrors.felt-privacy-v1", true],
    ],
  });
});

add_task(async function testNotYetValidCert() {
  const pem = await IOUtils.readUTF8(getTestFilePath(NOT_YET_VALID_CERT_PATH));
  const certBase64 = pemToBase64(pem);
  const tab = await openErrorPage(BAD_CERT_PAGE);
  const browser = tab.linkedBrowser;

  await SpecialPowers.spawn(browser, [certBase64], async cert => {
    const mockErrorInfo = {
      errorCodeString: "MOZILLA_PKIX_ERROR_NOT_YET_VALID_CERTIFICATE",
      errorIsOverridable: true,
      channelStatus: 0,
      overridableErrorCategory: "expired-or-not-yet-valid",
      validNotBefore: Date.now() + 1000 * 1000,
      validNotAfter: Date.now() + 1000 * 2000,
      certValidityRangeNotBefore: Date.now() + 1000 * 1000,
      certValidityRangeNotAfter: Date.now() + 1000 * 2000,
      issuerCommonName: "Test CA",
      errorMessage: "The server presented a certificate that is not yet valid.",
      hasHSTS: false,
      hasHPKP: false,
      certChainStrings: [cert],
    };

    content.document.getFailedCertSecurityInfo = () => mockErrorInfo;

    const netErrorCard =
      content.document.querySelector("net-error-card").wrappedJSObject;
    const info = Cu.cloneInto(mockErrorInfo, netErrorCard);
    netErrorCard.errorInfo = info;
    netErrorCard.resolvedErrorId =
      "MOZILLA_PKIX_ERROR_NOT_YET_VALID_CERTIFICATE";
    netErrorCard.errorConfig = netErrorCard.getErrorConfig();
    netErrorCard.hideExceptionButton = netErrorCard.shouldHideExceptionButton();
    await netErrorCard.getUpdateComplete();

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

    Assert.ok(
      netErrorCard.advancedShowing,
      "Advanced details are shown for not-yet-valid certificates."
    );
    Assert.ok(
      netErrorCard.exceptionButton,
      "Proceed button should be shown for not-yet-valid certificates."
    );
    Assert.equal(
      netErrorCard.errorIntro.dataset.l10nId,
      "fp-certerror-intro",
      "Using the 'certificate error' intro."
    );
    Assert.equal(
      netErrorCard.whyDangerous.dataset.l10nId,
      "fp-certerror-pkix-not-yet-valid-why-dangerous-body",
      "Using the 'not yet valid' variant of the 'Why Dangerous' copy."
    );
    Assert.equal(
      netErrorCard.whatCanYouDo.dataset.l10nId,
      "fp-certerror-pkix-not-yet-valid-what-can-you-do-body",
      "Using the 'not yet valid' variant of the 'What can you do' copy."
    );
    Assert.equal(
      netErrorCard.learnMoreLink.getAttribute("support-page"),
      "time-errors",
      "'Learn more' link points to the time-related errors support page."
    );
  });

  await BrowserTestUtils.removeTab(tab);
});
