/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { ProgressStatusText, ProgressType } = ChromeUtils.importESModule(
  "chrome://global/content/ml/Utils.sys.mjs"
);
const { OPFS } = ChromeUtils.importESModule(
  "chrome://global/content/ml/OPFS.sys.mjs"
);

const { URLChecker } = ChromeUtils.importESModule(
  "chrome://global/content/ml/Utils.sys.mjs"
);

const { Progress, MLUtils } = ChromeUtils.importESModule(
  "chrome://global/content/ml/Utils.sys.mjs"
);

// Root URL of the fake hub, see the `data` dir in the tests.
const FAKE_HUB =
  "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/data";
const FAKE_URL_TEMPLATE = "{model}/resolve/{revision}";

const FAKE_MODEL_ARGS = {
  model: "acme/bert",
  revision: "main",
  file: "config.json",
  taskName: "task_model",
};

const FAKE_RELEASED_MODEL_ARGS = {
  model: "acme/bert",
  revision: "v0.1",
  file: "config.json",
  taskName: "task_released",
};

const FAKE_ONNX_MODEL_ARGS = {
  model: "acme/bert",
  revision: "main",
  file: "onnx/config.json",
  taskName: "task_onnx",
};

/**
 * Test the MOZ_ALLOW_EXTERNAL_ML_HUB environment variable
 */
add_task(async function test_allow_external_ml_hub() {
  Services.env.set("MOZ_ALLOW_EXTERNAL_ML_HUB", "1");
  new ModelHub({ rootUrl: "https://huggingface.co" });
  Services.env.set("MOZ_ALLOW_EXTERNAL_ML_HUB", "");
});

const badInputs = [
  [
    {
      model: "ac me/bert",
      revision: "main",
      file: "config.json",
    },
    "Org can only contain letters, numbers, and hyphens",
  ],
  [
    {
      model: "1111/bert",
      revision: "main",
      file: "config.json",
    },
    "Org cannot contain only numbers",
  ],
  [
    {
      model: "-acme/bert",
      revision: "main",
      file: "config.json",
    },
    "Org start or end with a hyphen, or use consecutive hyphens",
  ],
  [
    {
      model: "a-c-m-e/#bert",
      revision: "main",
      file: "config.json",
    },
    "Models can only contain letters, numbers, and hyphens, underscord, periods",
  ],
  [
    {
      model: "a-c-m-e/b$ert",
      revision: "main",
      file: "config.json",
    },
    "Models cannot contain spaces or control characters",
  ],
  [
    {
      model: "a-c-m-e/b$ert",
      revision: "main",
      file: ".filename",
    },
    "File",
  ],
];

/**
 * Make sure we reject bad inputs.
 */
add_task(async function test_bad_inputs() {
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });

  for (const badInput of badInputs) {
    const params = badInput[0];
    const errorMsg = badInput[1];
    try {
      await hub.getModelFileAsArrayBuffer(params);
    } catch (error) {
      continue;
    }
    throw new Error(errorMsg);
  }
});

/**
 * Test that we can retrieve a file as an ArrayBuffer.
 */
add_task(async function test_getting_file() {
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });

  let [array, headers] = await hub.getModelFileAsArrayBuffer(FAKE_MODEL_ARGS);

  Assert.equal(headers["Content-Type"], "application/json");

  // check the content of the file.
  let jsonData = JSON.parse(
    String.fromCharCode.apply(null, new Uint8Array(array))
  );

  Assert.equal(jsonData.hidden_size, 32);
});

/**
 * Test that we can retrieve a file as an ArrayBuffer even if we don't have headers
 */
add_task(async function test_getting_file_no_headers() {
  await SpecialPowers.pushPrefEnv({
    set: [
      // Enabled by default.
      ["browser.ml.logLevel", "All"],
    ],
  });

  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
    reset: true,
  });

  // Return empty headers
  sinon.stub(hub, "extractHeaders").callsFake(function () {
    return {};
  });

  let [array, headers] = await hub.getModelFileAsArrayBuffer(FAKE_MODEL_ARGS);

  Assert.equal(headers["Content-Type"], "application/octet-stream"); // default content type

  // check the content of the file.
  let jsonData = JSON.parse(
    String.fromCharCode.apply(null, new Uint8Array(array))
  );

  Assert.equal(jsonData.hidden_size, 32);

  hub.extractHeaders.restore();
  await deleteCache(hub.cache);
});

/**
 * Test that we can retrieve a file from a released model and skip head calls
 */
add_task(async function test_getting_released_file() {
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });
  let spy = sinon.spy(hub, "getETag");
  let [array, headers] = await hub.getModelFileAsArrayBuffer(
    FAKE_RELEASED_MODEL_ARGS
  );

  Assert.equal(headers["Content-Type"], "application/json");

  // check the content of the file.
  let jsonData = JSON.parse(
    String.fromCharCode.apply(null, new Uint8Array(array))
  );

  Assert.equal(jsonData.hidden_size, 768);

  // check that head calls were not made
  Assert.ok(!spy.called, "getETag should have never been called.");
  spy.restore();
});

/**
 * Make sure files can be located in sub directories
 */
add_task(async function test_getting_file_in_subdir() {
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });

  let [array, metadata] =
    await hub.getModelFileAsArrayBuffer(FAKE_ONNX_MODEL_ARGS);

  Assert.equal(metadata["Content-Type"], "application/json");

  // check the content of the file.
  let jsonData = JSON.parse(
    String.fromCharCode.apply(null, new Uint8Array(array))
  );

  Assert.equal(jsonData.hidden_size, 768);
});

/**
 * Test that we can use a custom URL template.
 */
add_task(async function test_getting_file_custom_path() {
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: "{model}/resolve/{revision}",
  });

  let res = await hub.getModelFileAsArrayBuffer(FAKE_MODEL_ARGS);

  Assert.equal(res[1]["Content-Type"], "application/json");
});

/**
 * Test that we can't use an URL with a query for the template
 */
add_task(async function test_getting_file_custom_path_rogue() {
  const urlTemplate = "{model}/resolve/{revision}/?some_id=bedqwdw";
  Assert.throws(
    () => new ModelHub({ rootUrl: FAKE_HUB, urlTemplate }),
    /Invalid URL template/,
    `Should throw with ${urlTemplate}`
  );
});

/**
 * Test that the file can be returned as a response and its content correct.
 */
add_task(async function test_getting_file_as_response() {
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });

  let response = await hub.getModelFileAsResponse(FAKE_MODEL_ARGS);

  // check the content of the file.
  let jsonData = await response.json();
  Assert.equal(jsonData.hidden_size, 32);
});

/**
 * Test that the cache is used when the data is retrieved from the server
 * and that the cache is updated with the new data.
 */
add_task(async function test_getting_file_from_cache() {
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });
  let array = await hub.getModelFileAsArrayBuffer(FAKE_MODEL_ARGS);

  var lastUsed = array[1].lastUsed;

  // stub to verify that the data was retrieved from IndexDB
  let matchMethod = hub.cache._testGetData;

  sinon.stub(hub.cache, "_testGetData").callsFake(function () {
    return matchMethod.apply(this, arguments).then(result => {
      Assert.notEqual(result, null);
      return result;
    });
  });

  // exercises the cache
  let array2 = await hub.getModelFileAsArrayBuffer(FAKE_MODEL_ARGS);
  hub.cache._testGetData.restore();

  let newLastUsed = array2[1].lastUsed;

  // make sure the last used field was updated
  Assert.greater(newLastUsed, lastUsed);

  // we don't compare the lastUsed fiel because it changes for each read
  Assert.deepEqual(stripLastUsed(array), stripLastUsed(array2));
});

/**
 * Test that the callback is appropriately called when the data is retrieved from the server
 * or from the cache.
 */
add_task(async function test_getting_file_from_url_cache_with_callback() {
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });

  hub.cache = await initializeCache();

  let numCalls = 0;
  let currentData = null;
  let array = await hub.getModelFileAsArrayBuffer({
    ...FAKE_MODEL_ARGS,
    progressCallback: data => {
      // expecting initiate status and download
      currentData = data;
      if (numCalls == 0) {
        Assert.deepEqual(
          {
            type: data.type,
            statusText: data.statusText,
            ok: data.ok,
            model: currentData?.metadata?.model,
            file: currentData?.metadata?.file,
            revision: currentData?.metadata?.revision,
            taskName: currentData?.metadata?.taskName,
          },
          {
            type: ProgressType.DOWNLOAD,
            statusText: ProgressStatusText.INITIATE,
            ok: true,
            ...FAKE_MODEL_ARGS,
          },
          "Initiate Data from server should be correct"
        );
      }

      if (numCalls == 1) {
        Assert.deepEqual(
          {
            type: data.type,
            statusText: data.statusText,
            ok: data.ok,
            model: currentData?.metadata?.model,
            file: currentData?.metadata?.file,
            revision: currentData?.metadata?.revision,
            taskName: currentData?.metadata?.taskName,
          },
          {
            type: ProgressType.DOWNLOAD,
            statusText: ProgressStatusText.SIZE_ESTIMATE,
            ok: true,
            ...FAKE_MODEL_ARGS,
          },
          "size estimate Data from server should be correct"
        );
      }

      numCalls += 1;
    },
  });

  var lastUsed = array[1].lastUsed;

  Assert.greaterOrEqual(numCalls, 3);

  // last received message is DONE
  Assert.deepEqual(
    {
      type: currentData?.type,
      statusText: currentData?.statusText,
      ok: currentData?.ok,
      model: currentData?.metadata?.model,
      file: currentData?.metadata?.file,
      revision: currentData?.metadata?.revision,
      taskName: currentData?.metadata?.taskName,
    },
    {
      type: ProgressType.DOWNLOAD,
      statusText: ProgressStatusText.DONE,
      ok: true,
      ...FAKE_MODEL_ARGS,
    },
    "Done Data from server should be correct"
  );

  // stub to verify that the data was retrieved from IndexDB
  let matchMethod = hub.cache._testGetData;

  sinon.stub(hub.cache, "_testGetData").callsFake(function () {
    return matchMethod.apply(this, arguments).then(result => {
      Assert.notEqual(result, null);
      return result;
    });
  });

  numCalls = 0;
  currentData = null;

  // Now we expect the callback to indicate cache usage.
  let array2 = await hub.getModelFileAsArrayBuffer({
    ...FAKE_MODEL_ARGS,
    progressCallback: data => {
      // expecting initiate status and download

      currentData = data;

      if (numCalls == 0) {
        Assert.deepEqual(
          {
            type: data.type,
            statusText: data.statusText,
            ok: data.ok,
            model: currentData?.metadata?.model,
            file: currentData?.metadata?.file,
            revision: currentData?.metadata?.revision,
            taskName: currentData?.metadata?.taskName,
          },
          {
            type: ProgressType.LOAD_FROM_CACHE,
            statusText: ProgressStatusText.INITIATE,
            ok: true,
            ...FAKE_MODEL_ARGS,
          },
          "Initiate Data from cache should be correct"
        );
      }

      numCalls += 1;
    },
  });
  hub.cache._testGetData.restore();

  let newLastUsed = array2[1].lastUsed;

  // make sure the last used field was updated
  Assert.greater(newLastUsed, lastUsed);

  Assert.deepEqual(stripLastUsed(array), stripLastUsed(array2));

  // last received message is DONE
  Assert.deepEqual(
    {
      type: currentData?.type,
      statusText: currentData?.statusText,
      ok: currentData?.ok,
      model: currentData?.metadata?.model,
      file: currentData?.metadata?.file,
      revision: currentData?.metadata?.revision,
      taskName: currentData?.metadata?.taskName,
    },
    {
      type: ProgressType.LOAD_FROM_CACHE,
      statusText: ProgressStatusText.DONE,
      ok: true,
      ...FAKE_MODEL_ARGS,
    },
    "Done Data from cache should be correct"
  );

  await deleteCache(hub.cache);
});

/**
 * Test parsing of a well-formed full URL, including protocol and path.
 */
add_task(async function testWellFormedFullUrl() {
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: "{model}/{revision}",
  });
  const url = `${FAKE_HUB}/org1/model1/v1/file/path`;
  const result = hub.parseUrl(url);

  Assert.equal(
    result.model,
    "org1/model1",
    "Model should be parsed correctly."
  );
  Assert.equal(result.revision, "v1", "Revision should be parsed correctly.");
  Assert.equal(
    result.file,
    "file/path",
    "File path should be parsed correctly."
  );
});

/**
 * Test parsing of well-formed URLs, starting with a slash.
 */
const URLS_AND_RESULT = [
  {
    url: "/Xenova/bert-base-NER/resolve/main/onnx/model.onnx",
    model: "Xenova/bert-base-NER",
    revision: "main",
    file: "onnx/model.onnx",
    urlTemplate: "{model}/resolve/{revision}",
  },
  {
    url: "/org1/model1/v1/file/path",
    model: "org1/model1",
    revision: "v1",
    file: "file/path",
    urlTemplate: "{model}/{revision}",
  },
];

add_task(async function testWellFormedRelativeUrl() {
  for (const example of URLS_AND_RESULT) {
    const hub = new ModelHub({
      rootUrl: FAKE_HUB,
      urlTemplate: example.urlTemplate,
    });
    const result = hub.parseUrl(example.url);

    Assert.equal(
      result.model,
      example.model,
      "Model should be parsed correctly."
    );
    Assert.equal(
      result.revision,
      example.revision,
      "Revision should be parsed correctly."
    );
    Assert.equal(
      result.file,
      example.file,
      "File path should be parsed correctly."
    );
  }
});

/**
 * Ensures an error is thrown when the URL does not start with the expected root URL or a slash.
 */
add_task(async function testInvalidDomain() {
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });
  const url = "https://example.com/org1/model1/resolve/v1/file/path";
  Assert.throws(
    () => hub.parseUrl(url),
    new RegExp(`Error: Invalid domain for model URL: ${url}`),
    `Should throw with ${url}`
  );
});

/**
 * Tests the method's error handling when the URL format does not include the required segments.
 */
add_task(async function testTooFewParts() {
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });
  const url = "/org1/model1/resolve";
  Assert.throws(
    () => hub.parseUrl(url),
    new RegExp(`Error: Invalid model URL format: ${url}`),
    `Should throw with ${url}`
  );
});

// IndexedDB tests

/**
 * Test the initialization and creation of the IndexedDBCache instance.
 */
add_task(async function test_Init() {
  const cache = await initializeCache();
  Assert.ok(
    cache instanceof IndexedDBCache,
    "The cache instance should be created successfully."
  );
  Assert.ok(
    IDBDatabase.isInstance(cache.db),
    `The cache should have an IDBDatabase instance. Found ${cache.db}`
  );
  await deleteCache(cache);
});

/**
 * Test checking existence of data in the cache.
 */
add_task(async function test_PutAndCheckExists() {
  const cache = await initializeCache();
  const testData = createBlob();
  const key = "file.txt";
  await cache.put({
    taskName: "task",
    model: "org/model",
    revision: "v1",
    file: "file.txt",
    data: testData,
    headers: {
      ETag: "ETAG123",
    },
  });

  // Checking if the file exists
  let exists = await cache.fileExists({
    model: "org/model",
    revision: "v1",
    file: key,
  });
  Assert.ok(exists, "The file should exist in the cache.");

  // Removing all files from the model
  await cache.deleteModels({ model: "org/model", revision: "v1" });

  exists = await cache.fileExists({
    taskName: "task",
    model: "org/model",
    revision: "v1",
    file: key,
  });
  Assert.ok(!exists, "The file should be gone from the cache.");

  await deleteCache(cache);
});

/**
 * Test adding data to the cache and retrieving it.
 */
add_task(async function test_PutAndGet() {
  const cache = await initializeCache();
  const testData = createBlob();
  await cache.put({
    taskName: "task",
    model: "org/model",
    revision: "v1",
    file: "file.txt",
    data: testData,
    headers: {
      ETag: "ETAG123",
    },
  });

  const [retrievedData, headers] = await cache.getFile({
    model: "org/model",
    revision: "v1",
    file: "file.txt",
  });
  Assert.deepEqual(
    retrievedData,
    testData,
    "The retrieved data should match the stored data."
  );
  Assert.equal(
    headers.ETag,
    "ETAG123",
    "The retrieved ETag should match the stored ETag."
  );

  await deleteCache(cache);
});

/**
 * Test retrieving the headers for a cache entry.
 */
add_task(async function test_GetHeaders() {
  const cache = await initializeCache();
  const testData = createBlob();
  const headers = {
    ETag: "ETAG123",
    status: 200,
    extra: "extra",
  };

  const when = await cache.put({
    taskName: "task",
    model: "org/model",
    revision: "v1",
    file: "file.txt",
    data: testData,
    headers,
  });

  const storedHeaders = await cache.getHeaders({
    model: "org/model",
    revision: "v1",
    file: "file.txt",
  });

  // The `extra` field should be removed from the stored headers because
  // it's not part of the allowed keys.
  // The content-type one is added when not present
  Assert.deepEqual(
    {
      ETag: "ETAG123",
      status: 200,
      "Content-Type": "application/octet-stream",
      fileSize: 8,
      lastUsed: when,
      lastUpdated: when,
    },
    storedHeaders,
    "The retrieved headers should match the stored headers."
  );
  await deleteCache(cache);
});

/**
 * Test listing all models stored in the cache.
 */
add_task(async function test_ListModels() {
  const cache = await initializeCache();

  await Promise.all([
    cache.put({
      taskName: "task1",
      model: "org1/modelA",
      revision: "v1",
      file: "file1.txt",
      data: createBlob(),
      headers: null,
    }),
    cache.put({
      taskName: "task2",
      model: "org2/modelB",
      revision: "v2",
      file: "file2.txt",
      data: createBlob(),
      headers: null,
    }),
  ]);

  const models = await cache.listModels();
  const expected = [
    { name: "org1/modelA", revision: "v1", taskName: "task1" },
    { name: "org2/modelB", revision: "v2", taskName: "task2" },
  ];
  Assert.deepEqual(models, expected, "All models should be listed");
  await deleteCache(cache);
});

/**
 * Test deleting a model and its data from the cache.
 */
add_task(async function test_DeleteModels() {
  const cache = await initializeCache();
  await cache.put({
    taskName: "task",
    model: "org/model",
    revision: "v1",
    file: "file.txt",
    data: createBlob(),
    headers: null,
  });
  await cache.deleteModels({ model: "org/model", revision: "v1" });

  const dataAfterDelete = await cache.getFile({
    model: "org/model",
    revision: "v1",
    file: "file.txt",
  });
  Assert.equal(
    dataAfterDelete,
    null,
    "The data for the deleted model should not exist."
  );
  await deleteCache(cache);
});

/**
 * Test that after deleting a model from the cache, the remaing models are still there.
 */
add_task(async function test_nonDeletedModels() {
  const cache = await initializeCache();

  const testData = createRandomBlob();

  await Promise.all([
    cache.put({
      taskName: "task1",
      model: "org/model",
      revision: "v1",
      file: "file.txt",
      data: testData,
      headers: {
        ETag: "ETAG123",
      },
    }),
    cache.put({
      taskName: "task2",
      model: "org/model2",
      revision: "v1",
      file: "file.txt",
      data: createRandomBlob(),
      headers: {
        ETag: "ETAG1234",
      },
    }),

    cache.put({
      taskName: "task3",
      model: "org/model2",
      revision: "v1",
      file: "file2.txt",
      data: createRandomBlob(),
      headers: {
        ETag: "ETAG1234",
      },
    }),
  ]);

  await cache.deleteModels({ model: "org/model2", revision: "v1" });

  const [retrievedData, headers] = await cache.getFile({
    model: "org/model",
    revision: "v1",
    file: "file.txt",
  });
  Assert.deepEqual(
    retrievedData,
    testData,
    "The retrieved data should match the stored data."
  );

  Assert.equal(
    headers.ETag,
    "ETAG123",
    "The retrieved ETag should match the stored ETag."
  );

  const dataAfterDelete = await cache.getFile({
    model: "org/model2",
    revision: "v1",
    file: "file.txt",
  });
  Assert.equal(
    dataAfterDelete,
    null,
    "The data for the deleted model should not exist."
  );

  const dataAfterDelete2 = await cache.getFile({
    model: "org/model2",
    revision: "v1",
    file: "file2.txt",
  });
  Assert.equal(
    dataAfterDelete2,
    null,
    "The data for the deleted model should not exist."
  );

  await deleteCache(cache);
});

/**
 * Test deleting a model and its data from the cache using a task name.
 */
add_task(async function test_DeleteModelsUsingTaskName() {
  const cache = await initializeCache();
  const model = "mozilla/distilvit";
  const revision = "main";
  const taskName = "echo";

  await cache.put({
    taskName,
    model,
    revision,
    file: "file.txt",
    data: createBlob(),
    headers: null,
  });

  await cache.deleteModels({ taskName });

  // Model should be gone.
  const models = await cache.listModels();
  const expected = [];
  Assert.deepEqual(models, expected, "All models should be deleted.");

  const dataAfterDelete = await cache.getFile({
    model,
    revision,
    file: "file.txt",
  });
  Assert.equal(
    dataAfterDelete,
    null,
    "The data for the deleted model should not exist."
  );
  await deleteCache(cache);
});

/**
 * Test deleting a model and its data from the cache using a non-existing task name.
 */
add_task(async function test_DeleteModelsUsingNonExistingTaskName() {
  const cache = await initializeCache();
  const model = "mozilla/distilvit";
  const revision = "main";
  const taskName = "echo";

  await cache.put({
    taskName,
    model,
    revision,
    file: "file.txt",
    data: createBlob(),
    headers: null,
  });

  await cache.deleteModels({ taskName: "non-existing-task" });

  // Model should still be there.
  const models = await cache.listModels();
  const expected = [{ name: model, revision, taskName }];
  Assert.deepEqual(models, expected, "All models should be listed");

  await deleteCache(cache);
});

/**
 * Test that after deleting a model from the cache, the remaing models are still there.
 */
add_task(async function test_deleteNonMatchingModelRevisions() {
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });

  const cache = await initializeCache();

  hub.cache = cache;

  const testData = createRandomBlob();

  const testData2 = createRandomBlob();

  const taskName = "task";

  const file = "file.txt";
  const hostname = new URL(FAKE_HUB).hostname;

  await Promise.all([
    cache.put({
      taskName,
      model: `${hostname}/org/model`,
      revision: "v1",
      file,
      data: testData,
      headers: {
        ETag: "ETAG123",
      },
    }),
    cache.put({
      taskName,
      model: `${hostname}/org/model2`,
      revision: "v1",
      file,
      data: createRandomBlob(),
      headers: {
        ETag: "ETAG1234",
      },
    }),

    cache.put({
      taskName,
      model: `${hostname}/org/model2`,
      revision: "v2",
      file,
      data: createRandomBlob(),
      headers: {
        ETag: "ETAG1234",
      },
    }),

    cache.put({
      taskName,
      model: `${hostname}/org/model2`,
      revision: "v3",
      file,
      data: testData2,
      headers: {
        ETag: "ETAG1234",
      },
    }),
  ]);

  await hub.deleteNonMatchingModelRevisions({
    taskName,
    modelWithHostname: `${hostname}/org/model2`,
    targetRevision: "v3",
  });

  const [retrievedData, headers] = await cache.getFile({
    model: `${hostname}/org/model`,
    revision: "v1",
    file,
  });
  Assert.deepEqual(
    retrievedData,
    testData,
    "The retrieved data should match the stored data."
  );
  Assert.equal(
    headers.ETag,
    "ETAG123",
    "The retrieved ETag should match the stored ETag."
  );

  const dataAfterDelete = await cache.getFile({
    model: `${hostname}/org/model2`,
    revision: "v1",
    file,
  });
  Assert.equal(dataAfterDelete, null, "The data for v1 should not exist.");

  const dataAfterDelete2 = await cache.getFile({
    model: `${hostname}/org/model2`,
    revision: "v2",
    file,
  });
  Assert.equal(dataAfterDelete2, null, "The data for v2 should not exist.");

  const [retrievedData2, headers2] = await cache.getFile({
    model: `${hostname}/org/model2`,
    revision: "v3",
    file,
  });
  Assert.deepEqual(
    retrievedData2,
    testData2,
    "The retrieved data for v3 should match the stored data."
  );

  Assert.equal(
    headers2.ETag,
    "ETAG1234",
    "The retrieved ETag for v3 should match the stored ETag."
  );

  await deleteCache(cache);
});

/**
 * Test listing files
 */
add_task(async function test_listFiles() {
  const cache = await initializeCache();
  const headers = { "Content-Length": "12345", ETag: "XYZ" };
  const blob = createBlob();

  const when1 = await cache.put({
    taskName: "task1",
    model: "org/model",
    revision: "v1",
    file: "file.txt",
    data: blob,
    headers: null,
  });

  const when2 = await cache.put({
    taskName: "task1",
    model: "org/model",
    revision: "v1",
    file: "file2.txt",
    data: blob,
    headers: null,
  });

  const when3 = await cache.put({
    taskName: "task2",
    model: "org/model",
    revision: "v1",
    file: "sub/file3.txt",
    data: createBlob(32),
    headers,
  });

  const { files } = await cache.listFiles({
    model: "org/model",
    revision: "v1",
  });
  const expected = [
    {
      path: "file.txt",
      headers: {
        "Content-Type": "application/octet-stream",
        fileSize: 8,
        ETag: "NO_ETAG",
        lastUsed: when1,
        lastUpdated: when1,
      },
      engineIds: [],
    },
    {
      path: "file2.txt",
      headers: {
        "Content-Type": "application/octet-stream",
        fileSize: 8,
        ETag: "NO_ETAG",
        lastUsed: when2,
        lastUpdated: when2,
      },
      engineIds: [],
    },
    {
      path: "sub/file3.txt",
      headers: {
        "Content-Length": "12345",
        "Content-Type": "application/octet-stream",
        fileSize: 32,
        ETag: "XYZ",
        lastUsed: when3,
        lastUpdated: when3,
      },
      engineIds: [],
    },
  ];

  Assert.deepEqual(files, expected);
  await deleteCache(cache);
});

/**
 * Test listing files using a task name
 */
add_task(async function test_listFilesUsingTaskName() {
  const cache = await initializeCache();

  const model = "mozilla/distilvit";
  const revision = "main";
  const taskName = "echo";

  const headers = { "Content-Length": "12345", ETag: "XYZ" };
  const blob = createBlob();

  const when1 = await cache.put({
    taskName,
    model,
    revision,
    file: "file.txt",
    data: blob,
    headers: null,
  });

  const when2 = await cache.put({
    taskName,
    model,
    revision,
    file: "file2.txt",
    data: blob,
    headers: null,
  });

  const when3 = await cache.put({
    taskName,
    model,
    revision,
    file: "sub/file3.txt",
    data: createBlob(32),
    headers,
  });

  const { files } = await cache.listFiles({ taskName, model, revision });
  const expected = [
    {
      path: "file.txt",
      headers: {
        "Content-Type": "application/octet-stream",
        fileSize: 8,
        ETag: "NO_ETAG",
        lastUsed: when1,
        lastUpdated: when1,
      },
      engineIds: [],
    },
    {
      path: "file2.txt",
      headers: {
        "Content-Type": "application/octet-stream",
        fileSize: 8,
        ETag: "NO_ETAG",
        lastUsed: when2,
        lastUpdated: when2,
      },
      engineIds: [],
    },
    {
      path: "sub/file3.txt",
      headers: {
        "Content-Length": "12345",
        "Content-Type": "application/octet-stream",
        fileSize: 32,
        ETag: "XYZ",
        lastUsed: when3,
        lastUpdated: when3,
      },
      engineIds: [],
    },
  ];

  Assert.deepEqual(files, expected);

  await deleteCache(cache);
});

/**
 * Test listing files using a non existing task name
 */
add_task(async function test_listFilesUsingNonExistingTaskName() {
  const cache = await initializeCache();

  const model = "mozilla/distilvit";
  const revision = "main";
  const taskName = "echo";

  const headers = { "Content-Length": "12345", ETag: "XYZ" };
  const blob = createBlob();

  await Promise.all([
    cache.put({
      taskName,
      model,
      revision,
      file: "file.txt",
      data: blob,
      headers: null,
    }),
    cache.put({
      taskName,
      model,
      revision,
      file: "file2.txt",
      data: blob,
      headers: null,
    }),
    cache.put({
      taskName,
      model,
      revision,
      file: "sub/file3.txt",
      data: createBlob(32),
      headers,
    }),
  ]);

  const { files } = await cache.listFiles({ taskName: "non-existing-task" });

  Assert.deepEqual(files, []);

  await deleteCache(cache);
});

/**
 * Test the ability to add a database from a non-existing database.
 */
add_task(async function test_initDbFromNonExisting() {
  const cache = await initializeCache();

  Assert.notEqual(cache, null);

  await deleteCache(cache);
});

/**
 * Test that we can upgrade even if the existing database is missing some stores or indices.
 */
add_task(async function test_initDbFromExistingEmpty() {
  const dbName = `modelFiles-${crypto.randomUUID()}`;
  await TestIndexedDBCache.deleteDatabaseAndWait(dbName).catch(() => {});

  const dbVersion = 1;

  const newVersion = dbVersion + 1;

  async function openDB() {
    return new Promise((resolve, reject) => {
      const request = indexedDB.open(dbName, dbVersion);
      request.onerror = event => reject(event.target.error);
      request.onsuccess = event => resolve(event.target.result);
    });
  }

  const db = await openDB();
  db.close();

  const cache = await IndexedDBCache.init({ dbName, version: newVersion });

  Assert.notEqual(cache, null);
  Assert.equal(cache.db.version, newVersion);

  const model = "mozilla/distilvit";
  const revision = "main";
  const taskName = "echo";

  const blob = createBlob();

  const when = await cache.put({
    taskName,
    model,
    revision,
    file: "file.txt",
    data: blob,
    headers: null,
  });

  const expected = [
    {
      path: "file.txt",
      headers: {
        "Content-Type": "application/octet-stream",
        fileSize: 8,
        ETag: "NO_ETAG",
        lastUsed: when,
        lastUpdated: when,
      },
      engineIds: [],
    },
  ];

  // Ensure every table & indices is on so that we can list files
  const { files } = await cache.listFiles({ taskName, model, revision });
  Assert.deepEqual(files, expected);

  await deleteCache(cache);
});

/**
 * Test that upgrading from version 1 to version 2 results in existing data being deleted.
 */
add_task(async function test_initDbFromExistingNoChange() {
  const dbName = `modelFiles-${crypto.randomUUID()}`;

  await TestIndexedDBCache.deleteDatabaseAndWait(dbName).catch(() => {});

  // Create version 1
  let cache = await IndexedDBCache.init({ dbName, version: 1 });

  Assert.notEqual(cache, null);
  Assert.equal(cache.db.version, 1);

  const model = "mozilla/distilvit";
  const revision = "main";
  const taskName = "echo";

  const blob = createBlob();

  await cache.put({
    taskName,
    model,
    revision,
    file: "file.txt",
    data: blob,
    headers: null,
  });

  cache.db.close();

  // Create version 2
  cache = await IndexedDBCache.init({ dbName, version: 2 });

  Assert.notEqual(cache, null);
  Assert.equal(cache.db.version, 2);

  // Ensure tables are all empty.
  const { files } = await cache.listFiles({ taskName });

  Assert.deepEqual(files, []);

  await deleteCache(cache);
});

/**
 * Test that upgrading an existing cache from another source is possible.
 */
add_task(async function test_initDbFromExistingElseWhereStoreChanges() {
  const dbName = `modelFiles-${crypto.randomUUID()}`;
  await TestIndexedDBCache.deleteDatabaseAndWait(dbName).catch(() => {});

  const dbVersion = 2;
  const model = "mozilla/distilvit";
  const revision = "main";
  const taskName = "echo";

  const blob = createBlob();
  // Create version 2
  const cache1 = await IndexedDBCache.init({ dbName, version: dbVersion });

  Assert.notEqual(cache1, null);
  Assert.equal(cache1.db.version, 2);

  // Cache1 is not closed by design of this test

  // Create version 3
  const cache2 = await IndexedDBCache.init({ dbName, version: dbVersion + 1 });

  Assert.notEqual(cache2, null);
  Assert.equal(cache2.db.version, 3);

  const when = await cache2.put({
    taskName,
    model,
    revision,
    file: "file.txt",
    data: blob,
    headers: null,
  });

  const expected = [
    {
      path: "file.txt",
      headers: {
        "Content-Type": "application/octet-stream",
        fileSize: 8,
        ETag: "NO_ETAG",
        lastUpdated: when,
        lastUsed: when,
      },
      engineIds: [],
    },
  ];

  // Ensure every table & indices is on so that we can list files
  const { files } = await cache2.listFiles({ taskName, model, revision });
  Assert.deepEqual(files, expected);

  await deleteCache(cache2);
});

/**
 * Test that we can use a custom hub on every API call to get files.
 */
add_task(async function test_getting_file_custom_hub() {
  // The hub is configured to use localhost
  const hub = new ModelHub({
    rootUrl: "https://localhost",
    urlTemplate: "{model}/boo/revision",
  });

  // but we can use APIs against another hub
  const args = {
    model: "acme/bert",
    revision: "main",
    file: "config.json",
    taskName: "task_model",
    modelHubRootUrl: FAKE_HUB,
    modelHubUrlTemplate: "{model}/resolve/{revision}",
  };

  let [array, headers] = await hub.getModelFileAsArrayBuffer(args);

  Assert.equal(headers["Content-Type"], "application/json");

  // check the content of the file.
  let jsonData = JSON.parse(
    String.fromCharCode.apply(null, new Uint8Array(array))
  );

  Assert.equal(jsonData.hidden_size, 32);

  let res = await hub.getModelFileAsBlob(args);
  Assert.equal(res[0].size, 548);

  let response = await hub.getModelFileAsResponse(args);
  Assert.equal((await response.blob()).size, 548);
});

/**
 * Make sure that we can't pass a rootUrl that is not allowed when using the API calls
 */
add_task(async function test_getting_file_disallowed_custom_hub() {
  // The hub is configured to use localhost
  const hub = new ModelHub({
    rootUrl: "https://localhost",
    urlTemplate: "{model}/boo/revision",
    allowDenyList: [{ filter: "ALLOW", urlPrefix: "https://example.com" }],
  });

  // and we can't use APIs against another hub if it's not allowed
  const args = {
    model: "acme/bert",
    revision: "main",
    file: "config.json",
    taskName: "task_model",
    modelHubRootUrl: "https://forbidden.com",
    modelHubUrlTemplate: "{model}/{revision}",
  };

  // This catch the error returned by getEtag when checking if file is in cache
  try {
    await hub.getModelFileAsArrayBuffer(args);
    throw new Error("Expected method to reject.");
  } catch (error) {
    Assert.throws(
      () => {
        throw error;
      },
      new RegExp(`ForbiddenURLError`),
      `Should throw with https://forbidden.com`
    );
  }

  // This catch the error returned when useCached is false
  try {
    await hub.getModelFileAsArrayBuffer({ ...args, revision: "v1" });
    throw new Error("Expected method to reject.");
  } catch (error) {
    Assert.throws(
      () => {
        throw error;
      },
      new RegExp(`ForbiddenURLError`),
      `Should throw with https://forbidden.com`
    );
  }

  try {
    await hub.getModelFileAsBlob(args);
    throw new Error("Expected method to reject.");
  } catch (error) {
    Assert.throws(
      () => {
        throw error;
      },
      new RegExp(`ForbiddenURLError`),
      `Should throw with https://forbidden.com`
    );
  }

  try {
    await hub.getModelFileAsResponse(args);
    throw new Error("Expected method to reject.");
  } catch (error) {
    Assert.throws(
      () => {
        throw error;
      },
      new RegExp(`ForbiddenURLError`),
      `Should throw with https://forbidden.com`
    );
  }

  // This catch the error when http error codes are returned, useCached is false
  try {
    await hub.getModelFileAsArrayBuffer({
      ...args,
      revision: "v1",
      modelHubRootUrl: "https://example.com",
    });
    throw new Error("Expected method to reject.");
  } catch (error) {
    Assert.throws(
      () => {
        throw error;
      },
      new RegExp(`HTTP error! Status: 404 Not Found`),
      `Should throw with 404`
    );
  }

  // This catch the error returned when useCached is true with no checks for etags
  try {
    // store a file in the hub
    await hub.cache.put({
      ...args,
      model: "forbidden.com/acme/bert",
      engineId: "engineOne",
      revision: "v1",
      data: createBlob(),
      headers: null,
    });

    await hub.getModelFileAsArrayBuffer({ ...args, revision: "v1" });
    throw new Error("Expected method to reject.");
  } catch (error) {
    Assert.throws(
      () => {
        throw error;
      },
      new RegExp(`ForbiddenURLError`),
      `Should throw with https://forbidden.com`
    );
  }
});

/**
 * isModelAvailable should resolve from the cache instead of hitting the
 * network when the file is already present locally.
 */
add_task(async function test_isModelAvailable_cached_skips_network() {
  const hub = new ModelHub({
    rootUrl: "https://example.com",
    urlTemplate: "{model}/{revision}",
    allowDenyList: [{ filter: "ALLOW", urlPrefix: "https://example.com" }],
  });
  hub.cache = await initializeCache();

  const model = "acme/bert";
  const revision = "main";
  const file = "config.json";

  await hub.cache.put({
    taskName: "task_model",
    model: "example.com/acme/bert",
    revision,
    file,
    data: createBlob(),
    headers: null,
  });

  const fetchStub = sinon.stub(MLUtils, "fetchUrl");

  const isAvailable = await hub.isModelAvailable(model, revision, { file });

  Assert.ok(isAvailable, "Cached model should be reported as available.");
  Assert.ok(
    fetchStub.notCalled,
    "isModelAvailable should not hit the network for a cached file."
  );

  fetchStub.restore();
  await deleteCache(hub.cache);
});

/**
 * isModelAvailable should not trust a cached file whose hub has since been
 * denylisted: it must delete the cached model and report unavailable.
 */
add_task(async function test_isModelAvailable_cached_denylisted() {
  const hub = new ModelHub({
    rootUrl: "https://forbidden.com",
    urlTemplate: "{model}/{revision}",
    allowDenyList: [{ filter: "DENY", urlPrefix: "https://forbidden.com" }],
  });
  hub.cache = await initializeCache();

  const model = "acme/bert";
  const revision = "main";
  const file = "config.json";

  await hub.cache.put({
    taskName: "task_model",
    model: "forbidden.com/acme/bert",
    revision,
    file,
    data: createBlob(),
    headers: null,
  });

  const isAvailable = await hub.isModelAvailable(model, revision, { file });

  Assert.ok(
    !isAvailable,
    "Cached model on a denylisted hub should be reported unavailable."
  );

  const dataAfterDenylist = await hub.cache.getFile({
    model: "forbidden.com/acme/bert",
    revision,
    file,
  });
  Assert.equal(
    dataAfterDenylist,
    null,
    "The denylisted cached model should be deleted."
  );

  await deleteCache(hub.cache);
});

/**
 * Test deleting files used by several engines
 */
add_task(async function test_DeleteFileByEngines() {
  const cache = await initializeCache();
  const testData = createBlob();
  const engineOne = "engine-1";
  const engineTwo = "engine-2";
  const model = "org/model";
  const revision = "v1";

  // a file is stored by engineOne
  await cache.put({
    engineId: engineOne,
    taskName: "task",
    model: "org/model",
    revision: "v1",
    file: "file.txt",
    data: createBlob(),
    headers: null,
  });

  // The file is read by engineTwo
  let retrievedData = await cache.getFile({
    engineId: engineTwo,
    model: "org/model",
    revision: "v1",
    file: "file.txt",
  });

  Assert.deepEqual(
    retrievedData[0],
    testData,
    "The retrieved data should match the stored data."
  );

  // We should have two engines associated with the model file
  let retrievedFiles = await cache.listFiles({ model, revision });
  Assert.equal(retrievedFiles.metadata.engineIds.length, 2);

  // if we delete the model by engineOne, it will still be around for engineTwo
  await cache.deleteFilesByEngine({ engineId: engineOne });

  retrievedData = await cache.getFile({
    engineId: engineTwo,
    model: "org/model",
    revision: "v1",
    file: "file.txt",
  });
  Assert.deepEqual(
    retrievedData[0],
    testData,
    "The retrieved data should match the stored data."
  );

  // We should now have one engine associated with the model file
  retrievedFiles = await cache.listFiles({ model, revision });
  Assert.equal(retrievedFiles.metadata.engineIds.length, 1);
  Assert.equal(retrievedFiles.metadata.engineIds[0], engineTwo);

  // now deleting via engineTwo
  await cache.deleteFilesByEngine({ engineId: engineTwo });

  // at this point we should not have anymore files
  const dataAfterDelete = await cache.getFile({
    engineId: engineOne,
    model: "org/model",
    revision: "v1",
    file: "file.txt",
  });
  Assert.equal(
    dataAfterDelete,
    null,
    "The data for the deleted model should not exist."
  );

  // Now we should have no more engine
  Assert.equal(await cache._testGetData(cache.enginesStoreName), null);

  await deleteCache(cache);
});

/**
 * Test deleting files used by an engine via the model hub. This is similar to
 * `test_DeleteFileByEngines`, except we are calling the model hub method.
 */
add_task(async function test_ModelHub_DeleteFileByEngines() {
  const cache = await initializeCache();
  const engineOne = "engine-1";
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
    allowDenyList: [],
  });
  hub.cache = cache;

  // a file is stored by engineOne
  await cache.put({
    engineId: engineOne,
    taskName: "task",
    model: "org/model",
    revision: "v1",
    file: "file.txt",
    data: createBlob(),
    headers: null,
  });

  // We should have at least one file
  const dataBeforeDelete = await cache.getFile({
    engineId: engineOne,
    model: "org/model",
    revision: "v1",
    file: "file.txt",
  });
  Assert.notEqual(dataBeforeDelete, null, "The data should exist");

  // We should have at least one engine
  Assert.notEqual(await cache._testGetData(cache.enginesStoreName), null);

  await hub.deleteFilesByEngine({ engineId: engineOne });

  // at this point we should not have anymore files
  const dataAfterDelete = await cache.getFile({
    engineId: engineOne,
    model: "org/model",
    revision: "v1",
    file: "file.txt",
  });
  Assert.equal(
    dataAfterDelete,
    null,
    "The data for the deleted model should not exist."
  );

  // The engine should be removed from the model/revision/file engine list.
  // In our case, this means we should have no entry in the engine list since it is
  // the last
  Assert.equal(await cache._testGetData(cache.enginesStoreName), null);

  await deleteCache(cache);
});

// tests allow deny list updating after model is cached
add_task(async function test_update_allow_deny_after_model_cache() {
  const cache = await initializeCache();
  const file = "config.json";
  const taskName = FAKE_MODEL_ARGS.taskName;
  const model = FAKE_MODEL_ARGS.model;
  const revision = "v0.1";
  await cache.put({
    taskName,
    model,
    revision,
    file,
    data: createBlob(),
    headers: null,
  });

  let exists = await cache.fileExists({
    model,
    revision,
    file,
  });
  Assert.ok(exists, "The file should exist in the cache.");

  let list = [
    {
      filter: "ALLOW",
      urlPrefix:
        "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/data/acme",
    },
  ];

  let hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
    allowDenyList: list,
  });
  hub.cache = cache;
  // should go through since model is allowed
  await hub.getModelFileAsArrayBuffer({ ...FAKE_MODEL_ARGS, file, revision });

  // put model in deny list
  list = [
    {
      filter: "DENY",
      urlPrefix:
        "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/data/acme",
    },
  ];

  hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
    allowDenyList: list,
  });
  hub.cache = cache;

  // now ensure the model cannot be called after being put in the deny list
  try {
    await hub.getModelFileAsArrayBuffer({ ...FAKE_MODEL_ARGS, file, revision });
  } catch (e) {
    Assert.strictEqual(e.name, "ForbiddenURLError");
  }
  // make sure that the model is deleted after
  const dataAfterForbidden = await cache.getFile({
    model,
    revision,
    file,
  });
  Assert.equal(
    dataAfterForbidden,
    null,
    "The data for the deleted model should not exist."
  );
});

/**
 * Test that data from OPFS is wiped
 */
add_task(async function test_migrateStore_modelsDeleted() {
  const dbName = `modelFiles-${crypto.randomUUID()}`;
  await TestIndexedDBCache.deleteDatabaseAndWait(dbName).catch(() => {});

  // Initialize version 4 of the database
  let cache = await IndexedDBCache.init({ dbName, version: 4 });

  // Add some test data for unknown models
  await Promise.all([
    cache.put({
      taskName: "task",
      model: "random/model",
      revision: "v1",
      file: "random.txt",
      data: createBlob(),
      headers: null,
    }),
    cache.put({
      taskName: "task",
      model: "unknown/model",
      revision: "v2",
      file: "unknown.txt",
      data: createBlob(),
      headers: null,
    }),
  ]);

  // Close version 4 and upgrade to version 5
  cache.db.close();
  cache = await IndexedDBCache.init({ dbName, version: 5 });

  // Verify all unknown model data is deleted
  const { files: random } = await cache.listFiles({
    model: "random/model",
    revision: "v1",
  });
  Assert.deepEqual(random, [], "All unknown model files should be deleted.");

  const { files: unknown } = await cache.listFiles({
    model: "unknown/model",
    revision: "v2",
  });
  Assert.deepEqual(unknown, [], "All unknown model files should be deleted.");

  await deleteCache(cache);
});

/**
 * Test migration when database starts empty.
 */
add_task(async function test_migrateStore_emptyDatabase() {
  const dbName = `modelFiles-${crypto.randomUUID()}`;
  await TestIndexedDBCache.deleteDatabaseAndWait(dbName).catch(() => {});

  // Initialize an empty version 4 database
  let cache = await IndexedDBCache.init({ dbName, version: 4 });
  cache.db.close();

  // Upgrade to version 5
  cache = await IndexedDBCache.init({ dbName, version: 5 });

  // Verify database is still empty
  const models = await cache.listModels();
  Assert.deepEqual(
    models,
    [],
    "The database should remain empty after migration."
  );

  await deleteCache(cache);
});

add_task(async function test_getOwnerIcon_cache() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.logLevel", "All"]],
  });

  const originalOPFSFile = OPFS.File;

  const localPaths = new Set();
  const stub = sinon.stub(OPFS, "File").callsFake(function (args) {
    localPaths.add(args.localPath);
    return new originalOPFSFile(args); // preserve original behavior
  });

  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });

  const fullyQualifiedModelName = `mochitests/mozilla/distilvit-${crypto.randomUUID()}`;

  // first call will get the icon from the web
  const icon = await hub.getOwnerIcon(fullyQualifiedModelName);
  Assert.notEqual(icon, null);

  // second call will get it from the cache
  let spy = sinon.spy(Progress, "fetchUrl");

  const icon2 = await hub.getOwnerIcon(fullyQualifiedModelName);
  Assert.notEqual(icon2, null);

  // check that it cames from OPFS
  Assert.equal(spy.called, false);

  spy.restore();
  stub.restore();
  for (const path of localPaths) {
    await OPFS.remove(path, { recursive: true });
  }
});

add_task(async function test_getOwnerIcon_download() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.logLevel", "All"]],
  });

  const originalOPFSFile = OPFS.File;

  const localPaths = new Set();
  const stub = sinon.stub(OPFS, "File").callsFake(function (args) {
    localPaths.add(args.localPath);
    return new originalOPFSFile(args); // preserve original behavior
  });

  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });

  const fullyQualifiedModelName = `mochitests/mozilla/distilvit-${crypto.randomUUID()}`;

  let spy = sinon.spy(Progress, "fetchUrl");
  // first call will get the icon from the web
  const icon = await hub.getOwnerIcon(fullyQualifiedModelName);
  Assert.notEqual(icon, null);
  // check that it didn't come from OPFS
  Assert.equal(spy.called, true);
  Assert.notEqual(await spy.lastCall?.returnValue, null);

  spy.restore();
  stub.restore();
  for (const path of localPaths) {
    await OPFS.remove(path, { recursive: true });
  }
});

/**
 * Test that downloads can be cancelled
 */
add_task(async function test_download_cancellation() {
  const cache = await initializeCache();
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });
  hub.cache = cache;

  const controller = new AbortController();
  const { signal } = controller;

  try {
    controller.abort();

    await Assert.rejects(
      hub.getModelDataAsFile({ ...FAKE_MODEL_ARGS, abortSignal: signal }),
      err => err?.name === "AbortError",
      "The call should be cancelled"
    );
  } catch (err) {
    Assert.ok(false, `Expected AbortError. Got ${err}`);
  } finally {
    await deleteCache(cache);
  }
});

/**
 * Test that downloads can be cancelled after fetch is successfull
 */
add_task(async function test_download_cancellation_after_fetch() {
  const cache = await initializeCache();
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });
  hub.cache = cache;

  const controller = new AbortController();
  const { signal } = controller;

  const fetchUrlStub = sinon
    .stub(MLUtils, "fetchUrl")
    .callsFake((url, { signal: _, ...rest } = {}) => {
      const p = fetch(url, rest);
      controller.abort();
      return p;
    });

  try {
    await Assert.rejects(
      hub.getModelDataAsFile({ ...FAKE_MODEL_ARGS, abortSignal: signal }),
      err => err?.name === "AbortError",
      "The call should be cancelled"
    );
  } catch (err) {
    Assert.ok(false, `Expected AbortError. Got ${err}`);
  } finally {
    await deleteCache(cache);
    fetchUrlStub.restore();
  }
});

/**
 * Bug 1967279: Verify the 6 -> 7 schema migration rewrites a row tagged with
 * the old "wllamapreview" engineId to "link-preview" without dropping the
 * underlying OPFS model file, so the renamed feature can still uninstall it.
 */
add_task(async function test_migrateStore_renamesWllamapreviewEngineId() {
  const dbName = `modelFiles-${crypto.randomUUID()}`;
  await TestIndexedDBCache.deleteDatabaseAndWait(dbName).catch(() => {});

  const model = "org/link-preview";
  const revision = "v1";
  const file = "weights.bin";

  // Seed a row under the pre-migration schema with the obsolete engineId.
  let cache = await IndexedDBCache.init({ dbName, version: 6 });
  await cache.put({
    engineId: "wllamapreview",
    taskName: "wllama-text-generation",
    model,
    revision,
    file,
    data: createBlob(),
    headers: null,
  });

  let listed = await cache.listFiles({ model, revision });
  Assert.deepEqual(
    listed.metadata.engineIds,
    ["wllamapreview"],
    "Row should be seeded with the legacy engineId."
  );
  cache.db.close();

  // Reopen at the current schema version and confirm the rename happened.
  cache = await IndexedDBCache.init({ dbName });

  listed = await cache.listFiles({ model, revision });
  Assert.deepEqual(
    listed.metadata.engineIds,
    ["link-preview"],
    "Migration should rewrite the legacy engineId to its replacement."
  );
  Assert.equal(
    listed.files.length,
    1,
    "Migration must not drop the OPFS model file."
  );

  // The uninstall path used by LinkPreview should now find and remove the row.
  await cache.deleteFilesByEngine({ engineId: "link-preview" });
  listed = await cache.listFiles({ model, revision });
  Assert.deepEqual(
    listed.files,
    [],
    "Uninstall via the new engineId should physically delete the file."
  );

  await deleteCache(cache);
});

/**
 * Bug 1967279: If a row already carries both the legacy and the new engineId
 * (e.g. a re-download happened before the migration), the rewrite must
 * de-duplicate rather than leaving the stale id behind.
 */
add_task(async function test_migrateStore_dedupsLegacyAndNewEngineIds() {
  const dbName = `modelFiles-${crypto.randomUUID()}`;
  await TestIndexedDBCache.deleteDatabaseAndWait(dbName).catch(() => {});

  const model = "org/link-preview";
  const revision = "v1";
  const file = "weights.bin";

  let cache = await IndexedDBCache.init({ dbName, version: 6 });
  await cache.put({
    engineId: "wllamapreview",
    taskName: "wllama-text-generation",
    model,
    revision,
    file,
    data: createBlob(),
    headers: null,
  });
  await cache.put({
    engineId: "link-preview",
    taskName: "wllama-text-generation",
    model,
    revision,
    file,
    data: createBlob(),
    headers: null,
  });
  cache.db.close();

  cache = await IndexedDBCache.init({ dbName });
  const listed = await cache.listFiles({ model, revision });
  Assert.deepEqual(
    listed.metadata.engineIds,
    ["link-preview"],
    "Migration should collapse duplicate ids to the new engineId only."
  );

  await deleteCache(cache);
});

/**
 * Verify the 7 -> 8 schema migration re-keys task rows whose taskName was
 * renamed, so already-downloaded model files keep hitting the cache instead
 * of being downloaded again.
 */
add_task(async function test_migrateStore_renamesWllamaTaskName() {
  const dbName = `modelFiles-${crypto.randomUUID()}`;
  await TestIndexedDBCache.deleteDatabaseAndWait(dbName).catch(() => {});

  const model = "org/link-preview";
  const revision = "v1";
  const file = "weights.bin";

  // Seed a row under the pre-migration schema with the obsolete taskName.
  let cache = await IndexedDBCache.init({ dbName, version: 7 });
  await cache.put({
    engineId: "link-preview",
    taskName: "wllama-text-generation",
    model,
    revision,
    file,
    data: createBlob(),
    headers: null,
  });
  cache.db.close();

  // Reopen at the current schema version and confirm the row was re-keyed.
  cache = await IndexedDBCache.init({ dbName });

  let listed = await cache.listFiles({
    taskName: "llama-text-generation",
    model,
    revision,
  });
  Assert.equal(
    listed.files.length,
    1,
    "Migration should re-key the row to the new taskName."
  );

  listed = await cache.listFiles({
    taskName: "wllama-text-generation",
    model,
    revision,
  });
  Assert.deepEqual(
    listed.files,
    [],
    "No row should remain under the old taskName."
  );

  await deleteCache(cache);
});
