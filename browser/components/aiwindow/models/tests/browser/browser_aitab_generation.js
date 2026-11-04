/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// End-to-end page generation with only the LLM mocked: the chat model calls the
// generate_page tool through Chat.fetchWithHistory, and the source pages are
// served over HTTP and read through the real GetPageContent, so this covers
// tool dispatch, extraction, prompt assembly, page validation, metadata
// derivation and storage.

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AITab: "moz-src:///browser/components/aiwindow/models/aitab/AITab.sys.mjs",
  AITabStore:
    "moz-src:///browser/components/aiwindow/ui/modules/AITabStore.sys.mjs",
  Chat: "moz-src:///browser/components/aiwindow/models/Chat.sys.mjs",
  ConversationStore:
    "moz-src:///browser/components/aiwindow/ui/modules/ConversationStore.sys.mjs",
  createAITab: "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  MLTestUtils: "resource://testing-common/MLTestUtils.sys.mjs",
  MODEL_FEATURES: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  PURPOSES: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  MockEngineManager: "resource://testing-common/AIWindowTestUtils.sys.mjs",
  newChatConversation: "resource://testing-common/AIWindowTestUtils.sys.mjs",
  newConversation: "resource://testing-common/AIWindowTestUtils.sys.mjs",
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
  GENERATE_AITAB: "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  expandUrlTokens:
    "moz-src:///browser/components/aiwindow/ui/modules/UrlTokenizer.sys.mjs",
  UrlTokenizer:
    "moz-src:///browser/components/aiwindow/ui/modules/UrlTokenizer.sys.mjs",
});

add_setup(async function () {
  // Raise AITab's log verbosity for the duration of this file so a failing run
  // surfaces the module's debug/error output. Reverted automatically at teardown.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.conversation.logLevel", "Debug"],
      ["browser.smartwindow.aitab.enabled", true],
    ],
  });
});

// What the mocked model returns. Schema coverage is browser_aitab_validation.js'
// job, so this is the smallest surface the packaged catalog accepts: a root
// Page, a Header (for the title), and one TextBlock.
const GENERATED_SURFACE = Object.freeze({
  components: [
    { id: "root", component: "Page", header: "hdr", children: ["lead"] },
    { id: "hdr", component: "Header", title: "Hotels in Lisbon" },
    {
      id: "lead",
      component: "TextBlock",
      lead: "Budget Central Hostel is $72 / night.",
    },
  ],
  dataModel: {},
});

// Serve a page for generation to read. The URL is not open in a tab, so
// GetPageContent extracts it headlessly — the real path, no stubbing. cleanup()
// resolves once the page has been served, so only call this when the test
// actually reads the URL.
function servePage() {
  const { html } = lazy.MLTestUtils.serveHTML();
  return html`
    <article>
      <h1>Hotels in Lisbon</h1>
      <p>Budget Central Hostel is $72 / night in Lisbon.</p>
      <p>It is a short walk from the Baixa district and has free breakfast.</p>
    </article>
  `;
}

/**
 * Have the chat model call generate_page. Answer the generation request before
 * awaiting the tool's result.
 *
 * @param {MockEngineManager} mockEngine
 * @param {ChatConversation} conversation
 * @param {object} toolParams - The generate_page arguments.
 */
function generatePage(mockEngine, conversation, toolParams) {
  return callTool(mockEngine, conversation, {
    toolName: lazy.GENERATE_AITAB,
    toolParams,
  });
}

/**
 * The page a successful generate_page call stored.
 *
 * @param {{body: object|string}} result - What `generatePage()` resolved with.
 */
async function storedPage({ body }) {
  Assert.ok(body.page, `generation should succeed: ${JSON.stringify(body)}`);
  return lazy.AITabStore.getBySlug(body.page.slug);
}

/**
 * The token generateAITab gives `url` when it is the first URL in the prompt
 * with its host and path.
 *
 * @param {string} url
 * @returns {string}
 */
function urlToken(url) {
  return new lazy.UrlTokenizer().formatToken(url);
}

add_task(async function test_generateAITab_requires_urls() {
  // No URLs: rejected before any model call, so no engine is needed.
  const result = await lazy.AITab.generateAITab(
    { urlList: [] },
    lazy.newConversation()
  );
  Assert.ok(result.error, "an empty urlList is reported as an error");
});

add_task(async function test_generateAITab_honors_aborted_signal() {
  // Cancellation is checked before the extraction, so the URL is never read and
  // no page needs serving. No model call either, so no engine is needed.
  const result = await lazy.AITab.generateAITab(
    {
      urlList: ["https://example.com/lisbon-hotels"],
      signal: AbortSignal.abort(),
    },
    lazy.newConversation()
  );
  Assert.ok(result.error, "an aborted signal cancels generation");
  Assert.ok(!result.surface, "no surface is returned when canceled");
});

add_task(async function test_createAITab_does_not_save_canceled_page() {
  const mockEngine = new lazy.MockEngineManager();
  const controller = new AbortController();
  const conversation = lazy.newConversation();

  try {
    const resultPromise = lazy.createAITab(
      { raw_content: "Hotels in Lisbon", focus: "Make a hotel comparison" },
      conversation,
      controller.signal
    );
    await mockEngine.captureRequest({
      purpose: lazy.MODEL_FEATURES.AITAB,
    });

    controller.abort();
    const result = await resultPromise;

    Assert.ok(
      !result.success,
      "A canceled generation process does not create an AITab"
    );
    Assert.ok(
      result.toolResult.includes("canceled"),
      "The result reports cancellation of the generation process"
    );
    Assert.deepEqual(
      await lazy.AITabStore.getAITabPagesByConvId(conversation.id),
      [],
      "The canceled AITab is not saved"
    );
  } finally {
    await mockEngine.rejectAllRequests();
    mockEngine.cleanupMocks();
  }
});

add_task(async function test_generateAITab_success() {
  const mockEngine = new lazy.MockEngineManager();
  const { url: GEN_URL, cleanup: stopServing } = servePage();
  try {
    const call = generatePage(mockEngine, await lazy.newChatConversation(), {
      url_list: [GEN_URL],
      focus: "hotels in Lisbon",
    });
    // AITAB generation runs on the chat engine purpose (selectFeatureConfig
    // remaps the aitab purpose to chat), so respond there with a valid config.
    const { request, respond } = await mockEngine.captureRequest({
      purpose: lazy.MODEL_FEATURES.AITAB,
    });
    const serializedRequest = JSON.stringify(request.args);
    Assert.ok(
      serializedRequest.includes("Budget Central Hostel is $72 / night"),
      "the extracted page content reaches the model prompt"
    );
    Assert.ok(
      serializedRequest.includes("hotels in Lisbon"),
      "the requested focus reaches the model prompt"
    );
    respond(JSON.stringify(GENERATED_SURFACE));

    const page = await storedPage(await call);
    const { metadata, surface } = page.components;
    Assert.deepEqual(
      surface,
      GENERATED_SURFACE,
      "the validated surface is stored unchanged"
    );
    Assert.equal(
      metadata.title,
      "Hotels in Lisbon",
      "metadata title comes from the Header component"
    );
    Assert.equal(
      metadata.id,
      "hotels-in-lisbon",
      "metadata id is the slugified title"
    );
    Assert.equal(metadata.howCreated, "chat", "howCreated is chat");
    Assert.deepEqual(
      page.context.urlsUsed.map(u => u.url),
      [GEN_URL],
      "the requested URL is recorded in the generation context"
    );
    Assert.ok(
      page.context.urlsUsed[0].extractedText.includes("Budget Central Hostel"),
      "the extracted page text is recorded in the generation context"
    );
  } finally {
    await mockEngine.rejectAllRequests();
    await stopServing();
    mockEngine.cleanupMocks();
    await lazy.AITabStore.destroyDatabase();
  }
});

add_task(async function test_generateAITab_telemetry_success() {
  Services.fog.testResetFOG();
  const mockEngine = new lazy.MockEngineManager();
  const { url: GEN_URL, cleanup: stopServing } = servePage();
  try {
    const conversation = await lazy.newChatConversation();
    // Set when the chat was opened from the tab group menu.
    conversation.aitabSource = "tab_group";
    const call = generatePage(mockEngine, conversation, {
      url_list: [GEN_URL, 42],
      raw_content: "some notes",
    });
    const response = JSON.stringify(GENERATED_SURFACE);
    await mockEngine.respondTo({
      purpose: lazy.MODEL_FEATURES.AITAB,
      response,
    });
    await storedPage(await call);

    const [initiate] = Glean.smartWindow.aitabCreateInitiate.testGetValue();
    Assert.deepEqual(
      initiate.extra,
      {
        num_tabs: "1",
        how_initiated: "tab_group",
        raw_content_len: "10",
        // The tool drops the URL that isn't a string before generating.
        num_tabs_total_attempted: "1",
      },
      "aitab_create_initiate describes the request"
    );

    const [complete] = Glean.smartWindow.aitabCreateComplete.testGetValue();
    Assert.equal(complete.extra.is_success, "true", "is_success is true");
    Assert.ok(!("error" in complete.extra), "no error on success");
    Assert.deepEqual(
      JSON.parse(complete.extra.components_used),
      ["Page", "Header", "TextBlock"],
      "components are listed in render order"
    );
    Assert.greater(
      Number(complete.extra.num_chars_read),
      "some notes".length,
      "num_chars_read counts the page and raw content"
    );
    Assert.equal(
      complete.extra.num_chars_out,
      String(response.length),
      "num_chars_out is the model output length"
    );
    Assert.ok("seconds_elapsed" in complete.extra, "seconds_elapsed is set");
    Assert.equal(
      Glean.smartWindow.aitabEditComplete.testGetValue(),
      null,
      "a new page is not reported as an edit"
    );
  } finally {
    await mockEngine.rejectAllRequests();
    await stopServing();
    mockEngine.cleanupMocks();
    await lazy.AITabStore.destroyDatabase();
  }
});

add_task(async function test_generateAITab_telemetry_failure() {
  Services.fog.testResetFOG();
  await lazy.AITab.generateAITab({ urlList: [] }, lazy.newConversation());

  const [initiate] = Glean.smartWindow.aitabCreateInitiate.testGetValue();
  Assert.equal(initiate.extra.how_initiated, "chat", "defaults to chat");
  const [complete] = Glean.smartWindow.aitabCreateComplete.testGetValue();
  Assert.equal(complete.extra.is_success, "false", "is_success is false");
  Assert.equal(complete.extra.error, "no_content", "the error code is set");
  Assert.equal(
    complete.extra.components_used,
    "[]",
    "no components on failure"
  );
});

add_task(async function test_generateAITab_includes_page_image() {
  // A page's cached preview image (og:image in Places) should reach the model
  // prompt as an `Image:` line and be recorded on its urlsUsed entry.
  const mockEngine = new lazy.MockEngineManager();
  const { url: GEN_URL, cleanup: stopServing } = servePage();
  const IMAGE_URL = "https://example.com/lisbon-hero.jpg";
  // Seed the preview image in Places for the page that generation will read.
  await lazy.PlacesTestUtils.addVisits(GEN_URL);
  await PlacesUtils.history.update({
    url: GEN_URL,
    previewImageURL: IMAGE_URL,
  });
  try {
    const call = generatePage(mockEngine, await lazy.newChatConversation(), {
      url_list: [GEN_URL],
      focus: "hotels in Lisbon",
    });
    const { request, respond } = await mockEngine.captureRequest({
      purpose: lazy.MODEL_FEATURES.AITAB,
    });
    const serializedRequest = JSON.stringify(request.args);
    Assert.ok(
      serializedRequest.includes(
        "Image: §url_token: EXAMPLE_COM_LISBON_HERO_JPG_1§"
      ),
      "the page's preview image URL reaches the model prompt as a token"
    );
    Assert.ok(
      !serializedRequest.includes(IMAGE_URL),
      "the raw preview image URL does not reach the model prompt"
    );
    respond(JSON.stringify(GENERATED_SURFACE));

    const page = await storedPage(await call);
    Assert.equal(
      page.context.urlsUsed[0].imageUrl,
      IMAGE_URL,
      "the preview image URL is recorded on the urlsUsed entry"
    );
  } finally {
    await mockEngine.rejectAllRequests();
    await stopServing();
    mockEngine.cleanupMocks();
    await lazy.AITabStore.destroyDatabase();
    await PlacesUtils.history.clear();
  }
});

add_task(async function test_generateAITab_omits_image_for_denied_url() {
  // Threat model: a prompt injection could ask AITab to build a page from an
  // attacker-chosen URL. When the conversation holds both untrusted input and
  // private data, get_page_content refuses to fetch unmentioned, non-SERP URLs
  // headlessly to block that exfiltration channel. This test asserts the image
  // path honors the same refusal — the denied URL's cached og:image must reach
  // neither the model prompt nor the urlsUsed metadata — so it cannot become a
  // side channel around the existing block.
  const mockEngine = new lazy.MockEngineManager();
  const DENIED_URL = "https://example.com/denied-private-untrusted-page";
  const IMAGE_URL = "https://example.com/denied-hero.jpg";
  await lazy.PlacesTestUtils.addVisits(DENIED_URL);
  await PlacesUtils.history.update({
    url: DENIED_URL,
    previewImageURL: IMAGE_URL,
  });
  // A second, readable URL: generation stops before the model when nothing at
  // all can be read, and the denied URL alone would trip that. The SERP ledger
  // exempts this one, so the read is partial rather than empty.
  const { url: ALLOWED_URL, cleanup: stopServing } = servePage();
  try {
    const conversation = await lazy.newChatConversation();
    // Mark the conversation as holding private data and untrusted input;
    // commit() makes the staged flags visible to the security getters.
    conversation.securityProperties.setPrivateData();
    conversation.securityProperties.setUntrustedInput();
    conversation.securityProperties.commit();
    conversation.addSerpUrlsForAnonymousFetch([ALLOWED_URL]);
    const call = generatePage(mockEngine, conversation, {
      url_list: [DENIED_URL, ALLOWED_URL],
    });
    const { request, respond } = await mockEngine.captureRequest({
      purpose: lazy.MODEL_FEATURES.AITAB,
    });
    const serializedRequest = JSON.stringify(request.args);
    Assert.ok(
      serializedRequest.includes("Access is not allowed"),
      "the denied URL surfaces the refusal message, not page content"
    );
    Assert.ok(
      !serializedRequest.includes(IMAGE_URL) &&
        !serializedRequest.includes("EXAMPLE_COM_DENIED_HERO_JPG"),
      "the denied URL's preview image does not reach the model prompt"
    );
    respond(JSON.stringify(GENERATED_SURFACE));

    const page = await storedPage(await call);
    Assert.equal(
      page.context.urlsUsed[0].imageUrl,
      null,
      "no preview image URL is recorded for a denied URL"
    );
  } finally {
    await mockEngine.rejectAllRequests();
    await stopServing();
    mockEngine.cleanupMocks();
    await lazy.AITabStore.destroyDatabase();
    await PlacesUtils.history.clear();
  }
});

// A decodable favicon payload for Places (setFaviconForPage rejects data it
// cannot decode as an image).
const FAVICON_DATA_URL =
  "data:image/svg+xml;base64," +
  btoa(
    `<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="16" height="16" fill="#424e5a"/></svg>`
  );

add_task(async function test_generateAITab_hydrates_link_favicons() {
  // Covers the `items` shapes hydration must handle: a literal array
  // (Header.references), an absolute data-model binding (SourceLinks), and a
  // relative binding (left alone — validation does not check those). Also
  // covers that model-supplied favicons are replaced or stripped, never kept.
  const mockEngine = new lazy.MockEngineManager();
  const { url: GEN_URL, cleanup: stopServing } = servePage();
  const FAVICON_URL = "https://example.com/favicon.ico";
  const NO_FAVICON_URL = "https://example.org/never-visited";
  const MODEL_FAVICON = "https://model.example/injected.ico";
  await lazy.PlacesTestUtils.addVisits(GEN_URL);
  await lazy.PlacesTestUtils.setFaviconForPage(
    GEN_URL,
    FAVICON_URL,
    FAVICON_DATA_URL
  );
  try {
    const call = generatePage(mockEngine, await lazy.newChatConversation(), {
      url_list: [GEN_URL],
      focus: `hotels in Lisbon, see ${NO_FAVICON_URL}`,
    });
    await mockEngine.respondTo({
      purpose: lazy.MODEL_FEATURES.AITAB,
      response: JSON.stringify({
        components: [
          {
            id: "root",
            component: "Page",
            header: "hdr",
            children: ["lead", "links", "more"],
          },
          {
            id: "hdr",
            component: "Header",
            title: "Hotels in Lisbon",
            references: {
              items: [
                {
                  href: urlToken(GEN_URL),
                  title: "Hotels",
                  favicon: MODEL_FAVICON,
                },
                {
                  href: urlToken(NO_FAVICON_URL),
                  title: "Unvisited",
                  favicon: MODEL_FAVICON,
                },
              ],
            },
          },
          {
            id: "lead",
            component: "TextBlock",
            lead: "Budget Central Hostel is $72 / night.",
          },
          {
            id: "links",
            component: "SourceLinks",
            items: { path: "/sources" },
          },
          {
            id: "more",
            component: "SourceLinks",
            items: { path: "more" },
          },
        ],
        dataModel: {
          sources: [{ href: urlToken(GEN_URL), title: "Hotels" }],
          more: [{ href: urlToken(GEN_URL), title: "More" }],
        },
      }),
    });

    const { surface } = (await storedPage(await call)).components;
    const header = surface.components.find(c => c.component === "Header");
    Assert.equal(
      header.references.items[0].favicon,
      `page-icon:${GEN_URL}`,
      "a literal SourceLink item gets its stored favicon URL, replacing the model's"
    );
    Assert.ok(
      !("favicon" in header.references.items[1]),
      "a model-supplied favicon is stripped when Places has none stored"
    );
    Assert.equal(
      surface.dataModel.sources[0].favicon,
      `page-icon:${GEN_URL}`,
      "an absolutely-bound SourceLink item gets its stored favicon URL"
    );
    Assert.ok(
      !("favicon" in surface.dataModel.more[0]),
      "a relatively-bound array is not resolved, so its items are not touched"
    );
  } finally {
    await mockEngine.rejectAllRequests();
    await stopServing();
    mockEngine.cleanupMocks();
    await lazy.AITabStore.destroyDatabase();
    await PlacesUtils.history.clear();
  }
});

add_task(async function test_generateAITab_hydrates_favicon_for_denied_url() {
  // Unlike the og:image path above, favicon hydration is not subject to the
  // conversation's access-control decision: Places only has favicons for
  // sites the user has visited, so they already count as seen. A visited
  // page's favicon hydrates (replacing the model's value) even when the
  // conversation refuses to read the page itself.
  const mockEngine = new lazy.MockEngineManager();
  const DENIED_URL = "https://example.com/denied-sourcelink-page";
  const FAVICON_URL = "https://example.com/denied-favicon.ico";
  await lazy.PlacesTestUtils.addVisits(DENIED_URL);
  await lazy.PlacesTestUtils.setFaviconForPage(
    DENIED_URL,
    FAVICON_URL,
    FAVICON_DATA_URL
  );
  // See the og:image test: a readable companion URL so the read is partial
  // rather than empty, which would stop generation before the model.
  const { url: ALLOWED_URL, cleanup: stopServing } = servePage();
  try {
    const conversation = await lazy.newChatConversation();
    conversation.securityProperties.setPrivateData();
    conversation.securityProperties.setUntrustedInput();
    conversation.securityProperties.commit();
    conversation.addSerpUrlsForAnonymousFetch([ALLOWED_URL]);
    // A refused URL may only be linked when the chat has seen it, as it would
    // have from history search.
    conversation.addSeenUrls([DENIED_URL]);
    const call = generatePage(mockEngine, conversation, {
      url_list: [DENIED_URL, ALLOWED_URL],
    });
    await mockEngine.respondTo({
      purpose: lazy.MODEL_FEATURES.AITAB,
      response: JSON.stringify({
        components: [
          {
            id: "root",
            component: "Page",
            header: "hdr",
            children: ["lead", "links"],
          },
          { id: "hdr", component: "Header", title: "Hotels in Lisbon" },
          {
            id: "lead",
            component: "TextBlock",
            lead: "Budget Central Hostel is $72 / night.",
          },
          {
            id: "links",
            component: "SourceLinks",
            items: [
              {
                href: urlToken(DENIED_URL),
                favicon: "https://model.example/injected.ico",
              },
            ],
          },
        ],
        dataModel: {},
      }),
    });

    const { surface } = (await storedPage(await call)).components;
    const links = surface.components.find(c => c.component === "SourceLinks");
    Assert.equal(
      links.items[0].favicon,
      `page-icon:${DENIED_URL}`,
      "the visited page's stored favicon hydrates despite the content refusal"
    );
  } finally {
    await mockEngine.rejectAllRequests();
    await stopServing();
    mockEngine.cleanupMocks();
    await lazy.AITabStore.destroyDatabase();
    await PlacesUtils.history.clear();
  }
});

add_task(async function test_generateAITab_rejects_invalid_page() {
  const mockEngine = new lazy.MockEngineManager();
  const { url: GEN_URL, cleanup: stopServing } = servePage();
  try {
    const conversation = await lazy.newChatConversation();
    const call = generatePage(mockEngine, conversation, {
      url_list: [GEN_URL],
    });
    // A structurally valid JSON object that does not match the catalog (a root
    // Page referencing an unknown component type) must fail validation rather
    // than be returned.
    await mockEngine.respondTo({
      purpose: lazy.MODEL_FEATURES.AITAB,
      response: JSON.stringify({
        components: [
          { id: "root", component: "Page", header: "hdr", children: ["b"] },
          { id: "hdr", component: "Header", title: "Hotels in Lisbon" },
          { id: "b", component: "Banner" },
        ],
      }),
    });

    const { body } = await call;
    Assert.ok(
      body.startsWith("The page could not be created"),
      `a surface that fails validation is an error: ${body}`
    );
    Assert.deepEqual(
      await lazy.AITabStore.getAITabPagesByConvId(conversation.id),
      [],
      "no page is stored on validation failure"
    );
  } finally {
    await mockEngine.rejectAllRequests();
    await stopServing();
    mockEngine.cleanupMocks();
    await lazy.AITabStore.destroyDatabase();
  }
});

add_task(async function test_generateAITab_slug_keeps_accented_letters() {
  const mockEngine = new lazy.MockEngineManager();
  const { url, cleanup } = servePage();
  try {
    const call = generatePage(mockEngine, await lazy.newChatConversation(), {
      url_list: [url],
    });

    await mockEngine.respondTo({
      purpose: lazy.MODEL_FEATURES.AITAB,
      response: JSON.stringify({
        ...GENERATED_SURFACE,
        components: GENERATED_SURFACE.components.map(c =>
          c.id == "hdr" ? { ...c, title: "Cafés in São Paulo" } : c
        ),
      }),
    });

    const page = await storedPage(await call);
    Assert.equal(
      page.components.metadata.id,
      "cafes-in-sao-paulo",
      "accents are dropped but their letters are kept"
    );
  } finally {
    await mockEngine.rejectAllRequests();
    await cleanup();
    mockEngine.cleanupMocks();
    await lazy.AITabStore.destroyDatabase();
  }
});

add_task(async function test_generateAITab_model_reported_error() {
  Services.fog.testResetFOG();
  const mockEngine = new lazy.MockEngineManager();
  const { url: GEN_URL, cleanup: stopServing } = servePage();
  try {
    const genPromise = lazy.AITab.generateAITab(
      { urlList: [GEN_URL] },
      lazy.newConversation()
    );

    await mockEngine.respondTo({
      purpose: lazy.MODEL_FEATURES.AITAB,
      response: JSON.stringify({ error: "the pages have nothing to compare" }),
    });

    const result = await genPromise;
    Assert.ok(
      result.error?.includes("the pages have nothing to compare"),
      `the model's error is returned: ${result.error}`
    );
    Assert.equal(result.code, "content_related", "the error code is returned");

    const [complete] = Glean.smartWindow.aitabCreateComplete.testGetValue();
    Assert.equal(complete.extra.is_success, "false", "is_success is false");
    Assert.equal(
      complete.extra.error,
      "content_related",
      "the error code is set"
    );
  } finally {
    await stopServing();
    mockEngine.cleanupMocks();
  }
});

add_task(async function test_generateAITab_default_title_is_localized() {
  const mockEngine = new lazy.MockEngineManager();
  // Two URLs, so the single-URL heading fallback doesn't apply either.
  const first = servePage();
  const second = servePage();
  try {
    const call = generatePage(mockEngine, await lazy.newChatConversation(), {
      url_list: [first.url, second.url],
    });
    // The model bound the Header title to data that is not a string, so no
    // title can be derived from the surface; with no focus from the user
    // either, it falls all the way through to the localized default.
    await mockEngine.respondTo({
      purpose: lazy.MODEL_FEATURES.AITAB,
      response: JSON.stringify({
        components: [
          { id: "root", component: "Page", header: "hdr", children: ["lead"] },
          { id: "hdr", component: "Header", title: { path: "/nights" } },
          {
            id: "lead",
            component: "TextBlock",
            lead: "Budget Central Hostel is $72 / night.",
          },
        ],
        dataModel: { nights: 3 },
      }),
    });

    const page = await storedPage(await call);
    Assert.equal(
      page.components.metadata.title,
      "Generated page",
      "the default title comes from ai-tab-default-page-title"
    );
  } finally {
    await mockEngine.rejectAllRequests();
    await first.cleanup();
    await second.cleanup();
    mockEngine.cleanupMocks();
    await lazy.AITabStore.destroyDatabase();
  }
});

add_task(async function test_createAITab_links_to_the_stored_page() {
  const mockEngine = new lazy.MockEngineManager();
  const { url, cleanup: stopServing } = servePage();
  try {
    // The tool generates the page and returns a markdown link carrying the
    // viewer URL as a token.
    const conversation = await lazy.newChatConversation();
    const call = generatePage(mockEngine, conversation, {
      url_list: [url],
      focus: "hotels in Lisbon",
    });
    await mockEngine.respondTo({
      purpose: lazy.MODEL_FEATURES.AITAB,
      response: JSON.stringify(GENERATED_SURFACE),
    });
    const { body: toolResult, toolUIData } = await call;
    Assert.equal(toolUIData.properties.state, "choose");

    Assert.ok(
      toolResult.page.slug,
      "the tool reports the stored page's slug, so a later turn can find it"
    );

    const expanded = lazy.expandUrlTokens(
      toolResult.message,
      conversation.tokenToUrl
    );
    const viewerURL = `about:smartpage?page=${toolResult.page.slug}`;
    Assert.ok(
      expanded.includes(`](${viewerURL})`),
      `the link names the stored page: ${expanded}`
    );
    Assert.ok(
      conversation.seenUrls.has(viewerURL),
      "the link is seen, so the chat renders it as a trusted link"
    );
    Assert.equal(toolUIData.properties.viewerURL, viewerURL);
  } finally {
    await mockEngine.rejectAllRequests();
    await lazy.AITabStore.destroyDatabase();
    await stopServing();
    mockEngine.cleanupMocks();
  }
});

add_task(async function test_modification_is_answered_by_the_model() {
  const mockEngine = new lazy.MockEngineManager();
  const { url, cleanup: stopServing } = servePage();
  let pageTab;
  try {
    const conversation = await lazy.newChatConversation();
    const created = generatePage(mockEngine, conversation, { url_list: [url] });
    await mockEngine.respondTo({
      purpose: lazy.MODEL_FEATURES.AITAB,
      response: JSON.stringify(GENERATED_SURFACE),
    });
    const { body: createdResult } = await created;

    const viewerURL = lazy.AITab.buildViewerURL(createdResult.page.slug);
    pageTab = await BrowserTestUtils.openNewForegroundTab(gBrowser, viewerURL);
    const reloadSpy = sinon.spy(pageTab.linkedBrowser, "reload");

    conversation.addUserMessage("Make it shorter");
    conversation.addAssistantMessage("text", "");
    const turn = lazy.Chat.fetchWithHistory({
      conversation,
      browsingContext: pageTab.linkedBrowser.browsingContext,
    });
    const id = await mockEngine.respondToWithToolCall({
      toolName: lazy.GENERATE_AITAB,
      toolParams: {
        modify_slug: createdResult.page.slug,
        modify_instructions: "Make it shorter",
      },
    });
    await mockEngine.respondTo({
      purpose: lazy.MODEL_FEATURES.AITAB,
      response: JSON.stringify(GENERATED_SURFACE),
    });

    const { request, respond } = await mockEngine.captureRequest({
      purpose: lazy.PURPOSES.CHAT,
    });
    Assert.ok(
      JSON.stringify(request.args).includes("The page was modified."),
      "the model gets a follow-up turn to tell the user about the change"
    );
    respond("I made the page shorter.");
    await turn;
    Assert.ok(
      reloadSpy.calledOnce,
      "the open page reloads to show the modified version"
    );

    Assert.ok(
      !conversation.messages.some(m => m.toolUIData?.toolCallId === id),
      "a modification leaves no tool card in the chat"
    );
  } finally {
    if (pageTab) {
      BrowserTestUtils.removeTab(pageTab);
    }
    await mockEngine.rejectAllRequests();
    await lazy.AITabStore.destroyDatabase();
    await stopServing();
    mockEngine.cleanupMocks();
  }
});

/**
 * Ids of every stored aitab conversation, so a test can tell which row a
 * generation just created.
 *
 * @returns {Promise<Set<string>>}
 */
async function storedAITabConvIds() {
  await lazy.ConversationStore.ensureDatabase();
  const rows = await lazy.ConversationStore.connection.execute(
    "SELECT conv_id FROM conversation WHERE feature = :feature",
    { feature: "aitab" }
  );
  return new Set(rows.map(row => row.getResultByName("conv_id")));
}

add_task(async function test_generateAITab_persists_its_tool_conversation() {
  const mockEngine = new lazy.MockEngineManager();
  const { url: GEN_URL, cleanup: stopServing } = servePage();

  const SEEN = "https://parent.example/already-seen";
  const SERP = "https://parent.example/search-result";

  const before = await storedAITabConvIds();
  let convId;
  try {
    const chat = await lazy.newChatConversation();
    chat.addSeenUrls([SEEN]);
    chat.addSerpUrlsForAnonymousFetch([SERP]);
    const call = generatePage(mockEngine, chat, {
      url_list: [GEN_URL],
      focus: "",
    });
    const { respond } = await mockEngine.captureRequest({
      purpose: lazy.MODEL_FEATURES.AITAB,
    });
    respond(JSON.stringify(GENERATED_SURFACE));
    await storedPage(await call);

    // The save runs in the background, so the row may land after the call
    // returns.
    let added = [];
    await TestUtils.waitForCondition(async () => {
      const after = await storedAITabConvIds();
      added = [...after].filter(id => !before.has(id));
      return added.length;
    }, "generation stores one tool conversation");
    Assert.equal(added.length, 1, "exactly one tool conversation is stored");
    convId = added[0];

    // Nothing here sets the flags or the URLs: generateAITab does. Reading
    // the row back is what shows it did, and did it before saving.
    const stored = await lazy.ConversationStore.findConversationById(convId);
    Assert.ok(
      stored.securityProperties.privateData,
      "the stored tool conversation is marked as holding private data"
    );
    Assert.ok(
      stored.securityProperties.untrustedInput,
      "the stored tool conversation is marked as holding untrusted input"
    );
    Assert.deepEqual(
      Array.from(stored.seenUrls),
      [SEEN],
      "it inherits the chat's seen URLs and adds none of its own"
    );
    Assert.deepEqual(
      Array.from(stored.serpUrlsForAnonymousFetch),
      [SERP],
      "search result URLs come across with their anonymous-fetch exemption"
    );
  } finally {
    await mockEngine.rejectAllRequests();
    if (convId) {
      await lazy.ConversationStore.deleteConversationById(convId);
    }
    await stopServing();
    mockEngine.cleanupMocks();
    await lazy.AITabStore.destroyDatabase();
  }
});
