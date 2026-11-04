/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test a http -> https redirect shows secure icon only for redirected https
 * request.
 */

add_task(async function () {
  // This test explicitly asserts http -> https redirects.
  await pushPref("dom.security.https_first", false);

  const { tab, monitor } = await initNetMonitor(CUSTOM_GET_URL, {
    requestCount: 1,
  });
  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 2);
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [HTTPS_REDIRECT_SJS],
    async function (url) {
      content.wrappedJSObject.performRequests(1, url);
    }
  );
  await onNetworkEvents;

  is(
    store.getState().requests.requests.length,
    2,
    "There were two requests due to redirect."
  );

  const [
    initialDomainSecurityIcon,
    initialUrlSecurityIcon,
    redirectDomainSecurityIcon,
    redirectUrlSecurityIcon,
  ] = document.querySelectorAll(".requests-security-state-icon");

  const initialDomainSecurityOk = await waitUntil(() =>
    initialDomainSecurityIcon.classList.contains("security-state-insecure")
  );
  ok(
    initialDomainSecurityOk,
    "Initial request was marked insecure for domain column."
  );

  const redirectDomainSecurityOk = await waitUntil(() =>
    redirectDomainSecurityIcon.classList.contains(
      "security-state-secure-custom-root"
    )
  );
  ok(
    redirectDomainSecurityOk,
    "Redirected request was marked secure for domain column."
  );

  const initialUrlSecurityOk = await waitUntil(() =>
    initialUrlSecurityIcon.classList.contains("security-state-insecure")
  );
  ok(
    initialUrlSecurityOk,
    "Initial request was marked insecure for URL column."
  );

  const redirectUrlSecurityOk = await waitUntil(() =>
    redirectUrlSecurityIcon.classList.contains(
      "security-state-secure-custom-root"
    )
  );
  ok(
    redirectUrlSecurityOk,
    "Redirected request was marked secure for URL column."
  );

  await teardown(monitor);
});
