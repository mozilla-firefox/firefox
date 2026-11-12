/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const TOKEN_TYPE = UrlbarShared.TOKEN_TYPE;
const RESTRICT_TOKENS = UrlbarShared.RESTRICT_TOKENS;

add_task(async function test_tokenizer() {
  let testContexts = [
    { desc: "Empty string", searchString: "", expectedTokens: [] },
    { desc: "Spaces string", searchString: "      ", expectedTokens: [] },
    {
      desc: "Single word string",
      searchString: "test",
      expectedTokens: [{ value: "test", type: TOKEN_TYPE.TEXT }],
    },
    {
      desc: "Multi word string with mixed whitespace types",
      searchString: " test1 test2\u1680test3\u2004test4\u1680",
      expectedTokens: [
        { value: "test1", type: TOKEN_TYPE.TEXT },
        { value: "test2", type: TOKEN_TYPE.TEXT },
        { value: "test3", type: TOKEN_TYPE.TEXT },
        { value: "test4", type: TOKEN_TYPE.TEXT },
      ],
    },
    {
      desc: "separate restriction char at beginning",
      searchString: `${RESTRICT_TOKENS.BOOKMARK} test`,
      expectedTokens: [
        {
          value: RESTRICT_TOKENS.BOOKMARK,
          type: TOKEN_TYPE.RESTRICT_BOOKMARK,
        },
        { value: "test", type: TOKEN_TYPE.TEXT },
      ],
    },
    {
      desc: "do not separate restriction char at beginning in search mode",
      searchMode: { engineName: "testEngine" },
      searchString: `${RESTRICT_TOKENS.SEARCH}test`,
      expectedTokens: [{ value: "?test", type: TOKEN_TYPE.TEXT }],
    },
    {
      desc: "separate restriction char at end",
      searchString: `test ${RESTRICT_TOKENS.BOOKMARK}`,
      expectedTokens: [
        { value: "test", type: TOKEN_TYPE.TEXT },
        {
          value: RESTRICT_TOKENS.BOOKMARK,
          type: TOKEN_TYPE.RESTRICT_BOOKMARK,
        },
      ],
    },
    {
      desc: "boundary restriction char at end",
      searchString: `test${RESTRICT_TOKENS.BOOKMARK}`,
      expectedTokens: [
        {
          value: `test${RESTRICT_TOKENS.BOOKMARK}`,
          type: TOKEN_TYPE.TEXT,
        },
      ],
    },
    {
      desc: "do not separate boundary search restriction char at end",
      searchString: `test${RESTRICT_TOKENS.SEARCH}`,
      expectedTokens: [{ value: "test?", type: TOKEN_TYPE.TEXT }],
    },
    {
      desc: "separate restriction char in the middle",
      searchString: `test ${RESTRICT_TOKENS.BOOKMARK} test`,
      expectedTokens: [
        { value: "test", type: TOKEN_TYPE.TEXT },
        {
          value: RESTRICT_TOKENS.BOOKMARK,
          type: TOKEN_TYPE.TEXT,
        },
        { value: "test", type: TOKEN_TYPE.TEXT },
      ],
    },
    {
      desc: "restriction char in the middle",
      searchString: `test${RESTRICT_TOKENS.BOOKMARK}test`,
      expectedTokens: [
        {
          value: `test${RESTRICT_TOKENS.BOOKMARK}test`,
          type: TOKEN_TYPE.TEXT,
        },
      ],
    },
    {
      desc: "restriction char in the middle 2",
      searchString: `test${RESTRICT_TOKENS.BOOKMARK} test`,
      expectedTokens: [
        {
          value: `test${RESTRICT_TOKENS.BOOKMARK}`,
          type: TOKEN_TYPE.TEXT,
        },
        { value: `test`, type: TOKEN_TYPE.TEXT },
      ],
    },
    {
      desc: "do not separate boundary search restriction char at end when using using a double non-combinable restriction char with a single-character string",
      searchString: `t${RESTRICT_TOKENS.BOOKMARK}${RESTRICT_TOKENS.SEARCH}`,
      expectedTokens: [
        {
          value: `t${RESTRICT_TOKENS.BOOKMARK}${RESTRICT_TOKENS.SEARCH}`,
          type: TOKEN_TYPE.TEXT,
        },
      ],
    },
    {
      desc: "only the boundary restriction char",
      searchString: RESTRICT_TOKENS.BOOKMARK,
      expectedTokens: [
        {
          value: RESTRICT_TOKENS.BOOKMARK,
          type: TOKEN_TYPE.RESTRICT_BOOKMARK,
        },
      ],
    },
    {
      desc: "# is not a restriction char at the beginning",
      searchString: "# test",
      expectedTokens: [
        { value: "#", type: TOKEN_TYPE.TEXT },
        { value: "test", type: TOKEN_TYPE.TEXT },
      ],
    },
    {
      desc: "# is not a restriction char at the end",
      searchString: "test #",
      expectedTokens: [
        { value: "test", type: TOKEN_TYPE.TEXT },
        { value: "#", type: TOKEN_TYPE.TEXT },
      ],
    },
    {
      desc: "$ is not a restriction char at the beginning",
      searchString: "$ test",
      expectedTokens: [
        { value: "$", type: TOKEN_TYPE.TEXT },
        { value: "test", type: TOKEN_TYPE.TEXT },
      ],
    },
    {
      desc: "$ is not a restriction char at the end",
      searchString: "test $",
      expectedTokens: [
        { value: "test", type: TOKEN_TYPE.TEXT },
        { value: "$", type: TOKEN_TYPE.TEXT },
      ],
    },
    {
      desc: "# and $ do not combine with a type restriction char",
      searchString: `${RESTRICT_TOKENS.BOOKMARK} test # $`,
      expectedTokens: [
        {
          value: RESTRICT_TOKENS.BOOKMARK,
          type: TOKEN_TYPE.RESTRICT_BOOKMARK,
        },
        { value: "test", type: TOKEN_TYPE.TEXT },
        { value: "#", type: TOKEN_TYPE.TEXT },
        { value: "$", type: TOKEN_TYPE.TEXT },
      ],
    },
    {
      desc: "boundary # char on path",
      searchString: "test/#",
      expectedTokens: [{ value: "test/#", type: TOKEN_TYPE.POSSIBLE_URL }],
    },
    // Some restriction chars may be ? that are also valid path parts.
    {
      desc: "boundary ? char on path",
      searchString: "test/?",
      expectedTokens: [{ value: "test/?", type: TOKEN_TYPE.POSSIBLE_URL }],
    },
    {
      desc: "multiple boundary restriction chars suffix",
      searchString: `test ${RESTRICT_TOKENS.HISTORY} ${RESTRICT_TOKENS.TAG}`,
      expectedTokens: [
        { value: "test", type: TOKEN_TYPE.TEXT },
        {
          value: RESTRICT_TOKENS.HISTORY,
          type: TOKEN_TYPE.TEXT,
        },
        {
          value: RESTRICT_TOKENS.TAG,
          type: TOKEN_TYPE.RESTRICT_TAG,
        },
      ],
    },
    {
      desc: "multiple boundary restriction chars prefix",
      searchString: `${RESTRICT_TOKENS.HISTORY} ${RESTRICT_TOKENS.TAG} test`,
      expectedTokens: [
        {
          value: RESTRICT_TOKENS.HISTORY,
          type: TOKEN_TYPE.RESTRICT_HISTORY,
        },
        {
          value: RESTRICT_TOKENS.TAG,
          type: TOKEN_TYPE.TEXT,
        },
        { value: "test", type: TOKEN_TYPE.TEXT },
      ],
    },
    {
      desc: "Math with division",
      searchString: "3.6/1.2",
      expectedTokens: [{ value: "3.6/1.2", type: TOKEN_TYPE.TEXT }],
    },
    {
      desc: "ipv4 in bookmarks",
      searchString: `${RESTRICT_TOKENS.BOOKMARK} 192.168.1.1:8`,
      expectedTokens: [
        { value: RESTRICT_TOKENS.BOOKMARK, type: TOKEN_TYPE.RESTRICT_BOOKMARK },
        { value: "192.168.1.1:8", type: TOKEN_TYPE.POSSIBLE_ORIGIN },
      ],
    },
    {
      desc: "email",
      searchString: "test@mozilla.com",
      expectedTokens: [{ value: "test@mozilla.com", type: TOKEN_TYPE.TEXT }],
    },
    {
      desc: "email2",
      searchString: "test.test@mozilla.co.uk",
      expectedTokens: [
        { value: "test.test@mozilla.co.uk", type: TOKEN_TYPE.TEXT },
      ],
    },
    {
      desc: "protocol",
      searchString: "http://test",
      expectedTokens: [{ value: "http://test", type: TOKEN_TYPE.POSSIBLE_URL }],
    },
    {
      desc: "bogus protocol with host (we allow visits to http://///example.com)",
      searchString: "http:///test",
      expectedTokens: [
        { value: "http:///test", type: TOKEN_TYPE.POSSIBLE_URL },
      ],
    },
    {
      desc: "file protocol with path",
      searchString: "file:///home",
      expectedTokens: [
        { value: "file:///home", type: TOKEN_TYPE.POSSIBLE_URL },
      ],
    },
    {
      desc: "almost a protocol",
      searchString: "http:",
      expectedTokens: [{ value: "http:", type: TOKEN_TYPE.POSSIBLE_URL }],
    },
    {
      desc: "almost a protocol 2",
      searchString: "http:/",
      expectedTokens: [{ value: "http:/", type: TOKEN_TYPE.POSSIBLE_URL }],
    },
    {
      desc: "bogus protocol (we allow visits to http://///example.com)",
      searchString: "http:///",
      expectedTokens: [{ value: "http:///", type: TOKEN_TYPE.POSSIBLE_URL }],
    },
    {
      desc: "file protocol",
      searchString: "file:///",
      expectedTokens: [{ value: "file:///", type: TOKEN_TYPE.POSSIBLE_URL }],
    },
    {
      desc: "userinfo",
      searchString: "user:pass@test",
      expectedTokens: [
        { value: "user:pass@test", type: TOKEN_TYPE.POSSIBLE_ORIGIN },
      ],
    },
    {
      desc: "domain with two dots",
      searchString: "www.mozilla.org",
      expectedTokens: [
        {
          value: "www.mozilla.org",
          type: TOKEN_TYPE.POSSIBLE_ORIGIN_BUT_SEARCH_ALLOWED,
        },
      ],
    },
    {
      desc: "domain with two dots and allowSearchSuggestionsForSimpleOrigins = false",
      searchString: "www.mozilla.org",
      allowSearchSuggestionsForSimpleOrigins: false,
      expectedTokens: [
        {
          value: "www.mozilla.org",
          type: TOKEN_TYPE.POSSIBLE_ORIGIN,
        },
      ],
    },
    {
      desc: "domain with one dot",
      searchString: "mozilla.org",
      expectedTokens: [
        {
          value: "mozilla.org",
          type: TOKEN_TYPE.POSSIBLE_ORIGIN_BUT_SEARCH_ALLOWED,
        },
      ],
    },
    {
      desc: "domain with one dot and allowSearchSuggestionsForSimpleOrigins = false",
      searchString: "mozilla.org",
      allowSearchSuggestionsForSimpleOrigins: false,
      expectedTokens: [
        {
          value: "mozilla.org",
          type: TOKEN_TYPE.POSSIBLE_ORIGIN,
        },
      ],
    },
    {
      desc: "looks like simple origin",
      searchString: "mozilla.o",
      expectedTokens: [
        {
          value: "mozilla.o",
          type: TOKEN_TYPE.POSSIBLE_ORIGIN_BUT_SEARCH_ALLOWED,
        },
      ],
    },
    {
      desc: "looks like simple origin with allowSearchSuggestionsForSimpleOrigins = false",
      searchString: "mozilla.o",
      allowSearchSuggestionsForSimpleOrigins: false,
      expectedTokens: [
        {
          value: "mozilla.o",
          type: TOKEN_TYPE.POSSIBLE_ORIGIN,
        },
      ],
    },
    {
      desc: "query ends with dot",
      searchString: "mozilla.",
      expectedTokens: [
        {
          value: "mozilla.",
          type: TOKEN_TYPE.POSSIBLE_ORIGIN_BUT_SEARCH_ALLOWED,
        },
      ],
    },
    {
      desc: "query ends with dot with allowSearchSuggestionsForSimpleOrigins = false",
      searchString: "mozilla.",
      allowSearchSuggestionsForSimpleOrigins: false,
      expectedTokens: [
        {
          value: "mozilla.",
          type: TOKEN_TYPE.POSSIBLE_ORIGIN,
        },
      ],
    },
    {
      desc: "data uri",
      searchString: "data:text/plain,Content",
      expectedTokens: [
        {
          value: "data:text/plain,Content",
          type: TOKEN_TYPE.POSSIBLE_URL,
        },
      ],
    },
    {
      desc: "ipv6",
      searchString: "[2001:db8::1]",
      expectedTokens: [
        { value: "[2001:db8::1]", type: TOKEN_TYPE.POSSIBLE_ORIGIN },
      ],
    },
    {
      desc: "numeric domain",
      searchString: "test1001.com",
      expectedTokens: [
        {
          value: "test1001.com",
          type: TOKEN_TYPE.POSSIBLE_ORIGIN_BUT_SEARCH_ALLOWED,
        },
      ],
    },
    {
      desc: "invalid ip",
      searchString: "192.2134.1.2",
      expectedTokens: [{ value: "192.2134.1.2", type: TOKEN_TYPE.TEXT }],
    },
    {
      desc: "ipv4",
      searchString: "1.2.3.4",
      expectedTokens: [{ value: "1.2.3.4", type: TOKEN_TYPE.POSSIBLE_ORIGIN }],
    },
    {
      desc: "host/path",
      searchString: "test/test",
      expectedTokens: [{ value: "test/test", type: TOKEN_TYPE.POSSIBLE_URL }],
    },
    {
      desc: "percent encoded string",
      searchString: "%E6%97%A5%E6%9C%AC",
      expectedTokens: [{ value: "%E6%97%A5%E6%9C%AC", type: TOKEN_TYPE.TEXT }],
    },
    {
      desc: "Uppercase",
      searchString: "TEST",
      expectedTokens: [{ value: "TEST", type: TOKEN_TYPE.TEXT }],
    },
    {
      desc: "Mixed case 1",
      searchString: "TeSt",
      expectedTokens: [{ value: "TeSt", type: TOKEN_TYPE.TEXT }],
    },
    {
      desc: "Mixed case 2",
      searchString: "tEsT",
      expectedTokens: [{ value: "tEsT", type: TOKEN_TYPE.TEXT }],
    },
    {
      desc: "Uppercase with spaces",
      searchString: "TEST EXAMPLE",
      expectedTokens: [
        { value: "TEST", type: TOKEN_TYPE.TEXT },
        { value: "EXAMPLE", type: TOKEN_TYPE.TEXT },
      ],
    },
    {
      desc: "Mixed case with spaces",
      searchString: "TeSt eXaMpLe",
      expectedTokens: [
        { value: "TeSt", type: TOKEN_TYPE.TEXT },
        { value: "eXaMpLe", type: TOKEN_TYPE.TEXT },
      ],
    },
    {
      desc: "plain number",
      searchString: "1001",
      expectedTokens: [{ value: "1001", type: TOKEN_TYPE.TEXT }],
    },
    {
      desc: "data uri with spaces",
      searchString: "data:text/html,oh hi?",
      expectedTokens: [
        {
          value: "data:text/html,oh hi?",
          type: TOKEN_TYPE.POSSIBLE_URL,
        },
      ],
    },
    {
      desc: "data uri with spaces ignored with other tokens",
      searchString: "hi data:text/html,oh hi?",
      expectedTokens: [
        {
          value: "hi",
          type: TOKEN_TYPE.TEXT,
        },
        {
          value: "data:text/html,oh",
          type: TOKEN_TYPE.POSSIBLE_URL,
        },
        {
          value: "hi?",
          type: TOKEN_TYPE.TEXT,
        },
      ],
    },
    {
      desc: "whitelisted host",
      searchString: "test whitelisted",
      expectedTokens: [
        {
          value: "test",
          type: TOKEN_TYPE.TEXT,
        },
        {
          value: "whitelisted",
          type: TOKEN_TYPE.POSSIBLE_ORIGIN,
        },
      ],
    },
  ];

  Services.prefs.setBoolPref("browser.fixup.domainwhitelist.whitelisted", true);

  for (let queryContext of testContexts) {
    info(queryContext.desc);
    queryContext.trimmedSearchString = queryContext.searchString.trim();
    for (let token of queryContext.expectedTokens) {
      token.lowerCaseValue = token.value.toLocaleLowerCase();
    }

    if (queryContext.hasOwnProperty("allowSearchSuggestionsForSimpleOrigins")) {
      Services.prefs.setBoolPref(
        "browser.urlbar.allowSearchSuggestionsForSimpleOrigins",
        queryContext.allowSearchSuggestionsForSimpleOrigins
      );
    }

    let tokens = UrlbarTokenizer.tokenize(queryContext);
    Assert.deepEqual(
      tokens,
      queryContext.expectedTokens,
      "Check the expected tokens"
    );

    Services.prefs.clearUserPref(
      "browser.urlbar.allowSearchSuggestionsForSimpleOrigins"
    );
  }
});

add_task(function test_getOriginInfo() {
  const tests = [
    ["example.com", { host: "example.com", path: "" }],
    ["Example.COM/Foo%20bar", { host: "example.com", path: "/Foo%20bar" }],
    ["https://www.example.com/a/b", { host: "example.com", path: "/a/b" }],
    ["http://example.com", { host: "example.com", path: "" }],
    ["example.com:8080/foo", { host: "example.com", path: "/foo" }],
    ["ftp://example.com/foo", null],
    ["user:pass@example.com/foo", null],
    ["example", null],
    ["example.com.", { host: "example.com", path: "" }],
    ["www.example.com./foo", { host: "example.com", path: "/foo" }],
  ];
  for (let [searchString, expected] of tests) {
    let [token] = UrlbarTokenizer.tokenize(createContext(searchString));
    Assert.deepEqual(
      UrlbarTokenizer.getOriginInfo(token),
      expected,
      `Origin info for "${searchString}"`
    );
  }
});
