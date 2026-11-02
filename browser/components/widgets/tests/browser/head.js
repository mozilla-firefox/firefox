/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PROMPT_MAJOR_VERSION } = ChromeUtils.importESModule(
  "moz-src:///browser/components/widgets/WidgetGenerator.sys.mjs"
);

const GENERATOR_RECORD = {
  id: "widget-generation-1-0",
  version: `${PROMPT_MAJOR_VERSION}.0`,
  model: "record-model",
  parameters: { reasoning_effort: "high" },
  prompt: "Record prompt",
};

/**
 * Replaces the widget-generation records in remote settings.
 *
 * @param {object[]} [records] - the records to store
 */
async function setGeneratorRecords(records = [GENERATOR_RECORD]) {
  let { RemoteSettings } = ChromeUtils.importESModule(
    "resource://services-settings/remote-settings.sys.mjs"
  );
  let client = RemoteSettings("widget-generation");
  await client.db.importChanges({}, Date.now(), records, { clear: true });
  registerCleanupFunction(() => client.db.clear());
}

/**
 * Builds a model reply that calls write_file once for each file.
 *
 * @param {{[path: string]: string}} files - the file contents, by path
 * @returns {object} the reply
 */
function simulateWriteFileResponse(files) {
  return {
    finalOutput: "",
    toolCalls: Object.entries(files).map(([file_path, content], index) => ({
      id: `call-${index}`,
      type: "function",
      function: {
        name: "write_file",
        arguments: JSON.stringify({ file_path, content }),
      },
    })),
  };
}
