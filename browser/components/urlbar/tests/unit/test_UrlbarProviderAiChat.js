/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  IntentClassifier:
    "moz-src:///browser/components/aiwindow/models/IntentClassifier.sys.mjs",
  UrlbarProviderAiChat:
    "moz-src:///browser/components/urlbar/UrlbarProviderAiChat.sys.mjs",
  UrlbarSearchUtils:
    "moz-src:///browser/components/urlbar/UrlbarSearchUtils.sys.mjs",
});

const fakeEngine = {
  run({ args: [[query]] }) {
    const searchKeywords = ["search", "find"];
    const formattedPrompt = query.toLowerCase();
    const isSearch = searchKeywords.some(keyword =>
      formattedPrompt.includes(keyword)
    );

    // Simulate model confidence scores
    if (isSearch) {
      return [
        { label: "search", score: 0.95 },
        { label: "chat", score: 0.05 },
      ];
    }
    return [
      { label: "chat", score: 0.95 },
      { label: "search", score: 0.05 },
    ];
  },
};
let gEngineStub;

add_task(async function setup() {
  gEngineStub = sinon
    .stub(lazy.IntentClassifier, "_createEngine")
    .resolves(fakeEngine);
  registerCleanupFunction(() => {
    sinon.restore();
  });

  await lazy.UrlbarSearchUtils.init();

  // Set required prefs
  Services.prefs.setBoolPref("browser.ml.enable", true);
});

add_task(async function test_isActive() {
  let aiWindowStub = sinon
    .stub(lazy.AIWindow, "isAIWindowActiveAndEnabled")
    .returns(false);
  const provider = new lazy.UrlbarProviderAiChat();
  let controller = UrlbarTestUtils.mockChildController();

  Assert.ok(
    !(await provider.isActive(createContext("hello"), controller)),
    "Provider is not active as this is not a Smart Window"
  );

  aiWindowStub.returns(true);
  Assert.ok(
    await provider.isActive(createContext("hello"), controller),
    "Provider is active"
  );

  Assert.equal(
    await provider.isActive(
      createContext("hello world", {
        searchMode: { engineName: "testEngine" },
      }),
      controller
    ),
    UrlbarPrefs.get("unifiedSearchButton.historyInSearchMode"),
    "Provider is only active in engine Search Mode with historyInSearchMode"
  );

  Assert.ok(
    !(await provider.isActive(createContext("hi"), controller)),
    "Provider is not active as prompt is too short"
  );
});

add_task(async function test_startQuery() {
  const provider = new lazy.UrlbarProviderAiChat();
  let engine = lazy.UrlbarSearchUtils.getDefaultEngine();
  let icon = await engine.getIconURL();

  const TESTS = [
    {
      desc: "Urlbar for 'chat' intent returns non-heuristic with suggestedIndex",
      sapName: "urlbar",
      input: "tell me a joke",
      expectedResults: [
        {
          heuristic: false,
          type: UrlbarShared.RESULT_TYPE.AI_CHAT,
          suggestedIndex: 1,
          query: "tell me a joke",
          icon: lazy.UrlbarProviderAiChat.CHAT_ICON_URL,
          title: "tell me a joke",
        },
      ],
    },
    {
      desc: "Urlbar for 'search' intent doesn't return any result",
      sapName: "urlbar",
      input: "search for cute cat pictures",
      expectedResults: [],
    },
    {
      desc: "Urlbar for 'navigate' intent doesn't return any result",
      sapName: "urlbar",
      input: "mozilla.org",
      expectedResults: [],
    },
    {
      desc: "Smartbar for 'search' intent returns a non heuristic result",
      sapName: "smartbar",
      input: "search for cute cat pictures",
      expectedResults: [
        {
          heuristic: false,
          type: UrlbarShared.RESULT_TYPE.AI_CHAT,
          suggestedIndex: 1,
          query: "search for cute cat pictures",
          icon: lazy.UrlbarProviderAiChat.CHAT_ICON_URL,
          title: "search for cute cat pictures",
        },
      ],
    },
    {
      desc: "Smartbar for 'chat' intent returns heuristic and search result",
      sapName: "smartbar",
      input: "tell me a joke",
      expectedResults: [
        {
          heuristic: true,
          type: UrlbarShared.RESULT_TYPE.AI_CHAT,
          suggestedIndex: undefined,
          query: "tell me a joke",
          icon: lazy.UrlbarProviderAiChat.CHAT_ICON_URL,
          title: "tell me a joke",
        },
        {
          heuristic: false,
          type: UrlbarShared.RESULT_TYPE.SEARCH,
          suggestedIndex: 1,
          query: "tell me a joke",
          icon,
          engine: engine.name,
          title: "tell me a joke",
        },
      ],
    },
    {
      desc: "Smartbar for 'navigate' intent doesn't return any result",
      sapName: "smartbar",
      input: "mozilla.org",
      expectedResults: [],
    },
  ];

  for (const { desc, sapName, input, expectedResults } of TESTS) {
    info(desc);

    let added = [];
    await provider.startQuery(
      createContext(input, {
        sapName,
      }),
      (_provider, result) => {
        added.push(result);
      }
    );

    Assert.equal(
      added.length,
      expectedResults.length,
      "Check number of results"
    );

    for (let i = 0; i < expectedResults.length; i++) {
      let result = added[i];
      let expected = expectedResults[i];

      for (let prop in expected) {
        let checkFn =
          expected[prop] === undefined
            ? Assert.strictEqual.bind(Assert)
            : Assert.equal.bind(Assert);
        let checkObj = ["engine", "icon", "query", "title"].includes(prop)
          ? result.payload
          : result;

        checkFn(
          checkObj[prop],
          expected[prop],
          `Check result ${i} property ${prop}`
        );
      }
    }
  }
});

add_task(async function test_urlbar_ask_shown_when_pinned_to_search() {
  const PREF = "browser.smartwindow.smartbar.intent";
  Services.prefs.setStringPref(PREF, "search");
  gEngineStub.resetHistory();
  const provider = new lazy.UrlbarProviderAiChat();
  try {
    for (let input of ["tell me a joke", "search for cute cat pictures"]) {
      let added = [];
      await provider.startQuery(
        createContext(input, { sapName: "urlbar" }),
        (_provider, result) => added.push(result)
      );
      Assert.equal(added.length, 1, `Ask row is shown for "${input}"`);
      Assert.equal(added[0].type, UrlbarShared.RESULT_TYPE.AI_CHAT);
      Assert.equal(added[0].heuristic, false);
      Assert.equal(added[0].suggestedIndex, 1);
    }

    let added = [];
    await provider.startQuery(
      createContext("mozilla.org", { sapName: "urlbar" }),
      (_provider, result) => added.push(result)
    );
    Assert.equal(added.length, 0, "URLs still do not show Ask");

    added = [];
    await provider.startQuery(
      createContext("tell me a joke", { sapName: "smartbar" }),
      (_provider, result) => added.push(result)
    );
    Assert.equal(
      added.length,
      1,
      "The smart bar still gets a non-heuristic chat row"
    );
    Assert.equal(added[0].heuristic, false);
    Assert.ok(
      gEngineStub.notCalled,
      "A search pin does not run the intent model"
    );
  } finally {
    Services.prefs.clearUserPref(PREF);
  }
});

add_task(async function test_intentClassifierThrows() {
  const provider = new lazy.UrlbarProviderAiChat();

  info("Stub the engine to throw, we should fallback to 'search' intent.");
  const throwingEngine = {
    run() {
      throw new Error("Intent classifier failure");
    },
  };
  gEngineStub.resolves(throwingEngine);

  let added = [];
  await provider.startQuery(
    createContext("tell me a joke", {
      sapName: "smartbar",
    }),
    (_provider, result) => {
      added.push(result);
    }
  );

  Assert.equal(
    added.length,
    1,
    "One result is returned, because we fallback to 'search' intent"
  );
  let result = added[0];
  Assert.equal(
    result.payload.query,
    "tell me a joke",
    "The result has the expected query"
  );
  Assert.equal(
    result.type,
    UrlbarShared.RESULT_TYPE.AI_CHAT,
    "The result is a chat result"
  );
  Assert.equal(
    result.suggestedIndex,
    1,
    "The result is non-heuristic (suggestedIndex is set)"
  );
  Assert.equal(
    result.heuristic,
    false,
    "The result is non-heuristic (heuristic is false)"
  );
});
