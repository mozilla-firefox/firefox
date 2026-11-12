/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

const { MAX_DOWNLOAD_BYTES } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/pageextractor/GoogleDocsExtraction.sys.mjs"
);

const EDITOR_MARKUP = `
  <div role="menubar">File Edit View Insert</div>
  <canvas width="800" height="600"></canvas>
`;
const EDITOR_TEXT = "File Edit View Insert";

const VIEW_MARKUP = `<!DOCTYPE html>
<html>
  <head><title>Quarterly report</title></head>
  <body>
    <div class="app-container"><div class="doc-container"><div class="doc"><div class="doc-content">
      <p><a href="#h.results">Results</a></p>
      <h1 id="h.intro"><span style="font-weight:700">Quarterly report</span><sup><a href="#cmnt1" id="cmnt_ref1">[a]</a></sup></h1>
      <p><span>Revenue grew by twenty percent, see the </span><a href="https://www.google.com/url?q=https://example.org/report&amp;sa=D&amp;source=editors&amp;ust=1&amp;usg=AOv">full report</a><span>.</span></p>
      <p><span><img alt="" src="https://docs.google.com/docs-images-rt/chart=s800"></span></p>
      <h2 id="h.results"><span style="font-weight:700">Results</span></h2>
      <p><span>The team shipped three major features.</span></p>
      <div><p><a href="#cmnt_ref1" id="cmnt1">[a]</a><span>Can we add last quarter for comparison?</span></p></div>
    </div></div></div></div>
  </body>
</html>`;

/**
 * Serves an editor page and its /mobilebasic view, and opens the page in a tab.
 *
 * @param {object} options
 * @param {string} [options.path] - The editor page path and query.
 * @param {(request: any, response: any) => void} [options.viewHandler]
 * @param {string} [options.viewPath] - Where the view is expected to be fetched.
 * @param {string[]} [options.pageCookies] - Set-Cookie values the page sets.
 * @param {number} [options.userContextId] - The container to open the tab in.
 */
async function serveGoogleDoc({
  path = "/document/d/testdoc/edit",
  viewHandler = serveView(VIEW_MARKUP),
  viewPath = "/document/d/testdoc/mobilebasic",
  pageCookies = [],
  userContextId = 0,
} = {}) {
  const server = new HttpServer();
  server.start(-1);
  const { primaryHost, primaryPort } = server.identity;

  const pagePath = path.split("?")[0];
  server.registerPathHandler(pagePath, (_request, response) => {
    response.setHeader("Content-Type", "text/html; charset=utf-8");
    for (const cookie of pageCookies) {
      response.setHeader("Set-Cookie", cookie, true);
    }
    response.write(`<!DOCTYPE html><body>${EDITOR_MARKUP}</body>`);
  });

  const viewRequests = [];
  const viewCookies = [];
  server.registerPathHandler(viewPath, (request, response) => {
    viewRequests.push(request.queryString);
    viewCookies.push(
      request.hasHeader("Cookie") ? request.getHeader("Cookie") : ""
    );
    viewHandler(request, response);
  });

  // eslint-disable-next-line sdl/no-insecure-url
  const pageUrl = `http://${primaryHost}:${primaryPort}${path}`;
  const tab = BrowserTestUtils.addTab(gBrowser, pageUrl, { userContextId });
  gBrowser.selectedTab = tab;
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);

  return {
    browser: tab.linkedBrowser,
    pageUrl,
    // The page is served locally, so a docs.google.com sourceUrl is what makes
    // the extractor treat it as a Google Doc. The view is still fetched from
    // the local server because its URL comes from the live page.
    sourceUrl: `https://docs.google.com${path}`,
    viewRequests,
    viewCookies,
    getPageExtractor() {
      return tab.linkedBrowser.browsingContext.currentWindowGlobal.getActor(
        "PageExtractor"
      );
    },
    async cleanup() {
      BrowserTestUtils.removeTab(tab);
      await new Promise(resolve => server.stop(resolve));
    },
  };
}

/**
 * @param {string} body
 * @param {string} [contentType]
 */
function serveView(body, contentType = "text/html; charset=utf-8") {
  return (_request, response) => {
    response.setHeader("Content-Type", contentType);
    response.write(body);
  };
}

/**
 * @param {number} totalBytes - The response length. The padding is an HTML
 *   comment, so it counts towards the download cap without adding text.
 */
function viewOfSize(totalBytes) {
  const before = "<!DOCTYPE html><body><p>Start marker</p><!--";
  const after = "--><p>End marker</p></body>";
  return before + "x".repeat(totalBytes - before.length - after.length) + after;
}

/**
 * @param {string} pageUrl
 * @returns {string} What the extractor reads from VIEW_MARKUP.
 */
function viewText(pageUrl) {
  return [
    `[Results](${pageUrl}#h.results)`,
    `Quarterly report[\\[a\\]](${pageUrl}#cmnt1)`,
    "Revenue grew by twenty percent, see the [full report](https://example.org/report).",
    "Results",
    "The team shipped three major features.",
    `[\\[a\\]](${pageUrl}#cmnt_ref1)Can we add last quarter for comparison?`,
  ].join("\n");
}

add_setup(async function () {
  // A slow run can take longer than the default to download a view at the
  // download cap, and the stall test is ended by its deadline, never a timer.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.pageextractor.googledocs.timeoutMs", 10 * 60 * 1000]],
  });
});

add_task(async function test_reads_the_view() {
  const doc = await serveGoogleDoc();
  try {
    const result = await doc.getPageExtractor().getText({
      sourceUrl: doc.sourceUrl,
    });

    is(
      result.text,
      viewText(doc.pageUrl),
      "The document and its comments are read from the view instead of the editor interface."
    );
    Assert.deepEqual(
      result.links.toSorted(),
      [
        `${doc.pageUrl}#cmnt1`,
        `${doc.pageUrl}#cmnt_ref1`,
        `${doc.pageUrl}#h.results`,
        "https://example.org/report",
      ],
      "Heading and comment links resolve against the document, and Google's redirect is unwrapped."
    );
    Assert.deepEqual(
      doc.viewRequests,
      [""],
      "The view is requested exactly once."
    );
  } finally {
    await doc.cleanup();
  }
});

add_task(async function test_view_respects_text_options() {
  const doc = await serveGoogleDoc();
  try {
    const result = await doc.getPageExtractor().getText({
      sourceUrl: doc.sourceUrl,
      sufficientLength: 1,
    });

    is(
      result.text,
      `[Results](${doc.pageUrl}#h.results)`,
      "The caller's text options apply to the view, so extraction stops once the text is sufficient."
    );
  } finally {
    await doc.cleanup();
  }
});

add_task(async function test_page_cannot_see_the_request() {
  const doc = await serveGoogleDoc();
  try {
    const result = await doc.getPageExtractor().getText({
      sourceUrl: doc.sourceUrl,
    });
    is(
      result.text,
      viewText(doc.pageUrl),
      "The document is read from the view."
    );

    const pageSeen = await SpecialPowers.spawn(doc.browser, [], () =>
      content.performance
        .getEntriesByType("resource")
        .map(entry => entry.name)
        .filter(name => name.includes("/mobilebasic"))
    );
    Assert.deepEqual(
      pageSeen,
      [],
      "The page's own resource timing does not show the view request."
    );
  } finally {
    await doc.cleanup();
  }
});

add_task(async function test_carries_page_params() {
  const doc = await serveGoogleDoc({
    path: "/document/u/1/d/testdoc/edit?tab=t.abc&resourcekey=k&usp=sharing",
    viewPath: "/document/u/1/d/testdoc/mobilebasic",
  });
  try {
    const result = await doc.getPageExtractor().getText({
      sourceUrl: doc.sourceUrl,
    });

    is(
      result.text,
      viewText(doc.pageUrl),
      "A document under an account selector is read from its view."
    );
    Assert.deepEqual(
      doc.viewRequests,
      ["tab=t.abc&resourcekey=k"],
      "The view is requested for the same document tab and sharing key the user is looking at."
    );
  } finally {
    await doc.cleanup();
  }
});

add_task(async function test_falls_back_to_the_generic_walk() {
  // Like accounts.google.com, the sign-in page sends no CORS headers.
  let signInRequests = 0;
  const signIn = new HttpServer();
  signIn.registerPathHandler("/signin", (_request, response) => {
    signInRequests++;
    response.setHeader("Content-Type", "text/html; charset=utf-8");
    response.write("<!DOCTYPE html><body><h1>Sign in</h1></body>");
  });
  signIn.start(-1);
  // eslint-disable-next-line sdl/no-insecure-url
  const signInUrl = `http://${signIn.identity.primaryHost}:${signIn.identity.primaryPort}/signin`;

  const cases = [
    {
      name: "the view is not found",
      status: "http-error",
      viewHandler: (request, response) => {
        response.setStatusLine(request.httpVersion, 404, "Not Found");
        response.setHeader("Content-Type", "text/html; charset=utf-8");
        response.write(
          "<!DOCTYPE html><body><p>Sorry, the file you have requested does not exist.</p></body>"
        );
      },
    },
    {
      name: "the view is not HTML",
      status: "not-html",
      viewHandler: serveView("Quarterly report", "text/plain"),
    },
    {
      name: "the view redirects to a sign-in page on another origin",
      status: "error",
      viewHandler: (request, response) => {
        response.setStatusLine(request.httpVersion, 302, "Found");
        response.setHeader("Location", signInUrl);
      },
    },
    {
      name: "the view has no text",
      status: "empty",
      viewHandler: serveView("<!DOCTYPE html><body></body>"),
    },
  ];

  for (const { name, status, viewHandler } of cases) {
    await Services.fog.testFlushAllChildren();
    Services.fog.testResetFOG();
    const doc = await serveGoogleDoc({ viewHandler });
    try {
      const result = await doc.getPageExtractor().getText({
        sourceUrl: doc.sourceUrl,
      });

      is(result.text, EDITOR_TEXT, `The generic walk is kept when ${name}.`);
      is(doc.viewRequests.length, 1, `The view was requested when ${name}.`);
      await Services.fog.testFlushAllChildren();
      const phaseEvent = Glean.pageExtractor.phase
        .testGetValue("page-extractor")
        ?.findLast(e => e.extra.phase === "google-docs-extract");
      is(
        phaseEvent?.extra.status,
        status,
        `The Google Docs phase reports "${status}" when ${name}.`
      );
    } finally {
      await doc.cleanup();
    }
  }

  is(signInRequests, 0, "No request follows the redirect to the sign-in page.");
  await new Promise(resolve => signIn.stop(resolve));
});

add_task(async function test_follows_same_origin_redirects() {
  // Google drops an authuser the profile isn't signed in to by redirecting to
  // the same view without it.
  const doc = await serveGoogleDoc({
    path: "/document/d/testdoc/edit?authuser=1",
    viewHandler: (request, response) => {
      if (request.queryString) {
        response.setStatusLine(request.httpVersion, 302, "Found");
        response.setHeader("Location", "/document/d/testdoc/mobilebasic");
        return;
      }
      serveView(VIEW_MARKUP)(request, response);
    },
  });

  try {
    const result = await doc.getPageExtractor().getText({
      sourceUrl: doc.sourceUrl,
    });

    is(
      result.text,
      viewText(doc.pageUrl),
      "The view is read after a same-origin redirect."
    );
    Assert.deepEqual(
      doc.viewRequests,
      ["authuser=1", ""],
      "The redirect was followed."
    );
  } finally {
    await doc.cleanup();
  }
});

add_task(async function test_sends_the_tabs_cookies() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "network.cookie.cookieBehavior",
        Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN,
      ],
    ],
  });
  const doc = await serveGoogleDoc({
    pageCookies: ["strict=1; SameSite=Strict; HttpOnly", "lax=1; SameSite=Lax"],
  });
  try {
    const result = await doc.getPageExtractor().getText({
      sourceUrl: doc.sourceUrl,
    });

    is(
      result.text,
      viewText(doc.pageUrl),
      "The document is read from the view."
    );
    Assert.deepEqual(
      doc.viewCookies.map(cookies => cookies.split("; ").toSorted()),
      [["lax=1", "strict=1"]],
      "The view request carries the session's SameSite and HttpOnly cookies, so private documents can be read."
    );
  } finally {
    await doc.cleanup();
  }
  Services.cookies.removeAll();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_sends_the_containers_cookies() {
  const defaultDoc = await serveGoogleDoc({ pageCookies: ["jar=default"] });
  await defaultDoc.cleanup();

  const doc = await serveGoogleDoc({
    pageCookies: ["jar=container"],
    userContextId: 2,
  });
  try {
    await doc.getPageExtractor().getText({ sourceUrl: doc.sourceUrl });

    Assert.deepEqual(
      doc.viewCookies,
      ["jar=container"],
      "The view request uses the container's cookies, not the default ones."
    );
  } finally {
    await doc.cleanup();
  }
  Services.cookies.removeAll();
});

add_task(async function test_falls_back_when_the_view_stalls() {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();

  const viewRequested = Promise.withResolvers();
  let heldResponse;
  const doc = await serveGoogleDoc({
    viewHandler: (_request, response) => {
      response.processAsync();
      heldResponse = response;
      viewRequested.resolve();
    },
  });

  try {
    const resultPromise = doc.getPageExtractor().getText({
      sourceUrl: doc.sourceUrl,
    });
    await viewRequested.promise;
    await SpecialPowers.spawn(doc.browser, [], () => {
      ChromeUtils.importESModule(
        "moz-src:///toolkit/components/pageextractor/GoogleDocsExtraction.sys.mjs"
      ).expireViewReadsForTesting();
    });
    const result = await resultPromise;

    is(
      result.text,
      EDITOR_TEXT,
      "The generic walk is kept when the view never answers."
    );
    is(doc.viewRequests.length, 1, "The view was requested.");
    await Services.fog.testFlushAllChildren();
    const phaseEvent = Glean.pageExtractor.phase
      .testGetValue("page-extractor")
      ?.findLast(e => e.extra.phase === "google-docs-extract");
    is(phaseEvent?.extra.status, "error", "The Google Docs phase failed.");
    is(
      phaseEvent?.extra.error_name,
      "TimeoutError",
      "The Google Docs phase failed because the view timed out."
    );
  } finally {
    heldResponse.finish();
    await doc.cleanup();
  }
});

add_task(async function test_no_view_request() {
  const cases = [
    {
      name: "only the viewport is wanted",
      options: { justViewport: true },
    },
    {
      name: "the page is on another host",
      options: { sourceUrl: "https://example.com/document/d/testdoc/edit" },
    },
    {
      name: "the page is a spreadsheet",
      options: { sourceUrl: "https://docs.google.com/spreadsheets/d/x/edit" },
    },
    {
      name: "the document is published to the web",
      options: { sourceUrl: "https://docs.google.com/document/d/e/x/pub" },
    },
    {
      name: "the page is not served over https",
      // eslint-disable-next-line sdl/no-insecure-url
      options: { sourceUrl: "http://docs.google.com/document/d/testdoc/edit" },
    },
  ];

  for (const { name, options } of cases) {
    const doc = await serveGoogleDoc();
    try {
      const result = await doc.getPageExtractor().getText({
        sourceUrl: doc.sourceUrl,
        ...options,
      });

      is(result.text, EDITOR_TEXT, `The generic walk is used when ${name}.`);
      is(doc.viewRequests.length, 0, `No view is requested when ${name}.`);
    } finally {
      await doc.cleanup();
    }
  }
});

add_task(async function test_no_view_request_when_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.pageextractor.googledocs.enabled", false]],
  });
  const doc = await serveGoogleDoc();
  try {
    const result = await doc.getPageExtractor().getText({
      sourceUrl: doc.sourceUrl,
    });
    is(result.text, EDITOR_TEXT, "The generic walk is used when disabled.");
    is(doc.viewRequests.length, 0, "No view is requested when disabled.");
  } finally {
    await doc.cleanup();
  }
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_view_at_the_cap_is_read_whole() {
  const doc = await serveGoogleDoc({
    viewHandler: serveView(viewOfSize(MAX_DOWNLOAD_BYTES)),
  });
  try {
    const result = await doc.getPageExtractor().getText({
      sourceUrl: doc.sourceUrl,
    });

    is(
      result.text,
      "Start marker\nEnd marker",
      "A view exactly at the download cap is read to the end."
    );
  } finally {
    await doc.cleanup();
  }
});

add_task(async function test_download_cap() {
  const doc = await serveGoogleDoc({
    viewHandler: serveView(viewOfSize(MAX_DOWNLOAD_BYTES + 1024)),
  });
  try {
    const result = await doc.getPageExtractor().getText({
      sourceUrl: doc.sourceUrl,
    });

    is(
      result.text,
      "Start marker",
      "A view cut short by the download cap still yields the part that arrived."
    );
  } finally {
    await doc.cleanup();
  }
});
