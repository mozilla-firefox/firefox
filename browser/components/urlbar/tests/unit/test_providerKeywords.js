/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Test for bug 392143 that puts keyword results into the autocomplete. Makes
 * sure that multiple parameter queries get spaces converted to +, + converted
 * to %2B, non-ascii become escaped, and pages in history that match the
 * keyword uses the page's title.
 *
 * Also test for bug 249468 by making sure multiple keyword bookmarks with the
 * same keyword appear in the list.
 */

testEngine_setup();

add_task(async function test_keyword_search() {
  let uri1 = "http://abc/?search=%s";
  let uri2 = "http://abc/?search=ThisPageIsInHistory";
  let uri3 = "http://abc/?search=%s&raw=%S";
  let uri4 = "http://abc/?search=%s&raw=%S&mozcharset=ISO-8859-1";
  let uri5 = "http://def/?search=%s";
  let uri6 = "http://ghi/?search=%s&raw=%S";
  let uri7 = "http://somedomain.example/key2";
  await PlacesTestUtils.addVisits([
    { uri: uri1 },
    { uri: uri2 },
    { uri: uri3 },
    { uri: uri6 },
    { uri: uri7 },
  ]);
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: uri1,
    title: "Keyword",
    keyword: "key",
  });
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: uri1,
    title: "Post",
    keyword: "post",
    postData: "post_search=%s",
  });
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: uri3,
    title: "Encoded",
    keyword: "encoded",
  });
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: uri4,
    title: "Charset",
    keyword: "charset",
  });
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: uri2,
    title: "Noparam",
    keyword: "noparam",
  });
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: uri2,
    title: "Noparam-Post",
    keyword: "post_noparam",
    postData: "noparam=1",
  });
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: uri5,
    title: "Keyword",
    keyword: "key2",
  });
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: uri6,
    title: "Charset-history",
    keyword: "charset_history",
  });

  await PlacesUtils.history.update({
    url: uri6,
    annotations: new Map([[PlacesUtils.CHARSET_ANNO, "ISO-8859-1"]]),
  });

  info("Plain keyword query");
  let context = createContext("key term", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://abc/?search=term",
        keyword: "key",
        title: "abc: term",
        iconUri: "page-icon:http://abc/?search=%s",
        heuristic: true,
      }),
    ],
  });

  info("Plain keyword UC");
  context = createContext("key TERM", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://abc/?search=TERM",
        keyword: "key",
        title: "abc: TERM",
        iconUri: "page-icon:http://abc/?search=%s",
        heuristic: true,
      }),
    ],
  });

  info("Multi-word keyword query");
  context = createContext("key multi word", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://abc/?search=multi%20word",
        keyword: "key",
        title: "abc: multi word",
        iconUri: "page-icon:http://abc/?search=%s",
        heuristic: true,
      }),
    ],
  });

  info("Keyword query with +");
  context = createContext("key blocking+", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://abc/?search=blocking%2B",
        keyword: "key",
        title: "abc: blocking+",
        iconUri: "page-icon:http://abc/?search=%s",
        heuristic: true,
      }),
    ],
  });

  info("Keyword query with *");
  // We need a space before the asterisk to ensure it's considered a restriction
  // token otherwise it will be a regular string character.
  context = createContext("key blocking *", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://abc/?search=blocking%20*",
        keyword: "key",
        title: "abc: blocking *",
        iconUri: "page-icon:http://abc/?search=%s",
        heuristic: true,
      }),
    ],
  });

  info("Keyword query with?");
  context = createContext("key blocking?", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://abc/?search=blocking%3F",
        keyword: "key",
        title: "abc: blocking?",
        iconUri: "page-icon:http://abc/?search=%s",
        heuristic: true,
      }),
    ],
  });

  info("Keyword query with ?");
  context = createContext("key blocking ?", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://abc/?search=blocking%20%3F",
        keyword: "key",
        title: "abc: blocking ?",
        iconUri: "page-icon:http://abc/?search=%s",
        heuristic: true,
      }),
    ],
  });

  info("Unescaped term in query");
  // ... but note that we call encodeURIComponent() on the query string when we
  // build the URL, so the expected result will have the ユニコード substring
  // encoded in the URL.
  context = createContext("key ユニコード", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://abc/?search=" + encodeURIComponent("ユニコード"),
        keyword: "key",
        title: "abc: ユニコード",
        iconUri: "page-icon:http://abc/?search=%s",
        heuristic: true,
      }),
    ],
  });

  info("Keyword that happens to match a page");
  context = createContext("key ThisPageIsInHistory", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://abc/?search=ThisPageIsInHistory",
        keyword: "key",
        title: "abc: ThisPageIsInHistory",
        iconUri: "page-icon:http://abc/?search=%s",
        heuristic: true,
      }),
    ],
  });

  info("Keyword with partial page match");
  context = createContext("key ThisPage", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://abc/?search=ThisPage",
        keyword: "key",
        title: "abc: ThisPage",
        iconUri: "page-icon:http://abc/?search=%s",
        heuristic: true,
      }),
      // Only the most recent bookmark for the URL:
      makeBookmarkResult(context, {
        uri: "http://abc/?search=ThisPageIsInHistory",
        title: "Noparam-Post",
      }),
    ],
  });

  // For the keyword with no query terms (with or without space after), the
  // domain is different from the other tests because otherwise all the other
  // test bookmarks and history entries would be matches.
  info("Keyword without query (without space)");
  context = createContext("key2", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://def/?search=",
        title: "http://def/?search=",
        keyword: "key2",
        iconUri: "page-icon:http://def/?search=%s",
        heuristic: true,
      }),
      makeBookmarkResult(context, {
        uri: uri5,
        title: "Keyword",
      }),
    ],
  });

  info("Keyword without query (with space)");
  context = createContext("key2 ", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://def/?search=",
        title: "http://def/?search=",
        keyword: "key2",
        iconUri: "page-icon:http://def/?search=%s",
        heuristic: true,
      }),
      makeBookmarkResult(context, {
        uri: uri5,
        title: "Keyword",
      }),
    ],
  });

  info("POST Keyword");
  context = createContext("post foo", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://abc/?search=foo",
        keyword: "post",
        title: "abc: foo",
        postData: "post_search=foo",
        iconUri: "page-icon:http://abc/?search=%s",
        heuristic: true,
      }),
    ],
  });

  info("escaping with default UTF-8 charset");
  context = createContext("encoded foé", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://abc/?search=fo%C3%A9&raw=foé",
        keyword: "encoded",
        title: "abc: foé",
        iconUri: "page-icon:http://abc/?search=%s&raw=%S",
        heuristic: true,
      }),
    ],
  });

  info("escaping with forced ISO-8859-1 charset");
  context = createContext("charset foé", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://abc/?search=fo%E9&raw=foé",
        keyword: "charset",
        title: "abc: foé",
        iconUri: "page-icon:http://abc/?search=%s&raw=%S&mozcharset=ISO-8859-1",
        heuristic: true,
      }),
    ],
  });

  info("escaping with ISO-8859-1 charset annotated in history");
  context = createContext("charset_history foé", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://ghi/?search=fo%E9&raw=foé",
        keyword: "charset_history",
        title: "ghi: foé",
        iconUri: "page-icon:http://ghi/?search=%s&raw=%S",
        heuristic: true,
      }),
    ],
  });

  info("Bug 359809: escaping +, / and @ with default UTF-8 charset");
  context = createContext("encoded +/@", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://abc/?search=%2B%2F%40&raw=+/@",
        keyword: "encoded",
        title: "abc: +/@",
        iconUri: "page-icon:http://abc/?search=%s&raw=%S",
        heuristic: true,
      }),
    ],
  });

  info("Bug 359809: escaping +, / and @ with forced ISO-8859-1 charset");
  context = createContext("charset +/@", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://abc/?search=%2B%2F%40&raw=+/@",
        keyword: "charset",
        title: "abc: +/@",
        iconUri: "page-icon:http://abc/?search=%s&raw=%S&mozcharset=ISO-8859-1",
        heuristic: true,
      }),
    ],
  });

  info("Bug 1228111 - Keyword with a space in front");
  context = createContext(" key test", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeKeywordSearchResult(context, {
        uri: "http://abc/?search=test",
        keyword: "key",
        title: "abc: test",
        iconUri: "page-icon:http://abc/?search=%s",
        heuristic: true,
      }),
    ],
  });

  info("Bug 1481319 - Keyword with a prefix in front");
  context = createContext("http://key2", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: "http://key2/",
        title: "http://key2/",
        heuristic: true,
        providerName: "UrlbarProviderHeuristicFallback",
      }),
      makeVisitResult(context, {
        uri: uri7,
        title: "test visit for http://somedomain.example/key2",
      }),
    ],
  });

  await cleanupPlaces();
});
