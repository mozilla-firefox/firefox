/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AITab } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/aitab/AITab.sys.mjs"
);
const { UrlTokenizer } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/UrlTokenizer.sys.mjs"
);
const { parsePageConfig } = AITab;

const PAGE = { header: { type: "header", title: "Hi" }, blocks: [] };
const JSON_TEXT = JSON.stringify(PAGE);

add_task(function test_parsePageConfig_shapes() {
  for (const [name, text] of [
    ["bare JSON", JSON_TEXT],
    ["fenced JSON", `\`\`\`json\n${JSON_TEXT}\n\`\`\``],
    ["fence without a language tag", `\`\`\`\n${JSON_TEXT}\n\`\`\``],
    ["unfenced prose", `Here is the page: ${JSON_TEXT} — let me know!`],
    // The {...} span covers the cases above; braces in the prose around a
    // fenced block defeat it and fall back to unwrapping the fence.
    [
      "braces in prose before a fence",
      `Use {curly}:\n\`\`\`json\n${JSON_TEXT}\n\`\`\``,
    ],
    [
      "braces in prose after a fence",
      `\`\`\`json\n${JSON_TEXT}\n\`\`\`\nThe {x} field.`,
    ],
  ]) {
    Assert.deepEqual(parsePageConfig(text), PAGE, `parses ${name}`);
  }
});

add_task(function test_parsePageConfig_rejects_non_json() {
  Assert.equal(parsePageConfig("no JSON here"), null, "text without an object");
  Assert.equal(parsePageConfig("{ not json }"), null, "malformed object");
});

add_task(function test_expandSurfaceUrlTokens() {
  const tokenizer = new UrlTokenizer();
  const known = "https://example.com/article";
  const image = "https://example.com/image.png";
  const knownToken = tokenizer.formatToken(known);
  const imageToken = tokenizer.formatToken(image);

  const surface = {
    components: [
      {
        component: "SourceLinks",
        items: [
          { title: "Known", href: knownToken },
          { title: "Raw", href: "https://evil.example/" },
          { title: "Hallucinated", href: "§url_token: EVIL_EXAMPLE_1§" },
        ],
      },
      {
        component: "Cards",
        items: [
          { title: "Image", href: knownToken, image: imageToken },
          { title: "Bad image", href: knownToken, image: "https://evil/x" },
        ],
      },
      {
        component: "Text",
        text: `Read ${knownToken} not §url_token: EVIL_EXAMPLE_1§.`,
      },
    ],
    dataModel: { links: [{ href: "https://evil.example/" }] },
  };

  Assert.deepEqual(
    AITab.expandSurfaceUrlTokens(surface, tokenizer),
    {
      components: [
        {
          component: "SourceLinks",
          items: [{ title: "Known", href: known }],
        },
        {
          component: "Cards",
          items: [
            { title: "Image", href: known, image },
            { title: "Bad image", href: known },
          ],
        },
        { component: "Text", text: `Read ${known} not .` },
      ],
      dataModel: { links: [] },
    },
    "known tokens expand; unknown links and hallucinated tokens are removed"
  );
});

add_task(function test_expandSurfaceUrlTokens_allowedLinks() {
  const tokenizer = new UrlTokenizer();
  const seen = "https://example.com/article";
  const unseen = "https://example.org/unseen";
  const image = "https://example.org/image.png";
  const seenToken = tokenizer.formatToken(seen);
  const unseenToken = tokenizer.formatToken(unseen);
  const imageToken = tokenizer.formatToken(image);

  const surface = {
    items: [
      { title: "Seen", href: seenToken, image: imageToken },
      { title: "Unseen", href: unseenToken },
    ],
  };

  Assert.deepEqual(
    AITab.expandSurfaceUrlTokens(surface, tokenizer, new Set([seen])),
    { items: [{ title: "Seen", href: seen, image }] },
    "an item linking to a URL outside allowedLinks is dropped"
  );
  Assert.deepEqual(
    AITab.expandSurfaceUrlTokens(surface, tokenizer),
    {
      items: [
        { title: "Seen", href: seen, image },
        { title: "Unseen", href: unseen },
      ],
    },
    "without allowedLinks any URL from the prompt may be linked"
  );
});

add_task(function test_tokenizeSurfaceUrls_round_trips() {
  const surface = {
    components: [
      {
        component: "Cards",
        items: [
          {
            title: "Image",
            href: "https://example.com/article",
            image: "https://example.com/image.png",
          },
        ],
      },
      { component: "Text", text: "Read https://example.com/article now." },
    ],
    dataModel: {},
  };

  const tokenizer = new UrlTokenizer();
  const tokenized = AITab.tokenizeSurfaceUrls(surface, tokenizer);
  Assert.ok(
    !JSON.stringify(tokenized).includes("https://"),
    "every URL is replaced with a token"
  );
  Assert.deepEqual(
    AITab.expandSurfaceUrlTokens(tokenized, tokenizer),
    surface,
    "expanding with the same tokenizer restores the surface"
  );
});

add_task(function test_expandSurfaceUrlTokens_depth_limit() {
  const tokenizer = new UrlTokenizer();
  const nest = (wrap, levels) => {
    let value = "leaf";
    for (let i = 0; i < levels; i++) {
      value = wrap(value);
    }
    return value;
  };
  const depthOf = value =>
    value && typeof value == "object"
      ? 1 + Math.max(0, ...Object.values(value).map(depthOf))
      : 0;

  for (const [name, wrap] of [
    ["objects", value => ({ child: value })],
    ["arrays", value => [value]],
    ["mixed", value => ({ items: [value] })],
  ]) {
    const shallow = nest(wrap, 5);
    Assert.deepEqual(
      AITab.expandSurfaceUrlTokens(shallow, tokenizer),
      shallow,
      `shallow nested ${name} are kept`
    );

    const deep = AITab.expandSurfaceUrlTokens(nest(wrap, 1000), tokenizer);
    Assert.lessOrEqual(
      depthOf(deep),
      34,
      `deeply nested ${name} are cut off at the depth limit`
    );
    Assert.ok(
      !JSON.stringify(deep).includes("leaf"),
      `the leaf beyond the depth limit of nested ${name} is dropped`
    );
  }
});
