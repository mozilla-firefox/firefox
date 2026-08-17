/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const BAD_CERT = "https://expired.example.com/";
const REVOKED_CERT_PATH =
  "../../../../../security/manager/ssl/tests/mochitest/browser/revoked.pem";

function pemToBase64(pem) {
  return pem
    .replace(/-----BEGIN CERTIFICATE-----/, "")
    .replace(/-----END CERTIFICATE-----/, "")
    .replace(/\s+/g, "");
}

// The revoked certificate page should not offer a proceed button.
add_task(async function checkRevokedCertificateAdvancedCopy() {
  await setSecurityCertErrorsFeltPrivacyToTrue();

  const revokedPem = await IOUtils.readUTF8(getTestFilePath(REVOKED_CERT_PATH));
  const revokedCertBase64 = pemToBase64(revokedPem);

  const tab = await openErrorPage(BAD_CERT);
  const browser = tab.linkedBrowser;

  await SpecialPowers.spawn(
    browser,
    [revokedCertBase64],
    async function (revokedCert_Base64) {
      const mockErrorInfo = {
        errorCodeString: "SEC_ERROR_REVOKED_CERTIFICATE",
        errorIsOverridable: false,
        channelStatus: 0,
        overridableErrorCategory: "trust-error",
        validNotBefore: Date.now() - 1000,
        validNotAfter: Date.now() + 1000,
        certValidityRangeNotAfter: Date.now() + 1000,
        certValidityRangeNotBefore: Date.now() - 1000,
        issuerCommonName: "ca",
        errorMessage: "Peer's Certificate has been revoked.",
        hasHSTS: false,
        hasHPKP: false,
        certChainStrings: [revokedCert_Base64],
      };

      content.document.getFailedCertSecurityInfo = () => mockErrorInfo;

      const netErrorCard =
        content.document.querySelector("net-error-card").wrappedJSObject;
      const info = Cu.cloneInto(mockErrorInfo, netErrorCard);
      netErrorCard.errorInfo = info;
      netErrorCard.resolvedErrorId = "SEC_ERROR_REVOKED_CERTIFICATE";
      netErrorCard.hostname = "revoked.example.com";
      netErrorCard.domainMismatchNames = null;
      netErrorCard.domainMismatchNamesPromise = null;
      netErrorCard.certificateErrorText = null;
      netErrorCard.certificateErrorTextPromise = null;
      netErrorCard.errorConfig = netErrorCard.getErrorConfig();
      netErrorCard.hideExceptionButton = netErrorCard.shouldHideExceptionButton(
        info.errorCodeString
      );
      netErrorCard.requestUpdate();
      await netErrorCard.getUpdateComplete();

      const advancedButton = netErrorCard.advancedButton;
      advancedButton.scrollIntoView(true);
      EventUtils.synthesizeMouseAtCenter(advancedButton, {}, content);

      await ContentTaskUtils.waitForCondition(
        () => netErrorCard.advancedContainer,
        "Advanced section should be rendered for revoked certificate"
      );
      await ContentTaskUtils.waitForCondition(
        () => netErrorCard.whyDangerous && netErrorCard.whatCanYouDo,
        "Revoked copy should be rendered"
      );

      Assert.ok(
        netErrorCard.advancedShowing,
        "Advanced details are shown for revoked certificates."
      );
      Assert.ok(
        !netErrorCard.exceptionButton,
        "Proceed button should not be shown for revoked certificates."
      );
      Assert.equal(
        netErrorCard.whyDangerous.dataset.l10nId,
        "fp-certerror-revoked-why-dangerous-body",
        "Using the 'revoked' variant of the 'Why Dangerous' copy."
      );
      Assert.equal(
        netErrorCard.whatCanYouDo.dataset.l10nId,
        "fp-certerror-revoked-what-can-you-do-body",
        "Using the 'revoked' variant of the 'What can you do' copy."
      );
      Assert.equal(
        netErrorCard.learnMoreLink.getAttribute("support-page"),
        "connection-not-secure",
        "'Learn more' link points to the standard support page."
      );
    }
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});
