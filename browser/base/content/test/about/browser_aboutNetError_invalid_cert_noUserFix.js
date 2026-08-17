/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["test.wait300msAfterTabSwitch", true]],
  });
});

const BAD_CERT = "https://expired.example.com/";

add_task(async function checkNoUserFixCertErrors() {
  await setSecurityCertErrorsFeltPrivacyToTrue();
  const tab = await openErrorPage(BAD_CERT);
  const browser = tab.linkedBrowser;

  await SpecialPowers.spawn(browser, [], async function () {
    const noUserFixErrors = [
      "SEC_ERROR_UNKNOWN_CRITICAL_EXTENSION",
      "MOZILLA_PKIX_ERROR_INVALID_INTEGER_ENCODING",
      "MOZILLA_PKIX_ERROR_ISSUER_NO_LONGER_TRUSTED",
      "MOZILLA_PKIX_ERROR_KEY_PINNING_FAILURE",
      "MOZILLA_PKIX_ERROR_SIGNATURE_ALGORITHM_MISMATCH",
      "SEC_ERROR_BAD_DER",
      "SEC_ERROR_BAD_SIGNATURE",
      "SEC_ERROR_CERT_NOT_IN_NAME_SPACE",
      "SEC_ERROR_EXTENSION_VALUE_INVALID",
      "SEC_ERROR_INADEQUATE_CERT_TYPE",
      "SEC_ERROR_INADEQUATE_KEY_USAGE",
      "SEC_ERROR_INVALID_KEY",
      "SEC_ERROR_PATH_LEN_CONSTRAINT_INVALID",
      "SEC_ERROR_UNSUPPORTED_EC_POINT_FORM",
      "SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE",
      "SEC_ERROR_UNSUPPORTED_KEYALG",
      "SEC_ERROR_UNTRUSTED_CERT",
    ];

    content.document.getFailedCertSecurityInfo = () => ({
      errorCodeString: "",
    });

    const netErrorCard =
      content.document.querySelector("net-error-card").wrappedJSObject;

    for (const errorCode of noUserFixErrors) {
      const mockErrorInfo = {
        errorCodeString: errorCode,
        errorIsOverridable: false,
      };
      const info = Cu.cloneInto(mockErrorInfo, netErrorCard);
      netErrorCard.errorInfo = info;
      netErrorCard.resolvedErrorId = errorCode;
      netErrorCard.errorConfig = netErrorCard.getErrorConfig();
      netErrorCard.advancedShowing = false;
      netErrorCard.hideExceptionButton = netErrorCard.shouldHideExceptionButton(
        info.errorCodeString
      );
      netErrorCard.showCustomNetErrorCard = false;
      netErrorCard.requestUpdate();
      await netErrorCard.getUpdateComplete();

      const advancedButton = netErrorCard.advancedButton;
      advancedButton.scrollIntoView(true);
      EventUtils.synthesizeMouseAtCenter(advancedButton, {}, content);

      await ContentTaskUtils.waitForCondition(
        () => netErrorCard.advancedContainer,
        `Advanced section should be rendered for ${errorCode}.`
      );
      await ContentTaskUtils.waitForCondition(
        () => netErrorCard.whyDangerous,
        `The 'Why Dangerous' copy should be rendered for ${errorCode}.`
      );
      const l10nId = netErrorCard.getNSSErrorWhyDangerousL10nId(errorCode);

      Assert.ok(
        netErrorCard.advancedShowing,
        `Advanced details are shown for ${errorCode}.`
      );
      Assert.ok(
        !netErrorCard.exceptionButton,
        `Proceed button should not be shown for ${errorCode}.`
      );
      Assert.notEqual(
        netErrorCard.whyDangerous.innerHTML.trim(),
        "",
        `Advanced string exists for ${errorCode}.`
      );
      Assert.equal(
        netErrorCard.whyDangerous.dataset.l10nId,
        l10nId,
        `Using the correct copy for ${errorCode}.`
      );
    }
  });

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});
