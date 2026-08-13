/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

function checkAndClickButton(document, id) {
  let button = document.getElementById(id);
  Assert.ok(button, `${id} button found`);
  Assert.equal(
    button.hasAttribute("disabled"),
    false,
    "button should be clickable"
  );
  button.click();
}

function is_element_visible(aElement, aMsg) {
  isnot(aElement, null, "Element should not be null, when checking visibility");
  Assert.ok(!BrowserTestUtils.isHidden(aElement), aMsg);
}

// Extracted from https://searchfox.org/mozilla-central/rev/40ef22080910c2e2c27d9e2120642376b1d8b8b2/browser/components/preferences/in-content/tests/head.js#41
function promiseLoadSubDialog(aURL) {
  return new Promise(resolve => {
    content.gSubDialog._dialogStack.addEventListener(
      "dialogopen",
      function dialogopen(aEvent) {
        if (
          aEvent.detail.dialog._frame.contentWindow.location == "about:blank"
        ) {
          return;
        }
        content.gSubDialog._dialogStack.removeEventListener(
          "dialogopen",
          dialogopen
        );

        Assert.equal(
          aEvent.detail.dialog._frame.contentWindow.location.toString(),
          aURL,
          "Check the proper URL is loaded"
        );

        // Check visibility
        is_element_visible(aEvent.detail.dialog._overlay, "Overlay is visible");

        // Check that stylesheets were injected
        let expectedStyleSheetURLs =
          aEvent.detail.dialog._injectedStyleSheets.slice(0);
        for (let styleSheet of aEvent.detail.dialog._frame.contentDocument
          .styleSheets) {
          let i = expectedStyleSheetURLs.indexOf(styleSheet.href);
          if (i >= 0) {
            info("found " + styleSheet.href);
            expectedStyleSheetURLs.splice(i, 1);
          }
        }
        Assert.equal(
          expectedStyleSheetURLs.length,
          0,
          "All expectedStyleSheetURLs should have been found"
        );

        // Wait for the next event tick to make sure the remaining part of the
        // testcase runs after the dialog gets ready for input.
        executeSoon(() => resolve(aEvent.detail.dialog._frame.contentWindow));
      }
    );
  });
}

async function injectErrorPageFrame(tab, src) {
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [{ frameSrc: src }],
    async function ({ frameSrc }) {
      let loaded = ContentTaskUtils.waitForEvent(
        content.wrappedJSObject,
        "DOMFrameContentLoaded"
      );
      let iframe = content.document.createElement("iframe");
      iframe.src = frameSrc;
      content.document.body.appendChild(iframe);
      await loaded;
      // We will have race conditions when accessing the frame content after setting a src,
      // so we can't wait for AboutNetErrorLoad. Let's wait for the certerror class to
      // appear instead (which should happen at the same time as AboutNetErrorLoad).
      await ContentTaskUtils.waitForCondition(
        () =>
          iframe.contentDocument.body.classList.contains("certerror") ||
          iframe.contentDocument.body.classList.contains("felt-privacy-body")
      );
    }
  );
  await BrowserTestUtils.waitForPaintingUnsuppressed(
    tab.linkedBrowser.browsingContext.children[0]
  );
}

async function openErrorPage(useFrame = false) {
  let src = "https://expired.example.com/";
  let dummyPage =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "https://example.com"
    ) + "dummy_page.html";

  let tab;
  if (useFrame) {
    info("Loading cert error page in an iframe");
    tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, dummyPage);
    await injectErrorPageFrame(tab, src);
  } else {
    let certErrorLoaded;
    tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      () => {
        gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, src);
        let browser = gBrowser.selectedBrowser;
        certErrorLoaded = BrowserTestUtils.waitForErrorPage(browser);
      },
      false
    );
    info("Loading and waiting for the cert error");
    await certErrorLoaded;
  }

  return tab;
}
