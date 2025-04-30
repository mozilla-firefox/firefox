/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Test for EmbeddingGenerator.sys.mjs
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  EmbeddingsGenerator: "chrome://global/content/ml/EmbeddingsGenerator.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

async function setup() {
  const { removeMocks, remoteClients } = await createAndMockMLRemoteSettings({
    autoDownloadFromRemoteSettings: false,
  });

  await SpecialPowers.pushPrefEnv({
    set: [
      // Enabled by default.
      ["browser.ml.enable", true],
      ["browser.ml.logLevel", "All"],
      ["browser.ml.modelCacheTimeout", 1000],
    ],
  });

  return {
    remoteClients,
    async cleanup() {
      await removeMocks();
      await waitForCondition(
        () => EngineProcess.areAllEnginesTerminated(),
        "Waiting for all of the engines to be terminated.",
        100,
        200
      );
    },
  };
}

add_task(async function test_EmbeddingsGenerator_for_minimum_physical_memory() {
  let embeddingsGenerator = new EmbeddingsGenerator();
  Assert.ok(
    embeddingsGenerator.isEnoughPhysicalMemoryAvailable(),
    "Physical Memory size < 7GiB."
  );
});

add_task(async function test_EmbeddingsGenerator_for_minimum_cpu_cores() {
  let embeddingsGenerator = new EmbeddingsGenerator();
  Assert.ok(
    embeddingsGenerator.isEnoughCpuCoresAvailable(),
    "Number CPU cores < 2."
  );
});

class MockMLEngineForEmbedMany {
  async run(request) {
    const texts = request.args[0];
    return texts.map(text => {
      if (typeof text !== "string" || text.trim() === "") {
        throw new Error("Invalid input: text must be a non-empty string");
      }
      // Return a mock embedding vector (e.g., an array of zeros)
      return Array(384).fill(0);
    });
  }
}

add_task(async function test_embedMany_valid_inputs() {
  const embeddingsGenerator = new EmbeddingsGenerator();
  sinon.stub(embeddingsGenerator, "createEngineIfNotPresent").callsFake(() => {
    return new MockMLEngineForEmbedMany();
  });
  embeddingsGenerator.setEngine(new MockMLEngineForEmbedMany());

  const texts = ["mdn documentation", "jira board"];
  const result = await embeddingsGenerator.embedMany(texts);

  Assert.ok(Array.isArray(result), "Result should be an array");
  Assert.equal(result.length, 2, "Should return 2 embeddings");
  for (const vector of result) {
    Assert.equal(vector.length, 384, "Each embedding should be of size 384");
  }

  sinon.restore();
});

add_task(async function test_embedMany_empty_array_input() {
  const embeddingsGenerator = new EmbeddingsGenerator();
  sinon.stub(embeddingsGenerator, "createEngineIfNotPresent").callsFake(() => {
    return new MockMLEngineForEmbedMany();
  });
  embeddingsGenerator.setEngine(new MockMLEngineForEmbedMany());

  let threw = false;
  try {
    await embeddingsGenerator.embedMany([]);
  } catch (e) {
    threw = true;
    Assert.ok(
      e.message.includes("empty array"),
      "Should throw for empty array input"
    );
  }
  Assert.ok(threw, "Error should be thrown for empty array input");

  sinon.restore();
});

add_task(async function test_embedMany_invalid_input_null() {
  const embeddingsGenerator = new EmbeddingsGenerator();
  sinon.stub(embeddingsGenerator, "createEngineIfNotPresent").callsFake(() => {
    return new MockMLEngineForEmbedMany();
  });
  embeddingsGenerator.setEngine(new MockMLEngineForEmbedMany());

  let caught = false;
  try {
    await embeddingsGenerator.embedMany([null, "hello"]);
  } catch (e) {
    caught = true;
    Assert.ok(e.message.includes("Invalid input"), "Should throw for null");
  }
  Assert.ok(caught, "Error should be thrown");

  sinon.restore();
});

add_task(async function test_embedMany_invalid_input_nonstring() {
  const embeddingsGenerator = new EmbeddingsGenerator();
  sinon.stub(embeddingsGenerator, "createEngineIfNotPresent").callsFake(() => {
    return new MockMLEngineForEmbedMany();
  });
  embeddingsGenerator.setEngine(new MockMLEngineForEmbedMany());

  let caught = false;
  try {
    await embeddingsGenerator.embedMany(["hello", 123]);
  } catch (e) {
    caught = true;
    Assert.ok(
      e.message.includes("Invalid input"),
      "Should throw for non-string"
    );
  }
  Assert.ok(caught, "Error should be thrown");

  sinon.restore();
});

class MockMLEngineForEmbed {
  async run(request) {
    const texts = [request.args[0]];
    return texts.map(text => {
      if (typeof text !== "string" || text.trim() === "") {
        throw new Error("Invalid input: text must be a non-empty string");
      }
      // Return a mock embedding vector (e.g., an array of zeros)
      return Array(384).fill(0);
    });
  }
}

add_task(async function test_embed_valid_input() {
  const embeddingsGenerator = new EmbeddingsGenerator();
  sinon.stub(embeddingsGenerator, "createEngineIfNotPresent").callsFake(() => {
    return new MockMLEngineForEmbed();
  });
  embeddingsGenerator.setEngine(new MockMLEngineForEmbed());

  const result = await embeddingsGenerator.embed("test string");

  Assert.ok(Array.isArray(result), "Embedding result should be an array");
  Assert.equal(result[0].length, 384, "Embedding should be of size 384");

  sinon.restore();
});

add_task(async function test_embed_invalid_input_empty_string() {
  const embeddingsGenerator = new EmbeddingsGenerator();
  sinon.stub(embeddingsGenerator, "createEngineIfNotPresent").callsFake(() => {
    return new MockMLEngineForEmbed();
  });
  embeddingsGenerator.setEngine(new MockMLEngineForEmbed());

  let threw = false;
  try {
    await embeddingsGenerator.embed("");
  } catch (e) {
    threw = true;
    Assert.ok(
      e.message.includes("Invalid input"),
      "Should throw for empty string"
    );
  }
  Assert.ok(threw, "Error should be thrown for empty string");

  sinon.restore();
});
