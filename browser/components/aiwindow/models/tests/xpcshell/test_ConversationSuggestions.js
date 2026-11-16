/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

do_get_profile();

const {
  NewTabStarterGenerator,
  trimConversation,
  addMemoriesToPrompt,
  cleanInferenceOutput,
  generateConversationStartersSidebar,
  generateFollowupPrompts,
  MemoriesGetterForSuggestionPrompts,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/ConversationSuggestions.sys.mjs"
);

const { sanitizeUntrustedContent } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs"
);

const { AuthProvider } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs"
);

const {
  openAIEngine,
  MODEL_FEATURES,
  _setRemoteClientForTesting,
  _clearRemoteClientForTesting,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

registerCleanupFunction(() => _clearRemoteClientForTesting());
const { MemoriesManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs"
);
const { MESSAGE_ROLE } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AIWindowConstants.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

// Fake RS records for all features used by ConversationSuggestions
const FAKE_RECORDS = [
  {
    feature: MODEL_FEATURES.CONVERSATION_SUGGESTIONS_SIDEBAR_STARTER,
    version: "3.0",
    model: "test-model",
    is_default: true,
    parameters: {},
    service_type: "convo-starters",
    purpose: "convo-starters-sidebar",
    prompts:
      "Suggest {n} starters.\nCurrent tab:\n{current_tab}\nOpen tabs:\n{open_tabs}\nDate: {date}\nLocale: {locale}\n{assistant_limitations}",
  },
  {
    feature: MODEL_FEATURES.CONVERSATION_STARTERS_SIDEBAR_SYSTEM,
    version: "1.0",
    model: "test-model",
    is_default: true,
    parameters: {},
    service_type: "convo-starters",
    purpose: "convo-starters-sidebar",
    prompts: "You are a helpful assistant.",
  },
  {
    feature: MODEL_FEATURES.CONVERSATION_SUGGESTIONS_ASSISTANT_LIMITATIONS,
    version: "1.0",
    model: "test-model",
    is_default: true,
    parameters: {},
    service_type: "convo-starters",
    purpose: "convo-starters-sidebar",
    prompts: "limitations",
  },
  {
    feature: MODEL_FEATURES.CONVERSATION_SUGGESTIONS_FOLLOWUP,
    version: "1.0",
    model: "test-model",
    is_default: true,
    parameters: {},
    service_type: "ai",
    purpose: "convo-starters-sidebar",
    prompts:
      "Suggest {n} followups.\nCurrent tab:\n{current_tab}\nConversation: {conversation}\nDate: {date}\n{assistant_limitations}",
  },
  {
    feature: MODEL_FEATURES.CONVERSATION_SUGGESTIONS_MEMORIES,
    version: "1.0",
    model: "test-model",
    is_default: true,
    parameters: {},
    service_type: "convo-starters",
    purpose: "convo-starters-sidebar",
    prompts: "User memories:\n{memories}",
  },
];

/**
 * Creates a sandbox with stubs for openAIEngine methods used by buildConversation/loadPrompt.
 * Returns { sb, fakeEngineInstance } — caller must call sb.restore() in finally.
 *
 * @param {object} runResult - Resolved value for fakeEngineInstance.run.
 * @param {sinon.SinonSandbox} [existingSb] - Reuse an existing sandbox instead of creating one.
 * @returns {{ sb: sinon.SinonSandbox, fakeEngineInstance: object }}
 */
function setupStubs(runResult, existingSb) {
  const sb = existingSb ?? sinon.createSandbox();
  const fakeEngineInstance = { run: sb.stub().resolves(runResult) };

  _setRemoteClientForTesting({
    get: sb.stub().resolves(FAKE_RECORDS),
  });
  sb.stub(openAIEngine, "build").resolves(fakeEngineInstance);
  sb.stub(AuthProvider, "getToken").resolves(null);

  return { sb, fakeEngineInstance };
}

/**
 * Constants for preference keys and test values
 */
const PREF_API_KEY = "browser.smartwindow.apiKey";
const PREF_ENDPOINT = "browser.smartwindow.endpoint";
const PREF_MODEL = "browser.smartwindow.model";
const PREF_HISTORY_ENABLED = "places.history.enabled";
const PREF_PRIVATE_BROWSING = "browser.privatebrowsing.autostart";

const API_KEY = "test-api-key";
const ENDPOINT = "https://api.test-endpoint.com/v1";
const MODEL = "test-model";

/**
 * Cleans up preferences after testing
 */
registerCleanupFunction(() => {
  for (let pref of [
    PREF_API_KEY,
    PREF_ENDPOINT,
    PREF_MODEL,
    PREF_HISTORY_ENABLED,
    PREF_PRIVATE_BROWSING,
  ]) {
    if (Services.prefs.prefHasUserValue(pref)) {
      Services.prefs.clearUserPref(pref);
    }
  }
});

/**
 * Tests for trimConversation function
 */
add_task(async function test_trimConversation() {
  const cases = [
    {
      input: [
        // empty case
      ],
      expected: [],
    },
    {
      input: [
        // more than 15 messages
        ...Array.from({ length: 20 }, (_, i) => ({
          role: i % 2 === 0 ? MESSAGE_ROLE.USER : MESSAGE_ROLE.ASSISTANT,
          content: `Message ${i + 1}`,
        })),
      ],
      expected: [
        ...Array.from({ length: 15 }, (_, i) => ({
          role: (i + 5) % 2 === 0 ? "user" : "assistant",
          content: `Message ${i + 6}`,
        })),
      ],
    },
    {
      input: [
        // should remove tool call/responses
        { role: MESSAGE_ROLE.USER, content: "What's the weather like?" },
        {
          role: MESSAGE_ROLE.ASSISTANT,
          content: "",
          tool_call: { name: "get_weather", arguments: "{}" },
        },
        {
          role: MESSAGE_ROLE.TOOL,
          content: "Here are the latest news articles.",
        },
      ],
      expected: [{ role: "user", content: "What's the weather like?" }],
    },
    {
      input: [
        // should remove system message
        { role: MESSAGE_ROLE.SYSTEM, content: "System message" },
        { role: MESSAGE_ROLE.USER, content: "Hello" },
        { role: MESSAGE_ROLE.ASSISTANT, content: "Hi there!" },
      ],
      expected: [
        { role: "user", content: "Hello" },
        { role: "assistant", content: "Hi there!" },
      ],
    },
    {
      input: [
        // should remove messages with empty content
        { role: MESSAGE_ROLE.USER, content: "\n" },
        { role: MESSAGE_ROLE.ASSISTANT, content: "   " },
      ],
      expected: [],
    },
    {
      input: [
        // no valid messages
        { role: MESSAGE_ROLE.SYSTEM, content: "System message" },
        {
          role: MESSAGE_ROLE.ASSISTANT,
          content: "",
          tool_call: { name: "get_info", arguments: "{}" },
        },
      ],
      expected: [],
    },
    {
      input: [
        // should slice after filtering invalid messages
        ...Array(10)
          .fill(0)
          .flatMap((_, i) => [
            {
              role: MESSAGE_ROLE.USER,
              content: `User message ${i + 1}`,
            },
            { role: MESSAGE_ROLE.SYSTEM, content: "System message" },
          ]),
      ],
      expected: [
        ...Array.from({ length: 10 }, (_, i) => ({
          role: "user",
          content: `User message ${i + 1}`,
        })),
      ],
    },
  ];

  for (const { input, expected } of cases) {
    const result = trimConversation(input);
    Assert.deepEqual(
      result,
      expected,
      "trimConversation should return only user/assistant messages with content"
    );
  }
});

/**
 * Test for addMemoriesToPrompt function when there are memories
 */
add_task(async function test_addMemoriesToPrompt_have_memories() {
  const sb = sinon.createSandbox();
  try {
    const basePrompt = "Base prompt content.";
    const fakeMemories = ["Memory summary 1", "Memory summary 2"];
    const memoriesStub = sb
      .stub(MemoriesGetterForSuggestionPrompts, "getMemorySummariesForPrompt")
      .resolves(fakeMemories);
    const conversationMemoriesPrompt = "Memories block:\n{memories}";
    const promptWithMemories = await addMemoriesToPrompt(
      basePrompt,
      conversationMemoriesPrompt
    );

    Assert.ok(
      memoriesStub.calledOnce,
      "getMemorySummariesForPrompt should be called"
    );
    Assert.ok(
      promptWithMemories.includes("- Memory summary 1") &&
        promptWithMemories.includes("- Memory summary 2"),
      "Prompt should include memories"
    );
  } finally {
    sb.restore();
  }
});

/**
 * Test for addMemoriesToPrompt function when there are no memories
 */
add_task(async function test_addMemoriesToPrompt_dont_have_memories() {
  const sb = sinon.createSandbox();
  try {
    const basePrompt = "Base prompt content.";
    const fakeMemories = [];
    const memoriesStub = sb
      .stub(MemoriesGetterForSuggestionPrompts, "getMemorySummariesForPrompt")
      .resolves(fakeMemories);
    const conversationMemoriesPrompt = "Memories block:\n{memories}";
    const promptWithMemories = await addMemoriesToPrompt(
      basePrompt,
      conversationMemoriesPrompt
    );

    Assert.ok(
      memoriesStub.calledOnce,
      "getMemorySummariesForPrompt should be called"
    );
    Assert.equal(
      promptWithMemories,
      basePrompt,
      "Prompt should be unchanged when no memories"
    );
  } finally {
    sb.restore();
  }
});

/**
 * Tests for cleanInferenceOutput function
 */
add_task(function test_cleanInferenceOutput() {
  const sb = sinon.createSandbox();
  try {
    const cases = [
      {
        input: {
          finalOutput: `- Suggestion 1\n\n* Suggestion 2\n1. Suggestion 3.\n2)Suggestion 4\n[5] Suggestion 5\n6.\nLabel: Suggestion 6\nSuggestion 7.\nSuggestion 8?`,
        },
        expected: [
          "Suggestion 1",
          "Suggestion 2",
          "Suggestion 3",
          "Suggestion 4",
          "Suggestion 5",
          "Suggestion 6",
          "Suggestion 7",
          "Suggestion 8?",
        ],
      },
      {
        input: { finalOutput: `Suggestion X\nSuggestion Y\nSuggestion Z` },
        expected: ["Suggestion X", "Suggestion Y", "Suggestion Z"],
      },
      {
        input: { finalOutput: "" },
        expected: [],
      },
    ];

    for (const { input, expected } of cases) {
      const result = cleanInferenceOutput(input);
      Assert.deepEqual(
        result,
        expected,
        "cleanInferenceOutput should return expected output"
      );
    }
  } finally {
    sb.restore();
  }
});

/**
 * Tests for createNewTabPromptGenerator generating prompts based on tab count
 */
add_task(
  async function test_createNewTabPromptGenerator_with_history_enabled() {
    const sb = sinon.createSandbox();
    const writingPrompts = [
      "aiwindow-starter-writing-first-draft",
      "aiwindow-starter-writing-improve",
      "aiwindow-starter-writing-proofread",
    ];

    const planningPrompts = [
      "aiwindow-starter-planning-simplify",
      "aiwindow-starter-planning-brainstorm",
      "aiwindow-starter-planning-plan",
    ];
    try {
      const cases = [
        {
          input: { tabCount: -1 },
          expectedBrowsing: [],
        },
        {
          input: { tabCount: 0 },
          expectedBrowsing: ["aiwindow-starter-browsing-history"],
        },
        {
          input: { tabCount: 1 },
          expectedBrowsing: [
            "aiwindow-starter-browsing-history",
            "aiwindow-starter-browsing-summarize",
          ],
        },
        {
          input: { tabCount: 2 },
          expectedBrowsing: [
            "aiwindow-starter-browsing-history",
            "aiwindow-starter-browsing-summarize",
            "aiwindow-starter-browsing-compare",
          ],
        },
        {
          input: { tabCount: 3 },
          expectedBrowsing: [
            "aiwindow-starter-browsing-history",
            "aiwindow-starter-browsing-summarize",
            "aiwindow-starter-browsing-compare",
          ],
        },
      ];
      const promptGenerator = NewTabStarterGenerator;
      for (const { input, expectedBrowsing } of cases) {
        const results = await promptGenerator.getPrompts(input.tabCount);
        if (input.tabCount <= -1) {
          Assert.equal(results.length, 2, "Should return 2 suggestions");
        } else {
          Assert.equal(results.length, 3, "Should return 3 suggestions");
        }
        for (const result of results) {
          Assert.equal(
            result.type,
            "chat",
            "Each result should have type 'chat'"
          );
        }
        Assert.ok(
          writingPrompts.includes(results[0].l10nId),
          "Results should include a valid writing prompt id"
        );
        Assert.ok(
          planningPrompts.includes(results[1].l10nId),
          "Results should include a valid planning prompt id"
        );
        if (results[2]) {
          Assert.ok(
            expectedBrowsing.includes(results[2].l10nId),
            "Results should include a valid browsing prompt id"
          );
        }
      }
    } finally {
      sb.restore();
    }
  }
);

/**
 * Tests for createNewTabPromptGenerator generating prompts based on tab count, with history disabled
 */
add_task(
  async function test_createNewTabPromptGenerator_with_history_disabled() {
    const sb = sinon.createSandbox();
    const writingPrompts = [
      "aiwindow-starter-writing-first-draft",
      "aiwindow-starter-writing-improve",
      "aiwindow-starter-writing-proofread",
    ];

    const planningPrompts = [
      "aiwindow-starter-planning-simplify",
      "aiwindow-starter-planning-brainstorm",
      "aiwindow-starter-planning-plan",
    ];
    try {
      const cases = [
        {
          input: { tabCount: -1 },
          expectedBrowsing: [],
        },
        {
          input: { tabCount: 0 },
          expectedBrowsing: [],
        },
        {
          input: { tabCount: 1 },
          expectedBrowsing: ["aiwindow-starter-browsing-summarize"],
        },
        {
          input: { tabCount: 2 },
          expectedBrowsing: [
            "aiwindow-starter-browsing-summarize",
            "aiwindow-starter-browsing-compare",
          ],
        },
        {
          input: { tabCount: 3 },
          expectedBrowsing: [
            "aiwindow-starter-browsing-summarize",
            "aiwindow-starter-browsing-compare",
          ],
        },
      ];
      for (const pref of [
        [{ key: PREF_HISTORY_ENABLED, value: false }],
        [{ key: PREF_PRIVATE_BROWSING, value: true }],
        [
          { key: PREF_HISTORY_ENABLED, value: false },
          { key: PREF_PRIVATE_BROWSING, value: true },
        ],
      ]) {
        for (const p of pref) {
          Services.prefs.setBoolPref(p.key, p.value);
        }
        const promptGenerator = NewTabStarterGenerator;
        for (const { input, expectedBrowsing } of cases) {
          const results = await promptGenerator.getPrompts(input.tabCount);
          if (input.tabCount <= 0) {
            Assert.equal(results.length, 2, "Should return 2 suggestions");
          } else {
            Assert.equal(results.length, 3, "Should return 3 suggestions");
          }
          for (const result of results) {
            Assert.equal(
              result.type,
              "chat",
              "Each result should have type 'chat'"
            );
          }
          Assert.ok(
            writingPrompts.includes(results[0].l10nId),
            "Results should include a valid writing prompt id"
          );
          Assert.ok(
            planningPrompts.includes(results[1].l10nId),
            "Results should include a valid planning prompt id"
          );
          if (results[2]) {
            Assert.ok(
              expectedBrowsing.includes(results[2].l10nId),
              "Results should include a valid browsing prompt id"
            );
          }
        }
        for (const p of pref) {
          Services.prefs.clearUserPref(p.key);
        }
      }
    } finally {
      sb.restore();
    }
  }
);

/**
 * Tests for generateConversationStartersSidebar successfully generating suggestions
 */
add_task(async function test_generateConversationStartersSidebar_happy_path() {
  Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
  Services.prefs.setStringPref(PREF_ENDPOINT, ENDPOINT);
  Services.prefs.setStringPref(PREF_MODEL, MODEL);

  const sb = sinon.createSandbox();
  try {
    const { fakeEngineInstance: fakeEngine } = setupStubs(
      {
        finalOutput: `1. Suggestion 1\n\n- Suggestion 2\nLabel: Suggestion 3.\nSuggestion 4\nSuggestion 5\nSuggestion 6`,
      },
      sb
    );

    const fakeMemories = ["Memory summary 1", "Memory summary 2"];
    const memoriesStub = sb
      .stub(MemoriesGetterForSuggestionPrompts, "getMemorySummariesForPrompt")
      .resolves(fakeMemories);

    const n = 3;
    const contextTabs = [
      { title: "Current Tab", url: "https://current.example.com" },
      { title: "Tab 2", url: "https://tab2.example.com" },
    ];

    const result = await generateConversationStartersSidebar(
      contextTabs,
      n,
      true
    );
    Assert.ok(fakeEngine.run.calledOnce, "Engine run should be called once");
    Assert.equal(
      openAIEngine.build.firstCall.args[0].serviceType,
      "convo-starters",
      "Sidebar starters engine should use the convo-starters service type"
    );
    Assert.ok(
      memoriesStub.calledOnce,
      "getMemorySummariesForPrompt should be called once"
    );

    // Verify the prompt content
    const callArgs = fakeEngine.run.firstCall.args[0];
    Assert.equal(
      callArgs.args.length,
      2,
      "run should be called with 2 messages"
    );

    Assert.ok(
      callArgs.args[1].content.includes(
        JSON.stringify({
          title: sanitizeUntrustedContent("Current Tab"),
          url: "https://current.example.com",
        })
      ),
      "Prompt should include current tab info"
    );
    Assert.ok(
      callArgs.args[1].content.includes(
        JSON.stringify({
          title: sanitizeUntrustedContent("Tab 2"),
          url: "https://tab2.example.com",
        })
      ),
      "Prompt should include other tab info"
    );
    Assert.ok(
      callArgs.args[1].content.includes(
        "\n- Memory summary 1\n- Memory summary 2"
      ),
      "Prompt should include memory summaries"
    );

    Assert.deepEqual(
      result,
      [
        { text: "Suggestion 1", type: "chat" },
        { text: "Suggestion 2", type: "chat" },
        { text: "Suggestion 3", type: "chat" },
      ],
      "Suggestions should match expected values"
    );
  } finally {
    sb.restore();
  }
});

/**
 * Tests for generateConversationStartersSidebar without including memories
 */
add_task(
  async function test_generateConversationStartersSidebar_without_memories() {
    Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
    Services.prefs.setStringPref(PREF_ENDPOINT, ENDPOINT);
    Services.prefs.setStringPref(PREF_MODEL, MODEL);

    const sb = sinon.createSandbox();
    try {
      const { fakeEngineInstance: fakeEngine } = setupStubs(
        {
          finalOutput: `1. Suggestion 1\n\n- Suggestion 2\nLabel: Suggestion 3.\nSuggestion 4\nSuggestion 5\nSuggestion 6`,
        },
        sb
      );

      const fakeMemories = ["Memory summary 1", "Memory summary 2"];
      const memoriesStub = sb
        .stub(MemoriesGetterForSuggestionPrompts, "getMemorySummariesForPrompt")
        .resolves(fakeMemories);

      const n = 3;
      const contextTabs = [
        { title: "Current Tab", url: "https://current.example.com" },
        { title: "Tab 2", url: "https://tab2.example.com" },
      ];

      const result = await generateConversationStartersSidebar(
        contextTabs,
        n,
        false
      );
      Assert.ok(fakeEngine.run.calledOnce, "Engine run should be called once");
      Assert.ok(
        !memoriesStub.calledOnce,
        "getMemorySummariesForPrompt shouldn't be called"
      );

      // Verify the prompt content
      const callArgs = fakeEngine.run.firstCall.args[0];
      Assert.equal(
        callArgs.args.length,
        2,
        "run should be called with 2 messages"
      );
      Assert.ok(
        callArgs.args[1].content.includes(
          JSON.stringify({
            title: sanitizeUntrustedContent("Current Tab"),
            url: "https://current.example.com",
          })
        ),
        "Prompt should include current tab info"
      );
      Assert.ok(
        callArgs.args[1].content.includes(
          JSON.stringify({
            title: sanitizeUntrustedContent("Tab 2"),
            url: "https://tab2.example.com",
          })
        ),
        "Prompt should include other tab info"
      );
      Assert.ok(
        !callArgs.args[1].content.includes(
          "\n- Memory summary 1\n- Memory summary 2"
        ),
        "Prompt should not include memory summaries"
      );

      Assert.deepEqual(
        result,
        [
          { text: "Suggestion 1", type: "chat" },
          { text: "Suggestion 2", type: "chat" },
          { text: "Suggestion 3", type: "chat" },
        ],
        "Suggestions should match expected values"
      );
    } finally {
      sb.restore();
    }
  }
);

/**
 * Tests for generateConversationStartersSidebar when no memories are returned
 */
add_task(
  async function test_generateConversationStartersSidebar_no_memories_returned() {
    Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
    Services.prefs.setStringPref(PREF_ENDPOINT, ENDPOINT);
    Services.prefs.setStringPref(PREF_MODEL, MODEL);

    const sb = sinon.createSandbox();
    try {
      const { fakeEngineInstance: fakeEngine } = setupStubs(
        {
          finalOutput: `1. Suggestion 1\n\n- Suggestion 2\nLabel: Suggestion 3.\nSuggestion 4\nSuggestion 5\nSuggestion 6`,
        },
        sb
      );

      const fakeMemories = [];
      const memoriesStub = sb
        .stub(MemoriesGetterForSuggestionPrompts, "getMemorySummariesForPrompt")
        .resolves(fakeMemories);

      const n = 3;
      const contextTabs = [
        { title: "Current Tab", url: "https://current.example.com" },
        { title: "Tab 2", url: "https://tab2.example.com" },
      ];

      const result = await generateConversationStartersSidebar(
        contextTabs,
        n,
        true
      );
      Assert.ok(fakeEngine.run.calledOnce, "Engine run should be called once");
      Assert.ok(
        memoriesStub.calledOnce,
        "getMemorySummariesForPrompt should be called once"
      );

      // Verify the prompt content
      const callArgs = fakeEngine.run.firstCall.args[0];
      Assert.equal(
        callArgs.args.length,
        2,
        "run should be called with 2 messages"
      );
      Assert.ok(
        callArgs.args[1].content.includes(
          JSON.stringify({
            title: sanitizeUntrustedContent("Current Tab"),
            url: "https://current.example.com",
          })
        ),
        "Prompt should include current tab info"
      );
      Assert.ok(
        callArgs.args[1].content.includes(
          JSON.stringify({
            title: sanitizeUntrustedContent("Tab 2"),
            url: "https://tab2.example.com",
          })
        ),
        "Prompt should include other tab info"
      );
      Assert.ok(
        !callArgs.args[1].content.includes("\nUser Memories:\n"),
        "Prompt shouldn't include user memories block"
      );

      Assert.deepEqual(
        result,
        [
          { text: "Suggestion 1", type: "chat" },
          { text: "Suggestion 2", type: "chat" },
          { text: "Suggestion 3", type: "chat" },
        ],
        "Suggestions should match expected values"
      );
    } finally {
      sb.restore();
    }
  }
);

/**
 * Tests for generateConversationStartersSidebar when no tabs are provided
 */
add_task(async function test_generateConversationStartersSidebar_no_tabs() {
  Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
  Services.prefs.setStringPref(PREF_ENDPOINT, ENDPOINT);
  Services.prefs.setStringPref(PREF_MODEL, MODEL);

  const sb = sinon.createSandbox();
  try {
    const { fakeEngineInstance: fakeEngine } = setupStubs(
      {
        finalOutput: `1. Suggestion 1\n\n- Suggestion 2\nLabel: Suggestion 3.\nSuggestion 4\nSuggestion 5\nSuggestion 6`,
      },
      sb
    );

    const fakeMemories = ["Memory summary 1", "Memory summary 2"];
    const memoriesStub = sb
      .stub(MemoriesGetterForSuggestionPrompts, "getMemorySummariesForPrompt")
      .resolves(fakeMemories);

    const n = 3;
    const contextTabs = [];

    const result = await generateConversationStartersSidebar(
      contextTabs,
      n,
      true
    );
    Assert.ok(fakeEngine.run.calledOnce, "Engine run should be called once");
    Assert.ok(
      memoriesStub.calledOnce,
      "getMemorySummariesForPrompt should be called once"
    );

    // Verify the prompt content
    const callArgs = fakeEngine.run.firstCall.args[0];
    Assert.equal(
      callArgs.args.length,
      2,
      "run should be called with 2 messages"
    );
    Assert.ok(
      callArgs.args[1].content.includes("\nNo current tab\n"),
      "Prompt should indicate no current tab"
    );
    Assert.ok(
      callArgs.args[1].content.includes("\nNo tabs available\n"),
      "Prompt should indicate no tabs available"
    );
    Assert.ok(
      callArgs.args[1].content.includes(
        "\n- Memory summary 1\n- Memory summary 2"
      ),
      "Prompt should include memory summaries"
    );

    Assert.deepEqual(
      result,
      [
        { text: "Suggestion 1", type: "chat" },
        { text: "Suggestion 2", type: "chat" },
        { text: "Suggestion 3", type: "chat" },
      ],
      "Suggestions should match expected values"
    );
  } finally {
    sb.restore();
  }
});

/**
 * Tests for generateConversationStartersSidebar with one tab provided
 */
add_task(async function test_generateConversationStartersSidebar_one_tab() {
  Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
  Services.prefs.setStringPref(PREF_ENDPOINT, ENDPOINT);
  Services.prefs.setStringPref(PREF_MODEL, MODEL);

  const sb = sinon.createSandbox();
  try {
    const { fakeEngineInstance: fakeEngine } = setupStubs(
      {
        finalOutput: `1. Suggestion 1\n\n- Suggestion 2\nLabel: Suggestion 3.\nSuggestion 4\nSuggestion 5\nSuggestion 6`,
      },
      sb
    );

    const fakeMemories = ["Memory summary 1", "Memory summary 2"];
    const memoriesStub = sb
      .stub(MemoriesGetterForSuggestionPrompts, "getMemorySummariesForPrompt")
      .resolves(fakeMemories);

    const n = 3;
    const contextTabs = [
      { title: "Only Tab", url: "https://only.example.com" },
    ];

    const result = await generateConversationStartersSidebar(
      contextTabs,
      n,
      true
    );
    Assert.ok(fakeEngine.run.calledOnce, "Engine run should be called once");
    Assert.ok(
      memoriesStub.calledOnce,
      "getMemorySummariesForPrompt should be called once"
    );

    // Verify the prompt content
    const callArgs = fakeEngine.run.firstCall.args[0];
    Assert.equal(
      callArgs.args.length,
      2,
      "run should be called with 2 messages"
    );
    Assert.ok(
      callArgs.args[1].content.includes(
        JSON.stringify({
          title: sanitizeUntrustedContent("Only Tab"),
          url: "https://only.example.com",
        })
      ),
      "Prompt should include current tab info"
    );
    Assert.ok(
      callArgs.args[1].content.includes("\nOnly current tab is open\n"),
      "Prompt should indicate only current tab is open"
    );
    Assert.ok(
      callArgs.args[1].content.includes(
        "\n- Memory summary 1\n- Memory summary 2"
      ),
      "Prompt should include memory summaries"
    );

    Assert.deepEqual(
      result,
      [
        { text: "Suggestion 1", type: "chat" },
        { text: "Suggestion 2", type: "chat" },
        { text: "Suggestion 3", type: "chat" },
      ],
      "Suggestions should match expected values"
    );
  } finally {
    sb.restore();
  }
});

/**
 * Tests that generateConversationStartersSidebar handles engine errors gracefully
 */
add_task(
  async function test_generateConversationStartersSidebar_engine_error() {
    Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
    Services.prefs.setStringPref(PREF_ENDPOINT, ENDPOINT);
    Services.prefs.setStringPref(PREF_MODEL, MODEL);

    const sb = sinon.createSandbox();
    try {
      const { fakeEngineInstance: fakeEngine } = setupStubs(null, sb);
      fakeEngine.run.rejects(new Error("Engine failure"));

      const fakeMemories = ["Memory summary 1", "Memory summary 2"];
      sb.stub(
        MemoriesGetterForSuggestionPrompts,
        "getMemorySummariesForPrompt"
      ).resolves(fakeMemories);

      const n = 3;
      const contextTabs = [
        { title: "Only Tab", url: "https://only.example.com" },
      ];

      const result = await generateConversationStartersSidebar(
        contextTabs,
        n,
        true
      );
      Assert.deepEqual(result, [], "Should return empty array on engine error");
    } finally {
      sb.restore();
    }
  }
);

/**
 * Tests that the system prompt, assistant limitations, and memories prompt are included in conversation starter prompts
 */
add_task(
  async function test_generateConversationStartersSidebar_includes_subcomponents() {
    Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
    Services.prefs.setStringPref(PREF_ENDPOINT, ENDPOINT);
    Services.prefs.setStringPref(PREF_MODEL, MODEL);

    const sb = sinon.createSandbox();
    try {
      const { fakeEngineInstance: fakeEngine } = setupStubs(
        { finalOutput: `Suggestion 1\nSuggestion 2\nSuggestion 3` },
        sb
      );

      sb.stub(
        MemoriesGetterForSuggestionPrompts,
        "getMemorySummariesForPrompt"
      ).resolves(["memory 1", "memory 2"]);

      const n = 3;
      const contextTabs = [
        { title: "Test Tab", url: "https://test.example.com" },
      ];

      await generateConversationStartersSidebar(contextTabs, n, true);

      Assert.ok(fakeEngine.run.calledOnce, "Engine run should be called once");

      const callArgs = fakeEngine.run.firstCall.args[0];
      console.log("Prompt content:", callArgs.args);
      // System prompt was loaded (non-empty string from remote settings)
      Assert.ok(
        typeof callArgs.args[0].content === "string" &&
          callArgs.args[0].content.length,
        "System prompt should be loaded from remote settings"
      );
      // Additional components (limitations + memories) were concatenated in
      Assert.ok(
        typeof callArgs.args[1].content === "string" &&
          callArgs.args[1].content.length,
        "Additional components should be included in the prompt"
      );
      // The test-provided memories (from the stub) were injected into the prompt
      Assert.ok(
        callArgs.args[1].content.includes("memory 1") &&
          callArgs.args[1].content.includes("memory 2"),
        "Memories from the stub should be injected into the prompt"
      );
      // The browser locale was injected into the prompt
      Assert.ok(
        callArgs.args[1].content.includes(Services.locale.appLocaleAsBCP47) &&
          !callArgs.args[1].content.includes("{locale}"),
        "Browser locale should be injected into the prompt and no {locale} placeholder should remain"
      );
    } finally {
      sb.restore();
    }
  }
);

/**
 * Tests for generateFollowupPrompts successfully generating suggestions
 */
add_task(async function test_generateFollowupPrompts_happy_path() {
  Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
  Services.prefs.setStringPref(PREF_ENDPOINT, ENDPOINT);
  Services.prefs.setStringPref(PREF_MODEL, MODEL);

  const sb = sinon.createSandbox();
  try {
    const { fakeEngineInstance: fakeEngine } = setupStubs(
      {
        finalOutput: `1. Suggestion 1\n\n- Suggestion 2.\nSuggestion 3.\nSuggestion 4\nSuggestion 5\nSuggestion 6`,
      },
      sb
    );

    const fakeMemories = ["Memory summary 1", "Memory summary 2"];
    const memoriesStub = sb
      .stub(MemoriesGetterForSuggestionPrompts, "getMemorySummariesForPrompt")
      .resolves(fakeMemories);

    const n = 2;
    const conversationHistory = [
      { role: MESSAGE_ROLE.USER, content: "Hello" },
      { role: MESSAGE_ROLE.ASSISTANT, content: "Hi there!" },
    ];
    const currentTab = {
      title: "Current Tab",
      url: "https://current.example.com",
    };

    // Using memories
    const result = await generateFollowupPrompts(
      conversationHistory,
      currentTab,
      n,
      true
    );
    Assert.ok(fakeEngine.run.calledOnce, "Engine run should be called once");
    Assert.ok(
      memoriesStub.calledOnce,
      "getMemorySummariesForPrompt should be called once"
    );

    const callArgs = fakeEngine.run.firstCall.args[0];
    Assert.equal(
      callArgs.args.length,
      2,
      "run should be called with 2 messages"
    );
    Assert.ok(
      callArgs.args[1].content.includes(
        JSON.stringify({
          title: sanitizeUntrustedContent("Current Tab"),
          url: "https://current.example.com",
        })
      ),
      "Prompt should include current tab info"
    );
    Assert.ok(
      callArgs.args[1].content.includes(
        '[{"role":"user","content":"Hello"},{"role":"assistant","content":"Hi there!"}]'
      ),
      "Prompt should include conversation history"
    );
    Assert.ok(
      callArgs.args[1].content.includes(
        "\n- Memory summary 1\n- Memory summary 2"
      ),
      "Prompt should include memory summaries"
    );

    Assert.deepEqual(
      result,
      [
        { text: "Suggestion 1", type: "chat" },
        { text: "Suggestion 2", type: "chat" },
      ],
      "Suggestions should match"
    );
  } finally {
    sb.restore();
  }
});

/**
 * Tests for generateFollowupPrompts without including memories
 */
add_task(async function test_generateFollowupPrompts_no_memories() {
  Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
  Services.prefs.setStringPref(PREF_ENDPOINT, ENDPOINT);
  Services.prefs.setStringPref(PREF_MODEL, MODEL);

  const sb = sinon.createSandbox();
  try {
    const { fakeEngineInstance: fakeEngine } = setupStubs(
      {
        finalOutput: `1. Suggestion 1\n\n- Suggestion 2.\nSuggestion 3.\nSuggestion 4\nSuggestion 5\nSuggestion 6`,
      },
      sb
    );

    const fakeMemories = ["Memory summary 1", "Memory summary 2"];
    const memoriesStub = sb
      .stub(MemoriesGetterForSuggestionPrompts, "getMemorySummariesForPrompt")
      .resolves(fakeMemories);

    const n = 2;
    const conversationHistory = [
      { role: MESSAGE_ROLE.USER, content: "Hello" },
      { role: MESSAGE_ROLE.ASSISTANT, content: "Hi there!" },
    ];
    const currentTab = {
      title: "Current Tab",
      url: "https://current.example.com",
    };

    const result = await generateFollowupPrompts(
      conversationHistory,
      currentTab,
      n,
      false
    );
    Assert.ok(fakeEngine.run.calledOnce, "Engine run should be called once");
    Assert.ok(
      !memoriesStub.calledOnce,
      "getMemorySummariesForPrompt shouldn't be called"
    );

    const callArgs = fakeEngine.run.firstCall.args[0];
    Assert.equal(
      callArgs.args.length,
      2,
      "run should be called with 2 messages"
    );
    Assert.ok(
      callArgs.args[1].content.includes(
        JSON.stringify({
          title: sanitizeUntrustedContent("Current Tab"),
          url: "https://current.example.com",
        })
      ),
      "Prompt should include current tab info"
    );
    Assert.ok(
      callArgs.args[1].content.includes(
        '[{"role":"user","content":"Hello"},{"role":"assistant","content":"Hi there!"}]'
      ),
      "Prompt should include conversation history"
    );
    Assert.ok(
      !callArgs.args[1].content.includes(
        "\n- Memory summary 1\n- Memory summary 2"
      ),
      "Prompt shouldn't include memory summaries"
    );

    Assert.deepEqual(
      result,
      [
        { text: "Suggestion 1", type: "chat" },
        { text: "Suggestion 2", type: "chat" },
      ],
      "Suggestions should match"
    );
  } finally {
    sb.restore();
  }
});

/**
 * Tests for generateFollowupPrompts when no memories are returned
 */
add_task(async function test_generateFollowupPrompts_no_memories_returned() {
  Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
  Services.prefs.setStringPref(PREF_ENDPOINT, ENDPOINT);
  Services.prefs.setStringPref(PREF_MODEL, MODEL);

  const sb = sinon.createSandbox();
  try {
    const { fakeEngineInstance: fakeEngine } = setupStubs(
      {
        finalOutput: `1. Suggestion 1\n\n- Suggestion 2.\nSuggestion 3.\nSuggestion 4\nSuggestion 5\nSuggestion 6`,
      },
      sb
    );

    const fakeMemories = [];
    const memoriesStub = sb
      .stub(MemoriesGetterForSuggestionPrompts, "getMemorySummariesForPrompt")
      .resolves(fakeMemories);

    const n = 2;
    const conversationHistory = [
      { role: MESSAGE_ROLE.USER, content: "Hello" },
      { role: MESSAGE_ROLE.ASSISTANT, content: "Hi there!" },
    ];
    const currentTab = {
      title: "Current Tab",
      url: "https://current.example.com",
    };

    // Using memories
    const result = await generateFollowupPrompts(
      conversationHistory,
      currentTab,
      n,
      true
    );
    Assert.ok(fakeEngine.run.calledOnce, "Engine run should be called once");
    Assert.ok(
      memoriesStub.calledOnce,
      "getMemorySummariesForPrompt should be called once"
    );

    const callArgs = fakeEngine.run.firstCall.args[0];
    Assert.equal(
      callArgs.args.length,
      2,
      "run should be called with 2 messages"
    );
    Assert.ok(
      callArgs.args[1].content.includes(
        JSON.stringify({
          title: sanitizeUntrustedContent("Current Tab"),
          url: "https://current.example.com",
        })
      ),
      "Prompt should include current tab info"
    );
    Assert.ok(
      callArgs.args[1].content.includes(
        '[{"role":"user","content":"Hello"},{"role":"assistant","content":"Hi there!"}]'
      ),
      "Prompt should include conversation history"
    );
    Assert.ok(
      !callArgs.args[1].content.includes("\nUser Memories:\n"),
      "Prompt shouldn't include user memories block"
    );

    Assert.deepEqual(
      result,
      [
        { text: "Suggestion 1", type: "chat" },
        { text: "Suggestion 2", type: "chat" },
      ],
      "Suggestions should match"
    );
  } finally {
    sb.restore();
  }
});

/**
 * Tests for generateFollowupPrompts without a current tab
 */
add_task(async function test_generateFollowupPrompts_no_current_tab() {
  Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
  Services.prefs.setStringPref(PREF_ENDPOINT, ENDPOINT);
  Services.prefs.setStringPref(PREF_MODEL, MODEL);

  const sb = sinon.createSandbox();
  try {
    const { fakeEngineInstance: fakeEngine } = setupStubs(
      {
        finalOutput: `1. Suggestion 1\n\n- Suggestion 2.\nSuggestion 3.\nSuggestion 4\nSuggestion 5\nSuggestion 6`,
      },
      sb
    );

    const fakeMemories = [];
    const memoriesStub = sb
      .stub(MemoriesGetterForSuggestionPrompts, "getMemorySummariesForPrompt")
      .resolves(fakeMemories);

    const n = 2;
    const conversationHistory = [
      { role: MESSAGE_ROLE.USER, content: "Hello" },
      { role: MESSAGE_ROLE.ASSISTANT, content: "Hi there!" },
    ];
    const currentTab = {};

    const result = await generateFollowupPrompts(
      conversationHistory,
      currentTab,
      n,
      false
    );
    Assert.ok(fakeEngine.run.calledOnce, "Engine run should be called once");
    Assert.ok(
      !memoriesStub.calledOnce,
      "getMemorySummariesForPrompt shouldn't be called"
    );

    const callArgs = fakeEngine.run.firstCall.args[0];
    Assert.equal(
      callArgs.args.length,
      2,
      "run should be called with 2 messages"
    );
    Assert.ok(
      callArgs.args[1].content.includes("\nNo tab\n"),
      "Prompt shouldn't include any tab info"
    );
    Assert.ok(
      callArgs.args[1].content.includes(
        '[{"role":"user","content":"Hello"},{"role":"assistant","content":"Hi there!"}]'
      ),
      "Prompt should include conversation history"
    );
    Assert.ok(
      !callArgs.args[1].content.includes("\nUser Memories:\n"),
      "Prompt shouldn't include user memories block"
    );

    Assert.deepEqual(
      result,
      [
        { text: "Suggestion 1", type: "chat" },
        { text: "Suggestion 2", type: "chat" },
      ],
      "Suggestions should match"
    );
  } finally {
    sb.restore();
  }
});

/**
 * Tests that generateFollowupPrompts handles engine errors gracefully
 */
add_task(async function test_generateFollowupPrompts_engine_error() {
  Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
  Services.prefs.setStringPref(PREF_ENDPOINT, ENDPOINT);
  Services.prefs.setStringPref(PREF_MODEL, MODEL);

  const sb = sinon.createSandbox();
  try {
    const { fakeEngineInstance: fakeEngine } = setupStubs(null, sb);
    fakeEngine.run.rejects(new Error("Engine failure"));

    const fakeMemories = [];
    sb.stub(
      MemoriesGetterForSuggestionPrompts,
      "getMemorySummariesForPrompt"
    ).resolves(fakeMemories);

    const n = 2;
    const conversationHistory = [
      { role: MESSAGE_ROLE.USER, content: "Hello" },
      { role: MESSAGE_ROLE.ASSISTANT, content: "Hi there!" },
    ];
    const currentTab = {};

    const result = await generateFollowupPrompts(
      conversationHistory,
      currentTab,
      n,
      false
    );
    Assert.deepEqual(result, [], "Should return empty array on engine error");
  } finally {
    sb.restore();
  }
});

/**
 * Tests that assistant limitations are included in followup prompts
 */
add_task(
  async function test_generateFollowupPrompts_includes_assistant_limitations() {
    Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
    Services.prefs.setStringPref(PREF_ENDPOINT, ENDPOINT);
    Services.prefs.setStringPref(PREF_MODEL, MODEL);

    const sb = sinon.createSandbox();
    try {
      const { fakeEngineInstance: fakeEngine } = setupStubs(
        { finalOutput: `Suggestion 1\nSuggestion 2` },
        sb
      );

      sb.stub(
        MemoriesGetterForSuggestionPrompts,
        "getMemorySummariesForPrompt"
      ).resolves([]);

      const n = 2;
      const conversationHistory = [
        { role: MESSAGE_ROLE.USER, content: "Hello" },
        { role: MESSAGE_ROLE.ASSISTANT, content: "Hi there!" },
      ];
      const currentTab = { title: "Test", url: "https://test.example.com" };

      await generateFollowupPrompts(conversationHistory, currentTab, n, false);

      Assert.ok(fakeEngine.run.calledOnce, "Engine run should be called once");

      const callArgs = fakeEngine.run.firstCall.args[0];
      Assert.ok(
        callArgs.args[1].content.includes("limitations"),
        "Prompt should include assistant limitations from remote settings"
      );
    } finally {
      sb.restore();
    }
  }
);

/**
 * Tests for getMemorySummariesForPrompt happy path
 */
add_task(async function test_getMemorySummariesForPrompt_happy_path() {
  const sb = sinon.createSandbox();
  try {
    // Mock the MemoryStore to return fixed memories
    const fakeMemories = [
      {
        memory_summary: "Memory summary 1",
      },
      {
        memory_summary: "Memory summary 2",
      },
      {
        memory_summary: "Memory summary 3",
      },
    ];

    sb.stub(MemoriesManager, "getAllMemories").resolves(fakeMemories);

    const maxMemories = 2;
    const summaries =
      await MemoriesGetterForSuggestionPrompts.getMemorySummariesForPrompt(
        maxMemories
      );

    Assert.deepEqual(
      summaries,
      ["Memory summary 1", "Memory summary 2"],
      "Memory summaries should match expected values"
    );
  } finally {
    sb.restore();
  }
});

/**
 * Tests for getMemorySummariesForPrompt when no memories are returned
 */
add_task(async function test_getMemorySummariesForPrompt_no_memories() {
  const sb = sinon.createSandbox();
  try {
    // Mock the MemoryStore to return fixed memories
    const fakeMemories = [];

    sb.stub(MemoriesManager, "getAllMemories").resolves(fakeMemories);

    const maxMemories = 2;
    const summaries =
      await MemoriesGetterForSuggestionPrompts.getMemorySummariesForPrompt(
        maxMemories
      );

    Assert.equal(
      summaries.length,
      0,
      `getMemorySummariesForPrompt(${maxMemories}) should return 0 summaries`
    );
  } finally {
    sb.restore();
  }
});

/**
 * Tests for getMemorySummariesForPrompt with fewer memories than maxMemories
 */
add_task(async function test_getMemorySummariesForPrompt_too_few_memories() {
  const sb = sinon.createSandbox();
  try {
    // Mock the MemoryStore to return fixed memories
    const fakeMemories = [
      {
        memory_summary: "Memory summary 1",
      },
    ];

    sb.stub(MemoriesManager, "getAllMemories").resolves(fakeMemories);

    const maxMemories = 2;
    const summaries =
      await MemoriesGetterForSuggestionPrompts.getMemorySummariesForPrompt(
        maxMemories
      );

    Assert.deepEqual(
      summaries,
      ["Memory summary 1"],
      "Memory summaries should match expected values"
    );
  } finally {
    sb.restore();
  }
});

/**
 * Tests for getMemorySummariesForPrompt handling duplicate summaries
 */
add_task(async function test_getMemorySummariesForPrompt_duplicates() {
  const sb = sinon.createSandbox();
  try {
    // Mock the MemoryStore to return fixed memories
    const fakeMemories = [
      {
        memory_summary: "Duplicate summary",
      },
      {
        memory_summary: "duplicate summary",
      },
      {
        memory_summary: "Unique summary",
      },
    ];

    sb.stub(MemoriesManager, "getAllMemories").resolves(fakeMemories);

    const maxMemories = 2;
    const summaries =
      await MemoriesGetterForSuggestionPrompts.getMemorySummariesForPrompt(
        maxMemories
      );

    Assert.deepEqual(
      summaries,
      ["Duplicate summary", "Unique summary"],
      "Memory summaries should match expected values"
    );
  } finally {
    sb.restore();
  }
});

/**
 * Tests for getMemorySummariesForPrompt handling empty and whitespace-only summaries
 */
add_task(
  async function test_getMemorySummariesForPrompt_empty_and_whitespace() {
    const sb = sinon.createSandbox();
    try {
      // Mock the MemoryStore to return fixed memories
      const fakeMemories = [
        {
          memory_summary: "   \n",
        },
        {
          memory_summary: "",
        },
        {
          memory_summary: "Valid summary",
        },
      ];

      sb.stub(MemoriesManager, "getAllMemories").resolves(fakeMemories);

      const maxMemories = 2;
      const summaries =
        await MemoriesGetterForSuggestionPrompts.getMemorySummariesForPrompt(
          maxMemories
        );

      Assert.deepEqual(
        summaries,
        ["Valid summary"],
        "Memory summaries should match expected values"
      );
    } finally {
      sb.restore();
    }
  }
);
