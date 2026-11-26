"use strict";

/* import-globals-from head_siteContainer.js */
loadTestSubscript("head_siteContainer.js");

// Runs aFn, which is expected to start exactly one download, and returns once
// it finished. The file it produced and the entry in the download list are
// removed before returning.
async function withDownload(aFn) {
  let list = await Downloads.getList(Downloads.ALL);
  let finished = new Promise(resolve => {
    let view = {
      onDownloadChanged(download) {
        if (download.succeeded || download.error) {
          list.removeView(view);
          resolve(download);
        }
      },
    };
    list.addView(view);
  });

  await aFn();
  let download = await finished;

  try {
    await IOUtils.remove(download.target.path);
  } catch (e) {
    // The file may not have been kept; ignore.
  }
  await list.remove(download);
  await download.finalize(true);
}

// The decision is per-hop, so a redirect into a bound site is retargeted too.
add_task(async function test_server_redirect_retargets() {
  let redirectURL =
    BASE_COM + "siteContainer_redirect.sjs?to=" + encodeURIComponent(URL_ORG);
  let { tab, retargeted } = await retargetFrom(
    OTHER_CONTAINER,
    BOUND_CONTAINER,
    redirectURL,
    "A server redirect into a bound site is retargeted",
    { wantLoad: URL_ORG }
  );
  await assertLeftBehind(tab, URL_COM, OTHER_CONTAINER, "The original tab");

  let history = retargeted.linkedBrowser.browsingContext.sessionHistory;
  is(history.count, 1, "The new tab has an entry for the redirected load");
  is(
    history.getEntryAtIndex(0).URI.spec,
    URL_ORG,
    "The entry is for the end of the redirect chain"
  );

  BrowserTestUtils.removeTab(retargeted);
  BrowserTestUtils.removeTab(tab);
});

// A hop bound to no container keeps the container the chain already runs in,
// rather than falling back to the container of the tab it started in.
add_task(async function test_redirect_to_unbound_site_keeps_the_container() {
  const START_CONTAINER = 4;

  try {
    setContainerCookie("example.com", "boundJar", BOUND_CONTAINER);
    setContainerCookie("example.com", "startJar", START_CONTAINER);

    let comURL = BASE_COM + "siteContainer_redirect.sjs?record=UNBOUND";
    let orgURL =
      BASE_ORG + "siteContainer_redirect.sjs?to=" + encodeURIComponent(comURL);

    let { tab, retargeted } = await retargetFrom(
      START_CONTAINER,
      BOUND_CONTAINER,
      orgURL,
      "The unbound destination stayed in the container of the bound hop",
      { start: "about:blank", wantLoad: comURL }
    );

    let recorded = await fetch(
      BASE_COM + "siteContainer_redirect.sjs?results=UNBOUND"
    ).then(r => r.json());
    is(
      recorded.UNBOUND,
      "ck=boundJar",
      "The unbound hop used the bound container's cookie jar"
    );

    BrowserTestUtils.removeTab(retargeted);
    BrowserTestUtils.removeTab(tab);
  } finally {
    Services.cookies.removeAll();
  }
});

// Every hop of a chain crossing three bound sites must carry the cookie seeded
// in its own container's jar. The decoy cookies in the tab's starting container
// are what an implementation not re-deciding per hop would wrongly send.
add_task(async function test_redirect_chain_per_container_cookies() {
  const CONTAINER_COM = 1;
  const CONTAINER_ORG = 2;
  const CONTAINER_NET = 3;
  const START_CONTAINER = 4;

  try {
    ContextualIdentityService.setSiteAssociation("example.com", CONTAINER_COM);
    ContextualIdentityService.setSiteAssociation("example.net", CONTAINER_NET);
    // "example.org" is already bound to CONTAINER_ORG by the setup.

    setContainerCookie("example.com", "com", CONTAINER_COM);
    setContainerCookie("example.org", "org", CONTAINER_ORG);
    setContainerCookie("example.net", "net", CONTAINER_NET);
    setContainerCookie("example.org", "orgDecoy", START_CONTAINER);
    setContainerCookie("example.net", "netDecoy", START_CONTAINER);

    let cUrl = BASE_NET + "siteContainer_redirect.sjs?record=C";
    let bUrl =
      BASE_ORG +
      "siteContainer_redirect.sjs?record=B&to=" +
      encodeURIComponent(cUrl);
    let aUrl =
      BASE_COM +
      "siteContainer_redirect.sjs?record=A&to=" +
      encodeURIComponent(bUrl);

    let { tab, retargeted } = await retargetFrom(
      START_CONTAINER,
      CONTAINER_NET,
      aUrl,
      "The load landed in the last hop's container",
      { start: "about:blank", wantLoad: cUrl }
    );

    let results = await fetch(
      BASE_COM + "siteContainer_redirect.sjs?results=A,B,C"
    ).then(r => r.json());

    is(results.A, "ck=com", "example.com hop used CONTAINER_COM's cookie jar");
    is(results.B, "ck=org", "example.org hop used CONTAINER_ORG's cookie jar");
    is(results.C, "ck=net", "example.net hop used CONTAINER_NET's cookie jar");

    BrowserTestUtils.removeTab(retargeted);
    BrowserTestUtils.removeTab(tab);
  } finally {
    ContextualIdentityService.removeSiteAssociation("example.com");
    ContextualIdentityService.removeSiteAssociation("example.net");
    Services.cookies.removeAll();
  }
});

// A custom viewer renders an http(s) response, so the container was decided on
// the underlying channel before the viewer took over.
add_task(async function test_json_viewer_retargets() {
  await withPrefs([["devtools.jsonview.enabled", true]], async () => {
    let jsonURL = BASE_ORG + "siteContainer_redirect.sjs?json=1";
    let { tab, retargeted } = await retargetFrom(
      OTHER_CONTAINER,
      BOUND_CONTAINER,
      jsonURL,
      "JSON viewer of a bound site landed in the bound container"
    );

    BrowserTestUtils.removeTab(retargeted);
    BrowserTestUtils.removeTab(tab);
  });
});

// An attachment response commits no document, so the tab keeps its container,
// but the request itself was issued in the bound container's cookie jar.
add_task(async function test_attachment_download_uses_bound_container() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.download.always_ask_before_handling_new_types", false]],
  });

  try {
    setContainerCookie("example.org", "bound", BOUND_CONTAINER);
    setContainerCookie("example.org", "decoy", OTHER_CONTAINER);

    let tab = await openTabIn(OTHER_CONTAINER);

    let downloadURL =
      BASE_ORG + "siteContainer_redirect.sjs?download=1&record=DL";
    await withDownload(() =>
      BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, downloadURL)
    );

    await assertContainer(
      tab,
      OTHER_CONTAINER,
      "A download does not change the tab's container"
    );

    let recorded = await fetch(
      BASE_COM + "siteContainer_redirect.sjs?results=DL"
    ).then(r => r.json());
    is(
      recorded.DL,
      "ck=bound",
      "Download request used the bound container's cookie jar"
    );

    BrowserTestUtils.removeTab(tab);
  } finally {
    Services.cookies.removeAll();
    await SpecialPowers.popPrefEnv();
  }
});

// A load the page asked to download has no document to commit, so it is not
// retargeted, but the request itself is issued in the bound container's cookie
// jar like the attachment response above.
add_task(async function test_anchor_download_uses_bound_container() {
  const DL_CONTAINER = 3;

  await SpecialPowers.pushPrefEnv({
    set: [["browser.download.always_ask_before_handling_new_types", false]],
  });

  try {
    setContainerCookie("test1.example.org", "tabJar", OTHER_CONTAINER);
    setContainerCookie("test1.example.org", "boundJar", DL_CONTAINER);

    // The tab is loaded while the site is still unbound, and the "download"
    // attribute is only honoured same-origin, so bind the site afterwards.
    let tab = await openTabIn(OTHER_CONTAINER, URL_TEST1);
    ContextualIdentityService.setSiteAssociation(
      "test1.example.org",
      DL_CONTAINER
    );

    await assertNoRetarget(gBrowser, "Anchor download", () =>
      withDownload(() =>
        SpecialPowers.spawn(
          tab.linkedBrowser,
          [BASE_TEST1 + "siteContainer_redirect.sjs?download=1&record=ANCHOR"],
          url => {
            let link = content.document.createElement("a");
            link.href = url;
            link.download = "container.bin";
            content.document.body.appendChild(link);
            link.click();
          }
        )
      )
    );

    await assertContainer(
      tab,
      OTHER_CONTAINER,
      "The tab that started the download keeps its container"
    );

    let recorded = await fetch(
      BASE_COM + "siteContainer_redirect.sjs?results=ANCHOR"
    ).then(r => r.json());
    is(
      recorded.ANCHOR,
      "ck=boundJar",
      "The download was issued in the bound container's cookie jar"
    );

    BrowserTestUtils.removeTab(tab);
  } finally {
    ContextualIdentityService.removeSiteAssociation("test1.example.org");
    Services.cookies.removeAll();
    await SpecialPowers.popPrefEnv();
  }
});
