/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { SmartTabGroupingManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/tabbrowser/SmartTabGrouping.sys.mjs"
);

add_task(function test_text_processing_basic_cases() {
  // trailing domain-like text should be removed
  Assert.equal(
    SmartTabGroupingManager.preprocessText("Some Title - Random Mail"),
    "Some Title",
    "Should remove '- Random Mail' suffix and lowercase result"
  );

  // trailing domain-like text with '|'
  Assert.equal(
    SmartTabGroupingManager.preprocessText(
      "Another Title | Some Video Website"
    ),
    "Another Title",
    "Should remove '| Some Video Website' suffix and lowercase result"
  );

  // no delimiter
  Assert.equal(
    SmartTabGroupingManager.preprocessText("Simple Title"),
    "Simple Title",
    "Should only be lowercased since there's no recognized delimiter"
  );

  // not enough info in first part
  Assert.equal(
    SmartTabGroupingManager.preprocessText("AB - Mail"),
    "AB - Mail",
    "Should not remove '- Mail' because the first part is too short"
  );

  // should not match for texts such as 'check-in'
  Assert.equal(
    SmartTabGroupingManager.preprocessText("Check-in for flight"),
    "Check-in for flight",
    "Should not remove '-in'"
  );
});

add_task(function test_text_processing_edge_cases() {
  // empty string
  Assert.equal(
    SmartTabGroupingManager.preprocessText(""),
    "",
    "Empty string returns empty string"
  );

  // exactly 20 chars
  const domain20Chars = "12345678901234567890"; // 20 characters
  Assert.equal(
    SmartTabGroupingManager.preprocessText(`My Title - ${domain20Chars}`),
    `My Title - ${domain20Chars}`,
    "Should not remove suffix because it’s exactly 20 chars long, not < 20"
  );

  // multiple delimiters, remove last only
  Assert.equal(
    SmartTabGroupingManager.preprocessText("Complex - Title - SomethingSmall"),
    "Complex Title",
    "Should remove only the last '- SomethingSmall', ignoring earlier delimiters"
  );

  // repeated delimiters
  Assert.equal(
    SmartTabGroupingManager.preprocessText("Title --- Domain"),
    "Title",
    "Should remove the last chunk and filter out empty strings"
  );

  Assert.equal(
    SmartTabGroupingManager.preprocessText("Title || Domain"),
    "Title",
    "Should remove the last chunk with double pipe delimiters too"
  );

  // long trailing text
  const longDomain = "Useful information is present";
  Assert.equal(
    SmartTabGroupingManager.preprocessText(`Some Title - ${longDomain}`),
    `Some Title - ${longDomain}`,
    "Should not remove suffix if it's >= 20 characters"
  );
});

// generateClusters must preprocess titles before embedding when it has no
// precomputed embeddings; stub the embedder to capture its input.
add_task(
  async function test_generateClusters_preprocessesTitlesBeforeEmbedding() {
    const manager = new SmartTabGroupingManager();
    manager.setDataTitleKey("title");

    let embeddedTexts = null;
    manager._generateEmbeddings = async texts => {
      embeddedTexts = texts;
      return texts.map((_, i) => (i % 2 ? [0, 1] : [1, 0]));
    };

    const tabs = [
      { title: "Q3 Roadmap - Confluence" },
      { title: "Trip Planner | Travel Site" },
      { title: "AB - Mail" },
    ];
    await manager.generateClusters(tabs, null, 0);

    Assert.deepEqual(
      embeddedTexts,
      ["Q3 Roadmap", "Trip Planner", "AB - Mail"],
      "generateClusters strips site suffixes via preprocessText before embedding, " +
        "leaving titles whose first part is too short untouched"
    );
  }
);
