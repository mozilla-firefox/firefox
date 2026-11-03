/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

"use strict";

const { JourneyManager, MAX_NUM_URLS_PER_MEMORY } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/journeys/JourneyManager.sys.mjs"
);
const { JourneyStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/journeys/JourneyStore.sys.mjs"
);
const { loadPrompt } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs"
);
const { Conversation } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs"
);
const { MemoryStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
);
const {
  HISTORY,
  CONVERSATION,
  SESSION,
  MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
  MEMORY_SENSITIVITY_CATEGORY_SENSITIVE,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesConstants.sys.mjs"
);
const { ChatStore, ChatConversation, ChatMessage, MESSAGE_ROLE } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
  );
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);
const { MemoriesManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs"
);
const { PURPOSES, MODEL_FEATURES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);
const { MESSAGE_LENGTH_THRESHOLD } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesChatSource.sys.mjs"
);
const { resolveUrlsForMemories, getDistanceThreshold } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesHistorySource.sys.mjs"
  );
const { getPlacesSemanticHistoryManager } = ChromeUtils.importESModule(
  "resource://gre/modules/PlacesSemanticHistoryManager.sys.mjs"
);
const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);

const JOURNEY_TEST_TIME = Date.UTC(2026, 0, 1);

function makePage(title, overrides = {}) {
  return {
    url: `https://example.com/${title.toLowerCase().replaceAll(" ", "-")}`,
    title,
    ...overrides,
  };
}

function makeChat(title, overrides = {}) {
  return {
    id: title.toLowerCase().replaceAll(" ", "-"),
    title,
    updatedDate: JOURNEY_TEST_TIME,
    messages: [
      { role: MESSAGE_ROLE.USER, body: `${title} user` },
      { role: MESSAGE_ROLE.ASSISTANT, body: `${title} assistant` },
    ],
    ...overrides,
  };
}

function makeMemory(summary, overrides = {}) {
  return {
    memory_summary: summary,
    sensitivity_category: MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
    sources: [HISTORY],
    frecency: 1,
    updated_at: JOURNEY_TEST_TIME,
    pages: [makePage(summary)],
    ...overrides,
  };
}

/**
 * Rewrites moz_places.url_hash for pages carrying a `urlHash` override, since
 * Places always computes the hash from the URL on insert.
 *
 * @param {Array<object>} pages - Pages from makePage
 */
async function applyUrlHashOverrides(pages) {
  const overridden = pages.filter(page => page.urlHash !== undefined);
  if (!overridden.length) {
    return;
  }
  await PlacesUtils.withConnectionWrapper(
    "test-journey-override-url-hash",
    async db => {
      for (const { url, urlHash } of overridden) {
        await db.execute(
          `UPDATE moz_places
           SET url_hash = :urlHash
           WHERE url = :url`,
          { url, urlHash }
        );
      }
    }
  );
}

async function addJourneyTestMemories(memories) {
  const pages = memories.flatMap(memory => memory.pages).filter(Boolean);
  if (pages.length) {
    await PlacesTestUtils.addVisits(
      pages.map((page, index) => ({
        url: page.url,
        title: page.title,
        visitDate: new Date(
          page.lastVisitDate ?? JOURNEY_TEST_TIME - index * 60_000
        ),
        transition: PlacesUtils.history.TRANSITIONS.LINK,
      }))
    );
    await applyUrlHashOverrides(pages);
    await waitForEmbeddings(
      pages.map(
        ({ url, urlHash }) => urlHash ?? PlacesUtils.history.hashURL(url)
      )
    );
  }

  const addedMemories = [];
  const overallAddedConversationIds = [];
  try {
    for (const { pages: memoryPages, chats, ...memory } of memories) {
      const conversationIdsPerMemory = [];
      for (const chat of chats ?? []) {
        const conversation = new ChatConversation({
          id: chat.id ?? crypto.randomUUID(),
          title: chat.title,
          updatedDate: chat.updatedDate,
          messages: chat.messages.map(
            (message, ordinal) =>
              new ChatMessage({
                ordinal,
                role: message.role,
                content: { type: "text", body: message.body },
              })
          ),
        });
        await ChatStore.updateConversation(conversation);
        conversationIdsPerMemory.push(conversation.id);
        overallAddedConversationIds.push(conversation.id);
      }
      addedMemories.push(
        await MemoryStore.addMemory({
          ...memory,
          source_ids: {
            history_source_ids: memoryPages?.map(
              ({ url, urlHash }) => urlHash ?? PlacesUtils.history.hashURL(url)
            ),
            conversation_source_ids: conversationIdsPerMemory,
          },
        })
      );
    }
  } catch (error) {
    for (const memory of addedMemories) {
      await MemoryStore.hardDeleteMemory(memory.id);
    }
    for (const conversationId of overallAddedConversationIds) {
      await ChatStore.deleteConversationById(conversationId);
    }
    throw error;
  }

  return addedMemories;
}

async function cleanupJourneyTestMemories(addedMemories) {
  try {
    for (const memory of addedMemories) {
      for (const conversationId of memory.source_ids?.conversation_source_ids ??
        []) {
        await ChatStore.deleteConversationById(conversationId);
      }
      await MemoryStore.hardDeleteMemory(memory.id);
    }
  } finally {
    // Undo any url_hash overrides so history.clear() sees a consistent table.
    await PlacesUtils.withConnectionWrapper(
      "test-journey-restore-url-hash",
      db =>
        db.execute(
          `UPDATE moz_places
           SET url_hash = hash(url)
           WHERE url_hash <> hash(url)`
        )
    );
    await PlacesUtils.history.clear();
  }
}

add_setup(async function setupJourneyTests() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["places.history.enabled", true],
      ["browser.privatebrowsing.autostart", false],
      [
        "browser.smartwindow.memories.resumeActivityUrlDistanceThreshold",
        "0.9",
      ],
    ],
  });

  registerCleanupFunction(async () => {
    try {
      await PlacesUtils.history.clear();
    } finally {
      await SpecialPowers.popPrefEnv();
    }
  });
});

add_task(async function test_generateAndPersistJourneys() {
  const minutesAgo = minutes => JOURNEY_TEST_TIME - minutes * 60_000;

  // Fixtures: eligible memories with history and conversation context.
  // Page A4 and its twin share a url_hash, so neither should resolve to a URL.
  const collidingUrl = "https://example.com/a4";
  const memoryFixtures = [
    makeMemory("Memory A", {
      reasoning: "Reason A",
      created_at: minutesAgo(0),
      pages: [
        makePage("Page A4 hash twin", {
          url: `${collidingUrl}-hash-twin`,
          urlHash: PlacesUtils.history.hashURL(collidingUrl),
          lastVisitDate: minutesAgo(-2),
        }),
        makePage("Page A4", {
          url: collidingUrl,
          lastVisitDate: minutesAgo(-1),
        }),
        makePage("Page A3", {
          url: "https://example.com/a3",
          lastVisitDate: minutesAgo(0),
        }),
        makePage('Page "A2"', {
          url: "https://example.com/a2",
          lastVisitDate: minutesAgo(1),
        }),
        makePage("Page A1", {
          url: "https://example.com/a1",
          lastVisitDate: minutesAgo(2),
        }),
      ],
    }),
    makeMemory("Memory B", {
      reasoning: "Reason B",
      sources: [HISTORY, CONVERSATION],
      created_at: minutesAgo(10),
      pages: [
        makePage("Page B2", { url: "https://example.com/b2" }),
        makePage("Page B1", { url: "https://example.com/b1" }),
      ],
      chats: [
        makeChat("Chat B1 older", {
          id: "chat1",
          updatedDate: minutesAgo(45),
        }),
        makeChat("Chat B2 newer", {
          id: "chat2",
          updatedDate: minutesAgo(20),
          messages: [
            {
              role: MESSAGE_ROLE.SYSTEM,
              body: "B2 system dropped",
            },
            {
              role: MESSAGE_ROLE.USER,
              body: "B2 oldest dropped",
            },
            {
              role: MESSAGE_ROLE.ASSISTANT,
              body: "B2 keep 1",
            },
            {
              role: MESSAGE_ROLE.TOOL,
              body: "B2 tool dropped",
            },
            {
              role: MESSAGE_ROLE.USER,
              body: "B2 keep 2",
            },
            {
              role: MESSAGE_ROLE.ASSISTANT,
              body: "B2 keep 3",
            },
          ],
        }),
      ],
    }),
    makeMemory("Memory C", { created_at: minutesAgo(11) }),
    makeMemory("Unresolvable", {
      created_at: minutesAgo(12),
      pages: [
        makePage("Unresolvable", {
          url: "https://example.com/unresolvable-generation",
        }),
      ],
    }),
  ];

  let memories = [];
  try {
    memories = await addJourneyTestMemories(memoryFixtures);
    await PlacesUtils.history.remove(
      "https://example.com/unresolvable-generation"
    );

    const mockEngineManager = new MockEngineManager();

    try {
      const journeyMetadataPromise =
        JourneyManager.generateAndPersistJourneys(memories);
      const { request, respond } = await mockEngineManager.captureRequest({
        purpose: PURPOSES.CHAT,
      });

      // Tests to ensure request includes correctly curated context
      Assert.deepEqual(
        request.args.map(message => message.role),
        ["system", "user"],
        "The request should contain system and user messages"
      );
      const userPrompt = request.args[1].content;
      for (const expectedContent of [
        "memory_summary: Memory A",
        "reasoning: Reason A",
        '  - "Page A1" (Untrusted webpage data)',
        '  - "Page \\"A2\\"" (Untrusted webpage data)',
        '  - "Page A3" (Untrusted webpage data)',
        "memory_summary: Memory B",
        "reasoning: Reason B",
        "B2 keep 1",
        "B2 keep 2",
        "B2 keep 3",
      ]) {
        Assert.ok(
          userPrompt.includes(expectedContent),
          `The prompt should include: ${expectedContent}`
        );
      }
      for (const excludedContent of [
        "B2 system dropped",
        "B2 tool dropped",
        "B2 oldest dropped",
        "Page A4",
      ]) {
        Assert.ok(
          !userPrompt.includes(excludedContent),
          `The prompt should exclude: ${excludedContent}`
        );
      }
      const pageA1Index = userPrompt.indexOf(
        '  - "Page A1" (Untrusted webpage data)'
      );
      const pageA2Index = userPrompt.indexOf(
        '  - "Page \\"A2\\"" (Untrusted webpage data)'
      );
      const pageA3Index = userPrompt.indexOf(
        '  - "Page A3" (Untrusted webpage data)'
      );
      Assert.ok(
        pageA1Index < pageA2Index && pageA2Index < pageA3Index,
        "The prompt should order pages from oldest to newest"
      );

      const newerChatIndex = userPrompt.indexOf("Chat B2 newer");
      const olderChatIndex = userPrompt.indexOf("Chat B1 older");
      Assert.ok(
        newerChatIndex > -1 && olderChatIndex > -1,
        "The prompt should include both newer and older chat"
      );
      Assert.less(
        olderChatIndex,
        newerChatIndex,
        "The prompt should order chats from oldest to newest"
      );

      Assert.deepEqual(
        [...userPrompt.matchAll(/memory_summary: ([^\n]+)/g)].map(
          match => match[1]
        ),
        ["Memory A", "Memory B", "Memory C"],
        "The request excludes memories without resolvable URLs"
      );

      // Test that LLM responses are correctly handled, including maintaining correct order, rejecting invalid metadata and omitted, unknown IDs.
      respond(
        JSON.stringify([
          { id: 1, headline: "Headline B!", status: "Status B;" },
          { id: 0, headline: "Headline A.", status: "Status A:" },
          { id: 2, headline: "", status: "Invalid headline" },
          { id: 99, headline: "Unknown", status: "Unknown" },
        ])
      );

      // Persistence: exact successful results.
      const expectedJourneyMetadata = [0, 1].map(index => ({
        memoryId: memories[index].id,
        headline: `Headline ${"AB"[index]}`,
        status: `Status ${"AB"[index]}`,
      }));
      Assert.deepEqual(
        await journeyMetadataPromise,
        expectedJourneyMetadata,
        "Returns only persisted results, mapped by batch-local ID and normalized"
      );
      Assert.equal(
        (await JourneyStore.getAll()).length,
        expectedJourneyMetadata.length,
        "Only successfully generated journeys are stored"
      );
      for (const { memoryId, headline, status } of expectedJourneyMetadata) {
        Assert.deepEqual(
          await JourneyStore.get(memoryId),
          {
            memory_id: memoryId,
            headline,
            status,
            dismissed: false,
            dismissed_until: null,
          },
          `Stored metadata matches the generated journey for ${headline}`
        );
      }
    } finally {
      mockEngineManager.cleanupMocks();
    }
  } finally {
    await JourneyStore.clear();
    await cleanupJourneyTestMemories(memories);
  }
});

add_task(async function test_generateAndPersistJourneys_securityProperties() {
  let memories = [];
  const mockEngineManager = new MockEngineManager();
  const runSpy = sinon.spy(Conversation.prototype, "run");
  try {
    memories = await addJourneyTestMemories([makeMemory("Security memory")]);
    const journeyMetadataPromise =
      JourneyManager.generateAndPersistJourneys(memories);
    const { request, respond } = await mockEngineManager.captureRequest({
      purpose: PURPOSES.CHAT,
    });
    Assert.ok(
      !("tools" in request),
      "The request should not offer any tools to the model"
    );
    Assert.ok(
      !("tool_choice" in request),
      "The request should not set tool_choice"
    );
    respond(
      JSON.stringify([{ id: 0, headline: "Security", status: "Started" }])
    );
    Assert.equal(
      (await journeyMetadataPromise).length,
      1,
      "The generated journey should be persisted"
    );

    Assert.ok(runSpy.calledOnce, "Generation should run one conversation");
    const [conversation] = runSpy.thisValues;
    Assert.equal(
      conversation.securityProperties.privateData,
      true,
      "privateData should be set: the prompt carries memories and browsing history"
    );
    Assert.equal(
      conversation.securityProperties.untrustedInput,
      true,
      "untrustedInput should be set: the prompt carries webpage titles"
    );
    Assert.deepEqual(
      [...conversation.seenUrls],
      [],
      "The generation conversation should not track any URLs"
    );
  } finally {
    runSpy.restore();
    mockEngineManager.cleanupMocks();
    await JourneyStore.clear();
    await cleanupJourneyTestMemories(memories);
  }
});

add_task(async function test_generateAndPersistJourneys_longInputs() {
  const pageCount = MAX_NUM_URLS_PER_MEMORY + 1;
  const pageLine = index => `  - "Page A${index}" (Untrusted webpage data)`;
  let memories = [];
  try {
    memories = await addJourneyTestMemories([
      makeMemory("Memory A", {
        reasoning: "Reason A",
        pages: Array.from({ length: pageCount }, (_, i) =>
          makePage(`Page A${i}`, {
            url: `https://example.com/a${i}`,
            lastVisitDate: JOURNEY_TEST_TIME + i * 60_000,
          })
        ),
        chats: [
          makeChat("Chat A1", {
            id: "chat1",
            messages: [
              {
                role: MESSAGE_ROLE.USER,
                body: "A".repeat(MESSAGE_LENGTH_THRESHOLD + 1),
              },
            ],
          }),
        ],
      }),
    ]);
    const mockEngineManager = new MockEngineManager();

    try {
      const journeyMetadataPromise =
        JourneyManager.generateAndPersistJourneys(memories);
      const { request, respond } = await mockEngineManager.captureRequest({
        purpose: PURPOSES.CHAT,
      });
      const userPrompt = request.args[1].content;
      Assert.ok(
        !userPrompt.includes(pageLine(0)),
        "The prompt should exclude the oldest page beyond the URL cap"
      );
      Assert.ok(
        userPrompt.includes(pageLine(pageCount - 1)),
        "The prompt should include the newest page"
      );
      Assert.equal(
        userPrompt.match(/^ {2}- ".*" \(Untrusted webpage data\)$/gm).length,
        MAX_NUM_URLS_PER_MEMORY,
        `The prompt should include exactly ${MAX_NUM_URLS_PER_MEMORY} pages`
      );
      Assert.ok(
        !userPrompt.includes("A".repeat(MESSAGE_LENGTH_THRESHOLD + 1)),
        "The prompt should exclude the full chat message that exceeds the threshold"
      );
      Assert.ok(
        userPrompt.includes("A".repeat(MESSAGE_LENGTH_THRESHOLD)),
        `The prompt should include the first ${MESSAGE_LENGTH_THRESHOLD} characters of the chat message`
      );

      respond(
        JSON.stringify([{ id: 0, headline: "Headline A", status: "Status A" }])
      );
      await journeyMetadataPromise;
      Assert.deepEqual(
        await JourneyStore.get(memories[0].id),
        {
          memory_id: memories[0].id,
          headline: "Headline A",
          status: "Status A",
          dismissed: false,
          dismissed_until: null,
        },
        "The journey generated from truncated inputs should be stored"
      );
    } finally {
      mockEngineManager.cleanupMocks();
    }
  } finally {
    await JourneyStore.clear();
    await cleanupJourneyTestMemories(memories);
  }
});

add_task(
  async function test_generateAndPersistJourneys_inferenceFailureSkipsBatch() {
    let memories = [];
    try {
      memories = await addJourneyTestMemories([
        makeMemory("Failed batch memory"),
        makeMemory("Successful batch memory"),
      ]);
      const successful = memories[1];
      const mockEngineManager = new MockEngineManager();

      try {
        const journeyMetadataPromise =
          JourneyManager.generateAndPersistJourneys(memories, {
            batchSize: 1,
          });
        for (let i = 0; i < memories.length; i++) {
          const { request, respond, reject } =
            await mockEngineManager.captureRequest({ purpose: PURPOSES.CHAT });
          if (request.args[1].content.includes("Failed batch memory")) {
            reject(new Error("Non-retryable failure"));
          } else {
            respond(
              JSON.stringify([
                { id: 0, headline: "Successful", status: "Started" },
              ])
            );
          }
        }

        Assert.deepEqual(
          await journeyMetadataPromise,
          [
            {
              memoryId: successful.id,
              headline: "Successful",
              status: "Started",
            },
          ],
          "A non-retryable inference failure should skip only its batch"
        );
        Assert.deepEqual(
          (await JourneyStore.getAll()).map(journey => journey.memory_id),
          [successful.id],
          "Only the journey from the successful batch should be stored"
        );
      } finally {
        mockEngineManager.cleanupMocks();
      }
    } finally {
      await JourneyStore.clear();
      await cleanupJourneyTestMemories(memories);
    }
  }
);

add_task(async function test_generateAndPersistJourneys_unsupportedLocale() {
  const originalAvailable = Services.locale.availableLocales;
  const originalRequested = Services.locale.requestedLocales;
  let memories = [];
  let mockEngineManager;

  try {
    memories = await addJourneyTestMemories([
      makeMemory("Locale gated memory", {
        frecency: 10,
        pages: [makePage("Locale gated page")],
      }),
    ]);
    mockEngineManager = new MockEngineManager();

    Services.locale.availableLocales = ["en-US", "de"];
    Services.locale.requestedLocales = ["de"];

    Assert.deepEqual(
      await JourneyManager.generateAndPersistJourneys(memories),
      [],
      "A non-English locale should produce no journeys"
    );
    Assert.equal(
      mockEngineManager.engines.size,
      0,
      "A non-English locale should not run inference"
    );
    Assert.deepEqual(
      await JourneyStore.getAll(),
      [],
      "A non-English locale should store no journey metadata"
    );
  } finally {
    Services.locale.requestedLocales = originalRequested;
    Services.locale.availableLocales = originalAvailable;
    mockEngineManager?.cleanupMocks();
    await cleanupJourneyTestMemories(memories);
  }
});

add_task(async function test_generateAndPersistJourneys_invalidBatchSize() {
  for (const batchSize of [0, 1.5]) {
    await Assert.rejects(
      JourneyManager.generateAndPersistJourneys([], { batchSize }),
      /batchSize must be a positive safe integer/,
      `Rejects invalid batch size: ${batchSize}`
    );
  }
});

// MockEngineManager only rejects with non-retryable errors, so retries are
// driven by stubbing inference directly.
add_task(async function test_generateAndPersistJourneys_retryableFailures() {
  const batchSize = 2;
  const memories = Array.from({ length: batchSize + 1 }, (_, index) => ({
    id: `retry-memory-${index}`,
    sensitivity_category: MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
    source_ids: { history_source_ids: [123] },
  }));
  const transientError = Object.assign(new Error("Service unavailable"), {
    status: 503,
  });

  function stubInference(sandbox, failures) {
    const resolve = sandbox
      .stub(JourneyManager, "_resolveJourneyContext")
      .callsFake(async batch =>
        batch.map(memory => ({ memory, urls: [], chats: [] }))
      );
    let calls = 0;
    const generate = sandbox
      .stub(JourneyManager, "_generateJourneyMetadata")
      .callsFake(async entries => {
        const error = failures[calls++];
        if (error) {
          throw error;
        }
        return entries.map(({ memory }) => ({
          memoryId: memory.id,
          headline: "Journey",
          status: "Started",
        }));
      });
    return { resolve, generate };
  }

  async function storedMemoryIds() {
    return (await JourneyStore.getAll())
      .map(journey => journey.memory_id)
      .sort();
  }

  let sandbox = sinon.createSandbox();
  try {
    const { resolve, generate } = stubInference(sandbox, [
      transientError,
      transientError,
    ]);
    Assert.equal(
      (
        await JourneyManager.generateAndPersistJourneys(memories, {
          batchSize,
          retryDelayMs: 0,
        })
      ).length,
      memories.length,
      "Transient failures should recover on the third attempt"
    );
    Assert.equal(
      generate.callCount,
      4,
      "The first batch should take three attempts and the second one"
    );
    Assert.equal(
      resolve.callCount,
      2,
      "Each batch should resolve context once, not once per attempt"
    );
    Assert.deepEqual(
      await storedMemoryIds(),
      memories.map(memory => memory.id),
      "Every journey should be stored after recovery"
    );
  } finally {
    sandbox.restore();
    await JourneyStore.clear();
  }

  sandbox = sinon.createSandbox();
  try {
    stubInference(sandbox, [
      null,
      transientError,
      transientError,
      transientError,
    ]);
    await Assert.rejects(
      JourneyManager.generateAndPersistJourneys(memories, {
        batchSize,
        retryDelayMs: 0,
      }),
      error => error === transientError,
      "Exhausted retries should propagate the final error"
    );
    Assert.deepEqual(
      await storedMemoryIds(),
      [memories[0].id, memories[1].id],
      "Journeys from earlier batches should remain stored"
    );
  } finally {
    sandbox.restore();
    await JourneyStore.clear();
  }
});

add_task(async function test_generateAndPersistJourneys_selectionAndBatching() {
  const withHistory = { source_ids: { history_source_ids: [123] } };
  const memories = [
    // Ineligible memories are the most recent, so they would displace eligible
    // ones if they were not filtered out before the cap is applied.
    makeMemory("Soft deleted", {
      ...withHistory,
      is_deleted: true,
      created_at: 2000,
    }),
    makeMemory("Sensitive", {
      ...withHistory,
      sensitivity_category: MEMORY_SENSITIVITY_CATEGORY_SENSITIVE,
      created_at: 2001,
    }),
    makeMemory("No history", {
      source_ids: { history_source_ids: [] },
      created_at: 2002,
    }),
    makeMemory("Stored", { ...withHistory, id: "stored", created_at: 2003 }),
    // Eligible memories in shuffled order.
    makeMemory("Rank 4", { ...withHistory, created_at: 997 }),
    makeMemory("Rank 1", { ...withHistory, created_at: 1000 }),
    makeMemory("Over cap", { ...withHistory, created_at: 996 }),
    makeMemory("Rank 3, older merge", {
      ...withHistory,
      created_at: 999,
      last_merged: 1,
    }),
    makeMemory("Rank 2, newer merge", {
      ...withHistory,
      created_at: 999,
      last_merged: 2,
    }),
  ];

  const sandbox = sinon.createSandbox();
  try {
    await JourneyStore.addOrUpdateJourney({
      memoryId: "stored",
      headline: "Stored",
      status: "Started",
    });
    const resolve = sandbox
      .stub(JourneyManager, "_resolveJourneyContext")
      .callsFake(async batch =>
        batch.map(memory => ({ memory, urls: [], chats: [] }))
      );
    const generate = sandbox
      .stub(JourneyManager, "_generateJourneyMetadata")
      .resolves([]);
    await JourneyManager.generateAndPersistJourneys(memories, {
      maxMemories: 4,
      batchSize: 2,
    });
    Assert.deepEqual(
      resolve.args.map(([batch]) => batch.map(memory => memory.memory_summary)),
      [
        ["Rank 1", "Rank 2, newer merge"],
        ["Rank 3, older merge", "Rank 4"],
      ],
      "Only the most recent eligible memories are selected, in ranked order and batches"
    );
    Assert.deepEqual(
      generate.args.flatMap(([entries]) =>
        entries.map(({ memory }) => memory.memory_summary)
      ),
      ["Rank 1", "Rank 2, newer merge", "Rank 3, older merge", "Rank 4"],
      "Exactly the selected memories reach journey metadata inference"
    );
  } finally {
    sandbox.restore();
    await JourneyStore.clear();
  }
});

add_task(async function test_generateAndPersistJourneys_contextFailure() {
  const sandbox = sinon.createSandbox();
  let memories = [];
  try {
    memories = await addJourneyTestMemories([makeMemory("Context failure")]);
    const malformed = {
      ...memories[0],
      source_ids: {
        ...memories[0].source_ids,
        conversation_source_ids: "not-an-array",
      },
    };
    const resolve = sandbox.spy(JourneyManager, "_resolveJourneyContext");
    const generate = sandbox.spy(JourneyManager, "_generateJourneyMetadata");
    await Assert.rejects(
      JourneyManager.generateAndPersistJourneys([malformed], {
        retryDelayMs: 0,
      }),
      () => true,
      "Context failures propagate without entering inference retries"
    );
    Assert.ok(resolve.calledOnce, "Context is resolved once");
    Assert.ok(generate.notCalled, "Inference is not attempted");
  } finally {
    sandbox.restore();
    await cleanupJourneyTestMemories(memories);
  }
});

add_task(async function test_runJourneyMaintenance() {
  const memories = [];
  try {
    for (const id of ["live", "dismissed", "softDeleted"]) {
      memories.push(
        await MemoryStore.addMemory({
          id,
          memory_summary: id,
          is_deleted: id === "softDeleted",
        })
      );
    }
    for (const memoryId of ["live", "dismissed", "softDeleted", "absent"]) {
      await JourneyStore.addOrUpdateJourney({
        memoryId,
        headline: memoryId,
        status: "Started",
        dismissed: memoryId === "dismissed",
      });
    }
    await JourneyManager.runJourneyMaintenance();
    Assert.deepEqual(
      (await JourneyStore.getAll()).map(record => record.memory_id).sort(),
      ["dismissed", "live"],
      "Maintenance removes orphaned and soft-deleted journeys"
    );
    Assert.ok(
      (await JourneyStore.get("dismissed")).dismissed,
      "Maintenance preserves dismissal state"
    );
  } finally {
    for (const memory of memories) {
      await MemoryStore.hardDeleteMemory(memory.id);
    }
    await JourneyStore.clear();
  }
});

/**
 * Mock engine embedding text as a bag of words, giving each distinct word its
 * own dimension. Keying on the words rather than on the whole string means a
 * change to the text a caller embeds shifts the distance.
 */
class MockMLEngine {
  #embeddingSize;
  #dimensions = new Map();

  constructor(embeddingSize) {
    this.#embeddingSize = embeddingSize;
  }

  async run(request) {
    const texts = Array.isArray(request.args[0])
      ? request.args[0]
      : request.args;
    return texts.map(text => this.#embed(text));
  }

  #embed(text) {
    const vector = Array(this.#embeddingSize).fill(0);
    for (const word of text.toLowerCase().match(/[a-z]+/g) ?? []) {
      if (!this.#dimensions.has(word)) {
        if (this.#dimensions.size === this.#embeddingSize) {
          throw new Error(
            `Test corpus exceeds ${this.#embeddingSize} distinct words`
          );
        }
        this.#dimensions.set(word, this.#dimensions.size);
      }
      vector[this.#dimensions.get(word)] = 1;
    }
    return vector;
  }
}

async function urlHashFor(url) {
  return PlacesUtils.withConnectionWrapper("test-url-hash", async db => {
    const rows = await db.execute(
      "SELECT url_hash FROM moz_places WHERE url = :url",
      { url }
    );
    return rows[0].getResultByName("url_hash");
  });
}

function makeSemanticMemory(id, memory_summary, urlHashes, reasoning) {
  return {
    id,
    memory_summary,
    reasoning,
    source_ids: { history_source_ids: urlHashes },
  };
}

// Each summary shares exactly one word with its own page title and none with
// the other, so a summary keeps its own page and rejects the other.
const ALPHA_SUMMARY = "qqzz alpha probe";
const BETA_SUMMARY = "qqzz beta probe";
const ALPHA_URL = "https://alpha.moz.com/";
const BETA_URL = "https://beta.moz.com/";

// A summary sharing no word with either page title, so on its own it is out of
// range of both and only a matching reasoning can bring a page back in.
const UNRELATED_SUMMARY = "qqzz probe";
const BETA_REASONING = "beta entry page";

let sb;
let semanticManager;
let alphaHash;
let betaHash;

/**
 * Waits until the semantic DB holds an embedding for every given URL hash, so
 * the distance query has rows to score against.
 *
 * @param {Array<number>} urlHashes
 */
async function waitForEmbeddings(urlHashes) {
  const wanted = [...new Set(urlHashes)];
  const bindings = {};
  const placeholders = wanted.map((urlHash, index) => {
    bindings[`urlHash${index}`] = urlHash;
    return `:urlHash${index}`;
  });
  const conn = await semanticManager.getConnection();
  await TestUtils.waitForCondition(async () => {
    const [row] = await conn.execute(
      `SELECT COUNT(DISTINCT url_hash) AS indexed
       FROM vec_history_mapping
       WHERE url_hash IN (${placeholders.join(", ")})`,
      bindings
    );
    return row.getResultByName("indexed") === wanted.length;
  }, "Waiting for the seeded pages to be embedded");
}

/**
 * Seeds the two scored pages and waits for them to be embedded.
 */
async function ensureSemanticPages() {
  if (await PlacesUtils.history.fetch(ALPHA_URL)) {
    return;
  }

  await PlacesTestUtils.addVisits([
    { url: ALPHA_URL, title: "alpha entry page" },
    { url: BETA_URL, title: "beta entry page" },
  ]);
  alphaHash = await urlHashFor(ALPHA_URL);
  betaHash = await urlHashFor(BETA_URL);
  await waitForEmbeddings([alphaHash, betaHash]);
}

add_setup(async function setupSemanticUrlResolutionTests() {
  sb = sinon.createSandbox();

  registerCleanupFunction(async () => {
    sb.restore();
    await semanticManager?.shutdown();
    Services.prefs.clearUserPref("places.semanticHistory.initialized");
    await SpecialPowers.popPrefEnv();
    getPlacesSemanticHistoryManager({}, true);
  });

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.enable", true],
      ["places.semanticHistory.featureGate", true],
      ["places.semanticHistory.smartwindow.featureGate", true],
      ["browser.search.region", "US"],
    ],
  });

  await PlacesUtils.history.clear();

  await getPlacesSemanticHistoryManager().shutdown();

  semanticManager = getPlacesSemanticHistoryManager(
    { changeThresholdCount: 1, deferredTaskInterval: 100 },
    true
  );

  sb.stub(semanticManager, "hasSufficientEntriesForSearching").resolves(true);
  sb.stub(semanticManager, "isEnabledForSmartWindow").value(true);

  await semanticManager.semanticDB.removeDatabaseFiles();
  await semanticManager.getConnection();
  semanticManager.embedder.setEngine(
    new MockMLEngine(semanticManager.embedder.embeddingSize)
  );
});

add_task(function test_getJourneyTypeMemories() {
  const eligible = {
    id: "eligible",
    sensitivity_category: MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
    source_ids: { history_source_ids: [123] },
  };
  const cases = [
    ["Eligible memory", {}, true],
    [
      "Sensitive memory",
      { sensitivity_category: MEMORY_SENSITIVITY_CATEGORY_SENSITIVE },
      false,
    ],
    ["Missing sources", { source_ids: undefined }, false],
    ["Empty history IDs", { source_ids: { history_source_ids: [] } }, false],
    [
      "Conversation-only memory",
      { source_ids: { conversation_source_ids: ["chat"] } },
      false,
    ],
    [
      "History and conversation memory",
      {
        source_ids: {
          history_source_ids: [123],
          conversation_source_ids: ["chat"],
        },
      },
      true,
    ],
  ];
  for (const [name, overrides, included] of cases) {
    const memory = { ...eligible, ...overrides };
    Assert.deepEqual(
      JourneyManager._getJourneyTypeMemories([memory]),
      included ? [memory] : [],
      name
    );
  }

  const memories = [
    { ...eligible, id: "older", created_at: 1 },
    { ...eligible, id: "excluded", created_at: 3 },
    { ...eligible, id: "newer", created_at: 2 },
  ];
  Assert.deepEqual(
    JourneyManager._getJourneyTypeMemories(memories, new Set(["excluded"])),
    [memories[0], memories[2]],
    "Excluded memories are omitted without sorting the remaining memories"
  );
});

add_task(async function test_suggestJourneysToResume() {
  const unresolvableUrl = "https://example.com/unresolvable-suggestion";
  const staleUnresolvableUrl =
    "https://example.com/stale-unresolvable-suggestion";
  const mockEngineManager = new MockEngineManager();
  let memories = [];
  try {
    memories = await addJourneyTestMemories([
      makeMemory("Older journey", { created_at: JOURNEY_TEST_TIME - 60_000 }),
      makeMemory("Newer journey", {
        created_at: JOURNEY_TEST_TIME,
        pages: Array.from({ length: MAX_NUM_URLS_PER_MEMORY + 1 }, (_, i) =>
          makePage(`Newer page ${i}`, {
            lastVisitDate: JOURNEY_TEST_TIME + i * 60_000,
          })
        ),
      }),
      makeMemory("No journey"),
      makeMemory("Dismissed journey"),
      makeMemory("Snoozed journey"),
      makeMemory("Soft deleted journey"),
      makeMemory("Sensitive journey", {
        sensitivity_category: MEMORY_SENSITIVITY_CATEGORY_SENSITIVE,
      }),
      makeMemory("Unresolvable journey", {
        pages: [makePage("Unresolvable page", { url: unresolvableUrl })],
      }),
      makeMemory("Stale unresolvable journey", {
        created_at: JOURNEY_TEST_TIME - 30_000,
        pages: [
          makePage("Stale unresolvable page", { url: staleUnresolvableUrl }),
        ],
      }),
    ]);
    await PlacesUtils.history.remove([unresolvableUrl, staleUnresolvableUrl]);
    const [
      older,
      newer,
      ,
      dismissed,
      snoozed,
      softDeleted,
      ,
      ,
      staleUnresolvable,
    ] = memories;
    for (const memory of memories) {
      if (memory.memory_summary !== "No journey") {
        await JourneyStore.addOrUpdateJourney({
          memoryId: memory.id,
          headline: `${memory.memory_summary} headline`,
          status: `${memory.memory_summary} status`,
        });
      }
    }
    await JourneyStore.addOrUpdateJourney({
      memoryId: dismissed.id,
      dismissed: true,
    });
    await JourneyStore.addOrUpdateJourney({
      memoryId: snoozed.id,
      dismissed_until: Date.now() + 60 * 60_000,
    });
    await MemoriesManager.softDeleteMemoryById(softDeleted.id);

    Assert.deepEqual(
      (await JourneyManager.suggestJourneysToResume()).map(
        ({ memory, content }) => ({ memoryId: memory.id, content })
      ),
      [
        {
          memoryId: newer.id,
          content: {
            headline: "Newer journey headline",
            status: "Newer journey status",
            previewTabs: Array.from(
              { length: MAX_NUM_URLS_PER_MEMORY },
              (_, i) => makePage(`Newer page ${i + 1}`)
            ),
          },
        },
        {
          memoryId: older.id,
          content: {
            headline: "Older journey headline",
            status: "Older journey status",
            previewTabs: [makePage("Older journey")],
          },
        },
      ],
      "Suggests stored journeys only for live, unsuppressed, non-sensitive memories with resolvable URLs, newest first, with the newest preview tabs in visit order"
    );
    const suggestedIds = async (...args) =>
      (await JourneyManager.suggestJourneysToResume(...args)).map(
        ({ memory }) => memory.id
      );
    Assert.deepEqual(
      await suggestedIds({ excludeIds: new Set([newer.id]) }),
      [older.id],
      "Excluded journey IDs are not suggested"
    );
    Assert.deepEqual(
      await suggestedIds({ limit: 1 }),
      [newer.id],
      "Suggestions are limited after sorting"
    );
    Assert.deepEqual(
      await suggestedIds({
        excludeIds: new Set([newer.id, staleUnresolvable.id]),
        limit: 1,
      }),
      [older.id],
      "Limit applies after exclusion"
    );
    Assert.deepEqual(
      await suggestedIds({ excludeIds: new Set([newer.id]), limit: 1 }),
      [],
      "Supplied limit caps the number of candidate journeys eligible to pass URL existence checks, even if more journeys exist"
    );
    Assert.deepEqual(
      await suggestedIds({ limit: 0 }),
      [],
      "Zero limit returns no suggestions"
    );
    Assert.equal(
      mockEngineManager.engines.size,
      0,
      "Suggesting journeys should not run inference"
    );
  } finally {
    mockEngineManager.cleanupMocks();
    await JourneyStore.clear();
    await cleanupJourneyTestMemories(memories);
  }
});

add_task(async function test_suggestJourneysToResume_unsupportedLocale() {
  const originalAvailable = Services.locale.availableLocales;
  const originalRequested = Services.locale.requestedLocales;
  const getAllSpy = sinon.spy(JourneyStore, "getAll");
  try {
    await JourneyStore.addOrUpdateJourney({
      memoryId: "memory-1",
      headline: "Headline",
      status: "Status",
    });
    Services.locale.availableLocales = ["en-US", "de"];
    Services.locale.requestedLocales = ["de"];

    Assert.deepEqual(
      await JourneyManager.suggestJourneysToResume(),
      [],
      "A non-English locale should suggest no journeys"
    );
    Assert.ok(
      getAllSpy.notCalled,
      "A non-English locale should not read the journey store"
    );
  } finally {
    Services.locale.requestedLocales = originalRequested;
    Services.locale.availableLocales = originalAvailable;
    getAllSpy.restore();
    await JourneyStore.clear();
  }
});

add_task(async function test_constructConversationToResumeJourney() {
  const testMemories = [
    makeMemory("Test memory", {
      reasoning: "Test reasoning",
      frecency: 10,
      pages: [makePage("Test page", { url: "https://example.com/test" })],
    }),
  ];
  let addedMemories = [];

  try {
    addedMemories = await addJourneyTestMemories(testMemories);
    const journeySuggestion = {
      memory: addedMemories[0],
      content: {
        headline: "Test headline",
        status: "Test status",
        previewTabs: [{ url: "https://example.com/test", title: "Test page" }],
      },
    };
    const mockEngineManager = new MockEngineManager();

    try {
      const conversation =
        await JourneyManager.constructConversationToResumeJourney(
          journeySuggestion
        );
      Assert.ok(
        conversation instanceof ChatConversation,
        "Should return a ChatConversation instance"
      );
      Assert.equal(
        conversation.title,
        journeySuggestion.content.headline,
        "Should use the journey headline as the title"
      );

      // Seen Urls should be empty
      Assert.deepEqual(
        [...conversation.seenUrls],
        [],
        "The conversation should not track any seen URLs"
      );

      const messages = conversation.messages;
      Assert.deepEqual(
        messages.map(message => message.role),
        [MESSAGE_ROLE.SYSTEM, MESSAGE_ROLE.USER],
        "Should initialize one system message and one user message"
      );

      const [systemMessage, userMessage] = messages;
      Assert.ok(
        userMessage instanceof ChatMessage,
        "Messages should be ChatMessage instances"
      );
      const { prompt: journeySystemPrompt } = await loadPrompt(
        MODEL_FEATURES.RESUME_ACTIVITY_CONVERSATION,
        { module: "system-instructions" }
      );
      Assert.greater(
        systemMessage.content.body.indexOf(`\n${journeySystemPrompt}`),
        0,
        "The system message should append the journey instructions to the chat system prompt"
      );

      Assert.equal(
        userMessage.content.body,
        journeySuggestion.content.headline,
        "The user message should contain the journey headline"
      );
      const { resumeActivityContext } = userMessage.content.userContext;
      for (const expectedContent of [
        "<resume_activity_context>",
        "memory_summary: Test memory",
        "reasoning: Test reasoning",
        '  - "Test page" (Untrusted webpage data)',
        "headline: Test headline",
        "status: Test status",
      ]) {
        Assert.ok(
          resumeActivityContext.includes(expectedContent),
          `The journey context should include: ${expectedContent}`
        );
      }

      Assert.equal(
        userMessage.content.relevantMemories[0].memory_summary,
        "Test memory",
        "The user message should include the relevant memory summary"
      );

      Assert.equal(
        conversation.securityProperties.privateData,
        true,
        "privateData should be set"
      );
      Assert.equal(
        conversation.securityProperties.untrustedInput,
        true,
        "untrustedInput should be set"
      );

      const originatingId = "originating-conversation-id";
      const conversationWithId =
        await JourneyManager.constructConversationToResumeJourney(
          journeySuggestion,
          originatingId
        );
      Assert.equal(
        conversationWithId.id,
        originatingId,
        "Should reuse the id of the conversation the resume pill was clicked in"
      );
      Assert.notEqual(
        conversation.id,
        originatingId,
        "Should generate a new id when no originating id is passed"
      );
    } finally {
      mockEngineManager.cleanupMocks();
    }
  } finally {
    await cleanupJourneyTestMemories(addedMemories);
  }
});

add_task(
  async function test_constructConversationToResumeJourney_invalidInput() {
    const memory = makeMemory("Test memory");
    const validContent = () => ({
      headline: "Test headline",
      status: "Test status",
      previewTabs: [{ url: "https://example.com/test", title: "Test page" }],
    });

    const invalidSuggestions = [
      ["a missing suggestion", null],
      ["a suggestion without a memory", { content: validContent() }],
      ["a suggestion without content", { memory }],
      [
        "content without a headline",
        { memory, content: { ...validContent(), headline: "" } },
      ],
      [
        "content without a status",
        { memory, content: { ...validContent(), status: "" } },
      ],
      [
        "content without previewTabs",
        {
          memory,
          content: { headline: "Test headline", status: "Test status" },
        },
      ],
      [
        "content with empty previewTabs",
        { memory, content: { ...validContent(), previewTabs: [] } },
      ],
    ];

    for (const [description, suggestion] of invalidSuggestions) {
      Assert.strictEqual(
        await JourneyManager.constructConversationToResumeJourney(suggestion),
        null,
        `Should return null for ${description}`
      );
    }
  }
);

/**
 * Tests a call to resolveUrlsForMemories without the semantic filter
 * returns all hashes.
 */
add_task(async function test_without_filter_resolves_every_hash() {
  await ensureSemanticPages();

  const resolved = await resolveUrlsForMemories([
    makeSemanticMemory("m1", ALPHA_SUMMARY, [alphaHash, betaHash]),
  ]);

  Assert.deepEqual(
    [...resolved.keys()].sort(),
    [alphaHash, betaHash].sort(),
    "Both referenced URLs resolve when the filter is off"
  );
  Assert.equal(
    resolved.get(alphaHash).url,
    ALPHA_URL,
    "Resolved entry carries its URL"
  );
  Assert.ok(
    !("distance" in resolved.get(alphaHash)),
    "Unfiltered path does not add a distance"
  );
});

/**
 * Tests a call to resolveUrlsForMemories with the semantic filter turned
 * on returns just the close URL
 */
add_task(async function test_filter_keeps_urls_close_to_own_summary() {
  await ensureSemanticPages();

  const resolved = await resolveUrlsForMemories(
    [makeSemanticMemory("m1", ALPHA_SUMMARY, [alphaHash, betaHash])],
    { filterBySummary: true }
  );

  Assert.deepEqual(
    [...resolved.keys()],
    [alphaHash],
    "Only the URL close to this memory's summary is resolved"
  );

  const entry = resolved.get(alphaHash);
  Assert.equal(entry.url, ALPHA_URL, "Resolved entry carries its URL");
  Assert.equal(
    entry.title,
    "alpha entry page",
    "Resolved entry carries a title"
  );
  Assert.greater(entry.lastVisitDate, 0, "Resolved entry carries a visit date");
  Assert.ok(
    entry.distance >= 0 && entry.distance <= getDistanceThreshold(),
    `Resolved entry carries a distance within the threshold, got ${entry.distance}`
  );
});

/**
 * Tests that the URLs returned are the ones closest to each memory's summary.
 */
add_task(async function test_each_memory_is_scored_against_its_own_summary() {
  await ensureSemanticPages();

  const resolved = await resolveUrlsForMemories(
    [
      makeSemanticMemory("m1", ALPHA_SUMMARY, [alphaHash, betaHash]),
      makeSemanticMemory("m2", BETA_SUMMARY, [alphaHash, betaHash]),
    ],
    { filterBySummary: true }
  );

  Assert.deepEqual(
    [...resolved.keys()].sort(),
    [alphaHash, betaHash].sort(),
    "Each memory contributes the URL matching its own summary"
  );

  const alphaOnly = await resolveUrlsForMemories(
    [makeSemanticMemory("m1", ALPHA_SUMMARY, [alphaHash, betaHash])],
    { filterBySummary: true }
  );
  Assert.deepEqual(
    [...alphaOnly.keys()],
    [alphaHash],
    "The alpha memory alone keeps only the alpha URL"
  );

  const betaOnly = await resolveUrlsForMemories(
    [makeSemanticMemory("m2", BETA_SUMMARY, [alphaHash, betaHash])],
    { filterBySummary: true }
  );
  Assert.deepEqual(
    [...betaOnly.keys()],
    [betaHash],
    "The beta memory alone keeps only the beta URL"
  );
});

/**
 * Tests that a URL kept for more than one memory is returned once, scored by
 * whichever memory it sits closest to.
 */
add_task(async function test_shared_url_resolves_once_at_closest_distance() {
  await ensureSemanticPages();

  const farOnly = await resolveUrlsForMemories(
    [makeSemanticMemory("m1", ALPHA_SUMMARY, [alphaHash])],
    { filterBySummary: true }
  );

  const shared = await resolveUrlsForMemories(
    [
      makeSemanticMemory("m1", ALPHA_SUMMARY, [alphaHash]),
      makeSemanticMemory("m2", "alpha entry page", [alphaHash]),
    ],
    { filterBySummary: true }
  );

  Assert.deepEqual(
    [...shared.keys()],
    [alphaHash],
    "A URL shared by several memories is returned once"
  );
  Assert.less(
    shared.get(alphaHash).distance,
    farOnly.get(alphaHash).distance,
    "The shared entry keeps the closest of the two memories' distances"
  );
});

/**
 * Tests that resolveUrlsForMemories respected distanceThreshold overrides.
 */
add_task(async function test_explicit_threshold_overrides_default() {
  await ensureSemanticPages();

  const permissive = await resolveUrlsForMemories(
    [makeSemanticMemory("m1", ALPHA_SUMMARY, [alphaHash, betaHash])],
    { filterBySummary: true, distanceThreshold: 2 }
  );

  Assert.deepEqual(
    [...permissive.keys()],
    [alphaHash, betaHash],
    "A permissive threshold keeps both URLs, ordered closest first"
  );

  const strict = await resolveUrlsForMemories(
    [makeSemanticMemory("m1", ALPHA_SUMMARY, [alphaHash, betaHash])],
    { filterBySummary: true, distanceThreshold: 0 }
  );

  Assert.equal(strict.size, 0, "A zero threshold filters everything out");
});

/**
 * Tests that including memory reasoning in the embedded text changes
 * the URLs resolveUrlsForMemories returns.
 */
add_task(async function test_reasoning_is_part_of_the_scored_text() {
  await ensureSemanticPages();

  const summaryOnly = await resolveUrlsForMemories(
    [makeSemanticMemory("m1", UNRELATED_SUMMARY, [betaHash])],
    { filterBySummary: true }
  );

  Assert.equal(
    summaryOnly.size,
    0,
    "The summary alone scores the page out of range"
  );

  const withReasoning = await resolveUrlsForMemories(
    [makeSemanticMemory("m2", UNRELATED_SUMMARY, [betaHash], BETA_REASONING)],
    { filterBySummary: true }
  );

  Assert.deepEqual(
    [...withReasoning.keys()],
    [betaHash],
    "The reasoning is scored alongside the summary, bringing the page in range"
  );
});

/**
 * Tests that resolveUrlsForMemories skips memories without summaries.
 */
add_task(async function test_memory_without_summary_is_skipped() {
  await ensureSemanticPages();

  const resolved = await resolveUrlsForMemories(
    [
      makeSemanticMemory("m1", "   ", [alphaHash]),
      makeSemanticMemory("m2", BETA_SUMMARY, [betaHash]),
    ],
    { filterBySummary: true }
  );

  Assert.deepEqual(
    [...resolved.keys()],
    [betaHash],
    "A memory with a blank summary cannot be scored, so its URLs are omitted"
  );
});

/**
 * Tests resolveUrlsForMemories falls back to the unfiltered URLs when the
 * profile cannot run semantic scoring at all.
 */
add_task(async function test_semantic_unavailable_falls_back_to_unfiltered() {
  await ensureSemanticPages();

  sb.stub(semanticManager, "isEnabledForSmartWindow").value(false);

  try {
    const resolved = await resolveUrlsForMemories(
      [makeSemanticMemory("m1", ALPHA_SUMMARY, [alphaHash, betaHash])],
      { filterBySummary: true }
    );

    Assert.deepEqual(
      [...resolved.keys()].sort(),
      [alphaHash, betaHash].sort(),
      "Scoring cannot run, so every referenced URL resolves rather than none"
    );
    Assert.ok(
      !("distance" in resolved.get(alphaHash)),
      "The fallback entries come from the unfiltered path, so carry no distance"
    );
  } finally {
    sb.stub(semanticManager, "isEnabledForSmartWindow").value(true);
  }
});

/**
 * Tests that an empty scoring result is distinguished from scoring being
 * unavailable: a memory whose URLs are all too far away resolves to nothing
 * rather than falling back to the unfiltered URLs.
 */
add_task(async function test_empty_scoring_result_does_not_fall_back() {
  await ensureSemanticPages();

  const resolved = await resolveUrlsForMemories(
    [makeSemanticMemory("m1", ALPHA_SUMMARY, [alphaHash, betaHash])],
    { filterBySummary: true, distanceThreshold: 0 }
  );

  Assert.equal(
    resolved.size,
    0,
    "Scoring ran and kept nothing, which must not be read as unavailable"
  );
});

/**
 * Tests resolveUrlsForMemories returns nothing when places is disabled.
 */
add_task(async function test_history_disabled_resolves_nothing() {
  await ensureSemanticPages();

  Services.prefs.setBoolPref("places.history.enabled", false);

  try {
    const resolved = await resolveUrlsForMemories(
      [makeSemanticMemory("m1", ALPHA_SUMMARY, [alphaHash])],
      { filterBySummary: true }
    );

    Assert.equal(
      resolved.size,
      0,
      "The history pref gate still applies when filtering"
    );
  } finally {
    Services.prefs.clearUserPref("places.history.enabled");
  }
});

add_setup(async function setupJourneyStore() {
  registerCleanupFunction(async () => {
    await JourneyStore.clear();
    await JourneyStore.testOnlyFlushAndResetCache();
  });
  await JourneyStore.clear();
});

add_task(async function has_eligible_journeys() {
  const originalAvailable = Services.locale.availableLocales;
  const originalRequested = Services.locale.requestedLocales;
  let memories = [];
  try {
    memories = await addJourneyTestMemories([
      makeMemory("Compare hotels"),
      makeMemory("Book flights"),
      makeMemory("Deleted memory"),
    ]);
    const [hotels, flights, deleted] = memories;

    Assert.strictEqual(
      await JourneyManager.hasEligibleJourneys(),
      false,
      "Empty store has no eligible journeys"
    );

    await JourneyStore.addOrUpdateJourney({
      memoryId: hotels.id,
      headline: "Compare hotels",
      status: "Deciding between two hotels.",
      dismissed: true,
    });
    Assert.strictEqual(
      await JourneyManager.hasEligibleJourneys(),
      false,
      "A journey store containing only dismissed journeys does not have eligible journeys"
    );

    await JourneyStore.clear();
    await JourneyStore.addOrUpdateJourney({
      memoryId: deleted.id,
      headline: "Deleted memory",
      status: "Memory was deleted.",
    });
    await MemoriesManager.softDeleteMemoryById(deleted.id);
    Assert.strictEqual(
      await JourneyManager.hasEligibleJourneys(),
      false,
      "Journeys for deleted memories are not eligible"
    );

    await JourneyStore.clear();
    await JourneyStore.addOrUpdateJourney({
      memoryId: flights.id,
      headline: "Book flights",
      status: "Deciding between two flights.",
      dismissed_until: null,
    });
    Assert.strictEqual(
      await JourneyManager.hasEligibleJourneys(),
      true,
      "Non-dismissed journeys are eligible"
    );

    Services.locale.availableLocales = ["en-US", "de"];
    Services.locale.requestedLocales = ["de"];
    Assert.equal(
      Services.locale.appLocaleAsBCP47,
      "de",
      "The app locale should be German for this check"
    );
    Assert.strictEqual(
      await JourneyManager.hasEligibleJourneys(),
      false,
      "A non-English locale has no eligible journeys"
    );
  } finally {
    Services.locale.requestedLocales = originalRequested;
    Services.locale.availableLocales = originalAvailable;
    await JourneyStore.clear();
    await cleanupJourneyTestMemories(memories);
  }
});

add_task(function journey_suppression() {
  const now = 1000;
  for (const dismissed of [false, true]) {
    for (const dismissed_until of [null, now - 1, now, now + 1]) {
      Assert.strictEqual(
        JourneyManager.isJourneySuppressed({ dismissed, dismissed_until }, now),
        dismissed || dismissed_until === now + 1,
        `Suppression for dismissed=${dismissed}, dismissed_until=${dismissed_until}`
      );
    }
  }
});

add_task(async function stored_journey_suppression() {
  try {
    const now = 1000;
    await JourneyStore.addOrUpdateJourney({
      memoryId: "memory-1",
      headline: "Compare hotels",
      status: "Compared two hotels.",
      dismissed_until: now + 1,
    });
    const metadata = await JourneyStore.get("memory-1");
    Assert.ok(
      JourneyManager.isJourneySuppressed(metadata, now),
      "Stored timed dismissal suppresses the journey"
    );
    Assert.ok(
      !JourneyManager.isJourneySuppressed(metadata, now + 1),
      "Stored timed dismissal expires at its timestamp"
    );
    await JourneyStore.addOrUpdateJourney({
      memoryId: "memory-1",
      dismissed: true,
    });
    Assert.ok(
      JourneyManager.isJourneySuppressed(
        await JourneyStore.get("memory-1"),
        now + 1
      ),
      "Stored permanent dismissal suppresses the journey after expiry"
    );
  } finally {
    await JourneyStore.clear();
  }
});

add_task(function get_urls_to_reopen() {
  Assert.deepEqual(
    JourneyManager.getUrlsToReopen({
      memory: { id: "memory-1" },
      content: {
        previewTabs: [
          { url: "https://a.example/", title: "A" },
          { url: "https://b.example/", title: "B" },
        ],
      },
    }),
    ["https://a.example/", "https://b.example/"],
    "Returns preview tab URLs in order"
  );
  for (const suggestion of [null, {}, { content: {} }]) {
    Assert.deepEqual(
      JourneyManager.getUrlsToReopen(suggestion),
      [],
      "Returns no URLs for incomplete suggestions"
    );
  }
});

add_task(async function test_dismissed_journeys_are_not_suggested() {
  let memories = [];
  try {
    memories = await addJourneyTestMemories([makeMemory("Compare hotels")]);
    const [memory] = memories;
    for (const timed of [false, true]) {
      await JourneyStore.clear();
      await JourneyStore.addOrUpdateJourney({
        memoryId: memory.id,
        headline: "Compare hotels",
        status: "Deciding between two hotels.",
      });
      const suggestions = await JourneyManager.suggestJourneysToResume();
      Assert.deepEqual(
        suggestions.map(suggestion => suggestion.memory.id),
        [memory.id],
        "The journey is suggested before dismissal"
      );

      if (timed) {
        const until = Date.now() + 60 * 60_000;
        Assert.ok(
          await JourneyManager.dismissUntil(suggestions[0], until),
          "Snoozing the suggested journey succeeds"
        );
      } else {
        Assert.ok(
          await JourneyManager.dismiss(suggestions[0]),
          "Permanently dismissing the suggested journey succeeds"
        );
      }
      await JourneyStore.testOnlyFlushAndResetCache();

      const suggestionsAfterDismissal =
        await JourneyManager.suggestJourneysToResume();
      Assert.deepEqual(
        suggestionsAfterDismissal,
        [],
        "The dismissed journey is no longer suggested"
      );
    }
  } finally {
    await JourneyStore.clear();
    await cleanupJourneyTestMemories(memories);
  }
});

add_task(async function dismiss_and_dismiss_until() {
  try {
    for (const memoryId of ["forever", "timed"]) {
      await JourneyStore.addOrUpdateJourney({
        memoryId,
        headline: "Compare hotels",
        status: "Compared two hotels.",
      });
    }

    Assert.ok(
      await JourneyManager.dismiss({ memory: { id: "forever" } }),
      "Dismiss reports success"
    );
    Assert.ok(
      await JourneyManager.dismissUntil({ memory: { id: "timed" } }, 5000),
      "DismissUntil reports success"
    );
    Assert.ok(
      !(await JourneyManager.dismiss({ memory: { id: "unknown" } })),
      "Dismissing an unknown journey reports failure"
    );
    await Assert.rejects(
      JourneyManager.dismiss(null),
      () => true,
      "Dismiss rejects a missing suggestion"
    );
    await Assert.rejects(
      JourneyManager.dismissUntil({ memory: {} }, 5000),
      /memoryId must be a nonblank string/,
      "DismissUntil rejects a suggestion without a memory id"
    );
    await JourneyStore.testOnlyFlushAndResetCache();

    const forever = await JourneyStore.get("forever");
    Assert.strictEqual(forever.dismissed, true, "Dismiss persists");

    const timed = await JourneyStore.get("timed");
    Assert.strictEqual(timed.dismissed, false, "DismissUntil is not permanent");
    Assert.strictEqual(timed.dismissed_until, 5000, "DismissUntil persists");
    Assert.equal(
      timed.headline,
      "Compare hotels",
      "Dismissal preserves journey text"
    );
  } finally {
    await JourneyStore.clear();
  }
});
