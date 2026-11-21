/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/// <reference path="head.js" />

/**
 * Regression coverage for bug 2043430.
 *
 * The byte-fallback.gguf test model deterministically emits, regardless of the
 * prompt, the byte-fallback tokens:
 *
 *     E5 97 A3 80   (then EOS)
 *
 * where E5 97 A3 is the valid codepoint U+55E3 "嗣" and 0x80 is a lone UTF-8
 * continuation byte (always malformed). Because the runner streams each token's
 * piece as a UTF8String that is converted to a JS string, any chunk that is not
 * itself valid UTF-8 makes the conversion throw
 * "malformed UTF-8 character sequence at offset N".
 *
 * Stopping at different token counts (`nPredict`) and varying the flush size
 * (`minOutputBufferSize`) reproduces every root of that failure with a single
 * model. The runner must instead stream only valid UTF-8: reassembling split
 * codepoints losslessly and substituting U+FFFD for bytes that can never form a
 * valid codepoint.
 */

// U+FFFD REPLACEMENT CHARACTER, what a UTF-8 decoder substitutes for bytes that
// cannot form a valid codepoint.
const REPLACEMENT = String.fromCharCode(0xfffd);

const UTF8_MODEL = {
  taskName: "text-generation",
  modelId: "Mozilla/test-llama",
  modelFile: "byte-fallback.gguf",
  modelRevision: "main",
  backend: "llama.cpp",
};

async function runUtf8Generation({ minOutputBufferSize, nPredict }) {
  const { cleanup } = await setup();
  try {
    const engine = await createEngine(UTF8_MODEL);

    const generator = engine.runWithGenerator({
      prompt: [{ role: "user", content: "x" }],
      // top-k=1 narrows to a single candidate and `dist` then selects it, so
      // generation is deterministic (the model's logits are also effectively
      // one-hot). `dist` is required: top-k only filters, it never selects.
      samplers: [{ type: "top-k", topK: 1 }, { type: "dist" }],
      minOutputBufferSize,
      nPredict,
    });

    let text = "";
    let result;
    do {
      result = await generator.next();
      if (!result.done) {
        text += result.value.text;
      }
    } while (!result.done);
    return text;
  } finally {
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
}

/**
 * Control: when a whole codepoint is buffered into a single chunk and
 * generation stops on a codepoint boundary, the stream is valid UTF-8 and we
 * get the character back. Independent of the fix.
 */
add_task(async function test_ml_llama_utf8_valid_buffered() {
  await runOnBothInferenceProcesses(async () => {
    const text = await runUtf8Generation({
      minOutputBufferSize: 20,
      nPredict: 3,
    });
    Assert.equal(text, "嗣", "Whole codepoint buffered into one chunk decodes");
  });
});

/**
 * Root 1 - codepoint split across chunks. With minOutputBufferSize=1 each
 * byte-fallback token flushes on its own, so the three bytes of "嗣" are
 * converted to JS individually. The runner must reassemble the codepoint.
 */
add_task(async function test_ml_llama_utf8_split_across_chunks() {
  await runOnBothInferenceProcesses(async () => {
    const text = await runUtf8Generation({
      minOutputBufferSize: 1,
      nPredict: 3,
    });
    Assert.equal(
      text,
      "嗣",
      "Codepoint split across byte-token chunks is reassembled"
    );
  });
});

/**
 * Root 2 - codepoint truncated at end of generation. Generation stops
 * (nPredict=2) after E5 97, mid-codepoint. The missing byte was never emitted,
 * so the incomplete trailing sequence stays buffered and is dropped rather than
 * thrown.
 */
add_task(async function test_ml_llama_utf8_truncated_at_end() {
  await runOnBothInferenceProcesses(async () => {
    const text = await runUtf8Generation({
      minOutputBufferSize: 20,
      nPredict: 2,
    });
    Assert.equal(
      text,
      "",
      "Codepoint truncated at end-of-generation is dropped, not thrown"
    );
  });
});

/**
 * Root 3 - genuinely malformed byte mid-stream. The lone 0x80 continuation byte
 * can never form a valid codepoint, so it must be substituted with U+FFFD while
 * the preceding valid codepoint is preserved.
 */
add_task(async function test_ml_llama_utf8_malformed_byte() {
  await runOnBothInferenceProcesses(async () => {
    const text = await runUtf8Generation({
      minOutputBufferSize: 20,
      nPredict: 4,
    });
    Assert.equal(
      text,
      "嗣" + REPLACEMENT,
      "Malformed byte becomes U+FFFD without dropping the valid codepoint"
    );
  });
});
