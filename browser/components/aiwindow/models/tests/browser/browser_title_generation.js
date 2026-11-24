/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { generateChatTitle } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/TitleGeneration.sys.mjs"
);
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

add_task(async function test_title_generation_with_mock_engine() {
  const mockEngineManager = new MockEngineManager();

  try {
    const generated = generateChatTitle("message", null);
    await mockEngineManager.respondTo({
      purpose: "title-generation",
      response: "fake title",
    });
    Assert.equal(
      await generated,
      "fake title",
      "Should return the generated title"
    );
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});
