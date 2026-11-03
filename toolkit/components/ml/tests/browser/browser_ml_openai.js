/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const BASE_ENGINE_OPTIONS = {
  featureId: "about-inference",
  taskName: "text-generation",
  modelId: "qwen3:0.6b",
  modelRevision: "main",
};

const TEST_INFERENCE_OPTIONS_RECORD_ID = "74a71cfd-1734-44e6-85c0-69cf3e874138";

/**
 * How long a streaming turn is allowed to take before the test fails it.
 * Far below the 120s engine timeout, far above the pipeline's own drain bound,
 * so a hang is caught quickly without racing a healthy run.
 *
 * @type {number}
 */
const STREAM_TURN_TIMEOUT_MS = 15000;

const SHARED_TOOLS = [
  {
    type: "function",
    function: {
      name: "search_open_tabs",
      description: "Search open tabs by type.",
      parameters: {
        type: "object",
        properties: { type: { type: "string" } },
        required: ["type"],
      },
    },
  },
];

async function setupOpenAICancellationTest(onRequest) {
  const { cleanup: cleanupEnvironment } = await setup({
    records: [
      {
        ...BASE_ENGINE_OPTIONS,
        id: TEST_INFERENCE_OPTIONS_RECORD_ID,
      },
    ],
  });
  const { server, port, releaseHeldStreams, resumeResponses } = startMockOpenAI(
    {
      holdResponses: true,
      onRequest,
    }
  );
  const baseURL = `http://localhost:${port}/v1`;
  const httpRequestStopped = Promise.withResolvers();
  const onRequestStopped = subject => {
    const channel = subject.QueryInterface(Ci.nsIHttpChannel);
    if (
      channel.URI.spec === `${baseURL}/chat/completions` &&
      channel.requestMethod === "POST"
    ) {
      httpRequestStopped.resolve(channel.status);
    }
  };
  Services.obs.addObserver(onRequestStopped, "http-on-stop-request");

  const cleanup = async () => {
    try {
      releaseHeldStreams();
      await EngineProcess.destroyMLEngine();
      await cleanupEnvironment();
      await stopMockOpenAI(server);
    } finally {
      Services.obs.removeObserver(onRequestStopped, "http-on-stop-request");
    }
  };

  try {
    const engine = await createEngine({
      ...BASE_ENGINE_OPTIONS,
      backend: "openai",
      apiKey: "test",
      baseURL,
    });
    return {
      engine,
      httpRequestStopped: httpRequestStopped.promise,
      resumeResponses,
      cleanup,
    };
  } catch (error) {
    await cleanup();
    throw error;
  }
}

add_task(async function test_openai_aborted_signal_prevents_http_request() {
  let requestCount = 0;
  const { engine, cleanup } = await setupOpenAICancellationTest(() => {
    requestCount++;
  });
  try {
    const request = { args: [{ role: "user", content: "Generate a page" }] };

    await Assert.rejects(
      engine.run(request, { signal: AbortSignal.abort() }),
      /AbortError/,
      "An already aborted signal rejects the model call with AbortError"
    );

    Assert.equal(requestCount, 0, "No HTTP request is sent");
  } finally {
    await cleanup();
  }
});

add_task(
  async function test_openai_signal_aborts_pending_non_streaming_request() {
    const requestStarted = Promise.withResolvers();
    let requestCount = 0;
    const { engine, httpRequestStopped, cleanup } =
      await setupOpenAICancellationTest(() => {
        requestCount++;
        requestStarted.resolve();
      });
    try {
      const controller = new AbortController();
      const request = { args: [{ role: "user", content: "Generate a page" }] };
      const abortAfterRequestStarts = requestStarted.promise.then(() =>
        controller.abort()
      );

      await Assert.rejects(
        engine.run(request, { signal: controller.signal }),
        /AbortError/,
        "Aborting the signal rejects the pending non-streaming request with AbortError"
      );
      await abortAfterRequestStarts;
      Assert.equal(
        await httpRequestStopped,
        Cr.NS_BINDING_ABORTED,
        "The HTTP request is aborted while the server keeps its response pending"
      );
      Assert.equal(requestCount, 1, "Cancellation does not retry the request");
    } finally {
      await cleanup();
    }
  }
);

add_task(async function test_openai_signal_aborts_pending_streaming_request() {
  const requestStarted = Promise.withResolvers();
  let requestCount = 0;
  const { engine, httpRequestStopped, cleanup } =
    await setupOpenAICancellationTest(() => {
      requestCount++;
      requestStarted.resolve();
    });
  try {
    const controller = new AbortController();
    const request = {
      args: [{ role: "user", content: "Generate a page" }],
      streamOptions: { enabled: true },
    };
    const abortAfterRequestStarts = requestStarted.promise.then(() =>
      controller.abort()
    );

    await Assert.rejects(
      engine.run(request, { signal: controller.signal }),
      /AbortError/,
      "Aborting the signal rejects the pending streaming request with AbortError"
    );
    await abortAfterRequestStarts;
    Assert.equal(
      await httpRequestStopped,
      Cr.NS_BINDING_ABORTED,
      "The HTTP request is aborted while the server keeps its response pending"
    );
    Assert.equal(requestCount, 1, "Cancellation does not retry the request");
  } finally {
    await cleanup();
  }
});

add_task(async function test_openai_request_succeeds_after_cancellation() {
  const requestStarted = Promise.withResolvers();
  let requestCount = 0;
  const { engine, httpRequestStopped, resumeResponses, cleanup } =
    await setupOpenAICancellationTest(() => {
      requestCount++;
      requestStarted.resolve();
    });
  try {
    const controller = new AbortController();
    const request = { args: [{ role: "user", content: "Generate a page" }] };
    const abortAfterRequestStarts = requestStarted.promise.then(() =>
      controller.abort()
    );
    await Assert.rejects(
      engine.run(request, { signal: controller.signal }),
      /AbortError/,
      "Aborting the signal rejects the first request with AbortError"
    );
    await abortAfterRequestStarts;
    Assert.equal(
      await httpRequestStopped,
      Cr.NS_BINDING_ABORTED,
      "The HTTP request is aborted while the server keeps its response pending"
    );
    resumeResponses();

    const result = await engine.run(request);

    Assert.equal(
      result.finalOutput,
      "This is a mock summary for testing end-to-end flow.",
      "The engine completes a new request after the previous request is canceled"
    );
    Assert.equal(
      requestCount,
      2,
      "Only the canceled request and the subsequent request are sent"
    );
  } finally {
    await cleanup();
  }
});

/**
 * Test that createEngine successfully talks to the OpenAI client.
 */
add_task(async function test_openai_client() {
  const records = [
    {
      featureId: "about-inference",
      taskName: "text-generation",
      modelId: "qwen3:0.6b",
      modelRevision: "main",
      id: TEST_INFERENCE_OPTIONS_RECORD_ID,
    },
  ];
  const { cleanup } = await setup({ records });
  const { server: mockServer, port } = startMockOpenAI({
    echo: "This gets echoed.",
  });

  const engineInstance = await createEngine({
    featureId: "about-inference",
    task: "text-generation",
    modelId: "qwen3:0.6b",
    modelRevision: "main",
    apiKey: "ollama",
    baseURL: `http://localhost:${port}/v1`,
    backend: "openai",
  });

  const request = {
    args: [
      {
        role: "system",
        content:
          "You are a helpful assistant that summarizes text clearly and concisely.",
      },
      {
        role: "user",
        content: `Please summarize the following text:\n\n blah bla`,
      },
    ],
  };

  try {
    info("Run the inference");
    const inferencePromise = engineInstance.run(request);

    const result = await inferencePromise;

    Assert.equal(
      result.finalOutput,
      "This is a mock summary for testing end-to-end flow."
    );
  } finally {
    await EngineProcess.destroyMLEngine();
    await cleanup();
    await stopMockOpenAI(mockServer);
  }
});

add_task(async function test_openai_client_tools_non_streaming() {
  const records = [
    {
      ...BASE_ENGINE_OPTIONS,
      id: TEST_INFERENCE_OPTIONS_RECORD_ID,
    },
  ];
  const { cleanup } = await setup({ records });
  const { server: mockServer, port } = startMockOpenAI();

  const engineInstance = await createEngine({
    ...BASE_ENGINE_OPTIONS,
    apiKey: "ollama",
    baseURL: `http://localhost:${port}/v1`,
    backend: "openai",
  });

  // First request: ask with tools; server responds with tool_calls
  const requestWithTools = {
    args: [
      { role: "system", content: "You are a helpful assistant." },
      { role: "user", content: "Find my open news tabs." },
    ],
    tools: SHARED_TOOLS,
  };

  try {
    info("Run request that triggers tool calls");
    const result1 = await engineInstance.run(requestWithTools);

    // The pipeline should surface toolCalls from the OpenAI message
    Assert.ok(result1.toolCalls, "toolCalls should exist on the result");
    Assert.equal(result1.toolCalls.length, 1, "Exactly one tool call");
    Assert.equal(
      result1.toolCalls[0].function.name,
      "search_open_tabs",
      "Tool name should match"
    );

    // Second request: append assistant tool_calls + our tool result
    const assistantToolCallsMsg = {
      role: "assistant",
      tool_calls: result1.toolCalls.map(tc => ({
        id: tc.id,
        type: "function",
        function: {
          name: tc.function.name,
          arguments: tc.function.arguments,
        },
      })),
    };

    const toolResultMsg = {
      role: "tool",
      tool_call_id: result1.toolCalls[0].id,
      content: JSON.stringify({ query: "news", allTabs: [] }),
    };

    const followup = await engineInstance.run({
      args: [...requestWithTools.args, assistantToolCallsMsg, toolResultMsg],
      tools: requestWithTools.tools, // still valid to include
    });

    Assert.equal(
      followup.finalOutput,
      "Here are the tabs I found for you.",
      "Should get assistant follow-up after tool result"
    );
  } finally {
    await EngineProcess.destroyMLEngine();
    await cleanup();
    await stopMockOpenAI(mockServer);
  }
});

add_task(async function test_openai_client_tools_streaming() {
  const records = [
    {
      ...BASE_ENGINE_OPTIONS,
      id: "b3b2b661-daa6-4b7f-8d3c-7db0df0dbeef",
    },
  ];
  const { cleanup } = await setup({ records });
  const { server: mockServer, port } = startMockOpenAI();

  const engineInstance = await createEngine({
    ...BASE_ENGINE_OPTIONS,
    apiKey: "ollama",
    baseURL: `http://localhost:${port}/v1`,
    backend: "openai",
  });

  const starter = {
    args: [
      { role: "system", content: "You are a helpful assistant." },
      { role: "user", content: "Find my open news tabs." },
    ],
    tools: SHARED_TOOLS,
    streamOptions: { enabled: true },
  };

  try {
    // --- First turn: expect tool_calls via streaming ---
    const gen = engineInstance.runWithGenerator(starter);

    let toolCalls = null;
    for await (const chunk of gen) {
      // Your MLEngineParent + OpenAIPipeline put toolCalls onto the yielded chunk
      if (chunk.toolCalls && chunk.toolCalls.length) {
        toolCalls = chunk.toolCalls;
        break; // we end the turn when model asks for tools
      }
      // (Optional) you could accumulate chunk.text here; expected empty in this turn
    }

    Assert.ok(toolCalls, "Should receive toolCalls via streaming");
    Assert.equal(toolCalls.length, 1, "One tool call");
    Assert.equal(
      toolCalls[0].function.name,
      "search_open_tabs",
      "Tool name should match"
    );

    // --- Second turn: send tool result, stream final answer ---
    const assistantToolCallsMsg = {
      role: "assistant",
      tool_calls: toolCalls.map(tc => ({
        id: tc.id,
        type: "function",
        function: {
          name: tc.function.name,
          arguments: tc.function.arguments,
        },
      })),
    };

    const toolResultMsg = {
      role: "tool",
      tool_call_id: toolCalls[0].id,
      content: JSON.stringify({ query: "news", allTabs: [] }),
    };

    const gen2 = engineInstance.runWithGenerator({
      args: [...starter.args, assistantToolCallsMsg, toolResultMsg],
      tools: SHARED_TOOLS,
      streamOptions: { enabled: true },
    });

    let final = "";
    for await (const chunk of gen2) {
      if (chunk.text) {
        final += chunk.text;
      }
    }

    Assert.ok(final.length, "Should stream some final content");
    Assert.equal(
      final,
      "Here are the tabs I found for you.",
      "Should stream the expected assistant follow-up"
    );
  } finally {
    await EngineProcess.destroyMLEngine();
    await cleanup();
    await stopMockOpenAI(mockServer);
  }
});

/**
 * Consumes a generator to completion, failing with a legible message instead of
 * hanging until the harness or the 120s engine timeout kills the run.
 *
 * @param {AsyncGenerator} generator - The generator to drain.
 * @param {number} timeoutMs - How long the turn is allowed to take.
 * @returns {Promise<object[]>} Every chunk yielded before the turn ended.
 */
async function collectChunksBounded(generator, timeoutMs) {
  const TIMED_OUT = Symbol("timed-out");
  const chunks = [];
  let timeoutId;

  const collected = (async () => {
    for await (const chunk of generator) {
      chunks.push(chunk);
    }
  })();

  const outcome = await Promise.race([
    collected,
    new Promise(resolve => {
      // The failure bound itself, not a sleep.
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      timeoutId = setTimeout(() => resolve(TIMED_OUT), timeoutMs);
    }),
  ]);
  clearTimeout(timeoutId);

  Assert.notStrictEqual(
    outcome,
    TIMED_OUT,
    `The turn ends on its own within ${timeoutMs}ms, so a stalled stream ` +
      `cannot hold the engine open`
  );

  return chunks;
}

/**
 * A tool_calls turn has to be read to the end of the stream: the usage chunk
 * arrives after finish_reason, so ending the turn at finish_reason makes the
 * whole tool round invisible to telemetry.
 *
 * Uses startMockOpenAI rather than MockEngineManager because the subject is the
 * wire protocol itself: only the SSE bytes are mocked.
 */
add_task(async function test_openai_tool_call_turn_reports_usage() {
  const records = [
    {
      ...BASE_ENGINE_OPTIONS,
      id: "5c1f0e64-3a7d-4b28-9f51-2ad0c6e4b7aa",
    },
  ];
  const { cleanup } = await setup({ records });
  const { server: mockServer, port } = startMockOpenAI();

  const engineInstance = await createEngine({
    ...BASE_ENGINE_OPTIONS,
    apiKey: "ollama",
    baseURL: `http://localhost:${port}/v1`,
    backend: "openai",
  });

  try {
    const chunks = await collectChunksBounded(
      engineInstance.runWithGenerator({
        args: [
          { role: "system", content: "You are a helpful assistant." },
          { role: "user", content: "Find my open news tabs." },
        ],
        tools: SHARED_TOOLS,
        streamOptions: { enabled: true },
      }),
      STREAM_TURN_TIMEOUT_MS
    );

    const toolCalls = chunks.findLast(
      chunk => chunk.toolCalls?.length
    )?.toolCalls;
    Assert.ok(toolCalls, "The tool calls still reach the caller");
    Assert.equal(
      toolCalls[0].function.name,
      "search_open_tabs",
      "The tool the model asked for is the one the caller is told about"
    );

    const usage = chunks.findLast(chunk => chunk.usage)?.usage;
    Assert.ok(
      usage,
      "Usage sent after finish_reason reaches the caller, so a tool round is " +
        "measurable rather than invisible"
    );
    Assert.equal(
      usage.prompt_tokens,
      9839,
      "The prompt token count survives the trip from the wire to the caller"
    );
    Assert.equal(
      usage.prompt_tokens_details.cached_tokens,
      9800,
      "The cached token count survives, which is what the prompt cache is " +
        "judged on"
    );
  } finally {
    await EngineProcess.destroyMLEngine();
    await cleanup();
    await stopMockOpenAI(mockServer);
  }
});

/**
 * Reading past finish_reason must not mean reading forever. A server that stops
 * talking after finish_reason and never closes the connection has to be bounded
 * by the client, otherwise the turn hangs until the engine timeout and the tool
 * calls the model already asked for are never acted on.
 *
 * Uses startMockOpenAI for the same reason as the test above.
 */
add_task(async function test_openai_tool_call_drain_is_bounded() {
  const records = [
    {
      ...BASE_ENGINE_OPTIONS,
      id: "9d4a7b12-6c38-4e5f-b071-8fa2c3e5d604",
    },
  ];
  // The harness default idle timeout is 1s, shorter than the drain itself, so
  // the engine would be reaped mid-drain. Use the production value instead.
  const { cleanup } = await setup({
    records,
    prefs: [["browser.ml.modelCacheTimeout", 120000]],
  });
  const {
    server: mockServer,
    port,
    releaseHeldStreams,
  } = startMockOpenAI({ holdStreamOpenAfterFinish: true });

  const engineInstance = await createEngine({
    ...BASE_ENGINE_OPTIONS,
    apiKey: "ollama",
    baseURL: `http://localhost:${port}/v1`,
    backend: "openai",
  });

  try {
    const chunks = await collectChunksBounded(
      engineInstance.runWithGenerator({
        args: [
          { role: "system", content: "You are a helpful assistant." },
          { role: "user", content: "Find my open news tabs." },
        ],
        tools: SHARED_TOOLS,
        streamOptions: { enabled: true },
      }),
      STREAM_TURN_TIMEOUT_MS
    );

    const toolCalls = chunks.findLast(
      chunk => chunk.toolCalls?.length
    )?.toolCalls;
    Assert.ok(
      toolCalls,
      "The tool calls reach the caller even though the server never closed " +
        "the stream"
    );
    Assert.equal(
      toolCalls[0].function.name,
      "search_open_tabs",
      "Giving up on the drain does not corrupt the tool call that was already " +
        "complete"
    );
    Assert.equal(
      toolCalls[0].function.arguments,
      '{ "type": "news" }',
      "The arguments accumulated across partial chunks are still intact"
    );
  } finally {
    releaseHeldStreams();
    await EngineProcess.destroyMLEngine();
    await cleanup();
    await stopMockOpenAI(mockServer);
  }
});

add_task(async function test_openai_abandoned_generator_rejection_is_handled() {
  const records = [
    { ...BASE_ENGINE_OPTIONS, id: "d4f8c1a9-2e7b-4c60-9a3d-8b1e5f0c2d47" },
  ];
  const { cleanup } = await setup({ records });
  const {
    server: mockServer,
    port,
    releaseHeldStreams,
  } = startMockOpenAI({ holdStreamOpenAfterFinish: true });

  const engineInstance = await createEngine({
    ...BASE_ENGINE_OPTIONS,
    apiKey: "ollama",
    baseURL: `http://localhost:${port}/v1`,
    backend: "openai",
  });

  try {
    const generator = engineInstance.runWithGenerator({
      args: [
        { role: "system", content: "You are a helpful assistant." },
        { role: "user", content: "Find my open news tabs." },
      ],
      tools: SHARED_TOOLS,
      streamOptions: { enabled: true },
    });

    // Read one chunk, then abandon the generator by returning it early. This
    // unwinds it past `await completionPromise`, like a consumer walking away
    // mid-run (a `for await` that breaks does the same thing under the hood).
    await generator.next();
    await generator.return();

    // The run is still pending (the mock holds the stream open). Tearing the
    // engine down rejects it. With the completionPromise handler that rejection
    // is swallowed; without it, it's an uncaught rejection the harness fails on.
    await EngineProcess.destroyMLEngine();

    // Let the rejection settle and be observed before the test ends.
    await new Promise(executeSoon);
    await new Promise(executeSoon);

    Assert.ok(
      true,
      "Abandoning runWithGenerator and destroying the engine raises no uncaught rejection"
    );
  } finally {
    releaseHeldStreams();
    await cleanup();
    await stopMockOpenAI(mockServer);
  }
});

/**
 * Test that token is properly passed to the OpenAI client
 */
add_task(async function test_openai_fxaccount_token() {
  const records = [
    {
      ...BASE_ENGINE_OPTIONS,
      id: TEST_INFERENCE_OPTIONS_RECORD_ID,
    },
  ];
  const { cleanup } = await setup({ records });

  // Mock server that checks for fxAccount token in headers
  let capturedFxaHeader = null;
  let capturedServiceTypeHeader = null;
  let capturedChatIdHeader = null;
  let capturedUserAgentHeader = null;
  const { server: mockServer, port } = startMockOpenAI({
    echo: "Response with FxA token",
    onRequest: req => {
      try {
        if (req.hasHeader("authorization")) {
          capturedFxaHeader = req.getHeader("authorization");
        }
        if (req.hasHeader("service-type")) {
          capturedServiceTypeHeader = req.getHeader("service-type");
        }
        if (req.hasHeader("chat-id")) {
          capturedChatIdHeader = req.getHeader("chat-id");
        }
        if (req.hasHeader("user-agent")) {
          capturedUserAgentHeader = req.getHeader("user-agent");
        }
      } catch (e) {
        info("Failed to get header: " + e);
      }
    },
  });

  const token = "test_fxa_token_12345";
  const apiKey = "test-api-key";
  const chatId = "test-chat-id-12345";

  const engineInstance = await createEngine({
    ...BASE_ENGINE_OPTIONS,
    apiKey,
    baseURL: `http://localhost:${port}/v1`,
    backend: "openai",
  });

  const request = {
    args: [
      {
        role: "user",
        content: "Test request with FxA token",
      },
    ],
    token,
    chatId,
  };

  try {
    info("Run the inference with token");
    const result = await engineInstance.run(request);

    Assert.equal(
      result.finalOutput,
      "This is a mock summary for testing end-to-end flow.",
      "Should get expected response"
    );

    // Verify that the FxA token was sent in the request headers
    const expectedValue = `Bearer ${apiKey}`;

    Assert.equal(
      capturedFxaHeader,
      expectedValue,
      `FxA token should be included in request headers. Expected: ${expectedValue}, Got: ${capturedFxaHeader}`
    );

    Assert.equal(
      capturedServiceTypeHeader,
      "ai",
      "service-type header should be 'ai' when FxA token is provided"
    );

    Assert.equal(
      capturedChatIdHeader,
      chatId,
      `chat-id header should be included in request headers. Expected: ${chatId}, Got: ${capturedChatIdHeader}`
    );

    Assert.equal(
      capturedUserAgentHeader,
      navigator.userAgent,
      "User-Agent header should be the browser's user agent"
    );

    info("Test without token - should not include header");

    // Create another engine without token
    const engineWithoutToken = await createEngine({
      ...BASE_ENGINE_OPTIONS,
      apiKey: "test-api-key",
      baseURL: `http://localhost:${port}/v1`,
      backend: "openai",
      // No token
    });

    capturedFxaHeader = null;
    capturedServiceTypeHeader = null;
    const requestWithoutToken = {
      args: [
        {
          role: "user",
          content: "Test request with FxA token",
        },
      ],
      // No token
    };
    await engineWithoutToken.run(requestWithoutToken);

    // Verify Authorization header is the API key when no FxA token is provided
    Assert.equal(
      capturedFxaHeader,
      "Bearer test-api-key",
      "Authorization should fall back to API key when no FxA token is provided"
    );

    Assert.equal(
      capturedServiceTypeHeader,
      "ai",
      "service-type header should be present"
    );
  } finally {
    await EngineProcess.destroyMLEngine();
    await cleanup();
    await stopMockOpenAI(mockServer);
  }
});

/**
 * A service that rejects a request reports why through the HTTP status and a
 * body error code. Both have to survive the trip from the inference process,
 * where structured clone keeps only the Error's name and message.
 */
add_task(async function test_openai_error_keeps_status_and_code() {
  const records = [
    {
      ...BASE_ENGINE_OPTIONS,
      id: "74a71cfd-1734-44e6-85c0-69cf3e874138",
    },
  ];
  const { cleanup } = await setup({ records });
  const { server: mockServer, port } = startMockOpenAI({
    errorResponse: { status: 429, body: { error: 1 } },
  });

  const engineInstance = await createEngine({
    ...BASE_ENGINE_OPTIONS,
    apiKey: "ollama",
    baseURL: `http://localhost:${port}/v1`,
    backend: "openai",
    maxRetries: 0,
  });

  try {
    let error;
    try {
      await engineInstance.run({
        args: [{ role: "user", content: "Hello" }],
      });
    } catch (e) {
      error = e;
    }
    Assert.ok(
      error?.message.includes("429"),
      "The run rejects with the backend's error"
    );
    Assert.equal(error.status, 429, "The HTTP status is kept on the error");
    Assert.equal(error.error, 1, "The body error code is kept on the error");
  } finally {
    await EngineProcess.destroyMLEngine();
    await cleanup();
    await stopMockOpenAI(mockServer);
  }
});
