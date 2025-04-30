/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const {
  MultiProgressAggregator,
  ProgressAndStatusCallbackParams,
  ProgressStatusText,
  readResponse,
  modelToResponse,
  URLChecker,
  RejectionType,
  BlockListManager,
  RemoteSettingsManager,
} = ChromeUtils.importESModule("chrome://global/content/ml/Utils.sys.mjs");

const { OPFS } = ChromeUtils.importESModule(
  "chrome://global/content/ml/OPFS.sys.mjs"
);

/**
 * Test that we can retrieve the correct content without a callback.
 */
add_task(async function test_correct_response_no_callback() {
  const content = "This is the expected response.";
  const blob = new Blob([content]);
  const response = new Response(blob, {
    headers: new Headers({ "Content-Length": blob.size }),
  });

  const responseArray = await readResponse(response);

  const responseContent = new TextDecoder().decode(responseArray);

  Assert.equal(content, responseContent, "The response content should match.");
});

/**
 * Test that we can retrieve the correct content with a callback.
 */
add_task(async function test_correct_response_callback() {
  const content = "This is the expected response.";
  const blob = new Blob([content]);
  const response = new Response(blob, {
    headers: new Headers({ "Content-Length": blob.size }),
  });

  const responseArray = await readResponse(response, data => {
    data;
  });

  const responseContent = new TextDecoder().decode(responseArray);

  Assert.equal(content, responseContent, "The response content should match.");
});

/**
 * Test that we can retrieve the correct content with a content-lenght lower than the actual len
 */
add_task(async function test_correct_response_content_length_under_reported() {
  const content = "This is the expected response.";
  const blob = new Blob([content]);
  const response = new Response(blob, {
    headers: new Headers({
      "Content-Length": 1,
    }),
  });

  const responseArray = await readResponse(response, data => {
    data;
  });

  const responseContent = new TextDecoder().decode(responseArray);

  Assert.equal(content, responseContent, "The response content should match.");
});

/**
 * Test that we can retrieve the correct content with a content-lenght larger than the actual len
 */
add_task(async function test_correct_response_content_length_over_reported() {
  const content = "This is the expected response.";
  const blob = new Blob([content]);
  const response = new Response(blob, {
    headers: new Headers({
      "Content-Length": 2 * blob.size + 20,
    }),
  });

  const responseArray = await readResponse(response, data => {
    data;
  });

  const responseContent = new TextDecoder().decode(responseArray);

  Assert.equal(content, responseContent, "The response content should match.");
});

/**
 * Test that we can retrieve and the callback provide correct information
 */
add_task(async function test_correct_response_callback_correct() {
  const contents = ["Carrot", "Broccoli", "Tomato", "Spinach"];

  let contentSizes = [];

  let totalSize = 0;

  for (const value of contents) {
    contentSizes.push(new Blob([value]).size);

    totalSize += contentSizes[contentSizes.length - 1];
  }

  const numChunks = contents.length;

  let encoder = new TextEncoder();

  // const stream = ReadableStream.from(contents);

  let streamId = -1;

  const stream = new ReadableStream({
    pull(controller) {
      streamId += 1;

      if (streamId < numChunks) {
        controller.enqueue(encoder.encode(contents[streamId]));
      } else {
        controller.close();
      }
    },
  });

  const response = new Response(stream, {
    headers: new Headers({
      "Content-Length": totalSize,
    }),
  });

  let chunkId = -1;
  let expectedTotalLoaded = 0;

  const responseArray = await readResponse(response, data => {
    chunkId += 1;
    // The callback is called on time with no data loaded and just the total
    if (chunkId == 0) {
      Assert.deepEqual(
        {
          total: data.total,
          currentLoaded: data.currentLoaded,
          totalLoaded: data.totalLoaded,
        },
        {
          total: totalSize,
          currentLoaded: 0,
          totalLoaded: 0,
        },
        "The callback should be called on time with an estimate of the total size and no data read. "
      );
    } else {
      Assert.less(
        chunkId - 1,
        numChunks,
        "The number of times the callback is called should be lower than the number of chunks"
      );

      expectedTotalLoaded += contentSizes[chunkId - 1];

      Assert.deepEqual(
        {
          total: data.total,
          currentLoaded: data.currentLoaded,
          totalLoaded: data.totalLoaded,
        },
        {
          total: totalSize,
          currentLoaded: contentSizes[chunkId - 1],
          totalLoaded: expectedTotalLoaded,
        },
        "The reported value by the callback should match the correct values"
      );
    }
  });

  Assert.equal(
    chunkId,
    numChunks,
    "The callback should be called exactly as many times as the number of chunks."
  );

  const responseContent = new TextDecoder().decode(
    responseArray.buffer.slice(
      responseArray.byteOffset,
      responseArray.byteLength + responseArray.byteOffset
    )
  );

  Assert.equal(
    contents.join(""),
    responseContent,
    "The response content should match."
  );
});

/**
 * Test that multi-aggregator only call the callback for the provided types.
 */
add_task(async function test_multi_aggregator_watchtypes() {
  let numCalls = 0;
  let aggregator = new MultiProgressAggregator({
    progressCallback: _ => {
      numCalls += 1;
    },
    watchedTypes: ["t1"],
  });

  aggregator.aggregateCallback(
    new ProgressAndStatusCallbackParams({
      type: "download",
    })
  );

  Assert.equal(numCalls, 0);

  aggregator.aggregateCallback(
    new ProgressAndStatusCallbackParams({
      type: "t1",
    })
  );

  Assert.equal(numCalls, 1);
});

/**
 * Test that multi-aggregator aggregate correctly.
 */
add_task(async function test_multi_aggregator() {
  // Ids for all available tasks. Should be unique per element.
  const taskIds = ["A", "B", "C", "D", "E", "F"];

  // The type for each available tasks.
  const taskTypes = ["t1", "t1", "t2", "t2", "t3", "t3"];

  // The total size available for each task
  const taskSizes = [5, 11, 13, 17, 19, 23];

  // The chunk sizes. The sum for indices with same chunk task index (according to chunkTaskIndex)
  // should be equal to the corresponding size in taskSizes
  const chunkSizes = [2, 3, 5, 6, 11, 7, 12, 6, 8, 9, 9, 10];

  // Task index for each chunk. Index in the array taskIds. Order was chosen so that we can simulate
  // overlaps in tasks.
  const chunkTaskIndex = [0, 0, 1, 2, 5, 2, 5, 1, 3, 4, 3, 4];

  // Indicating how much has been loaded for the task so far.
  const chunkTaskLoaded = [2, 5, 5, 6, 11, 13, 23, 11, 8, 9, 17, 19];

  // Whether the
  const chunkIsFinal = [0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1];

  let numDone = 0;

  let currentData = null;

  let expectedTotalToLoad = 0;

  let numCalls = 0;

  let expectedNumCalls = 0;

  let expectedTotalLoaded = 0;

  let taskIdsWithDataSet = new Set();

  const aggregator = new MultiProgressAggregator({
    progressCallback: data => {
      currentData = data;

      numCalls += 1;

      if (data.statusText == ProgressStatusText.DONE) {
        numDone += 1;
      }
    },
    watchedTypes: ["t1", "t2", "t3"],
  });

  // Initiate and advertise the size for each task
  for (const i in taskTypes) {
    currentData = null;
    expectedNumCalls += 1;
    aggregator.aggregateCallback(
      new ProgressAndStatusCallbackParams({
        type: taskTypes[i],
        statusText: ProgressStatusText.INITIATE,
        id: taskIds[i],
        total: taskSizes[i],
      })
    );
    Assert.equal(currentData.progress, 0, "Progress is 0%");

    Assert.ok(currentData, "Received data should be defined");
    Assert.deepEqual(
      {
        statusText: currentData?.statusText,
        type: currentData?.type,
        id: currentData?.id,
        totalObjectsSeen: currentData?.metadata?.totalObjectsSeen,
        numDone,
        numCalls,
      },
      {
        statusText: ProgressStatusText.INITIATE,
        type: taskTypes[i],
        id: taskIds[i],
        totalObjectsSeen: taskIdsWithDataSet.size,
        numDone: 0,
        numCalls: expectedNumCalls,
      },
      "Data received after initiate should be correct"
    );
    currentData = null;
    expectedNumCalls += 1;
    aggregator.aggregateCallback(
      new ProgressAndStatusCallbackParams({
        type: taskTypes[i],
        statusText: ProgressStatusText.SIZE_ESTIMATE,
        id: taskIds[i],
        total: taskSizes[i],
      })
    );
    Assert.ok(currentData, "Received data should be defined");

    expectedTotalToLoad += taskSizes[i];
    taskIdsWithDataSet.add(taskIds[i]);

    Assert.deepEqual(
      {
        numDone,
        numCalls,
        total: currentData.total,
        totalObjectsSeen: currentData?.metadata?.totalObjectsSeen,
      },
      {
        numDone: 0,
        total: expectedTotalToLoad,
        numCalls: expectedNumCalls,
        totalObjectsSeen: taskIdsWithDataSet.size,
      },
      "Data received after size estimate should be correct."
    );
  }

  // Send progress status for each chunk.
  for (const chunkIndex in chunkTaskIndex) {
    let taskIndex = chunkTaskIndex[chunkIndex];
    currentData = null;
    expectedNumCalls += 1;
    expectedTotalLoaded += chunkSizes[chunkIndex];
    aggregator.aggregateCallback(
      new ProgressAndStatusCallbackParams({
        type: taskTypes[taskIndex],
        statusText: ProgressStatusText.IN_PROGRESS,
        id: taskIds[taskIndex],
        total: taskSizes[taskIndex],
        currentLoaded: chunkSizes[chunkIndex],
        totalLoaded: chunkTaskLoaded[chunkIndex],
      })
    );

    Assert.ok(currentData, "Received data should be defined");

    Assert.deepEqual(
      {
        numDone,
        numCalls,
        total: currentData?.total,
        totalObjectsSeen: currentData?.metadata?.totalObjectsSeen,
        currentLoaded: currentData?.currentLoaded,
        totalLoaded: currentData?.totalLoaded,
      },
      {
        numDone: 0,
        numCalls: expectedNumCalls,
        total: expectedTotalToLoad,
        totalObjectsSeen: taskIdsWithDataSet.size,
        currentLoaded: chunkSizes[chunkIndex],
        totalLoaded: expectedTotalLoaded,
      },
      "Data received after in progress should be correct"
    );

    // Notify of task is done
    if (chunkIsFinal[chunkIndex]) {
      currentData = null;

      aggregator.aggregateCallback(
        new ProgressAndStatusCallbackParams({
          type: taskTypes[taskIndex],
          statusText: ProgressStatusText.IN_PROGRESS,
          id: taskIds[taskIndex],
          total: taskSizes[taskIndex],
          currentLoaded: chunkSizes[chunkIndex],
          totalLoaded: chunkTaskLoaded[chunkIndex],
        })
      );
      expectedNumCalls += 1;

      Assert.deepEqual(
        {
          numDone,
          numCalls,
          total: currentData?.total,
          totalObjectsSeen: currentData?.metadata?.totalObjectsSeen,
          currentLoaded: currentData?.currentLoaded,
          totalLoaded: currentData?.totalLoaded,
        },
        {
          numDone: 0,
          numCalls: expectedNumCalls,
          total: expectedTotalToLoad,
          totalObjectsSeen: taskIdsWithDataSet.size,
          currentLoaded: chunkSizes[chunkIndex],
          totalLoaded: expectedTotalLoaded,
        },
        "Extra data beyond what is expected or a process should not affect total downloaded"
      );

      currentData = null;
      expectedNumCalls += 1;
      aggregator.aggregateCallback(
        new ProgressAndStatusCallbackParams({
          type: taskTypes[taskIndex],
          statusText: ProgressStatusText.DONE,
          id: taskIds[taskIndex],
          total: taskSizes[chunkIndex],
        })
      );

      Assert.ok(currentData, "Received data should be defined");

      Assert.deepEqual(
        { total: currentData.total, numCalls },
        { total: expectedTotalToLoad, numCalls: expectedNumCalls },
        "Data received after completed tasks should be correct"
      );
    }
  }
  Assert.equal(currentData.progress, 100, "Progress is 100%");

  Assert.equal(numDone, 1, "Done status should be received");
});

/**
 * Test modelToResponse function.
 */
add_task(async function test_ml_utils_model_to_response() {
  const modelPath = "test.txt";
  await OPFS.getFileHandle(modelPath, { create: true });

  registerCleanupFunction(async () => {
    await OPFS.remove(modelPath);
  });

  const cases = [
    {
      model: modelPath,
      headers: null,
      expected: {},
      msg: "valid response with no headers",
    },
    {
      model: modelPath,
      headers: { some: "header" },
      expected: { some: "header" },
      msg: "valid response",
    },

    {
      model: modelPath,
      headers: { some: "header", int: 1234 },
      expected: { some: "header", int: "1234" },
      msg: "valid response with ints conversion",
    },
    {
      model: modelPath,
      headers: { some: null, int: 1234 },
      expected: { int: "1234" },
      msg: "valid response with null keys ignored",
    },
  ];

  for (const case_ of cases) {
    const response = await modelToResponse(case_.model, case_.headers);
    for (const [key, value] of Object.entries(case_.expected)) {
      Assert.deepEqual(response.headers.get(key), value, case_.message);
    }
  }
});

/**
 * Tests the URLChecker class
 *
 */
add_task(async function testURLChecker() {
  // Example of result from remote settings
  const list = [
    { filter: "ALLOW", urlPrefix: "https://huggingface.co/mozilla/" },
    { filter: "ALLOW", urlPrefix: "https://model-hub.mozilla.org/" },
    {
      filter: "ALLOW",
      urlPrefix:
        "https://huggingface.co/typeform/distilbert-base-uncased-mnli/",
    },
    {
      filter: "ALLOW",
      urlPrefix: "https://huggingface.co/mozilla/distilvit/blob/v0.5.0/",
    },
    {
      filter: "DENY",
      urlPrefix: "https://huggingface.co/mozilla/restricted-model",
    },
    {
      filter: "ALLOW",
      urlPrefix: "https://localhost:8080/myhub",
    },
  ];

  const checker = new URLChecker(list);

  // Test cases
  const testCases = [
    {
      url: "https://huggingface.co/mozilla/",
      expected: { allowed: true, rejectionType: RejectionType.NONE },
      description:
        "Allows all models and versions from Mozilla on Hugging Face",
    },
    {
      url: "https://huggingface.co/mozilla/distilvit/blob/v0.5.0/",
      expected: { allowed: true, rejectionType: RejectionType.NONE },
      description:
        "Allows a specific model and version from Mozilla on Hugging Face",
    },
    {
      url: "https://huggingface.co/mozilla/restricted-model",
      expected: { allowed: false, rejectionType: RejectionType.DENIED },
      description:
        "Denies a specific restricted model from Mozilla on Hugging Face",
    },
    {
      url: "https://model-hub.mozilla.org/some-model",
      expected: { allowed: true, rejectionType: RejectionType.NONE },
      description: "Allows any model from Mozilla's model hub",
    },
    {
      url: "https://my.cool.hub/",
      expected: { allowed: false, rejectionType: RejectionType.DISALLOWED },
      description: "Denies access to an unapproved hub URL",
    },
    {
      url: "https://sub.localhost/myhub",
      expected: { allowed: false, rejectionType: RejectionType.DISALLOWED },
      description: "Denies access to a subdomain of an approved domain",
    },
    {
      url: "https://model-hub.mozilla.org.evil.com",
      expected: { allowed: false, rejectionType: RejectionType.DISALLOWED },
      description:
        "Denies access to URL with allowed domain as a subdomain in another domain",
    },
    {
      url: "httpsz://localhost/myhub",
      expected: { allowed: false, rejectionType: RejectionType.DISALLOWED },
      description: "Denies access with a similar-looking scheme",
    },
    {
      url: "https://localhost./",
      expected: { allowed: false, rejectionType: RejectionType.DISALLOWED },
      description: "Denies access to URL with trailing dot in domain",
    },
    {
      url: "https://user@huggingface.co/mozilla/",
      expected: { allowed: false, rejectionType: RejectionType.DISALLOWED },
      description: "Denies access to URL with user info",
    },
    {
      url: "ftp://localhost/myhub/",
      expected: { allowed: false, rejectionType: RejectionType.DISALLOWED },
      description:
        "Denies access to URL with disallowed scheme but allowed host",
    },
    {
      url: "https://model-hub.mozilla.org.hack/",
      expected: { allowed: false, rejectionType: RejectionType.DISALLOWED },
      description: "Denies access to domain containing an allowed domain",
    },
    {
      url: "https:///huggingface.co/mozilla/",
      expected: { allowed: false, rejectionType: RejectionType.DISALLOWED },
      description:
        "Denies access to URL with triple slashes, just type correctly",
    },
    {
      url: "https://localhost:8080/myhub",
      expected: { allowed: true, rejectionType: RejectionType.NONE },
      description: "Allows access to URL with port specified",
    },
    {
      url: "http://localhost/myhub",
      expected: { allowed: true, rejectionType: RejectionType.NONE },
      description: "Allows access to URL with HTTP scheme on localhost",
    },
    {
      url: "https://model-hub.mozilla.org/",
      expected: { allowed: true, rejectionType: RejectionType.NONE },
      description: "Allows access to Mozilla's approved model hub URL",
    },
    {
      url: "chrome://gre/somewhere/in/the/code/base",
      expected: { allowed: true, rejectionType: RejectionType.NONE },
      description: "Allows access to internal resource URL in code base",
    },
    {
      url: "http://localhost:37001/Xenova/all-M",
      expected: { allowed: true, rejectionType: RejectionType.NONE },
      description: "Allows access to URL with localhost with a port",
    },
    {
      url: "https://user@localhost/Xenova/all-M",
      expected: { allowed: true, rejectionType: RejectionType.NONE },
      description: "Allows access to URL with localhost with a user",
    },
  ];

  for (const { url, expected, description } of testCases) {
    const result = checker.allowedURL(url);
    Assert.deepEqual(
      result,
      expected,
      `URL check for '${url}' should return ${JSON.stringify(
        expected
      )}: ${description}`
    );
  }
});

/**
 * Test the Block List Manager with a single blocked n-grams at word boundaries
 *
 */
add_task(async function testBlockListManager_single_blocked_word() {
  const manager = new BlockListManager({
    blockNgrams: [BlockListManager.encodeBase64("would like")],
  });

  Assert.equal(
    manager.blockNgramSet.values().next().value,
    "would like",
    "decoded blocked n-grams should match the original value"
  );

  Assert.equal(
    BlockListManager.decodeBase64(BlockListManager.encodeBase64("would like")),
    "would like",
    "round trip encode/decode should give original input"
  );

  Assert.equal(
    manager.matchAtWordBoundary({ text: "People are here" }),
    false,
    "should have no match if blocked n-gram not in input"
  );

  Assert.equal(
    manager.matchAtWordBoundary({ text: "People would likes" }),
    false,
    "should have no match even if only part of blocked n-gram in input"
  );

  Assert.equal(
    manager.matchAtWordBoundary({ text: "People would do like" }),
    false,
    "should have no match if they are other words separating a blocked  2-grams"
  );
  Assert.equal(
    manager.matchAtWordBoundary({ text: "People wouldlike" }),
    false,
    "should have no match if text contains blocked n-grams but without the spaces"
  );
  Assert.equal(
    manager.matchAtWordBoundary({ text: "People would_like" }),
    false,
    "should have no match text contain special characters between a blocked 2-grams"
  );
  Assert.equal(
    manager.matchAtWordBoundary({ text: "People would liketo " }),
    false,
    "should have no match if the blocked 2-grams is not at word boundary"
  );

  Assert.equal(
    manager.matchAtWordBoundary({ text: "People are here and would like go" }),
    true,
    "should match if blocked 2-grams in input"
  );

  Assert.equal(
    manager.matchAtWordBoundary({ text: "would like to do it" }),
    true,
    "should match if blocked 2-grams is at the beginning of input"
  );

  Assert.equal(
    manager.matchAtWordBoundary({ text: "I need to would like." }),
    true,
    "should match if blocked 2-grams is at end of input even with punctuation"
  );

  Assert.equal(
    manager.matchAtWordBoundary({ text: "I need to would like" }),
    true,
    "should match if blocked 2-grams is at end of input"
  );

  Assert.equal(
    manager.matchAtWordBoundary({ text: "I need to would like  " }),
    true,
    "should match even if blocked 2-grams has extra spaces after it"
  );
});

/**
 * Test the Block List Manager with multiple blocked n-grams at word boundaries
 *
 */
add_task(async function testBlockListManager_multiple_blocked_ngrams() {
  const manager = new BlockListManager({
    blockNgrams: [
      BlockListManager.encodeBase64("would like"),
      BlockListManager.encodeBase64("vast"),

      BlockListManager.encodeBase64("blocked"),
    ],
  });

  Assert.equal(
    manager.matchAtWordBoundary({ text: "People are here" }),
    false,
    "should have no match if blocked n-grams are not present"
  );
  Assert.equal(
    manager.matchAtWordBoundary({ text: "People wouldlike iblocked" }),
    false,
    "should have no match if blocked n-grams are not at words boundary"
  );

  Assert.equal(
    manager.matchAtWordBoundary({ text: "People are here and blocked" }),
    true,
    "should match if blocked n-grams are at word boundary"
  );

  Assert.equal(
    manager.matchAtWordBoundary({ text: "would like to do it" }),
    true,
    "should match for all blocked n-grams in the list"
  );

  Assert.equal(
    manager.matchAtWordBoundary({ text: "I need to would like blocked." }),
    true,
    "should match for all blocked n-grams in the list"
  );
  Assert.equal(
    manager.matchAtWordBoundary({ text: "I have a vast amount." }),
    true,
    "should match for all blocked n-grams in the list"
  );
});

/**
 * Test the Block List Manager with multiple blocked n-grams anywhere
 *
 */
add_task(async function testBlockListManager_anywhere() {
  const manager = new BlockListManager({
    blockNgrams: [
      BlockListManager.encodeBase64("would like"),
      BlockListManager.encodeBase64("vast"),

      BlockListManager.encodeBase64("blocked"),
    ],
  });

  Assert.equal(
    manager.matchAnywhere({ text: "People are here" }),
    false,
    "should have no match if blocked n-grams are not present"
  );
  Assert.equal(
    manager.matchAnywhere({ text: "People wouldlike iblocked" }),
    true,
    "should match even if blocked n-grams are not at word boundary"
  );

  Assert.equal(
    manager.matchAnywhere({ text: "People are here and blocked" }),
    true,
    "should match for all blocked n-grams"
  );

  Assert.equal(
    manager.matchAnywhere({ text: "would like to do it" }),
    true,
    "should match for all blocked n-grams"
  );

  Assert.equal(
    manager.matchAnywhere({ text: "I need to would like blocked." }),
    true,
    "should match for all blocked n-grams"
  );
  Assert.equal(
    manager.matchAnywhere({ text: "I have a vast amount." }),
    true,
    "should match for all blocked n-grams"
  );
  Assert.equal(
    manager.matchAnywhere({ text: "I have avast amount." }),
    true,
    "should match for all blocked n-grams even if not at word boundary"
  );
});

/**
 * Get test data for remote settings manager test.
 *
 */
async function getMockedRemoteSettingsClients() {
  const client1 = await createRemoteClient({
    collectionName: "test-block-list",
    records: [
      {
        version: "1.0.0",
        name: "test-link-preview-en",
        blockList: ["cGVyc29u"], // person
        language: "en",
        id: "1",
      },
      {
        version: "1.0.0",
        name: "test-link-preview-fr",
        blockList: ["bW9p"], // moi
        language: "fr",
        id: "2",
      },
      {
        version: "1.0.0",
        name: "base-fr",
        blockList: [],
        language: "fr",
        id: "3",
      },
      {
        version: "2.0.0",
        name: "test-link-preview-en",
        blockList: ["b25l"], // one
        language: "en",
        id: "4",
      },

      {
        version: "1.1.0",
        name: "test-link-preview-en",
        blockList: ["dHdv"], // two
        language: "en",
        id: "5",
      },
    ],
  });

  const client2 = await createRemoteClient({
    collectionName: "test-request-options",
    records: [
      {
        version: "1.0.0",
        featureId: "test-link-preview",
        options: JSON.stringify({ param1: "value1", number: 2 }),
        id: "10",
      },
      {
        version: "1.0.0",
        featureId: "test-ml-suggest",
        options: JSON.stringify({ suggest: "val" }),
        id: "11",
      },
      {
        version: "1.0.0",
        featureId: "ml-i2t",
        options: JSON.stringify({ size: 2 }),
        id: "12",
      },
      {
        version: "2.0.0",
        featureId: "test-link-preview",
        options: JSON.stringify({ param2: "value2", number2: 20 }),
        id: "13",
      },

      {
        version: "1.1.0",
        featureId: "test-link-preview",
        options: JSON.stringify({ param1: "value2", number: 3 }),
        id: "14",
      },
    ],
  });

  const client3 = await createRemoteClient({
    collectionName: "test-inference-options",
    records: [
      {
        featureId: "test-link-preview",
        id: "20",
      },
      {
        featureId: "test-ml-suggest",
        id: "21",
      },
    ],
  });

  return {
    "test-block-list": client1,
    "test-request-options": client2,
    "test-inference-options": client3,
  };
}

/**
 * Test the Remote Settings Manager
 *
 */
add_task(async function testRemoteSettingsManager() {
  RemoteSettingsManager.mockRemoteSettings(
    await getMockedRemoteSettingsClients()
  );

  let data = await RemoteSettingsManager.getRemoteData({
    collectionName: "test-block-list",
    filters: {
      name: "test-link-preview-en",
    },
    majorVersion: 1,
  });

  Assert.deepEqual(
    data,
    {
      version: "1.1.0",
      name: "test-link-preview-en",
      blockList: ["dHdv"],
      language: "en",
      id: "5",
    },
    "should retrieve the latest revision ignoring previous one"
  );

  data = await RemoteSettingsManager.getRemoteData({
    collectionName: "test-block-list",
    filters: {
      name: "test-link-preview-en",
      language: "en",
    },
    majorVersion: 2,
  });

  Assert.deepEqual(
    data,
    {
      version: "2.0.0",
      name: "test-link-preview-en",
      blockList: ["b25l"],
      language: "en",
      id: "4",
    },
    "should retrieve the exact revision"
  );

  data = await RemoteSettingsManager.getRemoteData({
    collectionName: "test-request-options",
    filters: {
      featureId: "test-link-preview",
    },
    majorVersion: 1,
    lookupKey: record => record.featureId,
  });

  Assert.deepEqual(
    data,
    {
      version: "1.1.0",
      featureId: "test-link-preview",
      options: JSON.stringify({ param1: "value2", number: 3 }),
      id: "14",
    },
    "should retrieve from the correct collection even in presence of multiple"
  );

  data = await RemoteSettingsManager.getRemoteData({
    collectionName: "test-inference-options",
    filters: {
      featureId: "test-link-preview",
    },
  });

  Assert.deepEqual(
    data,
    {
      featureId: "test-link-preview",
      id: "20",
    },
    "should retrieve from the correct collection even in presence of multiple"
  );

  data = await RemoteSettingsManager.getRemoteData(
    {
      collectionName: "test-request-options",
      filters: {
        featureId: "test-link-previewP",
      },
      majorVersion: 1,
      lookupKey: record => record.featureId,
    },
    "should work with lookupKey"
  );

  Assert.equal(data, null);

  RemoteSettingsManager.removeMocks();
});

/**
 * Test the Remote data Manager
 *
 */
add_task(async function testBlockListManagerWithRS() {
  RemoteSettingsManager.mockRemoteSettings(
    await getMockedRemoteSettingsClients()
  );

  let manager = await BlockListManager.initializeFromRemoteSettings({
    blockListName: "test-link-preview-en",
    language: "en",
    fallbackToDefault: false,
    majorVersion: 1,
    collectionName: "test-block-list",
  });

  Assert.equal(
    manager.matchAtWordBoundary({ text: "two guy is here" }),
    true,
    "should retrieve the correct list and match <two> for the blocked word"
  );

  Assert.equal(
    manager.matchAtWordBoundary({ text: "one person is here, moi" }),
    false,
    "should retrieve the correct list and match nothing"
  );

  await Assert.rejects(
    BlockListManager.initializeFromRemoteSettings({
      blockListName: "test-link-preview-en-non-existing",
      language: "en",
      fallbackToDefault: false,
      majorVersion: 1,
      collectionName: "test-block-list",
    }),
    /./,
    "should fails since the block list does not exist"
  );

  manager = await BlockListManager.initializeFromRemoteSettings({
    blockListName: "test-link-preview-en-non-existing",
    language: "en",
    fallbackToDefault: true,
    majorVersion: 1,
    collectionName: "test-block-list",
  });

  Assert.equal(
    manager.matchAtWordBoundary({
      text: "the bells are ringing, ding dong ...",
    }),
    true,
    "should not fail but fallback to default list with a match for dong"
  );

  Assert.equal(
    manager.matchAtWordBoundary({ text: "two person is here, moi" }),
    false,
    "should not fail but fallback to default list with no matches found"
  );

  RemoteSettingsManager.removeMocks();
});
