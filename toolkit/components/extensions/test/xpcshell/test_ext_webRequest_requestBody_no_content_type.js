/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const server = createHttpServer({ hosts: ["example.com"] });
server.registerPathHandler("/upload", (request, response) => {
  Assert.ok(!request.hasHeader("Content-Type"), "Upload has no Content-Type");
  response.setStatusLine(request.httpVersion, 200, "OK");
});

add_task(async function test_request_body_without_content_type() {
  const url = "http://example.com/upload";
  const body = [0, 42, 255];
  const extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["webRequest", "http://example.com/"],
    },
    background() {
      browser.test.onMessage.addListener(async (url, body) => {
        const requestDetails = new Promise(resolve => {
          browser.webRequest.onBeforeRequest.addListener(
            resolve,
            { urls: [url] },
            ["requestBody"]
          );
        });
        // Wait for the webRequest listener to be registered (bug 1300234).
        await browser.runtime.getPlatformInfo();
        const response = await fetch(url, {
          method: "POST",
          body: new Uint8Array(body),
        });
        const { method, requestBody } = await requestDetails;
        browser.test.sendMessage("upload-complete", {
          status: response.status,
          method,
          formData: requestBody?.formData,
          raw: requestBody?.raw?.flatMap(({ bytes }) =>
            Array.from(new Uint8Array(bytes))
          ),
        });
      });
    },
  });

  await extension.startup();
  const { messages, result } = await AddonTestUtils.promiseConsoleOutput(() => {
    extension.sendMessage(url, body);
    return extension.awaitMessage("upload-complete");
  });
  await extension.unload();

  Assert.equal(result.status, 200, "Server received the upload");
  Assert.equal(result.method, "POST", "webRequest intercepted the upload");
  Assert.equal(result.formData, undefined, "Upload has no parsed form data");
  Assert.deepEqual(result.raw, body, "webRequest returns the original bytes");
  AddonTestUtils.checkMessages(messages, {
    forbidden: [{ message: /NS_ERROR_NOT_AVAILABLE/ }],
  });
});
