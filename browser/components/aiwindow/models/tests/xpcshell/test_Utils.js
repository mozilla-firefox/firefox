/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const {
  DEFAULT_ENGINE_ID,
  MODEL_FEATURES,
  openAIEngine,
  renderPrompt,
  SERVICE_TYPES,
  PURPOSES,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

/**
 * Constants for preference keys and test values
 */
const PREF_API_KEY = "browser.smartwindow.apiKey";
const PREF_ENDPOINT = "browser.smartwindow.endpoint";
const PREF_CUSTOM_ENDPOINT = "browser.smartwindow.customEndpoint";
const PREF_MODEL = "browser.smartwindow.model";
const PREF_EXTRA_HEADERS = "browser.smartwindow.extraHeaders";

const API_KEY = "fake-key";
const ENDPOINT = "https://api.fake-endpoint.com/v1";
const MODEL = "fake-model";
const EXTRA_HEADERS = '{"x-fastly-request": "fake-key"}';

/**
 * Cleans up preferences after testing
 */
registerCleanupFunction(() => {
  for (let pref of [
    PREF_API_KEY,
    PREF_ENDPOINT,
    PREF_CUSTOM_ENDPOINT,
    PREF_MODEL,
  ]) {
    if (Services.prefs.prefHasUserValue(pref)) {
      Services.prefs.clearUserPref(pref);
    }
  }
});

/**
 * Tests the creation of an OpenAI engine instance
 */
add_task(async function test_createOpenAIEngine_with_chat_feature() {
  Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
  Services.prefs.setStringPref(PREF_ENDPOINT, ENDPOINT);
  Services.prefs.setStringPref(PREF_MODEL, MODEL);
  Services.prefs.setStringPref(PREF_EXTRA_HEADERS, EXTRA_HEADERS);

  const sb = sinon.createSandbox();
  try {
    // Take engine to stub out actual engine creation
    const fakeEngine = {
      runWithGenerator() {
        throw new Error("not used");
      },
    };

    const stub = sb.stub(openAIEngine, "_createEngine").resolves(fakeEngine);
    const engine = await openAIEngine.build({
      model: MODEL,
      serviceType: SERVICE_TYPES.AI,
      purpose: PURPOSES.CHAT,
      flowId: null,
      feature: MODEL_FEATURES.CHAT,
    });
    Assert.strictEqual(
      engine.engineInstance,
      fakeEngine,
      "Should return engine from _createEngine"
    );
    Assert.ok(stub.calledOnce, "_createEngine should be called once");

    // Test preferences were read correctly
    const opts = stub.firstCall.args[0];
    Assert.equal(opts.apiKey, "", "apiKey is empty on MLPA endpoint");
    Assert.equal(opts.backend, "openai", "backend should be openai");
    Assert.equal(opts.baseURL, ENDPOINT, "baseURL should come from pref");
    Assert.equal(
      opts.engineId,
      `${DEFAULT_ENGINE_ID}-${MODEL_FEATURES.CHAT}-${opts.modelId}`,
      "engineId should be derived from the feature name and model"
    );
    Assert.ok(opts.modelId, "modelId should be set");
    Assert.equal(opts.modelRevision, "main", "modelRevision should be main");
    Assert.equal(
      opts.taskName,
      "text-generation",
      "taskName should be text-generation"
    );
    Assert.equal(opts.serviceType, "ai", "serviceType should be ai");
    Assert.deepEqual(
      opts.extraHeaders,
      JSON.parse(EXTRA_HEADERS),
      "extraHeaders should come from pref"
    );
  } finally {
    sb.restore();
  }
});

/**
 * Tests that apiKey is passed when the custom model choice is active
 */
add_task(
  async function test_createOpenAIEngine_apiKey_when_custom_model_choice_active() {
    Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
    Services.prefs.setStringPref(PREF_CUSTOM_ENDPOINT, ENDPOINT);
    Services.prefs.setStringPref(PREF_MODEL, MODEL);

    const sb = sinon.createSandbox();
    try {
      const fakeEngine = { runWithGenerator() {} };
      const stub = sb.stub(openAIEngine, "_createEngine").resolves(fakeEngine);
      const { baseURL, apiKey } = openAIEngine.resolveEndpointConfig("0");
      await openAIEngine.build({
        model: MODEL,
        serviceType: SERVICE_TYPES.AI,
        purpose: PURPOSES.CHAT,
        flowId: null,
        feature: MODEL_FEATURES.CHAT,
        baseURL,
        apiKey,
      });

      const opts = stub.firstCall.args[0];
      Assert.equal(
        opts.apiKey,
        API_KEY,
        "apiKey should be returned when custom model choice is active"
      );
      Assert.equal(
        opts.baseURL,
        ENDPOINT,
        "baseURL should be the saved custom endpoint"
      );
    } finally {
      sb.restore();
    }
  }
);

/**
 * Tests that apiKey is blank when a preset model choice is set
 */
add_task(
  async function test_createOpenAIEngine_apiKey_blank_for_preset_choice() {
    Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
    Services.prefs.setStringPref(PREF_CUSTOM_ENDPOINT, ENDPOINT);
    Services.prefs.setStringPref(PREF_MODEL, MODEL);

    const sb = sinon.createSandbox();
    try {
      const fakeEngine = { runWithGenerator() {} };
      const stub = sb.stub(openAIEngine, "_createEngine").resolves(fakeEngine);
      const { baseURL, apiKey } = openAIEngine.resolveEndpointConfig("1");
      await openAIEngine.build({
        model: MODEL,
        serviceType: SERVICE_TYPES.AI,
        purpose: PURPOSES.CHAT,
        flowId: null,
        feature: MODEL_FEATURES.CHAT,
        baseURL,
        apiKey,
      });

      const opts = stub.firstCall.args[0];
      Assert.equal(
        opts.apiKey,
        "",
        "apiKey should be blank for a preset model choice"
      );
    } finally {
      sb.restore();
    }
  }
);

/**
 * Tests resolveEndpointConfig routing for preset and custom model choices
 */
add_task(async function test_resolveEndpointConfig() {
  Services.prefs.setStringPref(PREF_CUSTOM_ENDPOINT, ENDPOINT);
  Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
  // Set endpoint pref should not affect custom model routing
  Services.prefs.setStringPref(PREF_ENDPOINT, `${ENDPOINT}/preset`);

  const custom = openAIEngine.resolveEndpointConfig("0");
  Assert.equal(
    custom.baseURL,
    ENDPOINT,
    "custom choice uses the custom endpoint"
  );
  Assert.equal(custom.apiKey, API_KEY, "custom forwards the key");

  const preset = openAIEngine.resolveEndpointConfig("2");
  Assert.equal(
    preset.baseURL,
    `${ENDPOINT}/preset`,
    "preset uses the set endpoint pref"
  );
  Assert.equal(preset.apiKey, "", "preset do not use an API key");

  const none = openAIEngine.resolveEndpointConfig(undefined);
  Assert.equal(
    none.baseURL,
    `${ENDPOINT}/preset`,
    "no choice behaves as preset"
  );
  Assert.equal(none.apiKey, "", "no choice sends no key");

  Services.prefs.clearUserPref(PREF_CUSTOM_ENDPOINT);
  Services.prefs.clearUserPref(PREF_API_KEY);
  Services.prefs.clearUserPref(PREF_ENDPOINT);
});

/**
 * Tests hasCustomEndpoint reads the persistent custom endpoint
 */
add_task(async function test_hasCustomEndpoint() {
  Services.prefs.clearUserPref(PREF_CUSTOM_ENDPOINT);
  Assert.ok(
    !openAIEngine.hasCustomEndpoint(),
    "no custom model when custom endpoint is unset"
  );

  Services.prefs.setStringPref(PREF_CUSTOM_ENDPOINT, ENDPOINT);
  Assert.ok(
    openAIEngine.hasCustomEndpoint(),
    "custom model configured once custom endpoint has a value"
  );

  Services.prefs.clearUserPref(PREF_CUSTOM_ENDPOINT);
});

/**
 * Tests rendering a prompt from a file with placeholder string replacements
 */
add_task(async function test_renderPrompt() {
  // Render the test prompt with replacements
  const test_prompt = `
This is a test prompt.
{testToReplace1}

This is more content. {testToReplace2}

{testToReplace3} Here's the last line.`.trim();
  const promptContent = await renderPrompt(test_prompt, {
    testToReplace1: "replaced1",
    testToReplace2: "replaced2",
    testToReplace3: "replaced3",
  });

  Assert.equal(
    promptContent,
    "This is a test prompt.\nreplaced1\n\nThis is more content. replaced2\n\nreplaced3 Here's the last line.",
    "Should render the prompt correctly with provided replacement strings"
  );
});

add_task(function test_renderPrompt_optional_sections() {
  const prompt = "URL: {url}\n{#slug}Slug: {slug}{/slug}\nTitle: {title}";

  Assert.equal(
    renderPrompt(prompt, { url: "u", slug: "my-page", title: "t" }),
    "URL: u\nSlug: my-page\nTitle: t",
    "A section with a value keeps its content and line"
  );
  Assert.equal(
    renderPrompt(prompt, { url: "u", slug: "", title: "t" }),
    "URL: u\nTitle: t",
    "A standalone section with an empty value removes its whole line"
  );
  Assert.equal(
    renderPrompt(prompt, { url: "u", title: "t" }),
    "URL: u\nTitle: t",
    "A standalone section with a missing value removes its whole line"
  );
  Assert.equal(
    renderPrompt("Title: {title}{#slug} ({slug}){/slug}.", { title: "t" }),
    "Title: t.",
    "An inline section is removed without touching the rest of the line"
  );
  Assert.equal(
    renderPrompt("Title: {title}{#slug} ({slug}){/slug}.", {
      title: "t",
      slug: "s",
    }),
    "Title: t (s).",
    "An inline section with a value keeps its content"
  );
  Assert.equal(
    renderPrompt("A\n  {#slug}Slug: {slug}{/slug}\nB", { slug: "s" }),
    "A\n  Slug: s\nB",
    "A kept standalone section keeps its indentation"
  );
  Assert.equal(
    renderPrompt("A\n{#slug}Slug: {slug}{/slug}", {}),
    "A\n",
    "A standalone section on the last line is removed"
  );
  Assert.equal(
    renderPrompt('{ "path": "/x" } {#a}{a}{/b}', { a: "1" }),
    '{ "path": "/x" } {#a}1{/b}',
    "Braces that do not form a matched section are left alone"
  );
  Assert.equal(
    renderPrompt("{#slug}A{/slug} x\nB\n{#slug}C{/slug}\nD", {}),
    " x\nB\nD",
    "A section followed by text ends at its own closing tag, not a later one"
  );
  Assert.equal(
    renderPrompt("A\n{#slug}one\ntwo{/slug}\nB", {}),
    "A\nB",
    "A standalone section spanning several lines removes all of them"
  );
});

add_task(function test_is429Error() {
  Assert.equal(openAIEngine.is429Error(null), false, "null is not a 429 error");
  Assert.equal(
    openAIEngine.is429Error(undefined),
    false,
    "undefined is not a 429 error"
  );
  Assert.equal(
    openAIEngine.is429Error(new Error("boom")),
    false,
    "Plain error is not a 429 error"
  );

  // status field is the primary signal.
  const statusErr = new Error("Request failed");
  statusErr.status = 429;
  Assert.equal(
    openAIEngine.is429Error(statusErr),
    true,
    "status === 429 is detected"
  );

  // Any 429 sub-code matches — we don't differentiate.
  const budgetErr = new Error("Budget limit exceeded");
  budgetErr.status = 429;
  // MLPA spec code 1 = budgetExceeded
  budgetErr.error = 1;
  Assert.equal(
    openAIEngine.is429Error(budgetErr),
    true,
    "429 with budget code is detected"
  );

  const rateLimitErr = new Error("Rate limit exceeded");
  rateLimitErr.status = 429;
  // MLPA spec code 2 = rateLimitExceeded
  rateLimitErr.error = 2;
  Assert.equal(
    openAIEngine.is429Error(rateLimitErr),
    true,
    "429 with QPS rate-limit code is also detected"
  );

  // Fallback: message substring for cases where status isn't surfaced.
  Assert.equal(
    openAIEngine.is429Error(new Error("HTTP 429 status code returned")),
    true,
    "'429 status code' substring is detected as a fallback"
  );

  // 401 must not match.
  const authErr = new Error("401 status code");
  authErr.status = 401;
  Assert.equal(
    openAIEngine.is429Error(authErr),
    false,
    "401 auth errors must NOT match"
  );
});

add_task(function test_isRetryableError() {
  const withStatus = status => Object.assign(new Error("boom"), { status });

  Assert.equal(
    openAIEngine.isRetryableError(null),
    false,
    "null is not retryable"
  );

  // 429 (and its sub-codes) are retryable.
  Assert.equal(
    openAIEngine.isRetryableError(withStatus(429)),
    true,
    "429 is retryable"
  );

  // Timeout (408), conflict (409), and transient server errors (>= 500).
  for (const status of [408, 409, 500, 502, 503, 504, 529]) {
    Assert.equal(
      openAIEngine.isRetryableError(withStatus(status)),
      true,
      `${status} is retryable`
    );
  }

  // Status encoded only in the message text.
  Assert.equal(
    openAIEngine.isRetryableError(new Error("HTTP 503 status code returned")),
    true,
    "'503 status code' substring is retryable"
  );

  // Network-layer failures with no HTTP status.
  for (const message of [
    "NS_ERROR_NET_RESET",
    "NetworkError when attempting to fetch resource",
    "connection refused",
    "request timed out",
  ]) {
    Assert.equal(
      openAIEngine.isRetryableError(new Error(message)),
      true,
      `network error "${message}" is retryable`
    );
  }

  // Deterministic errors are NOT retryable.
  for (const status of [400, 401, 403, 404, 422]) {
    Assert.equal(
      openAIEngine.isRetryableError(withStatus(status)),
      false,
      `${status} is not retryable`
    );
  }
  Assert.equal(
    openAIEngine.isRetryableError(new Error("malformed json")),
    false,
    "Plain non-network error is not retryable"
  );
});

add_task(async function test_runWithGenerator_ends_on_abort_while_waiting() {
  const sb = sinon.createSandbox();
  try {
    const fakeEngine = {
      async *runWithGenerator() {
        yield { text: "first" };
        await new Promise(() => {});
      },
    };
    sb.stub(openAIEngine, "_createEngine").resolves(fakeEngine);
    const engine = await openAIEngine.build({
      model: MODEL,
      serviceType: SERVICE_TYPES.AI,
      purpose: PURPOSES.CHAT,
      flowId: null,
      feature: MODEL_FEATURES.CHAT,
    });

    const controller = new AbortController();
    const stream = engine.runWithGenerator({ signal: controller.signal });
    const first = await stream.next();
    Assert.equal(first.value.text, "first", "Should yield the first chunk");

    const pending = stream.next();
    controller.abort();
    const result = await pending;
    Assert.ok(
      result.done,
      "Aborting while waiting for a chunk should end the stream"
    );
  } finally {
    sb.restore();
  }
});
