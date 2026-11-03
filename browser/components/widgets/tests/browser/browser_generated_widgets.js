/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { GENERATION_ERRORS, WidgetGenerator } = ChromeUtils.importESModule(
  "moz-src:///browser/components/widgets/WidgetGenerator.sys.mjs"
);
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);
const { PURPOSES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

const LOCAL_PROMPT_PREF = "browser.widgets.generated.localPrompt";

const PROMPT = "weather in Paris";

const FILES = {
  "manifest.json": JSON.stringify({
    manifest_version: 3,
    name: "Test weather",
  }),
  "widget.html": `<!doctype html>
<script type="module" src="widget.js"></script>`,
  "widget.js": `import { formatTemperature } from "./lib/units.js";

document.body.textContent = formatTemperature(21.5, "°C");`,
  "lib/units.js": `export function formatTemperature(temperature, unit) {
  return \`\${temperature}\${unit}\`;
}`,
};

let mockEngine;

async function answer(...responses) {
  for (let response of responses) {
    await mockEngine.respondTo({
      purpose: PURPOSES.WIDGET_GENERATION,
      response,
    });
  }
}

async function generate(options, ...responses) {
  let [files] = await Promise.all([
    WidgetGenerator.generate(options),
    answer(...responses),
  ]);
  return files;
}

async function sentSystemPrompt() {
  let generating = WidgetGenerator.generate({ prompt: PROMPT });
  let { request, respond } = await mockEngine.captureRequest({
    purpose: PURPOSES.WIDGET_GENERATION,
  });
  respond("Done.");
  await generating.catch(() => {});
  return request.args.find(message => message.role == "system").content;
}

async function localPrompt() {
  let response = await fetch("chrome://browser/content/widgets/prompt.md");
  return response.text();
}

add_setup(async function () {
  mockEngine = new MockEngineManager();
  await setGeneratorRecords();

  registerCleanupFunction(async () => {
    mockEngine.cleanupMocks();
  });
});

add_task(async function test_a_prompt_makes_a_widget() {
  let files = await generate(
    { prompt: PROMPT },
    simulateWriteFileResponse(FILES),
    "Done."
  );
  Assert.deepEqual(Object.fromEntries(files), FILES, "Files");
});

add_task(async function test_the_model_writes_the_files() {
  let making = WidgetGenerator.generate({ prompt: PROMPT });
  let asked = await mockEngine.captureRequest({
    purpose: PURPOSES.WIDGET_GENERATION,
  });
  Assert.deepEqual(
    asked.request.tools.map(tool => tool.function.name),
    ["write_file"],
    "Tools"
  );
  let content = asked.request.args.findLast(
    message => message.role == "user"
  ).content;
  Assert.equal(content, PROMPT, "Request");
  Assert.equal(asked.request.token, "mock-fxa-token", "Account token");
  let written = simulateWriteFileResponse(FILES);
  asked.respond(written);
  let replayed = await mockEngine.captureRequest({
    purpose: PURPOSES.WIDGET_GENERATION,
  });
  Assert.deepEqual(
    replayed.request.args.filter(message => message.role !== "system"),
    [
      { role: "user", content },
      { role: "assistant", content: "", tool_calls: written.toolCalls },
      ...written.toolCalls.map(call => ({
        role: "tool",
        content: '"ok"',
        tool_call_id: call.id,
        name: "write_file",
      })),
    ],
    "Writes replayed"
  );
  replayed.respond("Done.");
  await making;
});

add_task(async function test_a_reply_without_files_is_rejected() {
  await Assert.rejects(
    generate({ prompt: PROMPT }, "That widget would harass others."),
    error =>
      error.code == GENERATION_ERRORS.GENERATION &&
      /no files/.test(error.message),
    "Reply without files"
  );
});

add_task(async function test_a_signed_out_account_is_an_auth_error() {
  let [, getToken] = mockEngine.mocks;
  getToken.resolves(null);
  await Assert.rejects(
    WidgetGenerator.generate({ prompt: PROMPT }),
    error => error.code == GENERATION_ERRORS.AUTH,
    "Signed out"
  );
  getToken.resolves("mock-fxa-token");
});

add_task(async function test_only_this_major_version_is_read() {
  await setGeneratorRecords([
    GENERATOR_RECORD,
    {
      ...GENERATOR_RECORD,
      id: "newer-minor",
      version: `${PROMPT_MAJOR_VERSION}.1`,
      prompt: "Newer",
    },
    {
      ...GENERATOR_RECORD,
      id: "next-major",
      version: `${PROMPT_MAJOR_VERSION + 1}.0`,
      prompt: "Next major",
    },
  ]);
  let generating = WidgetGenerator.generate({ prompt: PROMPT });
  let { request, respond } = await mockEngine.captureRequest({
    purpose: PURPOSES.WIDGET_GENERATION,
  });
  respond(simulateWriteFileResponse(FILES));
  await answer("Done.");
  await generating;
  Assert.equal(
    request.args.find(message => message.role == "system").content,
    "Newer",
    "Selected highest minor generator record"
  );

  await setGeneratorRecords([
    { ...GENERATOR_RECORD, version: `${PROMPT_MAJOR_VERSION + 1}.0` },
  ]);
  Assert.equal(
    await sentSystemPrompt(),
    await localPrompt(),
    "Local prompt without a record"
  );
  await setGeneratorRecords();
});

add_task(async function test_the_pref_forces_the_local_prompt() {
  await SpecialPowers.pushPrefEnv({ set: [[LOCAL_PROMPT_PREF, true]] });
  Assert.equal(
    await sentSystemPrompt(),
    await localPrompt(),
    "Local prompt with a record"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_a_failed_request_is_a_service_error() {
  let generating = WidgetGenerator.generate({ prompt: PROMPT });
  await mockEngine.captureRequest({ purpose: PURPOSES.WIDGET_GENERATION });
  mockEngine.rejectAllRequests();
  await Assert.rejects(
    generating,
    error => error.code == GENERATION_ERRORS.SERVICE,
    "Request error"
  );
});

add_task(async function test_an_aborted_signal_ends_generation() {
  let [createEngine, getToken] = mockEngine.mocks;
  createEngine.resetHistory();
  let controller = new AbortController();
  getToken.callsFake(async () => {
    controller.abort();
    return "mock-fxa-token";
  });
  await Assert.rejects(
    WidgetGenerator.generate({ prompt: PROMPT, signal: controller.signal }),
    error => error.name == "AbortError",
    "Aborted while setting up"
  );
  Assert.ok(createEngine.notCalled, "No engine");
  getToken.resolves("mock-fxa-token");

  controller = new AbortController();
  let generating = WidgetGenerator.generate({
    prompt: PROMPT,
    signal: controller.signal,
  });
  let { respond } = await mockEngine.captureRequest({
    purpose: PURPOSES.WIDGET_GENERATION,
  });
  controller.abort();
  respond(simulateWriteFileResponse(FILES));
  await Assert.rejects(
    generating,
    error => error.name == "AbortError",
    "Aborted while generating"
  );
});
