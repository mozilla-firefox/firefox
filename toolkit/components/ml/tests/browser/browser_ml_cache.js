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

function createRandomBlob(blockSize = 8, count = 1) {
  const blocks = Array.from({ length: count }, () =>
    Uint32Array.from(
      { length: blockSize / 4 },
      () => Math.random() * 4294967296
    )
  );
  return new Blob(blocks, { type: "application/octet-stream" });
}

function createBlob(size = 8) {
  return createRandomBlob(size);
}

function stripLastUsed(data) {
  return data.map(({ lastUsed: _unusedLastUsed, ...rest }) => {
    return rest;
  });
}

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

  Assert.equal(jsonData.hidden_size, 768);
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
  Assert.equal(jsonData.hidden_size, 768);
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

/** Tests the method's error handling when the URL format does not include the required segments.
 *
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
 * Helper function to initialize the cache
 */
async function initializeCache() {
  const randomSuffix = Math.floor(Math.random() * 10000);
  const dbName = `modelFiles-${randomSuffix}`;
  await OPFS.getDirectoryHandle(dbName, { create: true });
  return await IndexedDBCache.init({ dbName });
}

/**
 * Helper function to delete the cache database
 */
async function deleteCache(cache) {
  await cache.dispose();
  indexedDB.deleteDatabase(cache.dbName);
  try {
    await OPFS.remove(cache.dbName, { recursive: true });
  } catch (e) {
    // can be empty
  }
}

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
    { name: "org1/modelA", revision: "v1" },
    { name: "org2/modelB", revision: "v2" },
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
  const expected = [{ name: model, revision }];
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

  await Promise.all([
    cache.put({
      taskName,
      model: "org/model",
      revision: "v1",
      file,
      data: testData,
      headers: {
        ETag: "ETAG123",
      },
    }),
    cache.put({
      taskName,
      model: "org/model2",
      revision: "v1",
      file,
      data: createRandomBlob(),
      headers: {
        ETag: "ETAG1234",
      },
    }),

    cache.put({
      taskName,
      model: "org/model2",
      revision: "v2",
      file,
      data: createRandomBlob(),
      headers: {
        ETag: "ETAG1234",
      },
    }),

    cache.put({
      taskName,
      model: "org/model2",
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
    model: "org/model2",
    targetRevision: "v3",
  });

  const [retrievedData, headers] = await cache.getFile({
    model: "org/model",
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
    model: "org/model2",
    revision: "v1",
    file,
  });
  Assert.equal(dataAfterDelete, null, "The data for v1 should not exist.");

  const dataAfterDelete2 = await cache.getFile({
    model: "org/model2",
    revision: "v2",
    file,
  });
  Assert.equal(dataAfterDelete2, null, "The data for v2 should not exist.");

  const [retrievedData2, headers2] = await cache.getFile({
    model: "org/model2",
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

  const files = await cache.listFiles({ model: "org/model", revision: "v1" });
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

  const files = await cache.listFiles({ taskName });
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

  const files = await cache.listFiles({ taskName: "non-existing-task" });

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
  const randomSuffix = Math.floor(Math.random() * 10000);
  const dbName = `modelFiles-${randomSuffix}`;

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
    },
  ];

  // Ensure every table & indices is on so that we can list files
  const files = await cache.listFiles({ taskName });
  Assert.deepEqual(files, expected);

  await deleteCache(cache);
});

/**
 * Test that upgrading from version 1 to version 2 results in existing data being deleted.
 */
add_task(async function test_initDbFromExistingNoChange() {
  const randomSuffix = Math.floor(Math.random() * 10000);
  const dbName = `modelFiles-${randomSuffix}`;

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
  const files = await cache.listFiles({ taskName });

  Assert.deepEqual(files, []);

  await deleteCache(cache);
});

/**
 * Test that upgrading an existing cache from another source is possible.
 */
add_task(async function test_initDbFromExistingElseWhereStoreChanges() {
  const randomSuffix = Math.floor(Math.random() * 10000);
  const dbName = `modelFiles-${randomSuffix}`;

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
    },
  ];

  // Ensure every table & indices is on so that we can list files
  const files = await cache2.listFiles({ taskName });
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
    modelHubUrlTemplate: "{model}/{revision}",
  };

  let [array, headers] = await hub.getModelFileAsArrayBuffer(args);

  Assert.equal(headers["Content-Type"], "application/json");

  // check the content of the file.
  let jsonData = JSON.parse(
    String.fromCharCode.apply(null, new Uint8Array(array))
  );

  Assert.equal(jsonData.hidden_size, 768);

  let res = await hub.getModelFileAsBlob(args);
  Assert.equal(res[0].size, 562);

  let response = await hub.getModelFileAsResponse(args);
  Assert.equal((await response.blob()).size, 562);
});

/**
 * Make sure that we can't pass a rootUrl that is not allowed when using the API calls
 */
add_task(async function test_getting_file_disallowed_custom_hub() {
  // The hub is configured to use localhost
  const hub = new ModelHub({
    rootUrl: "https://localhost",
    urlTemplate: "{model}/boo/revision",
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
});

/**
 * Test deleting files used by several engines
 */
add_task(async function test_DeleteFileByEngines() {
  const cache = await initializeCache();
  const testData = createBlob();
  const engineOne = "engine-1";
  const engineTwo = "engine-2";

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

  // if we delete the model by engineOne, it will still be around for engineTwo
  await cache.deleteFilesByEngine(engineOne);

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

  // now deleting via engineTwo
  await cache.deleteFilesByEngine(engineTwo);

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

  await hub.deleteFilesByEngine(engineOne);

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
    Assert.ok(e.name === "ForbiddenURLError");
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
  const randomSuffix = Math.floor(Math.random() * 10000);
  const dbName = `modelFiles-${randomSuffix}`;

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
  let remainingFiles = await cache.listFiles({
    model: "random/model",
    revision: "v1",
  });
  Assert.deepEqual(
    remainingFiles,
    [],
    "All unknown model files should be deleted."
  );

  remainingFiles = await cache.listFiles({
    model: "unknown/model",
    revision: "v2",
  });
  Assert.deepEqual(
    remainingFiles,
    [],
    "All unknown model files should be deleted."
  );

  await deleteCache(cache);
});

/**
 * Test migration when database starts empty.
 */
add_task(async function test_migrateStore_emptyDatabase() {
  const randomSuffix = Math.floor(Math.random() * 10000);
  const dbName = `modelFiles-${randomSuffix}`;

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

add_task(async function test_getOwnerIcon() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.logLevel", "All"]],
  });

  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });

  const fullyQualifiedModelName = "mochitests/mozilla/distilvit";

  // first call will get the icon from the web
  const icon = await hub.getOwnerIcon(fullyQualifiedModelName);
  Assert.notEqual(icon, null);

  // second call will get it from the cache
  let spy = sinon.spy(OPFS.File.prototype, "getBlobFromOPFS");

  const icon2 = await hub.getOwnerIcon(fullyQualifiedModelName);
  Assert.notEqual(icon2, null);

  // check that it cames from OPFS
  Assert.notEqual(await spy.lastCall.returnValue, null);

  sinon.restore();
});
