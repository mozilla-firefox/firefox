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
const { openAIEngine } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/openAIEngine.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
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

function simulateEditFileResponse(...edits) {
  return {
    finalOutput: "",
    toolCalls: edits.map((edit, index) => ({
      id: `call-edit-${index}`,
      type: "function",
      function: { name: "edit_file", arguments: JSON.stringify(edit) },
    })),
  };
}

function simulateReadFileResponse(...paths) {
  return {
    finalOutput: "",
    toolCalls: paths.map((path, index) => ({
      id: `call-read-${index}`,
      type: "function",
      function: {
        name: "read_file",
        arguments: JSON.stringify({ file_path: path }),
      },
    })),
  };
}

function simulateDeleteFileResponse(...paths) {
  return {
    finalOutput: "",
    toolCalls: paths.map((path, index) => ({
      id: `call-delete-${index}`,
      type: "function",
      function: {
        name: "delete_file",
        arguments: JSON.stringify({ file_path: path }),
      },
    })),
  };
}

function simulateDeclineRequestResponse(args) {
  return {
    finalOutput: "",
    toolCalls: [
      {
        id: "call-decline",
        type: "function",
        function: { name: "decline_request", arguments: JSON.stringify(args) },
      },
    ],
  };
}

add_setup(async function () {
  mockEngine = new MockEngineManager();
  // The widget-generation service takes no `purpose`. MockEngineManager finds
  // engines by purpose, so the tests add one.
  let build = sinon.stub(openAIEngine, "build").callsFake(options =>
    build.wrappedMethod.call(openAIEngine, {
      ...options,
      purpose: PURPOSES.WIDGET_GENERATION,
    })
  );
  await setGeneratorRecords();

  registerCleanupFunction(async () => {
    build.restore();
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
    ["write_file", "edit_file", "read_file", "delete_file", "decline_request"],
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
        content: JSON.stringify(
          `Wrote ${JSON.parse(call.function.arguments).file_path}.`
        ),
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

add_task(async function test_an_unknown_tool_is_answered_with_the_error() {
  let making = generate({ prompt: PROMPT });
  await answer({
    finalOutput: "",
    toolCalls: [
      {
        id: "call-rename",
        type: "function",
        function: {
          name: "rename_file",
          arguments: JSON.stringify({ file_path: "widget.js" }),
        },
      },
    ],
  });
  let { request, respond } = await mockEngine.captureRequest({
    purpose: PURPOSES.WIDGET_GENERATION,
  });
  Assert.deepEqual(
    request.args.at(-1),
    {
      role: "tool",
      content: JSON.stringify("Unknown tool rename_file."),
      tool_call_id: "call-rename",
      name: "rename_file",
    },
    "Error answer"
  );
  respond(simulateWriteFileResponse(FILES));
  await answer("Done.");
  await making;
});

add_task(async function test_arguments_that_are_not_an_object_are_answered() {
  let making = generate({ prompt: PROMPT });
  await answer({
    finalOutput: "",
    toolCalls: [
      {
        id: "call-write",
        type: "function",
        function: { name: "write_file", arguments: "null" },
      },
    ],
  });
  let { request, respond } = await mockEngine.captureRequest({
    purpose: PURPOSES.WIDGET_GENERATION,
  });
  is(
    JSON.parse(request.args.at(-1).content),
    "The write_file arguments are not a JSON object.",
    "Error answer"
  );
  respond(simulateWriteFileResponse(FILES));
  await answer("Done.");
  await making;
});

add_task(async function test_a_delete_removes_a_file() {
  let making = generate({ prompt: PROMPT });
  await answer(
    simulateWriteFileResponse(FILES),
    simulateDeleteFileResponse("./lib/units.js", "/widget.html", "widget.css"),
    simulateReadFileResponse("lib/units.js")
  );
  let { request, respond } = await mockEngine.captureRequest({
    purpose: PURPOSES.WIDGET_GENERATION,
  });
  Assert.deepEqual(
    request.args
      .filter(message => message.role == "tool")
      .slice(-4)
      .map(message => JSON.parse(message.content)),
    [
      "Deleted lib/units.js.",
      "Deleted widget.html.",
      "There is no file at widget.css.",
      "There is no file at lib/units.js.",
    ],
    "Delete and read answers"
  );
  respond("Done.");
  ok(!(await making).has("lib/units.js"), "Deleted file");
});

add_task(async function test_an_edit_needs_a_passage_found_once() {
  let making = generate({ prompt: PROMPT });
  await answer(
    simulateWriteFileResponse(FILES),
    simulateEditFileResponse(
      { file_path: "widget.js", old_string: "°F", new_string: "°C" },
      {
        file_path: "widget.js",
        old_string: "formatTemperature",
        new_string: "format",
      }
    )
  );
  let { request, respond } = await mockEngine.captureRequest({
    purpose: PURPOSES.WIDGET_GENERATION,
  });
  Assert.deepEqual(
    request.args.slice(-2).map(message => JSON.parse(message.content)),
    [
      "The old_string is not in widget.js. Read it with read_file and copy the passage exactly, or use write_file with the whole file.",
      "The old_string appears 2 times in widget.js. Include more of the surrounding text so it matches once, or set replace_all to replace every match.",
    ],
    "Error answers"
  );
  respond("Done.");
  let kept = (await making).get("widget.js");
  is(kept, FILES["widget.js"], "Unchanged file");
});

add_task(async function test_replace_all_edits_every_match() {
  let files = await generate(
    { prompt: PROMPT },
    simulateWriteFileResponse(FILES),
    simulateEditFileResponse({
      file_path: "widget.js",
      old_string: "formatTemperature",
      new_string: "$&Celsius",
      replace_all: true,
    }),
    "Done."
  );
  is(
    files.get("widget.js"),
    FILES["widget.js"].split("formatTemperature").join("$&Celsius"),
    "Every match replaced"
  );
});

add_task(async function test_a_read_is_answered_with_the_current_text() {
  let making = generate({ prompt: PROMPT });
  await answer(
    simulateWriteFileResponse(FILES),
    simulateEditFileResponse({
      file_path: "widget.js",
      old_string: '21.5, "°C"',
      new_string: '70.7, "°F"',
    }),
    simulateReadFileResponse("widget.js", "widget.css")
  );
  let { request, respond } = await mockEngine.captureRequest({
    purpose: PURPOSES.WIDGET_GENERATION,
  });
  Assert.deepEqual(
    request.args.slice(-2).map(message => JSON.parse(message.content)),
    [
      FILES["widget.js"].replace('21.5, "°C"', '70.7, "°F"'),
      "There is no file at widget.css.",
    ],
    "Read answers"
  );
  respond("Done.");
  await making;
});

add_task(async function test_a_declined_request_rejects_with_its_kind() {
  for (let [kind, code] of [
    ["unsupported", GENERATION_ERRORS.UNSUPPORTED],
    ["unclear", GENERATION_ERRORS.UNCLEAR],
    ["policy", GENERATION_ERRORS.POLICY],
  ]) {
    await Assert.rejects(
      generate(
        { prompt: PROMPT },
        simulateWriteFileResponse(FILES),
        simulateDeclineRequestResponse({ kind, reason: "Test reason" })
      ),
      error => error.code == code,
      kind
    );
  }
});

add_task(async function test_an_unknown_decline_kind_is_answered() {
  let making = generate({ prompt: PROMPT });
  await answer(simulateDeclineRequestResponse({ kind: "other" }));
  let { request, respond } = await mockEngine.captureRequest({
    purpose: PURPOSES.WIDGET_GENERATION,
  });
  is(
    JSON.parse(request.args.at(-1).content),
    "The kind must be one of unsupported, unclear, policy.",
    "Error answer"
  );
  respond(simulateWriteFileResponse(FILES));
  await answer("Done.");
  await making;
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

add_task(async function test_a_blocked_row_is_a_blocked_error() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ai.control.generatedWidgets", "blocked"]],
  });
  let [, getToken] = mockEngine.mocks;
  getToken.resetHistory();
  try {
    await Assert.rejects(
      WidgetGenerator.generate({ prompt: PROMPT }),
      error => error.code == GENERATION_ERRORS.BLOCKED,
      "Custom widgets are blocked"
    );
    Assert.ok(getToken.notCalled, "No account token requested");
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_the_global_block_is_a_blocked_error() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ai.control.generatedWidgets", "default"],
      ["browser.ai.control.default", "blocked"],
    ],
  });
  try {
    await Assert.rejects(
      WidgetGenerator.generate({ prompt: PROMPT }),
      error => error.code == GENERATION_ERRORS.BLOCKED,
      "Custom widgets inherit the global block"
    );
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_the_row_overrides_the_global_block() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ai.control.generatedWidgets", "available"],
      ["browser.ai.control.default", "blocked"],
    ],
  });
  try {
    let files = await generate(
      { prompt: PROMPT },
      simulateWriteFileResponse(FILES),
      "Done."
    );
    Assert.deepEqual(Object.fromEntries(files), FILES, "Files");
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_the_endpoint_override_needs_no_account() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.widgets.generated.endpoint", "http://localhost:1/"]],
  });
  let [, getToken] = mockEngine.mocks;
  getToken.resetHistory();
  try {
    let files = await generate(
      { prompt: PROMPT },
      simulateWriteFileResponse(FILES),
      "Done."
    );
    Assert.deepEqual(Object.fromEntries(files), FILES, "Files");
    Assert.ok(getToken.notCalled, "No account token requested");
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_the_endpoint_override_is_still_blocked() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.widgets.generated.endpoint", "http://localhost:1/"],
      ["browser.ai.control.generatedWidgets", "blocked"],
    ],
  });
  try {
    await Assert.rejects(
      WidgetGenerator.generate({ prompt: PROMPT }),
      error => error.code == GENERATION_ERRORS.BLOCKED,
      "The endpoint override respects the block"
    );
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_blocking_during_setup_sends_no_request() {
  let [, getToken] = mockEngine.mocks;
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ai.control.generatedWidgets", "available"]],
  });
  try {
    getToken.callsFake(async () => {
      Services.prefs.setStringPref(
        "browser.ai.control.generatedWidgets",
        "blocked"
      );
      return "mock-fxa-token";
    });
    await Assert.rejects(
      WidgetGenerator.generate({ prompt: PROMPT }),
      error => error.code == GENERATION_ERRORS.BLOCKED,
      "Blocked before the first request"
    );
  } finally {
    getToken.resolves("mock-fxa-token");
    await SpecialPowers.popPrefEnv();
  }
});

add_task(
  async function test_blocking_mid_generation_stops_before_the_next_turn() {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.ai.control.generatedWidgets", "available"]],
    });
    try {
      let generating = WidgetGenerator.generate({ prompt: PROMPT });
      let { respond } = await mockEngine.captureRequest({
        purpose: PURPOSES.WIDGET_GENERATION,
      });
      Services.prefs.setStringPref(
        "browser.ai.control.generatedWidgets",
        "blocked"
      );
      respond(simulateWriteFileResponse(FILES));
      await Assert.rejects(
        generating,
        error => error.code == GENERATION_ERRORS.BLOCKED,
        "Blocked before the next turn"
      );
    } finally {
      await SpecialPowers.popPrefEnv();
    }
  }
);

add_task(async function test_blocking_during_the_last_reply_discards_it() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ai.control.generatedWidgets", "available"]],
  });
  try {
    let generating = WidgetGenerator.generate({ prompt: PROMPT });
    await answer(simulateWriteFileResponse(FILES));
    let { respond } = await mockEngine.captureRequest({
      purpose: PURPOSES.WIDGET_GENERATION,
    });
    Services.prefs.setStringPref(
      "browser.ai.control.generatedWidgets",
      "blocked"
    );
    respond("Done.");
    await Assert.rejects(
      generating,
      error => error.code == GENERATION_ERRORS.BLOCKED,
      "The final reply is discarded when blocked"
    );
  } finally {
    await SpecialPowers.popPrefEnv();
  }
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
