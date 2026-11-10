/* Any copyright is dedicated to the Public Domain.
 *    https://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Tests getAlternateDomains API.
 */

"use strict";

add_setup(async function () {
  useHttpServer("");

  SearchTestUtils.setRemoteSettingsConfig([
    {
      identifier: "mainengine",
      partnerCode: "mainenginepc",
      base: {
        urls: {
          search: {
            base: "https://example.com/searchBar",
            params: [
              {
                name: "partnerCode",
                value: "{partnerCode}",
              },
            ],
            searchTermParamName: "foo",
          },
        },
      },
    },
  ]);
});

add_task(async function test_parseSubmissionURL() {
  let engine1 = await SearchTestUtils.installOpenSearchEngine({
    url: `${gHttpURL}/opensearch/generic1.xml`,
  });
  let engine2 = await SearchTestUtils.installOpenSearchEngine({
    url: `${gHttpURL}/opensearch/fr-domain-iso8859-1.xml`,
  });

  await SearchTestUtils.installSearchExtension({
    name: "bacon_addParam",
    keyword: "bacon_addParam",
    encoding: "windows-1252",
    search_url: "https://www.bacon.test/find",
  });
  await SearchTestUtils.installSearchExtension({
    name: "idn_addParam",
    keyword: "idn_addParam",
    search_url: "https://www.xn--bcher-kva.ch/search",
  });
  let engine3 = SearchService.getEngineByName("bacon_addParam");
  let engine4 = SearchService.getEngineByName("idn_addParam");

  // The following engine provides it's query keyword in
  // its template in the form of q={searchTerms}
  let engine5 = await SearchTestUtils.installOpenSearchEngine({
    url: `${gHttpURL}/opensearch/generic2.xml`,
  });

  // The following engines cannot identify the search parameter.
  await SearchTestUtils.installSearchExtension({
    name: "bacon",
    keyword: "bacon",
    search_url: "https://www.bacon.moz/search?q=",
    search_url_get_params: "",
  });

  // Test the first engine, whose URLs use UTF-8 encoding.
  // This also tests the query parameter in a different position not being the
  // first parameter.
  let url = "https://www.google.com/search?foo=bar&q=caff%C3%A8";
  let result = SearchService.parseSubmissionURL(url);
  Assert.equal(result.engine, engine1);
  Assert.equal(result.terms, "caff\u00E8");

  // The second engine uses a locale-specific domain that is an alternate domain
  // of the first one, but the second engine should get priority when matching.
  // The URL used with this engine uses ISO-8859-1 encoding instead.
  url = "https://www.google.fr/search?q=caff%E8";
  result = SearchService.parseSubmissionURL(url);
  Assert.equal(result.engine, engine2);
  Assert.equal(result.terms, "caff\u00E8");

  // Test a domain that is an alternate domain of those defined.  In this case,
  // the first matching engine from the ordered list should be returned.
  url = "https://www.google.co.uk/search?q=caff%C3%A8";
  result = SearchService.parseSubmissionURL(url);
  Assert.equal(result.engine, engine1);
  Assert.equal(result.terms, "caff\u00E8");

  // We support parsing URLs from a dynamically added engine.
  url = "https://www.bacon.test/find?q=caff%E8";
  result = SearchService.parseSubmissionURL(url);
  Assert.equal(result.engine, engine3);
  Assert.equal(result.terms, "caff\u00E8");

  // Test URLs with unescaped unicode characters.
  url = "https://www.google.com/search?q=foo+b\u00E4r";
  result = SearchService.parseSubmissionURL(url);
  Assert.equal(result.engine, engine1);
  Assert.equal(result.terms, "foo b\u00E4r");

  // Test search engines with unescaped IDNs.
  url = "https://www.b\u00FCcher.ch/search?q=foo+bar";
  result = SearchService.parseSubmissionURL(url);
  Assert.equal(result.engine, engine4);
  Assert.equal(result.terms, "foo bar");

  // Test search engines with escaped IDNs.
  url = "https://www.xn--bcher-kva.ch/search?q=foo+bar";
  result = SearchService.parseSubmissionURL(url);
  Assert.equal(result.engine, engine4);
  Assert.equal(result.terms, "foo bar");

  // Parsing of parameters from an engine template URL is not supported
  // if no matching parameter value template is provided.
  Assert.equal(
    SearchService.parseSubmissionURL("https://www.bacon.moz/search?q=").engine,
    null
  );

  // Parsing of parameters from an engine template URL is supported
  // if a matching parameter value template is provided.
  url = "https://duckduckgo.com/?foo=bar&q=caff%C3%A8";
  result = SearchService.parseSubmissionURL(url);
  Assert.equal(result.engine, engine5);
  Assert.equal(result.terms, "caff\u00E8");

  // If the search params are in the template, the query parameter
  // doesn't need to be separated from the host by a slash, only by
  // by a question mark.
  url = "https://duckduckgo.com?foo=bar&q=caff%C3%A8";
  result = SearchService.parseSubmissionURL(url);
  Assert.equal(result.engine, engine5);
  Assert.equal(result.terms, "caff\u00E8");

  // HTTP and HTTPS schemes are interchangeable.
  url = "https://www.google.com/search?q=caff%C3%A8";
  result = SearchService.parseSubmissionURL(url);
  Assert.equal(result.engine, engine1);
  Assert.equal(result.terms, "caff\u00E8");

  // Decoding search terms with multiple spaces should work.
  result = SearchService.parseSubmissionURL(
    "https://www.google.com/search?q=+with++spaces+"
  );
  Assert.equal(result.engine, engine1);
  Assert.equal(result.terms, " with  spaces ");

  // Parsing search terms with ampersands should work.
  result = SearchService.parseSubmissionURL(
    "https://www.google.com/search?q=with%26ampersand"
  );
  Assert.equal(result.engine, engine1);
  Assert.equal(result.terms, "with&ampersand");

  // Capitals in the path should work
  result = SearchService.parseSubmissionURL(
    "https://www.google.com/SEARCH?q=caps"
  );
  Assert.equal(result.engine, engine1);
  Assert.equal(result.terms, "caps");

  // An empty query parameter should work the same.
  url = "https://www.google.com/search?q=";
  result = SearchService.parseSubmissionURL(url);
  Assert.equal(result.engine, engine1);
  Assert.equal(result.terms, "");

  // There should be no match when the path is different.
  result = SearchService.parseSubmissionURL(
    "https://www.google.com/search/?q=test"
  );
  Assert.equal(result.engine, null);
  Assert.equal(result.terms, "");

  // There should be no match when the argument is different.
  result = SearchService.parseSubmissionURL(
    "https://www.google.com/search?q2=test"
  );
  Assert.equal(result.engine, null);
  Assert.equal(result.terms, "");

  // There should be no match for URIs that are not HTTP or HTTPS.
  result = SearchService.parseSubmissionURL("file://localhost/search?q=test");
  Assert.equal(result.engine, null);
  Assert.equal(result.terms, "");

  let mainEngine = SearchService.getEngineById("mainengine");
  result = SearchService.parseSubmissionURL(
    "https://example.com/searchBar?partnerCode=mainenginepc&foo=test"
  );
  Assert.equal(result.engine, mainEngine);
  Assert.equal(result.terms, "test");
  Assert.equal(result.partnerCodeParameterName, "partnerCode");
  Assert.equal(result.termsParameterName, "foo");
});
