/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

function test() {
  const URI = TEST_BASE_URL + "file_userTypedValue.html";
  window.browserDOMWindow.openURI(
    makeURI(URI),
    null,
    Ci.nsIBrowserDOMWindow.OPEN_NEWTAB,
    Ci.nsIBrowserDOMWindow.OPEN_EXTERNAL,
    Services.scriptSecurityManager.getSystemPrincipal()
  );

  is(gBrowser.userTypedValue, URI, "userTypedValue matches test URI");
  is(
    gURLBar.value,
    UrlbarTestUtils.trimURL(URI),
    "location bar value matches test URI"
  );

  gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser);
  gBrowser.removeCurrentTab({ skipPermitUnload: true });
  is(
    gBrowser.userTypedValue,
    URI,
    "userTypedValue matches test URI after switching tabs"
  );
  is(
    gURLBar.value,
    UrlbarTestUtils.trimURL(URI),
    "location bar value matches test URI after switching tabs"
  );

  waitForExplicitFinish();
  BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser).then(() => {
    is(
      gBrowser.userTypedValue,
      null,
      "userTypedValue is null as the page has loaded"
    );
    is(
      gURLBar.value,
      UrlbarTestUtils.trimURL(URI),
      "location bar value matches test URI as the page has loaded"
    );

    gBrowser.removeCurrentTab({ skipPermitUnload: true });
    finish();
  });
}
