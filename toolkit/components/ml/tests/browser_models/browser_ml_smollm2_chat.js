/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* global HWInference */

const { BACKENDS, EngineProcess, PipelineOptions, createEngine } =
  ChromeUtils.importESModule(
    "chrome://global/content/ml/EngineProcess.sys.mjs"
  );

const { runOnBothInferenceProcesses } = ChromeUtils.importESModule(
  "resource://testing-common/MLTestUtils.sys.mjs"
).MLTestUtils;

// CI sets this on workers that have an accelerator llama.cpp can use.
const EXPECT_ACCELERATOR =
  Services.env.get("MOZ_ML_EXPECT_ACCELERATOR") === "1";

// A cold-start CI worker has to fetch a ~386 MB GGUF before inference can run,
// so allow more than the default 45 s browser-chrome per-test timeout.
requestLongerTimeout(4);

async function runChat(device) {
  const engine = await createEngine(
    new PipelineOptions({
      engineId: "smollm2-chat-e2e",
      taskName: "text-generation",
      backend: BACKENDS.llamaCpp,
      modelId: "HuggingFaceTB/SmolLM2-360M-Instruct-GGUF",
      modelRevision: "main",
      modelHubUrlTemplate: "{model}/{revision}",
      modelHubRootUrl: "https://model-hub.mozilla.org/",
      modelFile: "smollm2-360m-instruct-q8_0.gguf",
      device,
    })
  );
  info("Engine created");

  const prompt = [
    {
      role: "system",
      content: "You are a helpful assistant. Answer briefly.",
    },
    {
      role: "user",
      content: "What color is a clear daytime sky? Answer in one word.",
    },
  ];
  const request = { prompt, nPredict: 24 };

  let text = "";
  let metrics;

  try {
    const generator = engine.runWithGenerator(request);
    let result;
    do {
      result = await generator.next();
      if (result.done) {
        metrics = result.value?.metrics;
      } else if (!result.value.isPrompt) {
        text += result.value.text ?? "";
      }
    } while (!result.done);

    info(`SmolLM2 chat output: ${text.trim()}`);
    info(`SmolLM2 chat metrics: ${JSON.stringify(metrics)}`);

    Assert.greater(text.trim().length, 0, "Model produced non-empty output.");
    Assert.ok(metrics, "The run returned metrics.");
    Assert.greater(metrics.outputTokens, 0, "Real tokens were decoded.");
    const promptText = prompt.map(m => m.content).join(" ");
    Assert.notEqual(
      text.trim(),
      promptText,
      "Output is not just the prompt echoed."
    );
  } finally {
    await engine.terminate();
    await EngineProcess.destroyMLEngine();
  }
}

add_task(async function test_smollm2_real_chat_generation() {
  info(`MOZ_ML_EXPECT_ACCELERATOR: ${EXPECT_ACCELERATOR}`);
  await runOnBothInferenceProcesses(async hwInference => {
    const placements = [null];
    if (hwInference) {
      info(`Devices: ${JSON.stringify(await HWInference.listDevices())}`);
      if (EXPECT_ACCELERATOR) {
        placements.push({ kinds: ["gpu"] });
      }
    }
    for (const device of placements) {
      info(
        `Chat in the ${hwInference ? "HWInference" : "inference content"} ` +
          `process, placement: ${JSON.stringify(device)}`
      );
      await runChat(device);
    }
  });
});
