/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests for custom DOM extraction strategies for search engine result pages.
 * These tests verify that the DOMExtractor applies site-specific extraction
 * rules when a sourceUrl option is provided.
 */

const SAMPLE_HTML = `<div class="MjjYud">
  <div class="A6K0A" data-rpos="12">
    <div ...>
      <div class="N54PNb BToiNc" data-snc="auw0Ab">
        <div class="kb0PBd A9Y9g jGGQ5e" data-snf="x5WNvb" data-snhf="0">
          <div class="yuRUbf">
            <div class="b8lM7">
              <span class="V9tjod" jsaction="trigger.mLt3mc">
                <a href="https://www.firefox.com/en-US/download/all/" ...>
                  <h3>
                    Choose which Firefox Browser to download in your language
                  </h3>
                  <br />
                  <div class="notranslate ESMNde HGLrXd ojE3Fb">
                    <div class="q0vns">
                      <span class="DDKf1c"><div class="eqA2re UnOTSe Vwoesf" aria-hidden="true">
                          <img .../></div
                      ></span>
                      <div class="CA5RN">
                        <div><span class="VuuXrf">Firefox</span></div>
                        <div class="byrV5b">
                          <cite ... role="text" >https://www.firefox.com<span> › en-US › download › all</span></cite>
                        </div>
                      </div>
                    </div>
                  </div>
                </a>
              </span>
              <div class="B6fmyf byrV5b Mg1HEd">
                <div class="HGLrXd ojE3Fb">
                  <div class="q0vns">
                    <span class="DDKf1c"><div...></div></span>
                    <div class="CA5RN">
                      <div><span class="VuuXrf">Firefox</span></div>
                      <div class="byrV5b">
                        <cite ... role="text">https://www.firefox.com<span ... role="text">› en-US › download › all</span></cite>
                      </div>
                    </div>
                  </div>
                </div>
                <div class="csDOgf BCF2pd ezY6nb L48a4c">
                  <div ...>
                    <div ...>
                      <span class="D6lY4c"><span ...><svg>...</svg></span></span>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
        <div ...>
          <div ...>
            <span
              ><em>Choose which Firefox Browser to download in your language</em
              >. Everyone deserves access to the internet — your language should
              never be a barrier.</span
            >
          </div>
        </div>
      </div>
    </div>
  </div>
</div>`;

/**
 * Test extraction without sourceUrl option (default behavior).
 */
add_task(async function test_extraction_without_page_url() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`${SAMPLE_HTML}`;
  const actor = getPageExtractor();

  const result = await actor.getText({});

  const expected = [
    "[Choose which Firefox Browser to download in your language](https://www.firefox.com/en-US/download/all/)",
    "Firefox",
    "https://www.firefox.com › en-US › download › all",
    "Firefox",
    "https://www.firefox.com› en-US › download › all",
    "Choose which Firefox Browser to download in your language. Everyone deserves access to the internet — your language should never be a barrier.",
  ].join("\n");

  is(
    result.text,
    expected,
    "Without a sourceUrl, the default strategy should preserve cite breadcrumbs and format block anchors as markdown"
  );

  return cleanup();
});

/**
 * Test that the Google search extraction strategy matches various Google domains.
 */
add_task(async function test_google_search_domain_matching() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`${SAMPLE_HTML}`;
  const actor = getPageExtractor();

  const googleDomains = [
    "https://www.google.com/search?q=test",
    "https://www.google.com/search?q=test",
    "https://www.google.co.uk/search?q=test",
    "https://www.google.fr/search?q=test",
    "https://www.google.com.au/search?q=test",
    "https://www.google.de/search?q=test",
  ];

  const expected = [
    "[Choose which Firefox Browser to download in your language](https://www.firefox.com/en-US/download/all/)",
    "Firefox",
    "Firefox",
    "Choose which Firefox Browser to download in your language. Everyone deserves access to the internet — your language should never be a barrier.",
  ].join("\n");

  for (const url of googleDomains) {
    const result = await actor.getText({ sourceUrl: url });
    is(
      result.text,
      expected,
      `Google strategy should be applied (cite excluded, block anchor formatted as markdown) for ${url}`
    );
  }

  return cleanup();
});

/**
 * Test that non-Google URLs do not trigger the google search extraction strategy:
 * they keep cite elements, and block anchors are still formatted as markdown
 * links, as they are on every page.
 */
add_task(async function test_non_google_sites_preserve_default_strategy() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`${SAMPLE_HTML}`;
  const actor = getPageExtractor();

  const nonGoogleUrls = [
    // other search engines
    "https://www.bing.com/search?q=test",
    "https://duckduckgo.com/?q=test",
    "https://www.example.com/search",

    // google subdomains
    "https://mail.google.com/search?q=test",
    "https://calendar.google.com/search?q=test",
    "https://maps.google.com/search?q=test",
    "https://photos.google.com/search?q=test",

    // other google pages
    "https://www.google.com/",
    "https://www.google.com/about",
    "https://www.google.com/maps",
    "https://www.google.com/images",
    "https://www.google.com/search-history",
    "https://www.google.com/searchpreferences",

    // edge cases
    "https://www.googlesearch.com/results?q=test",
    "https://www.thegoogle.com/search?q=test",
    "https://www.google-search.org/search?q=test",
    "https://notgoogle.com/search?q=test",
    "https://www.google.com.fake.com/search?q=test",
    "https://GOOGLE.COM/SEARCH?Q=TEST",
    "https://www.google.com/search",
    "https://www.google.com/search/",
    "https://www.google.com/search#q=test",
    "google.com/search?q=test",
    "www.google.com/search?q=test",
  ];

  const expected = [
    "[Choose which Firefox Browser to download in your language](https://www.firefox.com/en-US/download/all/)",
    "Firefox",
    "https://www.firefox.com › en-US › download › all",
    "Firefox",
    "https://www.firefox.com› en-US › download › all",
    "Choose which Firefox Browser to download in your language. Everyone deserves access to the internet — your language should never be a barrier.",
  ].join("\n");

  for (const url of nonGoogleUrls) {
    const result = await actor.getText({ sourceUrl: url });
    is(
      result.text,
      expected,
      `Default strategy should be applied (cite included, block anchor formatted as markdown) for ${url}`
    );
  }

  return cleanup();
});

/**
 * Test that Google search strategy handles multiple cite elements.
 */
add_task(
  async function test_google_search_filter_selector_removes_cite_elements() {
    const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
    const { getPageExtractor, cleanup } = await html`
      <div>
        <div class="result">
          <a href="https://example1.com/path">
            <h3>Result 1 Title</h3>
            <cite>https://example1.com &gt; path1</cite>
            <p>First result description.</p>
          </a>
        </div>
        <div class="result">
          <a href="https://example2.com/path">
            <h3>Result 2 Title</h3>
            <cite>https://example2.com &gt; path2</cite>
            <p>Second result description.</p>
          </a>
        </div>
        <div class="result">
          <a href="https://example3.com/path">
            <h3>Result 3 Title</h3>
            <cite>https://example3.com &gt; path3</cite>
            <p>Third result description.</p>
          </a>
        </div>
      </div>
    `;
    const actor = getPageExtractor();

    const result = await actor.getText({
      sourceUrl: "https://www.google.com/search?q=test",
    });

    const expected = [
      "[Result 1 Title](https://example1.com/path)",
      "First result description.",
      "[Result 2 Title](https://example2.com/path)",
      "Second result description.",
      "[Result 3 Title](https://example3.com/path)",
      "Third result description.",
    ].join("\n");

    is(
      result.text,
      expected,
      "Cite elements should be removed and block anchors should be formatted as markdown"
    );
    // Links should still be captured
    Assert.deepEqual(
      result.links,
      [
        "https://example1.com/path",
        "https://example2.com/path",
        "https://example3.com/path",
      ],
      "Links should be extracted from search results"
    );

    return cleanup();
  }
);

/**
 * Test that the filter selector removes matches that are not inside any anchor,
 * so removal is independent of block-anchor markdown formatting.
 */
add_task(async function test_google_search_filter_selector_outside_anchor() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <div>
      <p>Preamble text.</p>
      <cite>https://example.com &gt; standalone</cite>
      <p>Following description.</p>
    </div>
  `;
  const actor = getPageExtractor();

  const result = await actor.getText({
    sourceUrl: "https://www.google.com/search?q=test",
  });

  const expected = ["Preamble text.", "Following description."].join("\n");

  is(
    result.text,
    expected,
    "Cite outside any anchor should be removed; surrounding text should be preserved"
  );

  return cleanup();
});

/**
 * Test that anchors wrapping block content are formatted as markdown links
 * with cite elements excluded from the link text.
 */
add_task(async function test_google_search_markdown_deduped_per_block() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <div>
      <a href="https://example.com/article">
        <div>
          <h3>Article Title</h3>
        </div>
        <cite>https://example.com &gt; article</cite>
        <p>Shallow paragraph.</p>
        <div>
          <div>
            <p>Deeply nested paragraph.</p>
          </div>
        </div>
      </a>
    </div>
  `;
  const actor = getPageExtractor();

  const result = await actor.getText({
    sourceUrl: "https://www.google.com/search?q=test",
  });

  const expected = [
    "[Article Title](https://example.com/article)",
    "Shallow paragraph.",
    "Deeply nested paragraph.",
  ].join("\n");

  is(
    result.text,
    expected,
    "Block anchor wrapping descendants at varying depths should be formatted as markdown exactly once"
  );

  return cleanup();
});

/**
 * Test that block anchors WITHOUT cite elements are NOT formatted as markdown
 * even on Google search pages.
 */
add_task(async function test_google_search_block_links_with_selector() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <div>
      <a href="https://example.com/with-cite">
        <h3>Has Cite Title</h3>
        <cite>https://example.com &gt; with-cite</cite>
        <p>Description with cite.</p>
      </a>
      <a href="https://example.com/no-cite">
        <h3>No Cite Title</h3>
        <p>Description without cite.</p>
      </a>
    </div>
  `;
  const actor = getPageExtractor();

  const result = await actor.getText({
    sourceUrl: "https://www.google.com/search?q=test",
  });

  const expected = [
    "[Has Cite Title](https://example.com/with-cite)",
    "Description with cite.",
    "No Cite Title",
    "Description without cite.",
  ].join("\n");

  is(
    result.text,
    expected,
    "Block anchors with a cite descendant should be formatted as markdown; without a cite they shouldn't be formatted as markdown"
  );

  return cleanup();
});

/**
 * Test that inline anchors within paragraph text are formatted as markdown links.
 * Unlike block anchors, inline link formatting applies regardless of sourceUrl.
 */
add_task(async function test_inline_links_formatted_as_markdown() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <p>
      Visit <a href="https://example.com/page">the example page</a> for more
      information.
    </p>
  `;
  const actor = getPageExtractor();

  const expected =
    "Visit [the example page](https://example.com/page) for more information.";

  for (const url of [
    "https://www.google.com/search?q=test",
    "https://example.com/article",
    undefined,
  ]) {
    const result = await actor.getText({ sourceUrl: url });
    is(
      result.text,
      expected,
      `Inline anchor should be formatted as markdown for sourceUrl=${url}`
    );
  }
  await cleanup();
});

/**
 * Test that multiple inline anchors within the same block are all formatted as markdown.
 */
add_task(async function test_multiple_inline_links_in_block() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <p>
      See <a href="https://example.com/a">link A</a> and
      <a href="https://example.com/b">link B</a> for details.
    </p>
  `;
  const actor = getPageExtractor();

  const result = await actor.getText({
    sourceUrl: "https://www.google.com/search?q=test",
  });

  const expected =
    "See [link A](https://example.com/a) and [link B](https://example.com/b) for details.";

  is(
    result.text,
    expected,
    "Multiple inline anchors in the same block should each be formatted as markdown"
  );

  return cleanup();
});
