/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const {
  IntentClassifier,
  normalizeTextForChatAllowlist,
  tokenizeTextForChatAllowlist,
  buildChatAllowlist,
  makeIsolatedPhraseChecker,
  DEFAULT_INTENT_MODEL,
  INTENT_MODEL_ENGLISH_FRENCH,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/IntentClassifier.sys.mjs"
);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { Region } = ChromeUtils.importESModule(
  "resource://gre/modules/Region.sys.mjs"
);

const { NimbusFeatures } = ChromeUtils.importESModule(
  "resource://nimbus/ExperimentAPI.sys.mjs"
);
const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);

const SMARTBAR_INTENT_PREF = "browser.smartwindow.smartbar.intent";

NimbusTestUtils.init(this);

add_task(async function test_getPromptIntent_basic() {
  const sb = sinon.createSandbox();
  try {
    const cases = [
      { prompt: "please search for news on firefox", expected: "search" },
      {
        prompt: "Can you FIND me the docs for PageAssist?",
        expected: "search",
      }, // case-insensitive
      { prompt: "look up the best pizza in SF", expected: "search" },
      { prompt: "hello there, how are you?", expected: "chat" },
      { prompt: "tell me a joke", expected: "chat" },
    ];

    const fakeEngine = {
      run({ args: [[query]] }) {
        const searchKeywords = [
          "search",
          "find",
          "look",
          "query",
          "locate",
          "explore",
        ];
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

    sb.stub(IntentClassifier, "_createEngine").resolves(fakeEngine);

    for (const { prompt, expected } of cases) {
      const intent = await IntentClassifier.getPromptIntent(prompt);
      Assert.equal(
        intent,
        expected,
        `getPromptIntent("${prompt}") should return "${expected}"`
      );
    }
  } finally {
    sb.restore();
  }
});

add_task(function test_isForcedChat_is_model_aware() {
  // English phrases are forced-chat for both models.
  Assert.ok(
    IntentClassifier._isForcedChat("hello there", DEFAULT_INTENT_MODEL),
    "English phrase is forced-chat for the default model"
  );
  Assert.ok(
    IntentClassifier._isForcedChat("hello there", INTENT_MODEL_ENGLISH_FRENCH),
    "English phrase is still forced-chat for the French/English model"
  );

  // French phrases are only forced-chat for the French/English model.
  Assert.ok(
    IntentClassifier._isForcedChat("bonjour", INTENT_MODEL_ENGLISH_FRENCH),
    "French phrase is forced-chat for the French/English model"
  );
  Assert.ok(
    IntentClassifier._isForcedChat(
      "raconte une blague maintenant",
      INTENT_MODEL_ENGLISH_FRENCH
    ),
    "Multiword French phrase matches when embedded in a longer query"
  );
  Assert.ok(
    !IntentClassifier._isForcedChat("bonjour", DEFAULT_INTENT_MODEL),
    "French phrase is not forced-chat for the default (English) model"
  );

  // Hyphenated French spellings match their tokenized form.
  Assert.ok(
    IntentClassifier._isForcedChat("qui es-tu", INTENT_MODEL_ENGLISH_FRENCH),
    "Hyphenated French phrase matches"
  );

  // Unaccented input matches an accented French phrase (diacritic folding).
  Assert.ok(
    IntentClassifier._isForcedChat(
      "ecris un poeme",
      INTENT_MODEL_ENGLISH_FRENCH
    ),
    "Unaccented French input matches the accented allowlist phrase"
  );
});

add_task(async function test_getPromptIntent_override_skips_classifier() {
  const sb = sinon.createSandbox();
  try {
    const createEngineStub = sb
      .stub(IntentClassifier, "_createEngine")
      .resolves({
        run() {
          return [
            { label: "search", score: 0.99 },
            { label: "chat", score: 0.01 },
          ];
        },
      });

    Services.prefs.setStringPref(SMARTBAR_INTENT_PREF, "search");
    Assert.equal(
      await IntentClassifier.getPromptIntent("tell me a joke"),
      "search",
      "A search override wins over a forced-chat phrase"
    );
    Assert.ok(
      createEngineStub.notCalled,
      "A search override does not load the intent model"
    );

    Services.prefs.setStringPref(SMARTBAR_INTENT_PREF, "chat");
    Assert.equal(
      await IntentClassifier.getPromptIntent("search for news"),
      "chat",
      "A chat override wins over a search-shaped query"
    );
    Assert.ok(
      createEngineStub.notCalled,
      "A chat override does not load the intent model"
    );

    Services.prefs.setStringPref(SMARTBAR_INTENT_PREF, "");
    Assert.equal(
      await IntentClassifier.getPromptIntent("search for news"),
      "search",
      "An empty override uses the classifier"
    );
    Assert.ok(createEngineStub.calledOnce, "The classifier still runs");
  } finally {
    Services.prefs.clearUserPref(SMARTBAR_INTENT_PREF);
    sb.restore();
  }
});

add_task(async function test_getPromptIntent_nimbus_override() {
  const sb = sinon.createSandbox();
  const { cleanup } = await NimbusTestUtils.setupTest();
  try {
    sb.stub(IntentClassifier, "_createEngine").resolves({
      run() {
        return [
          { label: "chat", score: 0.99 },
          { label: "search", score: 0.01 },
        ];
      },
    });

    Assert.equal(
      NimbusFeatures.smartWindow.getVariable("smartbarIntent"),
      null,
      "smartbarIntent is null before enrollment"
    );

    const unenroll = await NimbusTestUtils.enrollWithFeatureConfig({
      featureId: "smartWindow",
      value: { smartbarIntent: "search" },
    });

    Assert.equal(
      await IntentClassifier.getPromptIntent("tell me a joke"),
      "search",
      "An enrollment pins the smart bar to search"
    );
    Assert.ok(
      !Services.prefs.prefHasUserValue(SMARTBAR_INTENT_PREF),
      "The fallbackPref enrollment does not write the pref"
    );

    await unenroll();

    Assert.equal(
      NimbusFeatures.smartWindow.getVariable("smartbarIntent"),
      null,
      "smartbarIntent is null after unenrollment"
    );
    Assert.equal(
      await IntentClassifier.getPromptIntent("search for news"),
      "chat",
      "Unenrollment uses the classifier again"
    );
  } finally {
    await cleanup();
    sb.restore();
  }
});

add_task(async function test_getPromptIntent_selects_model_by_region() {
  const sb = sinon.createSandbox();
  const originalHome = Region.home;
  try {
    const fakeEngine = {
      run() {
        return [
          { label: "search", score: 0.95 },
          { label: "chat", score: 0.05 },
        ];
      },
    };
    const createEngineStub = sb
      .stub(IntentClassifier, "_createEngine")
      .resolves(fakeEngine);

    const cases = [
      {
        region: "US",
        modelId: DEFAULT_INTENT_MODEL,
        featureId: "smart-intent",
      },
      {
        region: "FR",
        modelId: INTENT_MODEL_ENGLISH_FRENCH,
        featureId: "smart-intent-en-fr",
      },
    ];

    for (const { region, modelId, featureId } of cases) {
      Region._setHomeRegion(region, false);
      createEngineStub.resetHistory();

      // A query not on the forced-chat allowlist so the engine is used.
      await IntentClassifier.getPromptIntent("find me a restaurant");

      Assert.ok(
        createEngineStub.calledOnce,
        `Engine created once for region ${region}`
      );
      const opts = createEngineStub.firstCall.args[0];
      Assert.equal(
        opts.modelId,
        modelId,
        `Region ${region} should use model ${modelId}`
      );
      Assert.equal(
        opts.featureId,
        featureId,
        `Region ${region} should use feature ${featureId}`
      );
      Assert.equal(
        opts.backend,
        "best-onnx",
        `Region ${region} should use the best-onnx backend`
      );
    }
  } finally {
    if (originalHome) {
      Region._setHomeRegion(originalHome, false);
    }
    sb.restore();
  }
});

add_task(async function test_preprocessQuery_removes_question_marks() {
  // Call the real helper on the classifier
  const cases = [
    { input: "hello?", expected: "hello" },
    { input: "?prompt", expected: "prompt" },
    { input: "multiple???", expected: "multiple" },
    { input: "mid?dle", expected: "middle" },
    { input: "question? ", expected: "question" },
    { input: " no?  spaces? ", expected: "no  spaces" },
    { input: "???", expected: "" },
    { input: "clean input", expected: "clean input" },
  ];

  for (const { input, expected } of cases) {
    const result = IntentClassifier._preprocessQuery(input);
    Assert.equal(
      result,
      expected,
      `Expected "${input}" to preprocess to "${expected}", got "${result}"`
    );
  }
});

add_task(function test_normalizeTextForChatAllowlist_basic() {
  // lowercasing + trimming + collapsing internal spaces
  Assert.equal(
    normalizeTextForChatAllowlist("  HeLLo   There  "),
    "hello there",
    "Should lowercase, trim, and collapse spaces"
  );

  // NFKC normalization: compatibility forms → canonical
  // Fullwidth characters normalize: e.g., 'ＴＥＳＴ' → 'test'
  Assert.equal(
    normalizeTextForChatAllowlist("ＴＥＳＴ  １２３"),
    "test 123",
    "Should NFKC-normalize fullwidth letters/digits"
  );

  // Multiple whitespace kinds (NBSP, tabs, newlines) collapse
  Assert.equal(
    normalizeTextForChatAllowlist("a\u00A0b\tc\nd"),
    "a b c d",
    "Should collapse all whitespace kinds to single spaces"
  );

  // Diacritics are folded so accented and unaccented spellings match
  Assert.equal(
    normalizeTextForChatAllowlist("\u00C9cris un po\u00E8me"),
    "ecris un poeme",
    "Should fold French accents"
  );
  Assert.equal(
    normalizeTextForChatAllowlist("\u00E7a va"),
    normalizeTextForChatAllowlist("ca va"),
    "Accented and unaccented spellings should normalize identically"
  );
});

add_task(function test_tokenizeTextForChatAllowlist_unicode_and_boundaries() {
  // Splits on non-word chars, keeps letters/digits/underscore
  Assert.deepEqual(
    tokenizeTextForChatAllowlist("hello, world! 42_times"),
    ["hello", "world", "42_times"],
    "Should split on punctuation and keep underscores"
  );

  // Unicode letters are word chars (\p{L}); diacritics are folded away
  Assert.deepEqual(
    tokenizeTextForChatAllowlist("mañana—café!"),
    ["manana", "cafe"],
    "Should split on punctuation (em dash, bang) and fold accents"
  );

  // Apostrophes split (non-word), as intended
  Assert.deepEqual(
    tokenizeTextForChatAllowlist("what's up"),
    ["what", "s", "up"],
    "Apostrophes are separators, so tokens split around them"
  );
});

add_task(function test_buildChatAllowlist_grouping_and_normalization() {
  const phrases = [
    "sup",
    "hi there", // 2 tokens
    "what's up", // becomes "what s up" (3 tokens)
    " foo   bar  ", // leading/trailing + multiple spaces
    "", // empty should be skipped
    "___", // token of underscores counts as 1 token
  ];
  const sets = buildChatAllowlist(phrases);

  // Expect keys for lengths: 1, 2, 3
  Assert.ok(sets.has(1), "Should have set for single-token phrases");
  Assert.ok(sets.has(2), "Should have set for two-token phrases");
  Assert.ok(sets.has(3), "Should have set for three-token phrases");

  // 1-token set contains: "sup", "___"
  Assert.ok(sets.get(1).has("sup"), "Single-token set should contain 'sup'");
  Assert.ok(sets.get(1).has("___"), "Single-token set should contain '___'");

  // 2-token set contains normalized "hi there" and "foo bar"
  Assert.ok(
    sets.get(2).has("hi there"),
    "Two-token set should contain 'hi there'"
  );
  Assert.ok(
    sets.get(2).has("foo bar"),
    "Two-token set should contain normalized 'foo bar'"
  );

  // 3-token set contains "what s up" (note apostrophe split)
  Assert.ok(
    sets.get(3).has("what s up"),
    "Three-token set should contain 'what s up'"
  );

  // Empty phrase skipped: nothing added for length 0
  for (const [k, set] of sets) {
    Assert.ok(
      k > 0 && set.size >= 1,
      "No empty keys, each set has at least one entry"
    );
  }
});

add_task(function test_isolated_phrase_checker_single_word_boundaries() {
  const phrases = ["sup", "hello", "___"];
  const isForced = makeIsolatedPhraseChecker(phrases);

  // Positive: exact token present
  Assert.ok(
    isForced("sup bro"),
    "Should match 'sup' as an isolated token at start"
  );
  Assert.ok(
    isForced("hey, hello there"),
    "Should match 'hello' surrounded by punctuation"
  );
  Assert.ok(isForced("foo ___ bar"), "Should match token with underscores");

  // Negative: partial-word should NOT match
  Assert.ok(
    !isForced("supposingly, this should not match"),
    "No partial-word match for 'sup'"
  );
  Assert.ok(!isForced("supper time"), "No partial-word match inside 'supper'");
  Assert.ok(!isForced("shelloworld"), "No partial-word match for 'hello'");
});

add_task(function test_isolated_phrase_checker_multiword_and_punctuation() {
  // Multiword phrases; apostrophes become token splits -> "what's up" => "what s up"
  const phrases = ["hi there", "what's up"];
  const isForced = makeIsolatedPhraseChecker(phrases);

  // Positive: punctuation between words should still match (token split)
  Assert.ok(
    isForced("hi—there!"),
    "Em dash between words should match 'hi there'"
  );
  Assert.ok(
    isForced("well, hi there!!"),
    "Punctuation around phrase should match"
  );
  Assert.ok(
    isForced("so, what’s up today?"),
    "Curly apostrophe splits to tokens; should match 'what s up'"
  );

  // Negative: glued words should not match
  Assert.ok(
    !isForced("hithere"),
    "Concatenated words should not match 'hi there'"
  );
  Assert.ok(
    !isForced("whatssup"),
    "Should not match 'what s up' without separators"
  );
});

add_task(function test_isolated_phrase_checker_spacing_and_unicode_norm() {
  const phrases = ["good morning", "hello"];
  const isForced = makeIsolatedPhraseChecker(phrases);

  // Multiple spaces collapse
  Assert.ok(
    isForced("good     morning everyone"),
    "Multiple spaces between tokens should still match"
  );

  // Fullwidth / NFKC normalization (ＴＥＳＴ) and basic usage
  Assert.ok(
    isForced("  HELLO  "),
    "Case and surrounding spaces should normalize and match 'hello'"
  );

  // Non-breaking spaces and tabs
  Assert.ok(
    isForced("good\u00A0morning\tteam"),
    "NBSP and tabs normalize and match"
  );
});

add_task(function test_isolated_phrase_checker_no_match_cases() {
  const phrases = ["hi there", "sup"];
  const isForced = makeIsolatedPhraseChecker(phrases);

  Assert.ok(!isForced(""), "Empty string should not match");
  Assert.ok(
    !isForced("nothing to see here"),
    "Unrelated text should not match"
  );
  Assert.ok(
    !isForced("support"),
    "Partial token with 'sup' prefix should not match"
  );
});

add_task(function test_isolated_phrase_checker_caching_stability() {
  const phrases = ["hello", "hi there"];
  const isForced = makeIsolatedPhraseChecker(phrases);

  // Repeated calls with the same input should return identical results (cache sanity)
  const q1 = "Hello there!";
  const first = isForced(q1);
  const second = isForced(q1);
  Assert.equal(
    first,
    second,
    "Same query should yield identical result across calls (cache-stable)"
  );

  // Different whitespace should normalize to the same outcome
  Assert.equal(
    isForced("  hello   there "),
    isForced("hello there"),
    "Whitespace variations should not affect result"
  );
});
