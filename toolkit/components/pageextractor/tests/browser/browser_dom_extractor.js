/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * @import { BrowserTestUtils } from "../../../../../testing/mochitest/BrowserTestUtils/BrowserTestUtils.sys.mjs"
 * @import { PageExtractorParent } from "../../PageExtractorParent.sys.mjs"
 */

add_task(async function test_dom_extractor_default_options() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <article>
      <h1>Hello World</h1>
      <p>This is a paragraph</p>
    </article>
  `;
  const actor = getPageExtractor();

  is(
    (await actor.getText()).text,
    ["Hello World", "This is a paragraph"].join("\n"),
    "Text can be extracted from the page."
  );

  is(
    (
      await actor.getText({
        removeBoilerplate: true,
        _forceRemoveBoilerplate: true,
      })
    ).text,
    "Hello World\nThis is a paragraph",
    "Reader mode can extract page content."
  );

  return cleanup();
});

add_task(async function test_dom_extractor_sufficient_length_option() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <article>
      <h1>Hello World</h1>
      <p>First paragraph.</p>
      <p>Second paragraph.</p>
    </article>
  `;

  const header = "Hello World";
  const headerAndP1 = ["Hello World", "First paragraph."].join("\n");
  const allText = ["Hello World", "First paragraph.", "Second paragraph."].join(
    "\n"
  );

  is(
    (await getPageExtractor().getText()).text,
    allText,
    "All text is returned with the default options."
  );

  const max = allText.length + 1;
  const expectations = [
    [length => length === 0, ""],
    [length => length > 0 && length <= 12, header],
    [length => length > 12 && length <= 29, headerAndP1],
    [length => length > 29 && length <= max, allText],
  ];

  for (let sufficientLength = 0; sufficientLength <= max; ++sufficientLength) {
    let expectedValue;

    for (const [predicate, value] of expectations) {
      if (predicate(sufficientLength)) {
        expectedValue = value;
      }
    }

    is(
      (await getPageExtractor().getText({ sufficientLength })).text,
      expectedValue,
      `The text, given sufficientLength of ${sufficientLength}, matches the expectation.`
    );
  }

  return cleanup();
});

add_task(
  async function test_dom_extractor_sufficient_length_with_boilerplate_removal() {
    const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
    const { getPageExtractor, cleanup } = await html`
      <article>
        <p>
          This is some article text that has some kind of sufficient length. It
          is going to try and get some text at a sentence boundary.
        </p>
        <p>This will get cut off.</p>
      </article>
    `;

    // Note how this only wants up to the middle of a sentence. It stops at a DOM boundary though.
    const sufficientLength =
      "This is some article text that has some kind of sufficient length. It is going to try and"
        .length;

    is(
      (
        await getPageExtractor().getText({
          sufficientLength,
          removeBoilerplate: true,
          _forceRemoveBoilerplate: true,
        })
      ).text,
      [
        "This is some article text that has some kind of sufficient length. It",
        "is going to try and get some text at a sentence boundary.",
        // "This will get cut off." <- note how this isn't here.
      ].join("\n"),
      "The text is cutoff at a period."
    );

    return cleanup();
  }
);

/**
 * Unit test whitespace collapsing
 */
add_task(function test_whitespace_collapse() {
  const { collapseWhitespace } = ChromeUtils.importESModule(
    "moz-src:///toolkit/components/pageextractor/DOMExtractor.sys.mjs"
  );

  Assert.equal(
    collapseWhitespace("  collapsed   text   "),
    " collapsed text ",
    "Normal whitespace gets properly collapsed"
  );

  Assert.equal(
    collapseWhitespace("  \n collapsed \n  \n\n text   \n"),
    "\ncollapsed\n\ntext\n",
    "Text with newlines gets properly collapsed"
  );

  Assert.equal(
    collapseWhitespace("  \t collapsed \t  \t\t text   \t"),
    " collapsed text ",
    "Tabs get ignored"
  );

  Assert.equal(
    collapseWhitespace("  \n collapsed \n\r \r \n\n text   \n"),
    "\ncollapsed\n\ntext\n",
    "Tabs get ignored"
  );

  Assert.equal(
    collapseWhitespace("    next", "previous   "),
    "previous\nnext",
    "Whitespace on the previous text gets properly collapsed into a single newline."
  );

  Assert.equal(
    collapseWhitespace("    next", "previous \n  "),
    "previous\n\nnext",
    "Whitespace on the previous text gets placed into two newlines"
  );

  Assert.equal(
    collapseWhitespace("    next", "previous \n \n\n  "),
    "previous\n\nnext",
    "Whitespace connected chunks of text has at most two newlines"
  );
});

add_task(async function test_dom_extractor_whitespace_collapse_reader_mode() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  // prettier-ignore
  const { getPageExtractor, cleanup } = await html`
      <article>
        <!-- A single newlines by count -->
        <pre>newlines\nrepeated</pre>
        <pre>newlines\n\nrepeated</pre>
        <pre>newlines\n\n\nrepeated</pre>
        <pre>newlines\n\n\n\nrepeated</pre>

        <!-- Mixed newline and spaces -->
        <pre>mixed\t\n \t newlines</pre>
        <pre>mixed\t\n \t \nnewlines</pre>
        <pre>mixed\t\n \t\r \n \n \tnewlines</pre>

        <!-- Space behavior -->
        <pre>space behavior</pre>
        <pre>space  behavior</pre>
        <pre>space   behavior</pre>
        <pre>space\tbehavior</pre>
        <pre>space\t \tbehavior</pre>
      </article>
    `;

  const actor = getPageExtractor();

  is(
    (
      await actor.getText({
        removeBoilerplate: true,
        _forceRemoveBoilerplate: true,
      })
    ).text,
    [
      "newlines\nrepeated",
      "newlines\n\nrepeated",
      "newlines\n\nrepeated",
      "newlines\n\nrepeated",
      "mixed\nnewlines",
      "mixed\n\nnewlines",
      "mixed\n\nnewlines",
      "space behavior",
      "space behavior",
      "space behavior",
      "space behavior",
      "space behavior",
    ].join("\n"),
    `Whitespace is collapsed in various capacities.`
  );

  return cleanup();
});

/**
 * Test whitespace collapse behavior.
 */
add_task(async function test_dom_extractor_normalize_whitespace() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  // Ignore prettier since newlines get rewritten making this test case harder to read.
  // prettier-ignore
  const { getPageExtractor, cleanup } = await html`
    <!-- The HTML algorithm already collapses whitespace -->
    <p>aaaa      aaaa</p>
    <p>bbbb\n\n\ncccc</p>

    <!-- Multiple blank tags are ignored -->
    <p></p>
    <p></p>
    <p></p>

    <!-- Even random types of whitespace is ignored. -->
    <p> </p>
    <p>&nbsp;</p>
    <p>&#9;</p>

    <!-- Here the whitespace is preserved -->
    <pre>dddd      dddd</pre>
    <pre>eeee\n\n\nffff</pre>
  `;

  is(
    (await getPageExtractor().getText()).text,

    [
      // The whitespace is removed by the html rendering algorithm..
      "aaaa aaaa",
      "bbbb cccc",
      // The whitespace is preserved by the <pre> tag.
      "dddd      dddd",
      "eeee",
      "",
      "",
      "ffff",
    ].join("\n"),
    `DOMExtractor has correct whitespace behavior.`
  );

  await cleanup();
});

add_task(
  async function test_dom_extractor_ignores_hidden_and_collapsed_nodes() {
    const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
    const { getPageExtractor, cleanup } = await html`
      <article>
        <!-- Visible header -->
        <h1>Visible Title</h1>

        <!-- Visible paragraph -->
        <p>Visible paragraph</p>

        <!-- Hidden via the [hidden] attribute -->
        <p hidden>Hidden via [hidden]</p>

        <!-- Hidden via display:none -->
        <p style="display:none">Hidden via display:none</p>

        <!-- Hidden via visibility:hidden -->
        <p style="visibility:hidden">Hidden via visibility:hidden</p>

        <!-- Hidden via opacity:0 -->
        <p style="opacity:0">Hidden via opacity:0</p>

        <!-- Hidden via zero-sized block container -->
        <div style="width:0; height:0; overflow:hidden">
          <span>Inline text within zero-sized block container</span>
        </div>

        <!-- Hidden via zero-width block container (non-zero height only) -->
        <div style="width:0; height:16px; overflow:hidden">
          <span>Inline text within zero-width (height>0) block container</span>
        </div>

        <!-- Hidden via zero-height block container (non-zero width only) -->
        <div style="width:16px; height:0; overflow:hidden">
          <span>Inline text within zero-height (width>0) block container</span>
        </div>

        <!-- Visible block within hidden inline container -->
        <span style="width:0; height:0; overflow:hidden">
          <div>Block text within zero-sized inline container</div>
        </span>

        <!-- Hidden block container with inline descendant -->
        <div hidden>
          Hidden container outer text
          <span>Hidden container inner text</span>
        </div>

        <!-- Visible block container with hidden inline descendant -->
        <div>
          Visible container outer text (hidden descendant)
          <span hidden>Hidden child text in visible container</span>
        </div>

        <!-- Hidden inline container with block descendant -->
        <span hidden>
          Hidden inline outer text
          <div>Hidden inline inner text</div>
        </span>

        <!-- Visible inline container with hidden block descendant -->
        <span>
          Visible inline outer text (hidden descendant)
          <div hidden>Hidden block descendant text</div>
        </span>

        <!-- Collapsed <details> with <summary> still visible -->
        <details>
          <summary>Summary is visible</summary>
          <div>Hidden inside closed details</div>
          Text node directly under closed details (hidden)
        </details>
      </article>
    `;

    const expected = [
      "Visible Title",
      "Visible paragraph",
      "Block text within zero-sized inline container",
      "Visible container outer text (hidden descendant)",
      "Visible inline outer text (hidden descendant)",
      "Summary is visible",
    ].join("\n");

    is(
      (await getPageExtractor().getText()).text,
      expected,
      "The extractor returns only visible text."
    );

    return cleanup();
  }
);

add_task(async function test_dom_extractor_inline_batching() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <div>This is a simple section.</div>
    <div>
      <a href="http://example.com">This entire</a>
      section continues in a
      <b>batch</b>.
    </div>
  `;

  is(
    (await getPageExtractor().getText()).text,
    [
      "This is a simple section.",
      "[This entire](http://example.com/) section continues in a batch.",
    ].join("\n"),
    "Inline content is grouped within block elements."
  );

  return cleanup();
});

// Tests comprehensive anchor element handling per the HTML specification.
// https://html.spec.whatwg.org/multipage/links.html
// https://developer.mozilla.org/en-US/docs/Web/HTML/Reference/Elements/a

add_task(async function test_dom_extractor_link_anchors() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <article>
      <h1>Comprehensive Anchor Test</h1>

      <p>Reach us via <a href="mailto:user@mozilla.org">Email</a>.</p>
      <p>
        <a href="https://example.com/external" target="_blank">New Tab Link</a>
      </p>
      <p>
        <a href="https://example.com/files/report.pdf" download
          >Download Attribute</a
        >
      </p>

      <a href="https://example.com/card" id="card-link">
        <div class="card">
          <h2>Card Title</h2>
          <p>Card description.</p>
        </div>
      </a>

      <p>
        <a href="https://example.com/mixed">
          Read <strong>bold text</strong> and <em>italic text</em> inside.
        </a>
      </p>

      <p>
        <a href="https://example.com/no-alt"><img src="decoration.png" /></a>
      </p>

      <p>
        <a href="https://example.com/with-alt"
          ><img src="logo.png" alt="Company Logo"
        /></a>
      </p>
    </article>
  `;

  const result = await getPageExtractor().getText();
  const { text, links } = result;

  const actualLines = text
    .split("\n")
    .map(line => line.trim())
    .filter(line => !!line.length);

  Assert.deepEqual(
    actualLines,
    [
      "Comprehensive Anchor Test",
      "Reach us via [Email](mailto:user@mozilla.org).",
      "[New Tab Link](https://example.com/external)",
      "[Download Attribute](https://example.com/files/report.pdf)",
      "[Card Title](https://example.com/card)",
      "Card description.",
      "[Read bold text and italic text inside.](https://example.com/mixed)",
      "[Company Logo](https://example.com/with-alt)",
    ],
    "Text output matches expected markdown format with various anchor types"
  );

  Assert.deepEqual(
    links,
    [
      "mailto:user@mozilla.org",
      "https://example.com/external",
      "https://example.com/files/report.pdf",
      "https://example.com/card",
      "https://example.com/mixed",
      "https://example.com/no-alt",
      "https://example.com/with-alt",
    ],
    "Links array contains all extracted href values"
  );

  await cleanup();
});

// Test that empty href resolves to current page URL via .href property
add_task(async function test_dom_extractor_empty_href() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <article>
      <p><a href="">Empty Href Link</a></p>
    </article>
  `;

  const result = await getPageExtractor().getText();
  const { text, links } = result;

  // Empty href resolves to current page URL via .href property
  Assert.ok(
    text.includes("[Empty Href Link](http"),
    `Empty href formatted as markdown with resolved URL: ${text}`
  );
  Assert.equal(links.length, 1, "One link extracted");
  Assert.ok(
    links[0].startsWith("http"),
    `Empty href resolves to page URL: ${links[0]}`
  );

  await cleanup();
});

// Original test case from Bug 1995618 - validates the core requirement
add_task(async function test_dom_extractor_links() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <article>
      <h1>Example of Links</h1>
      <ul>
        <li>
          Here is the
          <a href="https://example.com/first">First link</a>
        </li>
        <li>
          Now this is an <a href="https://example.com/link">external link</a>
        </li>
      </ul>
    </article>
  `;
  const { text, links } = await getPageExtractor().getText();

  const lines = text.split("\n").filter(l => l.trim());

  Assert.deepEqual(
    lines,
    [
      "Example of Links",
      "Here is the [First link](https://example.com/first)",
      "Now this is an [external link](https://example.com/link)",
    ],
    "Text output matches expected markdown format"
  );

  Assert.deepEqual(
    links,
    ["https://example.com/first", "https://example.com/link"],
    "Links array contains extracted href values"
  );

  return cleanup();
});

add_task(async function test_dom_extractor_inline_block_styling() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    Bare text is sent in a batch.
    <span>Inline text at the root is sent in a <b>batch</b>.</span>
    <div>
      <span style="display: block">Display "block"</span>
      overrides the inline designation.
    </div>
  `;

  is(
    (await getPageExtractor().getText()).text,
    [
      "Bare text is sent in a batch.",
      "Inline text at the root is sent in a batch.",
      'Display "block"',
      "overrides the inline designation.",
    ].join("\n"),
    "Inline and block styling are extracted as separate blocks."
  );

  return cleanup();
});

add_task(async function test_extractor_edge_cases() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <article>
      <p>
        <a href="https://example.com/1">Link with [Brackets]</a>
        <a href="https://example.com/2">Link with (Parens)</a>
        <a href="https://en.wikipedia.org/wiki/HTML_(standard)"
          >URL with Parens</a
        >
      </p>

      <p>
        <a
          href="
          https://example.com/messy
        "
          >Multiline Href</a
        >
      </p>

      <a href="https://example.com/card">
        <div class="card">
          <h3>Card Title</h3>
          <span>Description</span>
        </div>
      </a>
    </article>
  `;

  const result = await getPageExtractor().getText();
  const { text, links } = result;

  const lines = text
    .split("\n")
    .map(l => l.trim())
    .filter(l => l.length);

  // Assert on the entire text output - markdown-formatted with escaped special characters
  // and block-level elements extracted as separate lines
  Assert.deepEqual(
    lines,
    [
      "[Link with \\[Brackets\\]](https://example.com/1) [Link with \\(Parens\\)](https://example.com/2) [URL with Parens](https://en.wikipedia.org/wiki/HTML_%28standard%29)",
      "[Multiline Href](https://example.com/messy)",
      "[Card Title](https://example.com/card)",
      "Description",
    ],
    "Text output matches expected markdown format with escaped characters"
  );

  // Assert on the entire links array - .href provides normalized absolute URLs
  Assert.deepEqual(
    links,
    [
      "https://example.com/1",
      "https://example.com/2",
      "https://en.wikipedia.org/wiki/HTML_(standard)",
      "https://example.com/messy",
      "https://example.com/card",
    ],
    "Links array contains all extracted href values"
  );

  await cleanup();
});

// Test nested anchors - invalid HTML but browsers handle it by closing outer anchor.
// Per HTML5 spec, nested <a> tags are invalid. Browsers handle them by closing
// the outer anchor when the inner anchor is encountered:
//   <a href="outer">text1<a href="inner">text2</a></a>
// becomes effectively:
//   <a href="outer">text1</a><a href="inner">text2</a>
add_task(async function test_extractor_nested_anchors() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <article>
      <div>
        <a href="https://example.com/outer-link">
          An outer link.
          <a href="https://example.com/inner-link"> An inner link.</a>
        </a>
      </div>
    </article>
  `;

  const result = await getPageExtractor().getText();
  const { text, links } = result;

  const lines = text
    .split("\n")
    .map(l => l.trim())
    .filter(l => l.length);

  // Browser closes outer anchor when inner anchor is encountered, so we get two
  // separate anchors. Since both are inline elements within the same block (div),
  // they are extracted together on the same line.
  Assert.deepEqual(
    lines,
    [
      "[An outer link.](https://example.com/outer-link)[An inner link.](https://example.com/inner-link)",
    ],
    "Nested anchors are parsed as separate inline anchors by the browser"
  );

  Assert.deepEqual(
    links,
    ["https://example.com/outer-link", "https://example.com/inner-link"],
    "Both links are extracted from nested anchor structure"
  );

  await cleanup();
});

/**
 * Decode a snapshot blob into ImageData and check the pixel at (x, y).
 *
 * @param {Blob} blob
 * @param {number} width
 * @param {number} height
 * @param {number} x
 * @param {number} y
 * @param {number[]} expectedRGBA - [r, g, b, a] each 0-255
 * @param {number} [tolerance]
 */
async function assertSnapshotPixel(
  blob,
  width,
  height,
  x,
  y,
  expectedRGBA,
  tolerance = 5
) {
  const bitmap = await createImageBitmap(blob);
  const canvas = new OffscreenCanvas(width, height);
  const ctx = canvas.getContext("2d");
  ctx.drawImage(bitmap, 0, 0);
  bitmap.close();
  const pixel = ctx.getImageData(x, y, 1, 1).data;
  for (let i = 0; i < 4; i++) {
    Assert.lessOrEqual(
      Math.abs(pixel[i] - expectedRGBA[i]),
      tolerance,
      `Pixel (${x},${y}) channel ${i}: expected ~${expectedRGBA[i]}, got ${pixel[i]}`
    );
  }
}

add_task(async function test_canvas_snapshot_basic() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <canvas id="test" width="200" height="200"></canvas>
    <script>
      const ctx = document.getElementById("test").getContext("2d");
      ctx.fillStyle = "red";
      ctx.fillRect(0, 0, 200, 200);
    </script>
  `;

  const { canvasSnapshots } = await getPageExtractor().getText({
    includeCanvasSnapshots: true,
  });

  is(canvasSnapshots.length, 1, "One canvas captured");
  is(canvasSnapshots[0].blob.type, "image/webp", "Format is WebP");
  is(canvasSnapshots[0].width, 200, "Width preserved");
  is(canvasSnapshots[0].height, 200, "Height preserved");

  await assertSnapshotPixel(
    canvasSnapshots[0].blob,
    200,
    200,
    100,
    100,
    [255, 0, 0, 255]
  );

  return cleanup();
});

add_task(async function test_canvas_snapshot_multiple_limited() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <canvas id="c1" width="100" height="100"></canvas>
    <canvas id="c2" width="100" height="100"></canvas>
    <canvas id="c3" width="100" height="100"></canvas>
    <canvas id="c4" width="100" height="100"></canvas>
    <script>
      for (const canvas of document.querySelectorAll("canvas")) {
        const ctx = canvas.getContext("2d");
        ctx.fillStyle = "blue";
        ctx.fillRect(0, 0, 100, 100);
      }
    </script>
  `;

  const { canvasSnapshots } = await getPageExtractor().getText({
    includeCanvasSnapshots: true,
    maxCanvasCount: 2,
  });

  is(canvasSnapshots.length, 2, "Limited to 2 canvases");

  return cleanup();
});

add_task(async function test_canvas_snapshot_scaling() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <canvas id="large" width="2000" height="1000"></canvas>
    <script>
      const ctx = document.getElementById("large").getContext("2d");
      ctx.fillStyle = "green";
      ctx.fillRect(0, 0, 2000, 1000);
    </script>
  `;

  const { canvasSnapshots } = await getPageExtractor().getText({
    includeCanvasSnapshots: true,
    maxCanvasDimension: 500,
  });

  is(canvasSnapshots.length, 1, "One canvas captured");
  is(canvasSnapshots[0].width, 500, "Scaled width to max dimension");
  is(canvasSnapshots[0].height, 250, "Height scaled proportionally");

  return cleanup();
});

add_task(async function test_canvas_snapshot_viewport_filtering() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <canvas
      id="visible"
      width="100"
      height="100"
      style="position: fixed; top: 10px; left: 10px;"
    ></canvas>
    <canvas
      id="offscreen"
      width="100"
      height="100"
      style="position: fixed; top: -500px; left: -500px;"
    ></canvas>
    <script>
      for (const canvas of document.querySelectorAll("canvas")) {
        const ctx = canvas.getContext("2d");
        ctx.fillStyle = "purple";
        ctx.fillRect(0, 0, 100, 100);
      }
    </script>
  `;

  const { canvasSnapshots } = await getPageExtractor().getText({
    includeCanvasSnapshots: true,
    justViewport: true,
  });

  is(canvasSnapshots.length, 1, "Only visible canvas captured");

  return cleanup();
});

add_task(async function test_canvas_snapshot_min_size_filter() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <canvas id="large" width="100" height="100"></canvas>
    <canvas id="small" width="30" height="30"></canvas>
    <script>
      for (const canvas of document.querySelectorAll("canvas")) {
        const ctx = canvas.getContext("2d");
        ctx.fillStyle = "orange";
        ctx.fillRect(0, 0, canvas.width, canvas.height);
      }
    </script>
  `;

  const { canvasSnapshots } = await getPageExtractor().getText({
    includeCanvasSnapshots: true,
    minCanvasSize: 50,
  });

  is(canvasSnapshots.length, 1, "Only large canvas captured");
  is(canvasSnapshots[0].width, 100, "Large canvas captured");

  return cleanup();
});

add_task(async function test_canvas_snapshot_shadow_dom() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <div id="host"></div>
    <script>
      const host = document.getElementById("host");
      const shadow = host.attachShadow({ mode: "open" });
      const canvas = document.createElement("canvas");
      canvas.width = 100;
      canvas.height = 100;
      const ctx = canvas.getContext("2d");
      ctx.fillStyle = "cyan";
      ctx.fillRect(0, 0, 100, 100);
      shadow.appendChild(canvas);
    </script>
  `;

  const { canvasSnapshots } = await getPageExtractor().getText({
    includeCanvasSnapshots: true,
  });

  is(canvasSnapshots.length, 1, "Shadow DOM canvas captured");
  is(canvasSnapshots[0].blob.type, "image/webp", "Format is WebP");

  await assertSnapshotPixel(
    canvasSnapshots[0].blob,
    100,
    100,
    50,
    50,
    [0, 255, 255, 255]
  );

  return cleanup();
});

add_task(async function test_canvas_snapshot_closed_shadow_dom() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <div id="host"></div>
    <script>
      const host = document.getElementById("host");
      const shadow = host.attachShadow({ mode: "closed" });
      const canvas = document.createElement("canvas");
      canvas.width = 100;
      canvas.height = 100;
      const ctx = canvas.getContext("2d");
      ctx.fillStyle = "magenta";
      ctx.fillRect(0, 0, 100, 100);
      shadow.appendChild(canvas);
    </script>
  `;

  const { canvasSnapshots } = await getPageExtractor().getText({
    includeCanvasSnapshots: true,
  });

  is(canvasSnapshots.length, 1, "Closed shadow DOM canvas captured");
  is(canvasSnapshots[0].blob.type, "image/webp", "Format is WebP");

  await assertSnapshotPixel(
    canvasSnapshots[0].blob,
    100,
    100,
    50,
    50,
    [255, 0, 255, 255]
  );

  return cleanup();
});

add_task(async function test_canvas_snapshot_crossorigin() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, tab, cleanup } = await html`
    <canvas id="crossorigin" width="200" height="200"></canvas>
    <img
      id="crossorigin-img"
      src="https://example.com/browser/browser/base/content/test/browser-general/moz.png"
    />
    <script>
      window.drawCrossOriginImage = () => {
        return new Promise(resolve => {
          const img = document.getElementById("crossorigin-img");
          const canvas = document.getElementById("crossorigin");
          const ctx = canvas.getContext("2d");
          if (img.complete) {
            ctx.drawImage(img, 0, 0);
            resolve();
          } else {
            img.onload = () => {
              ctx.drawImage(img, 0, 0);
              resolve();
            };
          }
        });
      };
    </script>
  `;

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.drawCrossOriginImage();
  });

  const { canvasSnapshots } = await getPageExtractor().getText({
    includeCanvasSnapshots: true,
  });

  is(canvasSnapshots.length, 1, "Cross-origin canvas captured");
  is(canvasSnapshots[0].blob.type, "image/webp", "Format is WebP");
  is(canvasSnapshots[0].width, 200, "Width preserved");

  return cleanup();
});

add_task(async function test_canvas_snapshot_empty() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <canvas id="empty" width="100" height="100"></canvas>
  `;

  const { canvasSnapshots } = await getPageExtractor().getText({
    includeCanvasSnapshots: true,
  });

  is(canvasSnapshots.length, 1, "Empty canvas captured");
  is(canvasSnapshots[0].blob.type, "image/webp", "Format is WebP");

  return cleanup();
});

add_task(async function test_canvas_snapshot_webp_format() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <canvas id="test" width="100" height="100"></canvas>
    <script>
      const ctx = document.getElementById("test").getContext("2d");
      ctx.fillStyle = "yellow";
      ctx.fillRect(0, 0, 100, 100);
    </script>
  `;

  const { canvasSnapshots } = await getPageExtractor().getText({
    includeCanvasSnapshots: true,
    canvasQuality: 0.5,
  });

  is(canvasSnapshots.length, 1, "Canvas captured");
  is(canvasSnapshots[0].blob.type, "image/webp", "Format is WebP");
  Assert.greater(canvasSnapshots[0].blob.size, 0, "Blob has content");

  return cleanup();
});

add_task(async function test_canvas_snapshot_quality_impact() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <canvas id="test" width="200" height="200"></canvas>
    <script>
      const ctx = document.getElementById("test").getContext("2d");
      const gradient = ctx.createLinearGradient(0, 0, 200, 200);
      gradient.addColorStop(0, "red");
      gradient.addColorStop(0.5, "green");
      gradient.addColorStop(1, "blue");
      ctx.fillStyle = gradient;
      ctx.fillRect(0, 0, 200, 200);
    </script>
  `;

  const highQuality = await getPageExtractor().getText({
    includeCanvasSnapshots: true,
    canvasQuality: 0.95,
  });

  const lowQuality = await getPageExtractor().getText({
    includeCanvasSnapshots: true,
    canvasQuality: 0.1,
  });

  Assert.greater(
    highQuality.canvasSnapshots[0].blob.size,
    lowQuality.canvasSnapshots[0].blob.size,
    "Higher quality produces larger blob"
  );

  is(highQuality.canvasSnapshots[0].blob.type, "image/webp", "Format is WebP");
  is(lowQuality.canvasSnapshots[0].blob.type, "image/webp", "Format is WebP");

  return cleanup();
});

add_task(async function test_canvas_snapshot_webgl() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <canvas id="webgl" width="100" height="100"></canvas>
    <script>
      const canvas = document.getElementById("webgl");
      const gl = canvas.getContext("webgl", { preserveDrawingBuffer: true });
      gl.clearColor(1.0, 0.0, 0.0, 1.0);
      gl.clear(gl.COLOR_BUFFER_BIT);
    </script>
  `;

  const { canvasSnapshots } = await getPageExtractor().getText({
    includeCanvasSnapshots: true,
  });

  is(canvasSnapshots.length, 1, "WebGL canvas captured");
  is(canvasSnapshots[0].blob.type, "image/webp", "Format is WebP");

  await assertSnapshotPixel(
    canvasSnapshots[0].blob,
    100,
    100,
    50,
    50,
    [255, 0, 0, 255]
  );

  return cleanup();
});

add_task(async function test_canvas_snapshot_disabled_by_default() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <canvas id="test" width="100" height="100"></canvas>
    <script>
      const ctx = document.getElementById("test").getContext("2d");
      ctx.fillStyle = "red";
      ctx.fillRect(0, 0, 100, 100);
    </script>
  `;

  const { canvasSnapshots } = await getPageExtractor().getText({});

  is(canvasSnapshots.length, 0, "No canvas captured when option disabled");

  return cleanup();
});

add_task(async function test_canvas_snapshot_with_text_extraction() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <h1>Page Title</h1>
    <p>Some text content</p>
    <canvas id="test" width="100" height="100"></canvas>
    <script>
      const ctx = document.getElementById("test").getContext("2d");
      ctx.fillStyle = "blue";
      ctx.fillRect(0, 0, 100, 100);
    </script>
  `;

  const { text, links, canvasSnapshots } = await getPageExtractor().getText({
    includeCanvasSnapshots: true,
  });

  ok(text.includes("Page Title"), "Text extracted");
  ok(text.includes("Some text content"), "Paragraph text extracted");
  Assert.deepEqual(links, [], "No links on page");
  is(canvasSnapshots.length, 1, "Canvas captured alongside text");

  return cleanup();
});

// useSimpleText

add_task(async function test_dom_extractor_simple_text_anchors() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  // The same markup as test_dom_extractor_link_anchors, so this asserts the
  // simplified form of an output that is already pinned down above.
  const { getPageExtractor, cleanup } = await html`
    <article>
      <h1>Comprehensive Anchor Test</h1>

      <p>Reach us via <a href="mailto:user@mozilla.org">Email</a>.</p>

      <p>Literal text with [name](value).</p>

      <p>
        <a href="https://example.com/external" target="_blank">New Tab Link</a>
      </p>

      <a href="https://example.com/card" id="card-link">
        <div class="card">
          <h2>Card Title</h2>
          <p>Card description.</p>
        </div>
      </a>

      <p>
        <a href="https://example.com/mixed">
          Read <strong>bold text</strong> and <em>italic text</em> inside.
        </a>
      </p>

      <p>
        <a href="https://example.com/with-alt"
          ><img src="logo.png" alt="Company Logo"
        /></a>
      </p>
    </article>
  `;

  const { text, links } = await getPageExtractor().getText({
    useSimpleText: true,
  });

  const actualLines = text
    .split("\n")
    .map(line => line.trim())
    .filter(line => !!line.length);

  Assert.deepEqual(
    actualLines,
    [
      "Comprehensive Anchor Test",
      "Reach us via Email.",
      "Literal text with [name](value).",
      "New Tab Link",
      "Card Title",
      "Card description.",
      "Read bold text and italic text inside.",
      "Company Logo",
    ],
    "Anchors keep their text and lose their target."
  );

  Assert.deepEqual(
    links,
    [
      "mailto:user@mozilla.org",
      "https://example.com/external",
      "https://example.com/card",
      "https://example.com/mixed",
      "https://example.com/with-alt",
    ],
    "Every href is still reported even though none appear in the text."
  );

  return cleanup();
});

add_task(async function test_dom_extractor_simple_text_whitespace() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  // The same markup as test_dom_extractor_normalize_whitespace, which asserts
  // this document extracts as "aaaa aaaa\nbbbb cccc\ndddd      dddd\neeee\n\n\nffff".
  // prettier-ignore
  const { getPageExtractor, cleanup } = await html`
    <p>aaaa      aaaa</p>
    <p>bbbb\n\n\ncccc</p>

    <pre>dddd      dddd</pre>
    <pre>eeee\n\n\nffff</pre>
  `;

  is(
    (await getPageExtractor().getText({ useSimpleText: true })).text,

    [
      "aaaa aaaa",
      "bbbb cccc",
      // Runs of spaces preserved by <pre> collapse, since they are within a block.
      "dddd dddd",
      // The blank lines between these collapse to a single paragraph break.
      "eeee",
      "ffff",
    ].join("\n"),
    "Whitespace collapses within a block while paragraph breaks survive."
  );

  return cleanup();
});

add_task(async function test_dom_extractor_simple_text_with_site_strategy() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <div>
      <a href="https://example.com/result">
        <div>
          <h3>Result Title</h3>
          <cite>https://example.com › path</cite>
        </div>
      </a>
    </div>
  `;

  const { text } = await getPageExtractor().getText({
    useSimpleText: true,
    sourceUrl: "https://www.google.com/search?q=test",
  });

  ok(
    text.includes("Result Title"),
    `The block text survives simplification: ${JSON.stringify(text)}`
  );
  ok(
    !text.includes("›"),
    `The site strategy's cite filter still applies: ${JSON.stringify(text)}`
  );
  ok(
    !text.includes("]("),
    `Markdown from formatBlockAnchorsAsMarkdown is flattened: ${JSON.stringify(text)}`
  );

  return cleanup();
});

add_task(async function test_dom_extractor_block_anchor_links() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <article>
      <h1>Hotels</h1>
      <a href="https://example.com/hotel-a">
        <div>
          <h2>Hotel A</h2>
          <p>Scored 8.9</p>
        </div>
      </a>
      <a href="https://example.com/hotel-b">
        <div>
          <div>Scored 9.1</div>
          <h3>Hotel B</h3>
          <p>Free cancellation</p>
        </div>
      </a>
      <a href="https://example.com/hotel-c">
        <div>
          <p>Hotel C</p>
          <p>Scored 7.6</p>
        </div>
      </a>
      <p>See <a href="https://example.com/all">all hotels</a>.</p>
    </article>
  `;
  const actor = getPageExtractor();
  const lines = text =>
    text
      .split("\n")
      .map(line => line.trim())
      .filter(line => !!line.length);

  const { text, links } = await actor.getText();
  Assert.deepEqual(
    lines(text),
    [
      "Hotels",
      "[Hotel A](https://example.com/hotel-a)",
      "Scored 8.9",
      "Scored 9.1",
      "[Hotel B](https://example.com/hotel-b)",
      "Free cancellation",
      "[Hotel C](https://example.com/hotel-c)",
      "Scored 7.6",
      "See [all hotels](https://example.com/all).",
    ],
    "Each anchor around block content links its heading, or else its first block."
  );
  Assert.deepEqual(
    links,
    [
      "https://example.com/hotel-a",
      "https://example.com/hotel-b",
      "https://example.com/hotel-c",
      "https://example.com/all",
    ],
    "Every anchor is collected once."
  );

  Assert.deepEqual(
    lines((await actor.getText({ useSimpleText: true })).text),
    [
      "Hotels",
      "Hotel A",
      "Scored 8.9",
      "Scored 9.1",
      "Hotel B",
      "Free cancellation",
      "Hotel C",
      "Scored 7.6",
      "See all hotels.",
    ],
    "useSimpleText keeps the text of block anchors without their URLs."
  );

  return cleanup();
});

/**
 * Lines of extracted text, trimmed, without empty lines.
 *
 * @param {string} text
 * @returns {string[]}
 */
function textLines(text) {
  return text
    .split("\n")
    .map(line => line.trim())
    .filter(line => !!line.length);
}

/**
 * Append a card, an anchor around a div, to the page's article. The HTML is
 * parsed inside the div, so an inner anchor stays nested in the card's anchor,
 * which the HTML parser would not allow in page markup.
 *
 * @param {string} href
 * @param {string} innerHTML
 */
async function appendCard(href, innerHTML) {
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [href, innerHTML],
    (cardHref, cardHTML) => {
      const doc = content.document;
      const anchor = doc.createElement("a");
      anchor.href = cardHref;
      const div = doc.createElement("div");
      anchor.appendChild(div);
      doc.querySelector("article").appendChild(anchor);
      div.innerHTML = cardHTML;
    }
  );
}

add_task(async function test_dom_extractor_link_target_length() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const tracking = "?tracking=" + "x".repeat(200);
  const { getPageExtractor, cleanup } = await html`
    <article>
      <p><a href="https://example.com/a${tracking}">Hotel A</a></p>
      <p><a href="https://example.com/b${tracking}">Hotel B</a></p>
      <p><a href="https://example.com/c${tracking}">Hotel C</a></p>
      <p>Filler after the links that the budget does not reach.</p>
    </article>
  `;
  // Each link counts as about 50 characters, so 3 links reach the budget. At
  // their real length, the first URL alone would use it up.
  const { text } = await getPageExtractor().getText({ sufficientLength: 140 });

  Assert.deepEqual(
    textLines(text),
    [
      `[Hotel A](https://example.com/a${tracking})`,
      `[Hotel B](https://example.com/b${tracking})`,
      `[Hotel C](https://example.com/c${tracking})`,
    ],
    "Link URLs count as a fixed length, and the text stops at the budget."
  );

  return cleanup();
});

add_task(async function test_dom_extractor_block_anchor_inner_link() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`<article></article>`;
  await appendCard(
    "https://example.com/card",
    `<h3>Hotel A</h3>
     <p>Read the <a href="https://example.com/review">review</a></p>`
  );

  Assert.deepEqual(
    textLines((await getPageExtractor().getText()).text),
    [
      "[Hotel A](https://example.com/card)",
      "Read the [review](https://example.com/review)",
    ],
    "The heading links the card, and the inner link keeps its own URL."
  );

  return cleanup();
});

add_task(
  async function test_dom_extractor_block_anchor_inner_link_no_heading() {
    const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
    const { getPageExtractor, cleanup } = await html`<article></article>`;
    await appendCard(
      "https://example.com/card",
      `<p>Read the <a href="https://example.com/review">review</a></p>
     <p>Hotel B</p>`
    );

    Assert.deepEqual(
      textLines((await getPageExtractor().getText()).text),
      [
        "Read the [review](https://example.com/review)",
        "[Hotel B](https://example.com/card)",
      ],
      "Without a heading, the first block with no link of its own links the card."
    );

    return cleanup();
  }
);

add_task(async function test_dom_extractor_block_anchor_linked_heading() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`<article></article>`;
  await appendCard(
    "https://example.com/card",
    `<h2><a href="https://example.com/hotel">Hotel C</a></h2>
     <p>Scored 8.9</p>`
  );

  Assert.deepEqual(
    textLines((await getPageExtractor().getText()).text),
    [
      "[Hotel C](https://example.com/hotel)",
      "[Scored 8.9](https://example.com/card)",
    ],
    "A heading with its own link is not the label, so no link is nested."
  );

  return cleanup();
});

add_task(async function test_dom_extractor_block_anchor_hidden_heading() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`<article></article>`;
  await appendCard(
    "https://example.com/card-1",
    `<h2 hidden>Hotel D</h2>
     <p>Hotel D, Lisbon</p>`
  );
  await appendCard(
    "https://example.com/card-2",
    `<div style="display: none"><h2>Hotel E</h2></div>
     <p>Hotel E, Porto</p>`
  );

  Assert.deepEqual(
    textLines((await getPageExtractor().getText()).text),
    [
      "[Hotel D, Lisbon](https://example.com/card-1)",
      "[Hotel E, Porto](https://example.com/card-2)",
    ],
    "A hidden heading is not the label, so the card's URL is still in the text."
  );

  return cleanup();
});

add_task(async function test_dom_extractor_block_anchor_without_text() {
  const { html } = await MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { getPageExtractor, cleanup } = await html`
    <article>
      <a href="https://example.com/empty"
        ><div><img src="spacer.png" /></div
      ></a>
      <p>After the empty card.</p>
    </article>
  `;
  const { text, links } = await getPageExtractor().getText();

  Assert.deepEqual(
    textLines(text),
    ["After the empty card."],
    "A card with no text adds no line."
  );
  Assert.deepEqual(
    links,
    [],
    "Like any block with no text, the empty card is skipped, so its URL is not collected."
  );

  return cleanup();
});
