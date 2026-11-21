/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* global TextGenerator */

add_task(async function test_textgenerator_webidl_surface() {
  const generator = await createTinyStoriesGenerator();
  let firstPromptTokens;
  try {
    let streamed = "";
    const result = await generator.generate(
      {
        messages: TINYSTORIES_STORYTELLER_PROMPT,
        maxTokens: 16,
        bufferLength: 4,
        samplers: TINYSTORIES_GREEDY_SAMPLERS,
      },
      text => {
        streamed += text;
      }
    );
    firstPromptTokens = result.usage.promptTokens;

    info(`Generated: ${result.content}`);
    Assert.greater(result.content.length, 0, "Generation produced text");
    Assert.equal(
      streamed,
      result.content,
      "Streamed deltas join to the full content"
    );
    Assert.greater(result.usage.promptTokens, 0, "promptTokens populated");
    Assert.greater(
      result.usage.generatedTokens,
      0,
      "generatedTokens populated"
    );
    Assert.ok(
      ["eos", "length", "stop-token"].includes(result.reason),
      `Finish reason is sane (got ${result.reason})`
    );

    Assert.greaterOrEqual(
      result.resources.after.cpuTimeMs,
      result.resources.before.cpuTimeMs,
      "CPU time is a counter that only moves forward across a generation"
    );
    Assert.greater(
      result.resources.after.memoryBytes,
      0,
      "The generator process reports its own memory"
    );

    const second = await generator.generate({
      messages: TINYSTORIES_STORYTELLER_PROMPT,
      maxTokens: 8,
      samplers: TINYSTORIES_GREEDY_SAMPLERS,
    });
    Assert.greater(
      second.usage.promptTokens,
      firstPromptTokens,
      "Second generate prefills the accumulated history"
    );

    generator.clear();
    const afterClear = await generator.generate({
      messages: TINYSTORIES_STORYTELLER_PROMPT,
      maxTokens: 8,
      samplers: TINYSTORIES_GREEDY_SAMPLERS,
    });
    Assert.equal(
      afterClear.usage.promptTokens,
      firstPromptTokens,
      "clear() empties the history: promptTokens is back to the first " +
        "run's value"
    );
  } finally {
    generator.terminate();
  }

  await Assert.rejects(
    generator.generate({ messages: TINYSTORIES_STORYTELLER_PROMPT }),
    /terminated/,
    "Generate on a terminated generator rejects"
  );
});

// maxTokens is an unsigned long, and neither end of that range may turn into
// a budget the caller did not ask for.
add_task(async function test_textgenerator_max_tokens_bounds() {
  const generator = await createTinyStoriesGenerator();
  try {
    const none = await generator.generate({
      messages: TINYSTORIES_STORYTELLER_PROMPT,
      maxTokens: 0,
      samplers: TINYSTORIES_GREEDY_SAMPLERS,
    });
    Assert.equal(
      none.usage.generatedTokens,
      0,
      "maxTokens: 0 generates nothing"
    );
    Assert.equal(none.content, "", "maxTokens: 0 produces no content");

    generator.clear();

    const huge = await generator.generate({
      messages: TINYSTORIES_STORYTELLER_PROMPT,
      maxTokens: 0xffffffff,
      samplers: TINYSTORIES_GREEDY_SAMPLERS,
    });
    info(`reason=${huge.reason} generated=${huge.usage.generatedTokens}`);
    Assert.greater(
      huge.usage.generatedTokens,
      1,
      "The largest legal budget does not cap the run at a single token"
    );
    Assert.lessOrEqual(
      huge.usage.generatedTokens,
      TINYSTORIES_CONTEXT_SIZE,
      "A budget past the context is served up to the context, not beyond"
    );
  } finally {
    generator.terminate();
  }
});

add_task(async function test_textgenerator_cancel() {
  const maxTokens = 4000;
  const generator = await createTinyStoriesGenerator({ contextSize: 4096 });
  try {
    let streamed = "";
    let cancelRequested = false;
    const result = await generator.generate(
      {
        messages: TINYSTORIES_STORYTELLER_PROMPT,
        maxTokens,
        stopOnEndOfGenerationTokens: false,
        bufferLength: 1,
        samplers: TINYSTORIES_GREEDY_SAMPLERS,
      },
      text => {
        streamed += text;
        if (!cancelRequested) {
          cancelRequested = true;
          generator.cancel();
        }
      }
    );

    Assert.equal(
      result.reason,
      "cancelled",
      "A cancelled generation resolves with reason 'cancelled'"
    );
    Assert.equal(
      result.content,
      streamed,
      "The cancelled result carries exactly the streamed deltas"
    );
    Assert.less(
      result.usage.generatedTokens,
      maxTokens,
      "Cancel stopped the generation before maxTokens"
    );
  } finally {
    generator.terminate();
  }
});

add_task(async function test_thread_count_limit() {
  for (const option of ["numThreads", "numThreadsDecoding"]) {
    await Assert.rejects(
      createTinyStoriesGenerator({ [option]: 513 }),
      /thread count exceeds/,
      `${option} rejects values above the backend limit`
    );
    await Assert.rejects(
      createTinyStoriesGenerator({ [option]: 0xffffffff }),
      /thread count exceeds/,
      `${option} cannot overflow the backend's signed thread count`
    );
  }
});

add_task(async function test_prompt_code_points() {
  const modelFile = await File.createFromFileName(
    getTestFilePath("data/Mozilla/test-llama/main/byte-fallback.gguf")
  );
  const generator = await TextGenerator.create(modelFile, { contextSize: 128 });
  try {
    const request = {
      messages: [{ role: "user", content: "abc" }],
      maxTokens: 1,
      samplers: TINYSTORIES_GREEDY_SAMPLERS,
    };
    const first = await generator.generate(request);
    Assert.greater(first.usage.promptCharacters, 0, "The prompt is counted");
    generator.clear();
    request.messages[0].content = "\u00e9\u4e2d\u{1f642}";
    const unicode = await generator.generate(request);
    Assert.equal(
      unicode.usage.promptCharacters,
      first.usage.promptCharacters,
      "Prompt usage counts code points, including supplementary characters"
    );
  } finally {
    generator.terminate();
  }
});

add_task(async function test_prompt_longer_than_batch() {
  const results = [];
  for (const batchSize of [4, 512]) {
    const generator = await createTinyStoriesGenerator({
      batchSize,
      contextSize: 512,
    });
    try {
      results.push(
        await generator.generate({
          messages: [
            { role: "user", content: "The cat sat on the mat. ".repeat(32) },
          ],
          maxTokens: 8,
          samplers: TINYSTORIES_GREEDY_SAMPLERS,
        })
      );
    } finally {
      generator.terminate();
    }
  }
  Assert.greater(results[0].usage.promptTokens, 32, "The prompt spans batches");
  Assert.greater(results[0].usage.generatedTokens, 0, "It generates output");
  Assert.equal(
    results[0].content,
    results[1].content,
    "Batching preserves greedy output"
  );
  Assert.equal(
    results[0].usage.promptTokens,
    results[1].usage.promptTokens,
    "Both runs consume the whole prompt"
  );
});
