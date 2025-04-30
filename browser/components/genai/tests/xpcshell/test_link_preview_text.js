/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { LinkPreviewModel } = ChromeUtils.importESModule(
  "moz-src:///browser/components/genai/LinkPreviewModel.sys.mjs"
);
const { SentencePostProcessor } = ChromeUtils.importESModule(
  "moz-src:///browser/components/genai/LinkPreviewModel.sys.mjs"
);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { BlockListManager } = ChromeUtils.importESModule(
  "chrome://global/content/ml/Utils.sys.mjs"
);

/**
 * Test that text is processed for ai.
 */
add_task(function test_text_processing() {
  const text = "This is a test sentence. ";
  Assert.equal(
    LinkPreviewModel.preprocessText(text),
    "This is a test sentence.",
    "basic sentence should be trimmed"
  );
  Assert.equal(
    LinkPreviewModel.preprocessText("Too short."),
    "",
    "short sentence should be removed"
  );
  Assert.equal(
    LinkPreviewModel.preprocessText("Today is Mar. 12, 2025."),
    "Today is Mar. 12, 2025.",
    "abbreviations don't break sentences"
  );
  Assert.equal(
    LinkPreviewModel.preprocessText("Thisisatestsentence but fewwords."),
    "",
    "needs enough words"
  );
  Assert.equal(
    LinkPreviewModel.preprocessText(
      "This is a test sentence without punctuation"
    ),
    "",
    "needs punctuation"
  );
  Assert.equal(
    LinkPreviewModel.preprocessText(`${text}
    Too short.
    Today is Mar. 12, 2025.
    Thisisatestsentence but fewwords.
    This is a test sentence without punctuation`),
    "This is a test sentence. Today is Mar. 12, 2025.",
    "multiple sentences should be processed"
  );

  Assert.equal(
    LinkPreviewModel.preprocessText(`${text}

     Short \t\t\t\t\t\t\t\t\t\t                      without spaces.



      It has           \t \t \t multiple consecutive spaces in between words.
      They should all go away
   `),
    "This is a test sentence. It has multiple consecutive spaces in between words.",
    "Remove consecutive spaces by single."
  );

  Assert.equal(
    LinkPreviewModel.preprocessText(text.repeat(100)),
    text.repeat(6).trim(),
    "restrict to 6 sentences"
  );
});

/**
 * Test that prefs affect text processing.
 */
add_task(function test_text_processing_prefs() {
  const text = "This is a test sentence. ";
  Services.prefs.setIntPref("browser.ml.linkPreview.inputSentences", 3);
  Assert.equal(
    LinkPreviewModel.preprocessText(text.repeat(100)),
    text.repeat(3).trim(),
    "restrict to 3 sentences"
  );
  Services.prefs.clearUserPref("browser.ml.linkPreview.inputSentences");
});

/**
 * Test that post processor provides sentences.
 */
add_task(function test_sentence_post_processor() {
  const processor = new SentencePostProcessor();
  Assert.deepEqual(
    processor.put("Hello "),
    { sentence: "", abort: false },
    "no sentence yet"
  );
  Assert.deepEqual(
    processor.put("world. "),
    { sentence: "", abort: false },
    "sentence complete but not next"
  );
  Assert.deepEqual(
    processor.put("How "),
    { sentence: "Hello world. ", abort: false },
    "previous sentence complete"
  );
  Assert.deepEqual(
    processor.put("are you today? I'm"),
    { sentence: "How are you today? ", abort: false },
    "question complete"
  );
  Assert.deepEqual(
    processor.put(" fine. And"),
    { sentence: "I'm fine. ", abort: true },
    "response complete"
  );
  Assert.deepEqual(
    processor.put("you? Good"),
    { sentence: "", abort: true },
    "hit limit"
  );
  Assert.equal(processor.flush(), "", "still nothing at limit");
});

/**
 * Test that generateTextAI works properly with the blocklist
 */
add_task(async function test_generateAI_with_blocklist() {
  // Mocked ML Engine
  const engine = {
    async *runWithGenerator() {
      const preview =
        "Hello world, I am here and would like to make a sentence. Now, more sentences are coming.  Even more are raining here. Bye";

      for (const text of preview) {
        yield { text };
      }
    },

    terminate() {},
  };

  // Mocked Blocked List Manager
  let manager = new BlockListManager({
    blockNgrams: [BlockListManager.encodeBase64("hello")],
    language: "en",
  });

  // Disable block list
  Services.prefs.setBoolPref("browser.ml.linkPreview.blockListEnabled", false);

  let createEngineStub = sinon
    .stub(LinkPreviewModel, "createEngine")
    .returns(engine);

  let managerStub = sinon
    .stub(BlockListManager, "initializeFromRemoteSettings")
    .returns(manager);

  let numOutputs = 0;

  await LinkPreviewModel.generateTextAI(
    "This is the big article. Now give me its preview. Please.",
    {
      onText: () => {
        numOutputs += 1;
      },
    }
  );

  Assert.equal(
    numOutputs,
    3,
    "should output all sentences when block list is disabled"
  );

  // Enable block list
  Services.prefs.setBoolPref("browser.ml.linkPreview.blockListEnabled", true);

  numOutputs = 0;

  await LinkPreviewModel.generateTextAI(
    "This is the big article. Now give me its preview. Please.",
    {
      onText: () => {
        numOutputs += 1;
      },
    }
  );

  Assert.equal(
    numOutputs,
    0,
    "Should output no sentences when 1st sentence contains block word and block list enabled."
  );

  managerStub.restore();
  manager = new BlockListManager({
    blockNgrams: [BlockListManager.encodeBase64("coming")],
    language: "en",
  });
  managerStub = sinon
    .stub(BlockListManager, "initializeFromRemoteSettings")
    .returns(manager);

  // Force link preview to reload blockList manager
  Services.prefs.setBoolPref("browser.ml.linkPreview.blockListEnabled", false);
  await LinkPreviewModel.generateTextAI(
    "This is the big article. Now give me its preview. Please."
  );

  Assert.equal(LinkPreviewModel.blockListManager, null);

  // Now re-enable block list
  Services.prefs.setBoolPref("browser.ml.linkPreview.blockListEnabled", true);

  numOutputs = 0;
  await LinkPreviewModel.generateTextAI(
    "This is the big article. Now give me its preview. Please.",
    {
      onText: () => {
        numOutputs += 1;
      },
    }
  );

  Assert.equal(
    numOutputs,
    1,
    "Should output 1 sentence when blocked word found 1st time in 2nd sentence and block list enabled."
  );

  Services.prefs.setBoolPref("browser.ml.linkPreview.blockListEnabled", false);

  numOutputs = 0;

  await LinkPreviewModel.generateTextAI(
    "This is the big article. Now give me its preview. Please.",
    {
      onText: () => {
        numOutputs += 1;
      },
    }
  );

  Assert.equal(
    numOutputs,
    3,
    "all sentences should be outputted when block list disabled"
  );

  Services.prefs.clearUserPref("browser.ml.linkPreview.blockListEnabled");
  createEngineStub.restore();
  managerStub.restore();
});

/**
 * Test post processor respects limits.
 */
add_task(function test_sentence_post_processor_limits() {
  const processor = new SentencePostProcessor({
    maxNumOutputSentences: 1,
  });
  Assert.deepEqual(
    processor.put("Hi. There. "),
    { sentence: "Hi. ", abort: true },
    "first sentence"
  );
  Assert.deepEqual(
    processor.put("Nope."),
    { sentence: "", abort: true },
    "no more sentences"
  );

  Services.prefs.setIntPref("browser.ml.linkPreview.outputSentences", 2);
  const viaPref = new SentencePostProcessor();
  Assert.deepEqual(
    viaPref.put("Hi. There. "),
    { sentence: "Hi. ", abort: false },
    "first sentence"
  );
  Assert.deepEqual(
    viaPref.put("Yup. "),
    { sentence: "There. ", abort: true },
    "second sentence"
  );
  Assert.deepEqual(
    viaPref.put("Nope."),
    { sentence: "", abort: true },
    "no more sentences"
  );
  Services.prefs.clearUserPref("browser.ml.linkPreview.outputSentences");
});
