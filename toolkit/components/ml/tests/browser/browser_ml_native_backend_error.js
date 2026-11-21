/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/// <reference path="head.js" />

// Bug 2049666. Checks that a failure raised by the backend while run() is
// already waiting for its first chunk rejects run() instead of hanging it.
// "min-p" passes request validation but is refused by LlamaBackend::Generate,
// which is the earliest point where the consumer is already waiting.
add_task(async function test_llama_backend_error_rejects_run() {
  const { cleanup } = await setup();
  try {
    const engine = await createEngine({
      taskName: "text-generation",
      modelId: "Mozilla/test-llama",
      modelFile: "TinyStories-656K.Q8_0.gguf",
      modelRevision: "main",
      backend: "llama.cpp",
    });

    await Assert.rejects(
      engine.run({
        prompt: [{ role: "user", content: "Once upon a time there was" }],
        nPredict: 4,
        samplers: [{ type: "min-p" }, { type: "dist" }],
      }),
      /Unimplemented sampler type/,
      "A backend failure inside the generation task rejects run()"
    );
  } finally {
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
});
