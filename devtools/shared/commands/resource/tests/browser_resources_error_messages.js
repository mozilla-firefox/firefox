/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test the ResourceCommand API around ERROR_MESSAGE
// Reproduces assertions from devtools/shared/webconsole/test/chrome/test_page_errors.html

// Create a simple server so we have a nice sourceName in the resources packets.
const httpServer = createTestHTTPServer();
httpServer.registerPathHandler(`/test_page_errors.html`, (req, res) => {
  res.setStatusLine(req.httpVersion, 200, "OK");
  res.write(`<!DOCTYPE html><meta charset=utf8>Test Error Messages`);
});

const TEST_URI = `http://localhost:${httpServer.identity.primaryPort}/test_page_errors.html`;

add_task(async function () {
  // Disable the preloaded process as it creates processes intermittently
  // which forces the emission of RDP requests we aren't correctly waiting for.
  await pushPref("dom.ipc.processPrelaunch.enabled", false);

  await testErrorMessagesResources();
  await testErrorMessagesResourcesWithIgnoreExistingResources();
});

async function testErrorMessagesResources() {
  // Open a test tab
  const tab = await addTab(TEST_URI);

  const { client, resourceCommand, targetCommand } =
    await initResourceCommand(tab);

  const receivedMessages = [];
  // The expected messages are the errors, twice (once for cached messages, once for live messages)
  const expectedMessages = Array.from(expectedPageErrors.values()).concat(
    Array.from(expectedPageErrors.values())
  );

  info(
    "Log some errors *before* calling ResourceCommand.watchResources in order to assert" +
      " the behavior of already existing messages."
  );
  await triggerErrors(tab, resourceCommand);

  let done;
  const onAllErrorReceived = new Promise(resolve => (done = resolve));
  const onAvailable = resources => {
    for (const resource of resources) {
      const { pageError } = resource;

      is(
        resource.targetFront,
        targetCommand.targetFront,
        "The targetFront property is the expected one"
      );

      if (!pageError.sourceName.includes("test_page_errors")) {
        info(`Ignore error from unknown source: "${pageError.sourceName}"`);
        continue;
      }

      const index = receivedMessages.length;
      receivedMessages.push(resource);

      const isAlreadyExistingResource =
        receivedMessages.length <= expectedPageErrors.size;
      is(
        resource.isAlreadyExistingResource,
        isAlreadyExistingResource,
        "isAlreadyExistingResource has expected value"
      );

      info(`checking received page error #${index}: ${pageError.errorMessage}`);
      ok(pageError, "The resource has a pageError attribute");
      checkPageErrorResource(pageError, expectedMessages[index]);

      if (receivedMessages.length == expectedMessages.length) {
        done();
      }
    }
  };

  await resourceCommand.watchResources([resourceCommand.TYPES.ERROR_MESSAGE], {
    onAvailable,
  });

  await BrowserTestUtils.waitForCondition(
    () => receivedMessages.length === expectedPageErrors.size
  );

  info(
    "Now log errors *after* the call to ResourceCommand.watchResources and after having" +
      " received all existing messages"
  );
  await triggerErrors(tab, resourceCommand);

  info("Waiting for all expected errors to be received");
  await onAllErrorReceived;
  ok(true, "All the expected errors were received");

  Services.console.reset();
  targetCommand.destroy();
  await client.close();
}

async function testErrorMessagesResourcesWithIgnoreExistingResources() {
  info("Test ignoreExistingResources option for ERROR_MESSAGE");
  const tab = await addTab(TEST_URI);

  const { client, resourceCommand, targetCommand } =
    await initResourceCommand(tab);

  info(
    "Check whether onAvailable will not be called with existing error messages"
  );
  await triggerErrors(tab, resourceCommand);

  const availableResources = [];
  await resourceCommand.watchResources([resourceCommand.TYPES.ERROR_MESSAGE], {
    onAvailable: resources => availableResources.push(...resources),
    ignoreExistingResources: true,
  });
  is(
    availableResources.length,
    0,
    "onAvailable wasn't called for existing error messages"
  );

  info(
    "Check whether onAvailable will be called with the future error messages"
  );
  await triggerErrors(tab, resourceCommand);

  const expectedMessages = Array.from(expectedPageErrors.values());
  await waitUntil(() => availableResources.length === expectedMessages.length);
  for (let i = 0; i < expectedMessages.length; i++) {
    const resource = availableResources[i];
    const { pageError } = resource;
    const expected = expectedMessages[i];
    checkPageErrorResource(pageError, expected);
    is(
      resource.isAlreadyExistingResource,
      false,
      "isAlreadyExistingResource is set to false for live messages"
    );
  }

  Services.console.reset();
  targetCommand.destroy();
  await client.close();
}

/**
 * Triggers all the errors in the content page.
 */
async function triggerErrors(tab, resourceCommand) {
  for (const [expression, expected] of expectedPageErrors.entries()) {
    if (
      !expected[noUncaughtException] &&
      !Services.appinfo.browserTabsRemoteAutostart
    ) {
      expectUncaughtException();
    }

    const { promise: onErrorMessage, resolve } = Promise.withResolvers();
    const onAvailable = resources => {
      if (
        resources.some(r =>
          expected.errorMessage.test(r.pageError.errorMessage)
        )
      ) {
        resolve();
      }
    };
    await resourceCommand.watchResources(
      [resourceCommand.TYPES.ERROR_MESSAGE],
      {
        onAvailable,
        ignoreExistingResources: true,
      }
    );

    await ContentTask.spawn(
      tab.linkedBrowser,
      expression,
      function frameScript(expr) {
        const document = content.document;
        const scriptEl = document.createElement("script");
        scriptEl.textContent = expr;
        document.body.appendChild(scriptEl);
      }
    );

    await onErrorMessage;
    await resourceCommand.unwatchResources(
      [resourceCommand.TYPES.ERROR_MESSAGE],
      {
        onAvailable,
      }
    );
  }
}

function checkPageErrorResource(pageErrorResource, expected) {
  // Let's remove test harness related frames in stacktrace
  const clonedPageErrorResource = { ...pageErrorResource };
  if (clonedPageErrorResource.stacktrace) {
    const index = clonedPageErrorResource.stacktrace.findIndex(frame =>
      frame.filename.startsWith("resource://testing-common/content-task.js")
    );
    if (index > -1) {
      clonedPageErrorResource.stacktrace =
        clonedPageErrorResource.stacktrace.slice(0, index);
    }
  }
  checkObject(clonedPageErrorResource, expected);
}

const noUncaughtException = Symbol();
const NUMBER_REGEX = /^\d+$/;
// timeStamp are the result of a number in microsecond divided by 1000.
// so we can't expect a precise number of decimals, or even if there would
// be decimals at all.
const FRACTIONAL_NUMBER_REGEX = /^\d+(\.\d{1,3})?$/;

const mdnUrl = path =>
  `https://developer.mozilla.org/${path}?utm_source=mozilla&utm_medium=firefox-console-errors&utm_campaign=default`;

const expectedPageErrors = new Map([
  [
    "document.doTheImpossible();",
    {
      errorMessage: /doTheImpossible/,
      errorMessageName: "JSMSG_NOT_FUNCTION",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      exceptionDocURL: mdnUrl(
        "docs/Web/JavaScript/Reference/Errors/Not_a_function"
      ),
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [
        {
          filename: /test_page_errors\.html/,
          lineNumber: 1,
          columnNumber: 10,
          functionName: null,
        },
      ],
      notes: null,
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
    },
  ],
  [
    "(42).toString(0);",
    {
      errorMessage: /radix/,
      errorMessageName: "JSMSG_BAD_RADIX",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      exceptionDocURL: mdnUrl("docs/Web/JavaScript/Reference/Errors/Bad_radix"),
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [
        {
          filename: /test_page_errors\.html/,
          lineNumber: 1,
          columnNumber: 6,
          functionName: null,
        },
      ],
      notes: null,
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
    },
  ],
  [
    "'use strict'; (Object.freeze({name: 'Elsa', score: 157})).score = 0;",
    {
      errorMessage: /read.only/,
      errorMessageName: "JSMSG_READ_ONLY",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      exceptionDocURL: mdnUrl("docs/Web/JavaScript/Reference/Errors/Read-only"),
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [
        {
          filename: /test_page_errors\.html/,
          lineNumber: 1,
          columnNumber: 23,
          functionName: null,
        },
      ],
      notes: null,
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
    },
  ],
  [
    "([]).length = -1",
    {
      errorMessage: /array length/,
      errorMessageName: "JSMSG_BAD_ARRAY_LENGTH",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      exceptionDocURL: mdnUrl(
        "docs/Web/JavaScript/Reference/Errors/Invalid_array_length"
      ),
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [
        {
          filename: /test_page_errors\.html/,
          lineNumber: 1,
          columnNumber: 2,
          functionName: null,
        },
      ],
      notes: null,
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
    },
  ],
  [
    "'abc'.repeat(-1);",
    {
      errorMessage: /repeat count.*non-negative/,
      errorMessageName: "JSMSG_NEGATIVE_REPETITION_COUNT",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      exceptionDocURL: mdnUrl(
        "docs/Web/JavaScript/Reference/Errors/Negative_repetition_count"
      ),
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [
        {
          filename: "self-hosted",
          sourceId: null,
          lineNumber: NUMBER_REGEX,
          columnNumber: NUMBER_REGEX,
          functionName: "repeat",
        },
        {
          filename: /test_page_errors\.html/,
          lineNumber: 1,
          columnNumber: 7,
          functionName: null,
        },
      ],
      notes: null,
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
    },
  ],
  [
    "'a'.repeat(2e28);",
    {
      errorMessage: /repeat count.*less than infinity/,
      errorMessageName: "JSMSG_RESULTING_STRING_TOO_LARGE",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      exceptionDocURL: mdnUrl(
        "docs/Web/JavaScript/Reference/Errors/Resulting_string_too_large"
      ),
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [
        {
          filename: "self-hosted",
          sourceId: null,
          lineNumber: NUMBER_REGEX,
          columnNumber: NUMBER_REGEX,
          functionName: "repeat",
        },
        {
          filename: /test_page_errors\.html/,
          lineNumber: 1,
          columnNumber: 5,
          functionName: null,
        },
      ],
      notes: null,
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
    },
  ],
  [
    "77.1234.toExponential(-1);",
    {
      errorMessage: /out of range/,
      errorMessageName: "JSMSG_PRECISION_RANGE",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      exceptionDocURL: mdnUrl(
        "docs/Web/JavaScript/Reference/Errors/Precision_range"
      ),
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [
        {
          filename: /test_page_errors\.html/,
          lineNumber: 1,
          columnNumber: 9,
          functionName: null,
        },
      ],
      notes: null,
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
    },
  ],
  [
    "function a() { return; 1 + 1; }",
    {
      errorMessage: /unreachable code/,
      errorMessageName: "JSMSG_STMT_AFTER_RETURN",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: false,
      warning: true,
      info: false,
      sourceId: null,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      exceptionDocURL: mdnUrl(
        "docs/Web/JavaScript/Reference/Errors/Stmt_after_return"
      ),
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: null,
      notes: null,
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
    },
  ],
  [
    "{let a, a;}",
    {
      errorMessage: /redeclaration of/,
      errorMessageName: "JSMSG_REDECLARED_VAR",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      sourceId: null,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      exceptionDocURL: mdnUrl(
        "docs/Web/JavaScript/Reference/Errors/Redeclared_parameter"
      ),
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [],
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
      notes: [
        {
          messageBody: /Previously declared at line/,
          frame: {
            source: /test_page_errors/,
          },
        },
      ],
    },
  ],
  [
    `var error = new TypeError("abc");
      error.name = "MyError";
      error.message = "here";
      throw error`,
    {
      errorMessage: /MyError: here/,
      errorMessageName: "",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      exceptionDocURL: undefined,
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [
        {
          filename: /test_page_errors\.html/,
          lineNumber: 1,
          columnNumber: 13,
          functionName: null,
        },
      ],
      notes: null,
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
    },
  ],
  [
    "DOMTokenList.prototype.contains.call([])",
    {
      errorMessage: /does not implement interface/,
      errorMessageName: "MSG_METHOD_THIS_DOES_NOT_IMPLEMENT_INTERFACE",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      exceptionDocURL: undefined,
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [
        {
          filename: /test_page_errors\.html/,
          lineNumber: 1,
          columnNumber: 33,
          functionName: null,
        },
      ],
      notes: null,
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
    },
  ],
  [
    `
      function promiseThrow() {
        var error2 = new TypeError("abc");
        error2.name = "MyPromiseError";
        error2.message = "here2";
        return Promise.reject(error2);
      }
      promiseThrow()`,
    {
      errorMessage: /MyPromiseError: here2/,
      errorMessageName: "",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      exceptionDocURL: undefined,
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [
        {
          filename: /test_page_errors\.html/,
          sourceId: null,
          lineNumber: 6,
          columnNumber: 24,
          functionName: "promiseThrow",
        },
        {
          filename: /test_page_errors\.html/,
          sourceId: null,
          lineNumber: 8,
          columnNumber: 7,
          functionName: null,
        },
      ],
      notes: null,
      chromeContext: false,
      isPromiseRejection: true,
      isForwardedFromContentProcess: false,
      [noUncaughtException]: true,
    },
  ],
  [
    // Error with a cause
    `var originalError = new TypeError("abc");
      var error = new Error("something went wrong", { cause: originalError })
      throw error`,
    {
      errorMessage: /Error: something went wrong/,
      errorMessageName: "",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [
        {
          filename: /test_page_errors\.html/,
          lineNumber: 2,
          columnNumber: 19,
          functionName: null,
        },
      ],
      exception: {
        preview: {
          cause: {
            class: "TypeError",
            preview: {
              message: "abc",
            },
          },
        },
      },
      notes: null,
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
    },
  ],
  [
    // Error with a cause chain
    `var a = new Error("err-a");
     var b = new Error("err-b", { cause: a });
     var c = new Error("err-c", { cause: b });
     var d = new Error("err-d", { cause: c });
      throw d`,
    {
      errorMessage: /Error: err-d/,
      errorMessageName: "",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [
        {
          filename: /test_page_errors\.html/,
          lineNumber: 4,
          columnNumber: 14,
          functionName: null,
        },
      ],
      exception: {
        preview: {
          cause: {
            class: "Error",
            preview: {
              message: "err-c",
              cause: {
                class: "Error",
                preview: {
                  message: "err-b",
                  cause: {
                    class: "Error",
                    preview: {
                      message: "err-a",
                    },
                  },
                },
              },
            },
          },
        },
      },
      notes: null,
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
    },
  ],
  [
    // Error with a null cause
    `throw new Error("something went wrong", { cause: null })`,
    {
      errorMessage: /Error: something went wrong/,
      errorMessageName: "",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [
        {
          filename: /test_page_errors\.html/,
          lineNumber: 1,
          columnNumber: 7,
          functionName: null,
        },
      ],
      exception: {
        preview: {
          cause: {
            type: "null",
          },
        },
      },
      notes: null,
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
    },
  ],
  [
    // Error with an undefined cause
    `throw new Error("something went wrong", { cause: undefined })`,
    {
      errorMessage: /Error: something went wrong/,
      errorMessageName: "",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [
        {
          filename: /test_page_errors\.html/,
          lineNumber: 1,
          columnNumber: 7,
          functionName: null,
        },
      ],
      exception: {
        preview: {
          cause: {
            type: "undefined",
          },
        },
      },
      notes: null,
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
    },
  ],
  [
    // Error with a number cause
    `throw new Error("something went wrong", { cause: 0 })`,
    {
      errorMessage: /Error: something went wrong/,
      errorMessageName: "",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [
        {
          filename: /test_page_errors\.html/,
          lineNumber: 1,
          columnNumber: 7,
          functionName: null,
        },
      ],
      exception: {
        preview: {
          cause: 0,
        },
      },
      notes: null,
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
    },
  ],
  [
    // Error with a string cause
    `throw new Error("something went wrong", { cause: "ooops" })`,
    {
      errorMessage: /Error: something went wrong/,
      errorMessageName: "",
      sourceName: /test_page_errors/,
      category: "content javascript",
      timeStamp: FRACTIONAL_NUMBER_REGEX,
      error: true,
      warning: false,
      info: false,
      lineNumber: NUMBER_REGEX,
      columnNumber: NUMBER_REGEX,
      innerWindowID: NUMBER_REGEX,
      private: false,
      stacktrace: [
        {
          filename: /test_page_errors\.html/,
          lineNumber: 1,
          columnNumber: 7,
          functionName: null,
        },
      ],
      exception: {
        preview: {
          cause: "ooops",
        },
      },
      notes: null,
      chromeContext: false,
      isPromiseRejection: false,
      isForwardedFromContentProcess: false,
    },
  ],
]);
