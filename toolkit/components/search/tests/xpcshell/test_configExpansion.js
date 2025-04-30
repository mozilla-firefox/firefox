/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

ChromeUtils.defineESModuleGetters(this, {
  JsonSchema: "resource://gre/modules/JsonSchema.sys.mjs",
});

add_task(async function test_expand_minimal_and_full() {
  let partialConfig = [
    {
      identifier: "all-param-engine",
      recordType: "engine",
      base: {
        aliases: ["testenginea", "testengineb"],
        charset: "EUC-JP",
        classification: "general",
        name: "testEngine name",
        partnerCode: "pc",
        urls: {
          search: {
            base: "https://example.com/1",
            // Method defaults to GET
            params: [
              { name: "partnerCode", value: "abc" },
              { name: "starbase", value: "Regula_I" },
              { name: "experiment", value: "Genesis" },
            ],
            searchTermParamName: "search",
          },
          suggestions: {
            base: "https://example.com/2",
            method: "POST",
            searchTermParamName: "suggestions",
          },
          trending: {
            base: "https://example.com/3",
            searchTermParamName: "trending",
          },
        },
      },
      variants: [{ environment: { allRegionsAndLocales: true } }],
    },
    { identifier: "minimal-engine" },
  ];

  let fullConfig = SearchTestUtils.expandPartialConfig(partialConfig);

  let schema = await IOUtils.readJSON(
    PathUtils.join(do_get_cwd().path, "search-config-v2-schema.json")
  );

  let validator = new JsonSchema.Validator(schema);

  for (let obj of fullConfig) {
    let result = validator.validate(obj);
    Assert.ok(result.valid, "Expanded config should be valid.");
  }

  Assert.equal(
    fullConfig.length,
    5,
    "Should have 2 engines, defaultEngines, engineOrders and availableLocales."
  );
});
