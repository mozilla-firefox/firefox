/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// This is testing the aboutCertError page (Bug 1207107).

const GOOD_PAGE = "https://example.com/";
const GOOD_PAGE_2 = "https://example.org/";
const BAD_CERT = "https://expired.example.com/";
const UNKNOWN_ISSUER = "https://self-signed.example.com";
const BAD_STS_CERT =
  "https://badchain.include-subdomains.pinning.example.com:443";
const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

// Security CertError Felt Privacy set to false & true
// checked in one checkReturnToAboutHome to avoid duplicating code
add_task(async function checkReturnToAboutHome() {
  info(
    "Loading a bad cert page directly and making sure 'return to previous page' goes to about:home"
  );
  for (let toggleFeltPrivacy of [
    setSecurityCertErrorsFeltPrivacyToFalse,
    setSecurityCertErrorsFeltPrivacyToTrue,
  ]) {
    await toggleFeltPrivacy();

    for (let useFrame of [false, true]) {
      let tab = await openErrorPage(BAD_CERT, useFrame);
      let browser = tab.linkedBrowser;
      await SpecialPowers.spawn(browser, [], () => {
        content.document.notifyUserGestureActivation();
      });

      is(browser.webNavigation.canGoBack, false, "!webNavigation.canGoBack");
      is(
        browser.webNavigation.canGoForward,
        false,
        "!webNavigation.canGoForward"
      );

      // Populate the shistory entries manually, since it happens asynchronously
      // and the following tests will be too soon otherwise.
      await TabStateFlusher.flush(browser);
      let { entries } = JSON.parse(SessionStore.getTabState(tab));
      is(entries.length, 1, "there is one shistory entry");

      info("Clicking the go back button on about:certerror");
      let bc = browser.browsingContext;
      if (useFrame) {
        bc = bc.children[0];
      }
      let locationChangePromise = BrowserTestUtils.waitForLocationChange(
        gBrowser,
        "about:home"
      );

      if (Services.prefs.getBoolPref("security.certerrors.felt-privacy-v1")) {
        info("Felt Privacy enabled - using net-error-card");

        await SpecialPowers.spawn(bc, [useFrame], async function (subFrame) {
          const netErrorCard = await ContentTaskUtils.waitForCondition(
            () =>
              content.document.querySelector("net-error-card")?.wrappedJSObject,
            "Waiting for net-error-card"
          );
          await netErrorCard.getUpdateComplete();
          const returnButton = netErrorCard.returnButton;

          if (!subFrame) {
            if (!Services.focus.focusedElement == returnButton) {
              await ContentTaskUtils.waitForEvent(returnButton, "focus");
            }
            Assert.ok(true, "returnButton has focus");
          }
          // Note that going back to about:newtab might cause a process flip, if
          // the browser is configured to run about:newtab in its own special
          // content process.
          returnButton.scrollIntoView(true);
          EventUtils.synthesizeMouseAtCenter(returnButton, {}, content);
        });
      } else {
        info("Felt Privacy disabled - using aboutNetError");
        await SpecialPowers.spawn(bc, [useFrame], async function (subFrame) {
          let returnButton = content.document.getElementById("returnButton");
          if (!subFrame) {
            if (!Services.focus.focusedElement == returnButton) {
              await ContentTaskUtils.waitForEvent(returnButton, "focus");
            }
            Assert.ok(true, "returnButton has focus");
          }
          // Note that going back to about:newtab might cause a process flip, if
          // the browser is configured to run about:newtab in its own special
          // content process.
          returnButton.click();
        });
      }

      await locationChangePromise;

      is(browser.webNavigation.canGoBack, true, "webNavigation.canGoBack");
      is(
        browser.webNavigation.canGoForward,
        false,
        "!webNavigation.canGoForward"
      );
      is(gBrowser.currentURI.spec, "about:home", "Went back");

      BrowserTestUtils.removeTab(gBrowser.selectedTab);
    }
    await SpecialPowers.popPrefEnv();
  }
});

// Security CertError Felt Privacy set to false only
add_task(async function checkReturnToPreviousPage_feltPrivacyToFalse() {
  await setSecurityCertErrorsFeltPrivacyToFalse();
  info(
    "Loading a bad cert page and making sure 'return to previous page' goes back"
  );
  for (let useFrame of [false, true]) {
    let tab;
    let browser;
    if (useFrame) {
      tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, GOOD_PAGE);
      browser = tab.linkedBrowser;
      await SpecialPowers.spawn(browser, [], () => {
        content.document.notifyUserGestureActivation();
      });

      BrowserTestUtils.startLoadingURIString(browser, GOOD_PAGE_2);
      await BrowserTestUtils.browserLoaded(browser, false, GOOD_PAGE_2);
      await injectErrorPageFrame(tab, BAD_CERT);
    } else {
      tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, GOOD_PAGE);
      browser = gBrowser.selectedBrowser;
      await SpecialPowers.spawn(browser, [], () => {
        content.document.notifyUserGestureActivation();
      });

      info("Loading and waiting for the cert error");
      let certErrorLoaded = BrowserTestUtils.waitForErrorPage(browser);
      BrowserTestUtils.startLoadingURIString(browser, BAD_CERT);
      await certErrorLoaded;
    }

    is(browser.webNavigation.canGoBack, true, "webNavigation.canGoBack");
    is(
      browser.webNavigation.canGoForward,
      false,
      "!webNavigation.canGoForward"
    );

    // Populate the shistory entries manually, since it happens asynchronously
    // and the following tests will be too soon otherwise.
    await TabStateFlusher.flush(browser);
    let { entries } = JSON.parse(SessionStore.getTabState(tab));
    is(entries.length, 2, "there are two shistory entries");

    info("Clicking the go back button on about:certerror");
    let bc = browser.browsingContext;
    if (useFrame) {
      bc = bc.children[0];
    }

    let pageShownPromise = BrowserTestUtils.waitForContentEvent(
      browser,
      "pageshow",
      true
    );
    await SpecialPowers.spawn(bc, [useFrame], async function () {
      let returnButton = content.document.getElementById("returnButton");
      returnButton.click();
    });
    await pageShownPromise;

    is(browser.webNavigation.canGoBack, false, "!webNavigation.canGoBack");
    is(browser.webNavigation.canGoForward, true, "webNavigation.canGoForward");
    is(gBrowser.currentURI.spec, GOOD_PAGE, "Went back");

    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
});

// Works for both Security CertError Felt Privacy set to true and false
// This checks that the appinfo.appBuildID starts with a date string,
// which is required for the misconfigured system time check.
add_task(async function checkAppBuildIDIsDate() {
  let appBuildID = Services.appinfo.appBuildID;
  let year = parseInt(appBuildID.substr(0, 4), 10);
  let month = parseInt(appBuildID.substr(4, 2), 10);
  let day = parseInt(appBuildID.substr(6, 2), 10);

  Assert.ok(year >= 2016 && year <= 2100, "appBuildID contains a valid year");
  Assert.ok(month >= 1 && month <= 12, "appBuildID contains a valid month");
  Assert.ok(day >= 1 && day <= 31, "appBuildID contains a valid day");
});

add_task(async function checkAdvancedDetails_feltPrivacyToFalse() {
  await setSecurityCertErrorsFeltPrivacyToFalse();
  info(
    "Loading a bad cert page and verifying the main error and advanced details section"
  );
  for (let useFrame of [false, true]) {
    let tab = await openErrorPage(BAD_CERT, useFrame);
    let browser = tab.linkedBrowser;

    let bc = browser.browsingContext;
    if (useFrame) {
      bc = bc.children[0];
    }

    let message = await SpecialPowers.spawn(bc, [], async function () {
      let doc = content.document;

      const shortDesc = doc.getElementById("errorShortDesc");
      const sdArgs = JSON.parse(shortDesc.dataset.l10nArgs);
      is(
        sdArgs.hostname,
        "expired.example.com",
        "Should list hostname in error message."
      );

      Assert.ok(
        doc.getElementById("certificateErrorDebugInformation").hidden,
        "Debug info is initially hidden"
      );

      let exceptionButton = doc.getElementById("exceptionDialogButton");
      Assert.ok(
        !exceptionButton.disabled,
        "Exception button is not disabled by default."
      );

      let advancedButton = doc.getElementById("advancedButton");
      advancedButton.click();

      // Wait until fluent sets the errorCode inner text.
      let errorCode;
      await ContentTaskUtils.waitForCondition(() => {
        errorCode = doc.getElementById("errorCode");
        return errorCode && errorCode.textContent != "";
      }, "error code has been set inside the advanced button panel");

      return {
        textContent: errorCode.textContent,
        tagName: errorCode.tagName.toLowerCase(),
      };
    });
    is(
      message.textContent,
      "SEC_ERROR_EXPIRED_CERTIFICATE",
      "Correct error message found"
    );
    is(message.tagName, "a", "Error message is a link");

    message = await SpecialPowers.spawn(bc, [], async function () {
      let doc = content.document;
      let errorCode = doc.getElementById("errorCode");
      errorCode.click();
      let div = doc.getElementById("certificateErrorDebugInformation");
      let text = doc.getElementById("certificateErrorText");
      Assert.notStrictEqual(
        content.getComputedStyle(div).display,
        "none",
        "Debug information is visible"
      );
      let handshakeCertificates =
        content.docShell.failedChannel.securityInfo.handshakeCertificates.map(
          cert => cert.getBase64DERString()
        );
      return {
        divDisplay: content.getComputedStyle(div).display,
        text: text.textContent,
        handshakeCertificates,
      };
    });
    isnot(message.divDisplay, "none", "Debug information is visible");
    ok(message.text.includes(BAD_CERT), "Correct URL found");
    ok(
      message.text.includes("Certificate has expired"),
      "Correct error message found"
    );
    ok(
      message.text.includes("HTTP Strict Transport Security: false"),
      "Correct HSTS value found"
    );
    ok(
      message.text.includes("HTTP Public Key Pinning: false"),
      "Correct HPKP value found"
    );
    let certChain = getCertChainAsString(message.handshakeCertificates);
    ok(message.text.includes(certChain), "Found certificate chain");

    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
});

add_task(async function checkAdvancedDetailsForHSTS_feltPrivacyToFalse() {
  await setSecurityCertErrorsFeltPrivacyToFalse();
  info(
    "Loading a bad STS cert page and verifying the advanced details section"
  );
  for (let useFrame of [false, true]) {
    let tab = await openErrorPage(BAD_STS_CERT, useFrame);
    let browser = tab.linkedBrowser;

    let bc = browser.browsingContext;
    if (useFrame) {
      bc = bc.children[0];
    }

    let message = await SpecialPowers.spawn(bc, [], async function () {
      let doc = content.document;
      let advancedButton = doc.getElementById("advancedButton");
      advancedButton.click();

      // Wait until fluent sets the errorCode inner text.
      let ec;
      await ContentTaskUtils.waitForCondition(() => {
        ec = doc.getElementById("errorCode");
        return ec.textContent != "";
      }, "error code has been set inside the advanced button panel");

      let cdl = doc.getElementById("cert_domain_link");
      return {
        ecTextContent: ec.textContent,
        ecTagName: ec.tagName.toLowerCase(),
        cdlTextContent: cdl.textContent,
        cdlTagName: cdl.tagName.toLowerCase(),
      };
    });

    const badStsUri = Services.io.newURI(BAD_STS_CERT);
    is(
      message.ecTextContent,
      "SSL_ERROR_BAD_CERT_DOMAIN",
      "Correct error message found"
    );
    is(message.ecTagName, "a", "Error message is a link");
    const url = badStsUri.prePath.slice(badStsUri.prePath.indexOf(".") + 1);
    is(message.cdlTextContent, url, "Correct cert_domain_link contents found");
    is(message.cdlTagName, "a", "cert_domain_link is a link");

    message = await SpecialPowers.spawn(bc, [], async function () {
      let doc = content.document;
      let errorCode = doc.getElementById("errorCode");
      errorCode.click();
      let div = doc.getElementById("certificateErrorDebugInformation");
      let text = doc.getElementById("certificateErrorText");
      let handshakeCertificates =
        content.docShell.failedChannel.securityInfo.handshakeCertificates.map(
          cert => cert.getBase64DERString()
        );
      return {
        divDisplay: content.getComputedStyle(div).display,
        text: text.textContent,
        handshakeCertificates,
      };
    });
    isnot(message.divDisplay, "none", "Debug information is visible");
    ok(message.text.includes(badStsUri.spec), "Correct URL found");
    ok(
      message.text.includes(
        "requested domain name does not match the server\u2019s certificate"
      ),
      "Correct error message found"
    );
    ok(
      message.text.includes("HTTP Strict Transport Security: false"),
      "Correct HSTS value found"
    );
    ok(
      message.text.includes("HTTP Public Key Pinning: true"),
      "Correct HPKP value found"
    );
    let certChain = getCertChainAsString(message.handshakeCertificates);
    ok(message.text.includes(certChain), "Found certificate chain");

    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
});

add_task(async function checkUnknownIssuerLearnMoreLink_feltPrivacyToFalse() {
  await setSecurityCertErrorsFeltPrivacyToFalse();
  info(
    "Loading a cert error for self-signed pages and checking the correct link is shown"
  );
  for (let useFrame of [false, true]) {
    let tab = await openErrorPage(UNKNOWN_ISSUER, useFrame);
    let browser = tab.linkedBrowser;

    let bc = browser.browsingContext;
    if (useFrame) {
      bc = bc.children[0];
    }

    let href = await SpecialPowers.spawn(bc, [], async function () {
      let learnMoreLink = content.document.getElementById("learnMoreLink");
      return learnMoreLink.href;
    });
    ok(href.endsWith("security-error"), "security-error in the Learn More URL");

    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
});

add_task(async function checkViewCertificate_feltPrivacyToFalse() {
  await setSecurityCertErrorsFeltPrivacyToFalse();
  info("Loading a cert error and checking that the certificate can be shown.");
  for (let useFrame of [true, false]) {
    if (useFrame) {
      // Bug #1573502
      continue;
    }
    let tab = await openErrorPage(UNKNOWN_ISSUER, useFrame);
    let browser = tab.linkedBrowser;

    let bc = browser.browsingContext;
    if (useFrame) {
      bc = bc.children[0];
    }

    let loaded = BrowserTestUtils.waitForNewTab(gBrowser, null, true);
    await SpecialPowers.spawn(bc, [], async function () {
      let viewCertificate = content.document.getElementById("viewCertificate");
      viewCertificate.click();
    });
    await loaded;

    let spec = gBrowser.currentURI.spec;
    Assert.ok(
      spec.startsWith("about:certificate"),
      "about:certificate is the new opened tab"
    );

    await SpecialPowers.spawn(
      gBrowser.selectedTab.linkedBrowser,
      [],
      async function () {
        let doc = content.document;
        let certificateSection = await ContentTaskUtils.waitForCondition(() => {
          return doc.querySelector("certificate-section");
        }, "Certificate section found");

        let infoGroup =
          certificateSection.shadowRoot.querySelector("info-group");
        Assert.ok(infoGroup, "infoGroup found");

        let items = infoGroup.shadowRoot.querySelectorAll("info-item");
        let commonnameID = items[items.length - 1].shadowRoot
          .querySelector("label")
          .getAttribute("data-l10n-id");
        Assert.equal(
          commonnameID,
          "certificate-viewer-common-name",
          "The correct item was selected"
        );

        let commonnameValue =
          items[items.length - 1].shadowRoot.querySelector(".info").textContent;
        Assert.equal(
          commonnameValue,
          "self-signed.example.com",
          "Shows the correct certificate in the page"
        );
      }
    );
    BrowserTestUtils.removeTab(gBrowser.selectedTab); // closes about:certificate
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
});

add_task(async function checkBadStsCertHeadline_feltPrivacyToFalse() {
  await setSecurityCertErrorsFeltPrivacyToFalse();
  info(
    "Loading a bad sts cert error page and checking that the correct headline is shown"
  );
  for (let useFrame of [false, true]) {
    let tab = await openErrorPage(BAD_CERT, useFrame);
    let browser = tab.linkedBrowser;

    let bc = browser.browsingContext;
    if (useFrame) {
      bc = bc.children[0];
    }

    await SpecialPowers.spawn(bc, [useFrame], async _useFrame => {
      const titleText = content.document.querySelector(".title-text");
      is(
        titleText.dataset.l10nId,
        _useFrame ? "nssBadCert-sts-title" : "nssBadCert-title",
        "Error page title is set"
      );
    });
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
});

add_task(async function checkSandboxedIframe_feltPrivacyToFalse() {
  await setSecurityCertErrorsFeltPrivacyToFalse();
  info(
    "Loading a bad sts cert error in a sandboxed iframe and check that the correct headline is shown"
  );
  let useFrame = true;
  let sandboxed = true;
  let tab = await openErrorPage(BAD_CERT, useFrame, sandboxed);
  let browser = tab.linkedBrowser;

  let bc = browser.browsingContext.children[0];
  await SpecialPowers.spawn(bc, [], async function () {
    let doc = content.document;

    const titleText = doc.querySelector(".title-text");
    is(
      titleText.dataset.l10nId,
      "nssBadCert-sts-title",
      "Title shows Did Not Connect: Potential Security Issue"
    );

    const errorLabel = doc.querySelector(
      '[data-l10n-id="cert-error-code-prefix-link"]'
    );
    const elArgs = JSON.parse(errorLabel.dataset.l10nArgs);
    is(
      elArgs.error,
      "SEC_ERROR_EXPIRED_CERTIFICATE",
      "Correct error message found"
    );
    is(
      doc.getElementById("errorCode").tagName.toLowerCase(),
      "a",
      "Error message contains a link"
    );
  });
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function checkViewSource_feltPrivacyToFalse() {
  await setSecurityCertErrorsFeltPrivacyToFalse();
  info(
    "Loading a bad sts cert error in a sandboxed iframe and check that the correct headline is shown"
  );
  let uri = "view-source:" + BAD_CERT;
  let tab = await openErrorPage(uri);
  let browser = tab.linkedBrowser;

  await SpecialPowers.spawn(browser, [], async function () {
    let doc = content.document;

    const errorLabel = doc.querySelector(
      '[data-l10n-id="cert-error-code-prefix-link"]'
    );
    const elArgs = JSON.parse(errorLabel.dataset.l10nArgs);
    is(
      elArgs.error,
      "SEC_ERROR_EXPIRED_CERTIFICATE",
      "Correct error message found"
    );
    is(
      doc.getElementById("errorCode").tagName.toLowerCase(),
      "a",
      "Error message contains a link"
    );

    const titleText = doc.querySelector(".title-text");
    is(titleText.dataset.l10nId, "nssBadCert-title", "Error page title is set");

    const shortDesc = doc.getElementById("errorShortDesc");
    const sdArgs = JSON.parse(shortDesc.dataset.l10nArgs);
    is(
      sdArgs.hostname,
      "expired.example.com",
      "Should list hostname in error message."
    );
  });

  let loaded = BrowserTestUtils.browserLoaded(browser, false, uri);
  info("Clicking the exceptionDialogButton in advanced panel");
  await SpecialPowers.spawn(browser, [], async function () {
    let doc = content.document;
    let exceptionButton = doc.getElementById("exceptionDialogButton");
    exceptionButton.click();
  });

  info("Loading the url after adding exception");
  await loaded;

  await SpecialPowers.spawn(browser, [], async function () {
    let doc = content.document;
    ok(
      !doc.documentURI.startsWith("about:certerror"),
      "Exception has been added"
    );
  });

  let certOverrideService = Cc[
    "@mozilla.org/security/certoverride;1"
  ].getService(Ci.nsICertOverrideService);
  certOverrideService.clearValidityOverride("expired.example.com", -1, {});

  loaded = BrowserTestUtils.waitForErrorPage(browser);
  BrowserCommands.reloadSkipCache();
  await loaded;

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function testCertificateTransparency() {
  info(
    "Test that when certificate transparency is enforced, the right error page is shown."
  );
  // Enforce certificate transparency for certificates issued by our test root.
  // This is only possible in debug builds, hence skipping this test in
  // non-debug builds (see below).
  await SpecialPowers.pushPrefEnv({
    set: [
      ["security.pki.certificate_transparency.mode", 2],
      [
        "security.test.built_in_root_hash",
        "8D:9D:57:09:E5:7D:D5:C6:4B:BE:24:70:E9:E5:BF:FF:16:F6:F2:C2:49:4E:0F:B9:37:1C:DD:3A:0E:10:45:F4",
      ],
    ],
  });

  for (let useFrame of [false, true]) {
    let tab;
    let browser;
    if (useFrame) {
      tab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        "about:blank"
      );
      browser = tab.linkedBrowser;

      // injectErrorPageFrame is a helper from head.js for this purpose
      await injectErrorPageFrame(tab, GOOD_PAGE, useFrame);
    } else {
      tab = await openErrorPage(GOOD_PAGE, useFrame);
      browser = tab.linkedBrowser;
    }

    let bc = browser.browsingContext;
    if (useFrame) {
      bc = bc.children[0];
    }

    let message = await SpecialPowers.spawn(bc, [], async function () {
      const doc = content.document;
      const shortDesc = doc.getElementById("errorShortDesc");
      const sdArgs = JSON.parse(shortDesc.dataset.l10nArgs);
      Assert.equal(
        sdArgs.hostname,
        "example.com",
        "Should list hostname in error message."
      );
      const advancedButton = doc.getElementById("advancedButton");
      advancedButton.scrollIntoView(true);
      EventUtils.synthesizeMouseAtCenter(advancedButton, {}, content);

      // Wait until fluent sets the errorCode inner text.
      let errorCode;
      await ContentTaskUtils.waitForCondition(() => {
        errorCode = doc.getElementById("errorCode");
        return errorCode && errorCode.textContent != "";
      }, "error code has been set inside the advanced button panel");

      return {
        textContent: errorCode.textContent,
        tagName: errorCode.tagName.toLowerCase(),
      };
    });

    Assert.equal(
      message.textContent,
      "MOZILLA_PKIX_ERROR_INSUFFICIENT_CERTIFICATE_TRANSPARENCY",
      "Correct error message found"
    );
    Assert.equal(message.tagName, "a", "Error message is a link");

    message = await SpecialPowers.spawn(bc, [], async function () {
      const doc = content.document;
      const errorCode = doc.getElementById("errorCode");
      errorCode.scrollIntoView(true);
      EventUtils.synthesizeMouseAtCenter(errorCode, {}, content);
      const text = doc.getElementById("certificateErrorText");
      return {
        text: text.textContent,
      };
    });
    Assert.ok(message.text.includes(GOOD_PAGE), "Correct URL found");

    BrowserTestUtils.removeTab(gBrowser.selectedTab);
    BrowserTestUtils.removeTab(tab);
  }

  await SpecialPowers.popPrefEnv();

  // Certificate transparency can only be enforced for our test certificates in
  // debug builds.
}).skip(!AppConstants.DEBUG);

add_task(async function testCertificateTransparency_feltPrivacyTrue() {
  info(
    "Test that when certificate transparency is enforced, the right error page is shown."
  );
  // Enforce certificate transparency for certificates issued by our test root.
  // This is only possible in debug builds, hence skipping this test in
  // non-debug builds (see below).
  await SpecialPowers.pushPrefEnv({
    set: [
      ["security.pki.certificate_transparency.mode", 2],
      [
        "security.test.built_in_root_hash",
        "8D:9D:57:09:E5:7D:D5:C6:4B:BE:24:70:E9:E5:BF:FF:16:F6:F2:C2:49:4E:0F:B9:37:1C:DD:3A:0E:10:45:F4",
      ],
      ["security.certerrors.felt-privacy-v1", true],
    ],
  });

  for (let useFrame of [false, true]) {
    const tab = await openErrorPage(GOOD_PAGE, useFrame);
    const browser = tab.linkedBrowser;

    let bc = browser.browsingContext;
    if (useFrame) {
      bc = bc.children[0];
    }

    const message = await SpecialPowers.spawn(bc, [], async function () {
      const doc = content.document;

      const netErrorCard = await ContentTaskUtils.waitForCondition(
        () => doc.querySelector("net-error-card")?.wrappedJSObject,
        "Waiting for net-error-card"
      );
      await netErrorCard.getUpdateComplete();

      netErrorCard.advancedButton.scrollIntoView();
      EventUtils.synthesizeMouseAtCenter(
        netErrorCard.advancedButton,
        {},
        content
      );

      await ContentTaskUtils.waitForCondition(() => {
        return (
          netErrorCard.whyDangerous &&
          netErrorCard.whyDangerous.textContent != ""
        );
      }, "Waiting for why dangerous text");
      const args = JSON.parse(netErrorCard.whyDangerous.dataset.l10nArgs);
      Assert.strictEqual(
        args.hostname,
        "example.com",
        "Should list hostname in error message."
      );

      // Wait until fluent sets the errorCode inner text.
      await ContentTaskUtils.waitForCondition(() => {
        return (
          netErrorCard.errorCode && netErrorCard.errorCode.textContent != ""
        );
      }, "error code has been set inside the advanced button panel");

      netErrorCard.errorCode.scrollIntoView();
      EventUtils.synthesizeMouseAtCenter(netErrorCard.errorCode, {}, content);
      await ContentTaskUtils.waitForCondition(() => {
        return (
          netErrorCard.certErrorText &&
          netErrorCard.certErrorText.textContent != ""
        );
      }, "Certificate Error is showing");

      return {
        certText: netErrorCard.certErrorText.textContent,
        errorCode: netErrorCard.errorCode.textContent,
        tagName: netErrorCard.errorCode.tagName.toLowerCase(),
      };
    });

    Assert.ok(
      message.errorCode.includes(
        "MOZILLA_PKIX_ERROR_INSUFFICIENT_CERTIFICATE_TRANSPARENCY"
      ),
      "Correct error message found"
    );
    Assert.strictEqual(message.tagName, "a", "Error message is a link");
    Assert.ok(message.certText.includes(GOOD_PAGE), "Correct URL found");

    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }

  await SpecialPowers.popPrefEnv();

  // Certificate transparency can only be enforced for our test certificates in
  // debug builds.
}).skip(!AppConstants.DEBUG);

/**
 * A reusable helper that runs assertions on a network error page.
 * It encapsulates the SpecialPowers.spawn call to be CSP-compliant.
 *
 * @param {object} params - Parameters for the assertion.
 * @param {string} params.expectedUrl - The URL to load and check.
 * @param {string} params.expectedHostname - The expected hostname to assert in the error page.
 * @param {string} params.expectedErrorCode - The expected error code to assert.
 * @param {string} params.expectedInfo - Info string for logging.
 * @param {string} params.expectedErrorMessage - Error message to assert in the error text.
 * @param {boolean} params.expectedHpkp - HPKP value to assert in the error text.
 */
async function assertNetErrorPage({
  expectedUrl,
  expectedHostname,
  expectedErrorCode,
  expectedInfo,
  expectedErrorMessage,
  expectedHpkp,
}) {
  await setSecurityCertErrorsFeltPrivacyToTrue();
  info(`${expectedInfo}`);

  for (let useFrame of [false, true]) {
    let tab = await openErrorPage(expectedUrl, useFrame);
    let browser = tab.linkedBrowser;
    let bc = browser.browsingContext;
    if (useFrame) {
      bc = bc.children[0];
    }

    const newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, null, true);
    const contentData = await SpecialPowers.spawn(
      bc,
      [expectedHostname, expectedErrorCode],
      async function (hostname, errorCode) {
        const netErrorCard = await ContentTaskUtils.waitForCondition(
          () =>
            content.document.querySelector("net-error-card")?.wrappedJSObject,
          "Waiting for net-error-card"
        );
        await netErrorCard.getUpdateComplete();

        // Assert Error Card Basics
        Assert.ok(
          netErrorCard.errorTitle,
          "The error page title should exist."
        );

        const shortDesc = netErrorCard.errorIntro;
        const shortDescArgs = JSON.parse(shortDesc.dataset.l10nArgs);
        Assert.equal(
          shortDescArgs.hostname,
          hostname,
          "Should list hostname in error message."
        );

        // Assert Advanced button
        Assert.ok(
          !netErrorCard.advancedContainer,
          "The Advanced container should NOT be found in shadow DOM before click."
        );
        const advancedButton = netErrorCard.advancedButton;
        Assert.ok(advancedButton, "The advanced button should exist.");
        Assert.equal(
          advancedButton.dataset.l10nId,
          "fp-certerror-advanced-button",
          "Button should have the 'advanced' l10n ID."
        );

        advancedButton.scrollIntoView(true);
        EventUtils.synthesizeMouseAtCenter(advancedButton, {}, content);
        await ContentTaskUtils.waitForCondition(
          () => netErrorCard.advancedContainer,
          "Wait for the advanced container."
        );

        const hideExceptionButton = netErrorCard.shouldHideExceptionButton();
        if (!hideExceptionButton) {
          await ContentTaskUtils.waitForCondition(
            () =>
              netErrorCard.exceptionButton &&
              !netErrorCard.exceptionButton.disabled,
            "Wait for the exception button to be created."
          );

          Assert.ok(
            !netErrorCard.exceptionButton.disabled,
            "The exception button is now enabled."
          );
        }

        Assert.equal(
          advancedButton.dataset.l10nId,
          "fp-certerror-hide-advanced-button",
          "Button should have the 'hide-advanced' l10n ID."
        );
        Assert.ok(
          netErrorCard.advancedShowing,
          "Advanced showing attribute should be true"
        );

        // Assert Error Code
        const certErrorCodeLink = netErrorCard.errorCode;
        await ContentTaskUtils.waitForCondition(
          () => certErrorCodeLink.textContent.includes(errorCode),
          "Wait for Fluent to populate error code text"
        );
        Assert.equal(
          certErrorCodeLink.textContent,
          `Error Code: ${errorCode}`,
          "Error code text is as expected"
        );
        Assert.equal(
          certErrorCodeLink.tagName.toLowerCase(),
          "a",
          "Error code is a link"
        );

        certErrorCodeLink.scrollIntoView(true);
        EventUtils.synthesizeMouse(certErrorCodeLink, 2, 2, {}, content);
        Assert.ok(
          netErrorCard.certErrorDebugInfoShowing,
          "The 'certErrorDebugInfoShowing' boolean should be toggled (to true) after Advance button click on assertAdvancedButton."
        );
        await content.document.l10n.translateRoots();
        Assert.ok(netErrorCard.certErrorText, "Error Code Detail should exist");

        // Assert Site Certificate
        info("Clicking the View Certificate button in advanced panel");
        netErrorCard.viewCertificate.scrollIntoView(true);
        EventUtils.synthesizeMouseAtCenter(
          netErrorCard.viewCertificate,
          {},
          content
        );

        // Extract data needed by the parent process
        const failedCertChain =
          content.docShell.failedChannel.securityInfo.handshakeCertificates.map(
            cert => cert.getBase64DERString()
          );

        return {
          errorText: netErrorCard.certErrorText.textContent,
          rawCertChain: failedCertChain,
        };
      }
    );

    Assert.ok(
      contentData.errorText.includes(expectedHostname),
      "Correct URL found"
    );
    Assert.ok(
      contentData.errorText.includes(expectedErrorMessage),
      "Correct error message exists"
    );
    Assert.ok(
      contentData.errorText.includes("HTTP Strict Transport Security: false"),
      "Correct HSTS value exists"
    );
    Assert.ok(
      contentData.errorText.includes(
        `HTTP Public Key Pinning: ${expectedHpkp}`
      ),
      "Correct HPKP value exists"
    );

    info("Loading the about:certificate page");
    let newTab = await newTabPromise;

    // Parent process checking if certificate viewer opened
    const spec = gBrowser.currentURI.spec;
    Assert.ok(
      spec.startsWith("about:certificate"),
      "about:certificate is the new opened tab"
    );

    await SpecialPowers.spawn(
      gBrowser.selectedTab.linkedBrowser,
      [expectedHostname],
      async function (hostname) {
        const doc = content.document;
        const certificateSection = await ContentTaskUtils.waitForCondition(
          () => {
            // Content process checking if we're no longer on the error page
            Assert.ok(
              !doc.documentURI.startsWith("about:certerror"),
              "We are now in a new tab with content including: about:certificate"
            );
            return doc.querySelector("certificate-section");
          },
          "Certificate section found"
        );

        const infoGroup =
          certificateSection.shadowRoot.querySelector("info-group");
        Assert.ok(infoGroup, "infoGroup found");

        const items = infoGroup.shadowRoot.querySelectorAll("info-item");
        const commonNameID = items[items.length - 1].shadowRoot
          .querySelector("label")
          .getAttribute("data-l10n-id");
        Assert.equal(
          commonNameID,
          "certificate-viewer-common-name",
          "The correct item was selected"
        );

        const commonNameValue =
          items[items.length - 1].shadowRoot.querySelector(".info").textContent;

        // Bug 1992278
        // Structuring the logic this way avoids issue to be addressed
        // in most cases the values match, "self-signed.example.com", "self-signed.example.com"
        // in the case of pinning, the commonNameValue is shorter, "pinning-test.example.com",
        // than the hostname "badchain.include-subdomains.pinning.example.com"
        // as a temporary measure I have reversed the logic such that hostname includes commonNameValue
        Assert.ok(
          hostname.includes(commonNameValue),
          "Error text should include the expected hostname"
        );
      }
    );

    BrowserTestUtils.removeTab(newTab);
    BrowserTestUtils.removeTab(tab);
  }
}

/**
 * A reusable helper that runs assertions on a view-source network error page.
 * It encapsulates the SpecialPowers.spawn call to be CSP-compliant.
 *
 * @param {object} params - Parameters for the assertion.
 * @param {string} params.expectedHostname - The expected hostname to assert in the error page.
 * @param {string} params.expectedUrl - The URL to load and check.
 */
async function assertViewSourceNetErrorPage({
  expectedHostname,
  expectedUrl,
  expectedInfo,
}) {
  await setSecurityCertErrorsFeltPrivacyToTrue();
  info(`${expectedInfo}`);

  let tab = await openErrorPage(expectedUrl);
  const browser = tab.linkedBrowser;
  const loaded = BrowserTestUtils.browserLoaded(browser, false, expectedUrl);

  await SpecialPowers.spawn(browser, [], async function () {
    const netErrorCard = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("net-error-card")?.wrappedJSObject,
      "Waiting for net-error-card"
    );
    await netErrorCard.getUpdateComplete();
    // Advanced button
    const advancedButton = netErrorCard.advancedButton;
    advancedButton.scrollIntoView(true);
    EventUtils.synthesizeMouseAtCenter(advancedButton, {}, content);
    await ContentTaskUtils.waitForCondition(
      () =>
        netErrorCard.exceptionButton && !netErrorCard.exceptionButton.disabled,
      "Wait for the exception button to be created."
    );

    info("Clicking the Proceed Risky button in advanced panel");
    netErrorCard.exceptionButton.scrollIntoView(true);
    EventUtils.synthesizeMouseAtCenter(
      netErrorCard.exceptionButton,
      {},
      content
    );
  });

  info("Loading the url after proceeding with risk");
  await loaded;

  // Clean up the cert override
  const certOverrideService = Cc[
    "@mozilla.org/security/certoverride;1"
  ].getService(Ci.nsICertOverrideService);
  certOverrideService.clearValidityOverride(expectedHostname, -1, {});

  // To ensure the state is reset, reload and wait for the error page to return.
  info("Reloading to ensure the certificate override was cleared.");
  const errorPageLoaded = BrowserTestUtils.waitForErrorPage(browser);

  BrowserCommands.reloadSkipCache();
  await errorPageLoaded;

  info("Override cleared and error page is shown again.");
  BrowserTestUtils.removeTab(tab);
}

// Security CertError Felt Privacy set to true only
add_task(async function checkReturnToPreviousPage_feltPrivacyToTrue() {
  await setSecurityCertErrorsFeltPrivacyToTrue();
  info(
    "Loading a bad cert page and making sure 'return to previous page' goes back"
  );
  for (let useFrame of [false, true]) {
    let tab;
    let browser;
    if (useFrame) {
      tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, GOOD_PAGE);
      browser = tab.linkedBrowser;
      await SpecialPowers.spawn(browser, [], () => {
        content.document.notifyUserGestureActivation();
      });

      BrowserTestUtils.startLoadingURIString(browser, GOOD_PAGE_2);
      await BrowserTestUtils.browserLoaded(browser, false, GOOD_PAGE_2);
      await injectErrorPageFrame(tab, BAD_CERT);
    } else {
      tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, GOOD_PAGE);
      browser = gBrowser.selectedBrowser;
      await SpecialPowers.spawn(browser, [], () => {
        content.document.notifyUserGestureActivation();
      });

      info("Loading and waiting for the cert error");
      const certErrorLoaded = BrowserTestUtils.waitForErrorPage(browser);
      BrowserTestUtils.startLoadingURIString(browser, BAD_CERT);
      await certErrorLoaded;
    }

    Assert.ok(browser.webNavigation.canGoBack, "webNavigation.canGoBack");
    Assert.ok(
      !browser.webNavigation.canGoForward,
      "!webNavigation.canGoForward"
    );

    // Populate the shistory entries manually, since it happens asynchronously
    // and the following tests will be too soon otherwise.
    await TabStateFlusher.flush(browser);
    const { entries } = JSON.parse(SessionStore.getTabState(tab));
    Assert.equal(entries.length, 2, "there are two history entries");

    info("Clicking the go back button on about:certerror");
    let bc = browser.browsingContext;
    if (useFrame) {
      bc = bc.children[0];
    }

    const pageShownPromise = BrowserTestUtils.waitForContentEvent(
      browser,
      "pageshow",
      true
    );
    await SpecialPowers.spawn(bc, [useFrame], async function () {
      const netErrorCard = await ContentTaskUtils.waitForCondition(
        () => content.document.querySelector("net-error-card")?.wrappedJSObject
      );
      const returnButton = await ContentTaskUtils.waitForCondition(
        () => netErrorCard.returnButton
      );
      returnButton.click();
    });
    await pageShownPromise;

    Assert.ok(!browser.webNavigation.canGoBack, "!webNavigation.canGoBack");
    Assert.ok(browser.webNavigation.canGoForward, "webNavigation.canGoForward");
    Assert.equal(gBrowser.currentURI.spec, GOOD_PAGE, "Went back");

    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
});

add_task(async function checkAdvancedDetails_feltPrivacyToTrue() {
  // Helper function does all the content-side checks.
  await assertNetErrorPage({
    expectedUrl: BAD_CERT,
    expectedHostname: new URL(BAD_CERT).hostname,
    expectedErrorCode: "SEC_ERROR_EXPIRED_CERTIFICATE",
    expectedInfo:
      "Loading a bad cert page and verifying the main error and advanced details section",
    expectedErrorMessage: "Certificate has expired",
    expectedHpkp: false,
  });
});

add_task(async function checkAdvancedDetailsForHSTS_feltPrivacyToTrue() {
  // Helper function does all the content-side checks.
  await assertNetErrorPage({
    expectedUrl: BAD_STS_CERT,
    expectedHostname: new URL(BAD_STS_CERT).hostname,
    expectedErrorCode: "SSL_ERROR_BAD_CERT_DOMAIN",
    expectedInfo:
      "Loading a bad STS cert page and verifying the advanced details section",
    expectedErrorMessage:
      "requested domain name does not match the server\u2019s certificate", // Expected error message
    expectedHpkp: true,
  });
});

add_task(async function checkUnknownIssuerDetails_feltPrivacyToTrue() {
  // Helper function does all the content-side checks.
  await assertNetErrorPage({
    expectedUrl: UNKNOWN_ISSUER,
    expectedHostname: new URL(UNKNOWN_ISSUER).hostname,
    expectedErrorCode: "MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT",
    expectedInfo:
      "Loading a cert error for self-signed pages and checking the correct link is shown",
    expectedErrorMessage:
      "The certificate is not trusted because it is self-signed.",
    expectedHpkp: false,
  });
});

add_task(async function checkViewSource_feltPrivacyToTrue() {
  await assertViewSourceNetErrorPage({
    expectedHostname: new URL(BAD_CERT).hostname,
    expectedUrl: "view-source:" + BAD_CERT,
    expectedInfo:
      "Loading a bad sts cert error in a sandboxed iframe and check that the correct headline is shown",
  });
});

add_task(async function checkSandboxedIframe_feltPrivacyToTrue() {
  await setSecurityCertErrorsFeltPrivacyToTrue();
  info(
    "Loading a bad sts cert error in a sandboxed iframe and check that the correct headline is shown"
  );
  let useFrame = true;
  let sandboxed = true;
  let tab = await openErrorPage(BAD_CERT, useFrame, sandboxed);
  let browser = tab.linkedBrowser;

  let bc = browser.browsingContext.children[0];
  await SpecialPowers.spawn(bc, [], async function () {
    const netErrorCard = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("net-error-card")?.wrappedJSObject,
      "Waiting for net-error-card"
    );
    await netErrorCard.getUpdateComplete();

    // Assert Error Card Basics
    Assert.ok(netErrorCard.errorTitle, "The error page title should exist.");
    const advancedButton = netErrorCard.advancedButton;
    advancedButton.scrollIntoView(true);
    EventUtils.synthesizeMouseAtCenter(advancedButton, {}, content);
    await ContentTaskUtils.waitForCondition(
      () => netErrorCard.advancedContainer,
      "Wait for the advanced container."
    );

    const hideExceptionButton = netErrorCard.shouldHideExceptionButton();
    if (!hideExceptionButton) {
      await ContentTaskUtils.waitForCondition(
        () =>
          netErrorCard.exceptionButton &&
          !netErrorCard.exceptionButton.disabled,
        "Wait for the exception button to be created."
      );
    }

    // Assert Error Code
    const certErrorCodeLink = netErrorCard.errorCode;
    await ContentTaskUtils.waitForCondition(
      () =>
        certErrorCodeLink.textContent.includes("SEC_ERROR_EXPIRED_CERTIFICATE"),
      "Wait for Fluent to populate error code text"
    );
    Assert.equal(
      certErrorCodeLink.textContent,
      `Error Code: SEC_ERROR_EXPIRED_CERTIFICATE`,
      "Error Code is as expected"
    );
    Assert.equal(
      certErrorCodeLink.tagName.toLowerCase(),
      "a",
      "Error Code is a link"
    );
  });
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
