/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { UrlTokenizer, expandUrlTokens, stripUnresolvedUrlTokens } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/UrlTokenizer.sys.mjs"
  );

add_task(async function test_UrlTokenizer_encodeToken() {
  const cases = [
    {
      message: "Works for a URL with a path.",
      url: "http://www.github.com/foo/bar/baz",
      expected: "GITHUB_COM_FOO_BAR_BAZ_1",
    },
    {
      message:
        "Returns a new number for a URL that is different but creates the same token.",
      url: "http://www.github.com/foo/bar/baz?ignored",
      expected: "GITHUB_COM_FOO_BAR_BAZ_2",
    },
    {
      message: "Returns the exact same token given another URL",
      url: "http://www.github.com/foo/bar/baz",
      expected: "GITHUB_COM_FOO_BAR_BAZ_1",
    },
    {
      message:
        "Returns a different token given the same URL with a different protocol",
      url: "https://www.github.com/foo/bar/baz",
      expected: "GITHUB_COM_FOO_BAR_BAZ_3",
    },
    {
      message: "Can handle about URLs.",
      url: "about:config",
      expected: "ABOUT_CONFIG_1",
    },
    {
      message: "Uses non-http protocols",
      url: "ftp://github.com/foo/bar/baz",
      expected: "FTP_GITHUB_COM_FOO_BAR_BAZ_1",
    },
    {
      message: "Uses invalid protocols",
      url: "asdf://github.com/foo/bar/baz",
      expected: "ASDF_GITHUB_COM_FOO_BAR_BAZ_1",
    },
    {
      message: "Ignores the port.",
      url: "http://github.com:1234/ignore/port",
      expected: "GITHUB_COM_IGNORE_PORT_1",
    },
    {
      message: "Ignores the params.",
      url: "http://www.github.com/ignore/params?token=xxx",
      expected: "GITHUB_COM_IGNORE_PARAMS_1",
    },
    {
      message: "Ignores the hash.",
      url: "http://www.github.com/ignore/hash/part?token=xxx#hash",
      expected: "GITHUB_COM_IGNORE_HASH_PART_1",
    },
    {
      message: "Truncates text in the host from 110 to 100.",
      url: `http://www.${"a".repeat(110)}.com/foo`,
      expected: "A".repeat(100) + "_1",
    },
    {
      message: "Skips text in the path that is too long",
      url: `http://github.com/skip/long/path/` + "A".repeat(100),
      expected: "GITHUB_COM_SKIP_LONG_PATH_1",
    },
  ];

  const urlTokenizer = new UrlTokenizer({});

  for (const { message, url, expected } of cases) {
    const token = urlTokenizer.encodeToken(url);
    Assert.equal(token, expected, message);

    const decodedUrl = urlTokenizer.tokenToUrl.get(token);
    Assert.equal(
      decodedUrl,
      url,
      `Expected the decoded token (${token}) to equal '${url}' but got: ${decodedUrl}`
    );
  }
});

add_task(function test_UrlTokenizer_tokenizeText() {
  const tokenizer = new UrlTokenizer();
  const text = tokenizer.tokenizeText(
    "See https://example.com/a?x=1. Also (https://example.org/b), fine"
  );
  Assert.equal(
    text,
    "See §url_token: EXAMPLE_COM_A_1§. Also (§url_token: EXAMPLE_ORG_B_1§), fine"
  );
  Assert.equal(
    tokenizer.resolveExactToken("§url_token: EXAMPLE_COM_A_1§"),
    "https://example.com/a?x=1"
  );
  Assert.equal(tokenizer.resolveExactToken("https://example.com/a?x=1"), null);
});

add_task(function test_UrlTokenizer_tokenizeText_no_raw_urls() {
  const tokenizer = new UrlTokenizer();
  const cases = [
    {
      message: "A valid URL after an invalid one is still tokenized.",
      text: "http://[bad]https://example.com",
      expected: "[bad]§url_token: EXAMPLE_COM_1§",
    },
    {
      message: "A URL without a word boundary before it is tokenized.",
      text: "xhttps://example.com",
      expected: "x§url_token: EXAMPLE_COM_1§",
    },
    {
      message: "An invalid URL has its scheme removed.",
      text: "Go to http://a:b:c/ now",
      expected: "Go to a:b:c/ now",
    },
    {
      message: "A lone scheme is removed.",
      text: "HTTPS:// only",
      expected: " only",
    },
    {
      message: "A closing parenthesis that belongs to the URL is kept.",
      text: "See (https://en.wikipedia.org/wiki/Function_(mathematics)).",
      expected:
        "See (§url_token: EN_WIKIPEDIA_ORG_WIKI_FUNCTION__MATHEMATICS__1§).",
    },
  ];
  for (const { message, text, expected } of cases) {
    Assert.equal(tokenizer.tokenizeText(text), expected, message);
  }
});

add_task(function test_UrlTokenizer_tokenizeText_markdown_emphasis() {
  const cases = [
    {
      text: "*Source: [EC](https://weather.gc.ca/f.html)*",
      expected: "*Source: [EC](§url_token: WEATHER_GC_CA_F_HTML_1§)*",
      url: "https://weather.gc.ca/f.html",
    },
    {
      text: "**[EC](https://weather.gc.ca/f.html)**.",
      expected: "**[EC](§url_token: WEATHER_GC_CA_F_HTML_1§)**.",
      url: "https://weather.gc.ca/f.html",
    },
    {
      text: "*[Math](https://en.wikipedia.org/wiki/Function_(mathematics))*",
      expected:
        "*[Math](§url_token: EN_WIKIPEDIA_ORG_WIKI_FUNCTION__MATHEMATICS__1§)*",
      url: "https://en.wikipedia.org/wiki/Function_(mathematics)",
    },
  ];
  for (const { text, expected, url } of cases) {
    const tokenizer = new UrlTokenizer();
    Assert.equal(tokenizer.tokenizeText(text), expected, text);
    Assert.deepEqual([...tokenizer.tokenToUrl.values()], [url], text);
  }
});

add_task(function test_UrlTokenizer_tokenizeText_markdown_escaped_parens() {
  const cases = [
    {
      message: "An escaped unbalanced '(' is part of the URL.",
      text: "[a](https://x.com/Foo_\\(bar)",
      expected: "[a](§url_token: X_COM_FOO__BAR_1§)",
      url: "https://x.com/Foo_(bar",
    },
    {
      message: "An escaped unbalanced ')' is part of the URL.",
      text: "[a](https://x.com/Foo_bar\\))",
      expected: "[a](§url_token: X_COM_FOO_BAR__1§)",
      url: "https://x.com/Foo_bar)",
    },
  ];
  for (const { message, text, expected, url } of cases) {
    const tokenizer = new UrlTokenizer();
    Assert.equal(tokenizer.tokenizeText(text), expected, message);
    Assert.deepEqual([...tokenizer.tokenToUrl.values()], [url], message);
    Assert.equal(
      expandUrlTokens(expected, tokenizer.tokenToUrl),
      text,
      `${message} Expanding restores the original Markdown.`
    );
  }
});

// expandUrlTokens tests

add_task(function test_expandUrlTokens_unbalanced_parens() {
  const mapping = new Map([
    ["X_COM_1", "https://x.com/Foo_(bar"],
    ["X_COM_2", "https://x.com/a)b(c"],
    ["X_COM_3", "https://x.com/Foo_(bar)"],
  ]);
  Assert.equal(
    expandUrlTokens("[a](§url_token: X_COM_1§)", mapping),
    "[a](https://x.com/Foo_\\(bar)",
    "Unbalanced parens in a link destination are escaped"
  );
  Assert.equal(
    expandUrlTokens("[a](§url_token: X_COM_2§)", mapping),
    "[a](https://x.com/a\\)b\\(c)",
    "Parens that are out of order are escaped"
  );
  Assert.equal(
    expandUrlTokens("[a](§url_token: X_COM_3§)", mapping),
    "[a](https://x.com/Foo_(bar))",
    "Balanced parens are left alone"
  );
  Assert.equal(
    expandUrlTokens("See §url_token: X_COM_1§ now", mapping),
    "See https://x.com/Foo_(bar now",
    "A token outside a link destination is not escaped"
  );
});

add_task(function test_expandUrlTokens_bare_token() {
  const mapping = new Map([["GITHUB_COM_1", "https://github.com/foo"]]);
  const result = expandUrlTokens(
    "Check out §url_token: GITHUB_COM_1§ for details.",
    mapping
  );
  Assert.equal(
    result,
    "Check out https://github.com/foo for details.",
    "Bare token should be replaced with full URL"
  );
});

add_task(function test_expandUrlTokens_bracketed_id_in_href_resolved() {
  const mapping = new Map([["GITHUB_COM_1", "https://github.com/foo"]]);
  const result = expandUrlTokens(
    "[Click here](§url_token: GITHUB_COM_1§)",
    mapping
  );
  Assert.equal(
    result,
    "[Click here](https://github.com/foo)",
    "Bracketed ID in href should be expanded to full URL"
  );
});

add_task(function test_expandUrlTokens_same_domain() {
  const mapping = new Map([
    ["GITHUB_COM_1", "https://github.com/foo"],
    ["GITHUB_COM_2", "https://github.com/foo/bar"],
  ]);
  const result = expandUrlTokens(
    "[Click here](§url_token: GITHUB_COM_2§)",
    mapping
  );
  Assert.equal(
    result,
    "[Click here](https://github.com/foo/bar)",
    "Bracketed ID in href should be expanded to longer URL"
  );
});

add_task(function test_expandUrlTokens_no_space() {
  const mapping = new Map([["GITHUB_COM_1", "https://github.com/foo"]]);
  const result = expandUrlTokens(
    "[Click here](§url_token:GITHUB_COM_1§)",
    mapping
  );
  Assert.equal(
    result,
    "[Click here](https://github.com/foo)",
    "Should handle missing space after token type and still expand to correct URL"
  );
});

add_task(function test_expandUrlTokens_no_mapping() {
  const mapping = new Map();
  const result = expandUrlTokens(
    "[Click here](§url_token: GITHUB_COM_2§)",
    mapping
  );
  Assert.equal(
    result,
    "[Click here](§url_token: GITHUB_COM_2§)",
    "ID should remain unchanged if not in mapping"
  );
});

add_task(function test_expandUrlTokens_lowercase() {
  const mapping = new Map([["github_com_1", "https://github.com/foo"]]);
  const result = expandUrlTokens(
    "[Click here](§url_token: github_com_1§)",
    mapping
  );
  Assert.equal(
    result,
    "[Click here](§url_token: github_com_1§)",
    "Shouldn't match lowercase ID"
  );
});

add_task(function test_expandUrlTokens_no_digits() {
  const mapping = new Map([["GITHUB_COM", "https://github.com/foo"]]);
  const result = expandUrlTokens(
    "[Click here](§url_token: GITHUB_COM§)",
    mapping
  );
  Assert.equal(
    result,
    "[Click here](§url_token: GITHUB_COM§)",
    "Shouldn't match tags missing digits"
  );
});

add_task(function test_expandUrlTokens_special_characters() {
  const mapping = new Map([["GITHUB_COM$_1", "https://github.com/foo"]]);
  const result = expandUrlTokens(
    "[Click here](§url_token: GITHUB_COM$_1§)",
    mapping
  );
  Assert.equal(
    result,
    "[Click here](§url_token: GITHUB_COM$_1§)",
    "Shouldn't match tags with special characters"
  );
});

add_task(function test_expandUrlTokens_missing_separator() {
  const mapping = new Map([["GITHUB_COM_1", "https://github.com/foo"]]);
  const result = expandUrlTokens(
    "[Click here](url_token: GITHUB_COM_1)",
    mapping
  );
  Assert.equal(
    result,
    "[Click here](url_token: GITHUB_COM_1)",
    "Shouldn't match missing § separators"
  );
});

add_task(function test_expandUrlTokens_wrong_tag_name() {
  const mapping = new Map([["GITHUB_COM_1", "https://github.com/foo"]]);
  const result = expandUrlTokens("[Click here](§url: GITHUB_COM_1§)", mapping);
  Assert.equal(
    result,
    "[Click here](§url: GITHUB_COM_1§)",
    "Shouldn't match wrong tag name"
  );
});

// stripUnresolvedUrlTokens tests

add_task(function test_stripUnresolvedUrlTokens() {
  Assert.equal(
    stripUnresolvedUrlTokens("Try §url_token: GITHUB_COM_1§ next."),
    "Try  next.",
    "An unresolved token should be removed from the text"
  );
  Assert.equal(
    stripUnresolvedUrlTokens("[Click here](§url: GITHUB_COM_1§)"),
    "[Click here](§url: GITHUB_COM_1§)",
    "Text that is not a URL token should be left alone"
  );
});
