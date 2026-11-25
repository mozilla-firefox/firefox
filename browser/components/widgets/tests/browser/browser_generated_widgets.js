/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { GENERATION_ERRORS, OUTPUT_DIRECTORY, WidgetGenerator } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/widgets/WidgetGenerator.sys.mjs"
  );
const { CREATION_ERRORS, WidgetCreator } = ChromeUtils.importESModule(
  "moz-src:///browser/components/widgets/WidgetCreator.sys.mjs"
);
const { GeneratedWidgetsProvider } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/widgets/GeneratedWidgetsProvider.sys.mjs"
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

const ZipReader = Components.Constructor(
  "@mozilla.org/libjar/zip-reader;1",
  "nsIZipReader",
  "open"
);

const PROVIDER_PREF = "extensions.widgets.generated.enabled";

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
let build;

async function answer(...responses) {
  for (let response of responses) {
    await mockEngine.respondTo({
      purpose: PURPOSES.WIDGET_GENERATION,
      response,
    });
  }
}

function cleanup(path) {
  registerCleanupFunction(() => IOUtils.remove(path, { ignoreAbsent: true }));
}

async function generate(
  { generator = WidgetGenerator, ...options },
  ...responses
) {
  let [path] = await Promise.all([
    generator.generate(options),
    answer(...responses),
  ]);
  cleanup(path);
  return path;
}

function readZip(path) {
  let reader = new ZipReader(new FileUtils.File(path));
  try {
    return new Map(
      [...reader.findEntries(null)]
        .filter(name => !reader.getEntry(name).isDirectory)
        .map(name => {
          let stream = reader.getInputStream(name);
          let count = stream.available();
          return [
            name,
            count
              ? NetUtil.readInputStreamToString(stream, count, {
                  charset: "UTF-8",
                })
              : "",
          ];
        })
    );
  } finally {
    reader.close();
  }
}

async function sentSystemPrompt() {
  let generating = WidgetGenerator.generate({ prompt: PROMPT });
  let { request, respond } = await mockEngine.captureRequest({
    purpose: PURPOSES.WIDGET_GENERATION,
  });
  respond("Done.");
  await generating.catch(() => {});
  return withoutDate(
    request.args.find(message => message.role == "system").content
  );
}

function withoutDate(prompt) {
  let match = prompt.match(/^([\s\S]*)\n\nToday is \d{4}-\d{2}-\d{2}\.$/);
  Assert.ok(match, "Prompt ends with the date");
  return match?.[1];
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
  build = sinon.stub(openAIEngine, "build").callsFake(options =>
    build.wrappedMethod.call(openAIEngine, {
      ...options,
      purpose: PURPOSES.WIDGET_GENERATION,
    })
  );
  await setGeneratorRecords();
  await SpecialPowers.pushPrefEnv({ set: [[PROVIDER_PREF, true]] });

  registerCleanupFunction(async () => {
    build.restore();
    mockEngine.cleanupMocks();
  });
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
  cleanup(await making);
});

add_task(async function test_the_zip_file_goes_to_the_generator_directory() {
  let path = await generate(
    { prompt: PROMPT },
    simulateWriteFileResponse(FILES),
    "Done."
  );
  Assert.equal(
    PathUtils.parent(path),
    PathUtils.join(PathUtils.profileDir, OUTPUT_DIRECTORY),
    "Directory"
  );
  Assert.equal(readZip(path).get("widget.js"), FILES["widget.js"], "Files");
});

add_task(async function test_the_first_zip_clears_the_generator_directory() {
  let generator = new WidgetGenerator.constructor();
  let directory = PathUtils.join(PathUtils.profileDir, OUTPUT_DIRECTORY);
  let leftover = PathUtils.join(directory, "leftover.zip");
  await IOUtils.makeDirectory(directory);
  await IOUtils.writeUTF8(leftover, "");
  cleanup(leftover);
  await generate(
    { prompt: PROMPT, generator },
    simulateWriteFileResponse(FILES),
    "Done."
  );
  Assert.ok(!(await IOUtils.exists(leftover)), "Leftover removed");
});

add_task(async function test_later_zips_keep_the_earlier_ones() {
  let generator = new WidgetGenerator.constructor();
  let first = await generate(
    { prompt: PROMPT, generator },
    simulateWriteFileResponse(FILES),
    "Done."
  );
  let second = await generate(
    { prompt: PROMPT, generator },
    simulateWriteFileResponse(FILES),
    "Done."
  );
  let directory = PathUtils.join(PathUtils.profileDir, OUTPUT_DIRECTORY);
  Assert.deepEqual(
    (await IOUtils.getChildren(directory)).sort(),
    [first, second].sort(),
    "Both zips"
  );
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
  respond(simulateWriteFileResponse({ "widget.html": FILES["widget.html"] }));
  await answer("Done.");
  let files = readZip(await making);
  ok(!files.has("lib/units.js"), "Deleted file");
});

add_task(async function test_a_path_outside_the_package_is_rejected() {
  let making = generate({ prompt: PROMPT });
  await answer(
    simulateWriteFileResponse({
      ...FILES,
      "../widget.js": "",
      "lib/../widget.js": "",
      "lib\\units.js": "",
      "lib/": "",
      "meta-inf/manifest.mf": "",
    })
  );
  let { request, respond } = await mockEngine.captureRequest({
    purpose: PURPOSES.WIDGET_GENERATION,
  });
  Assert.deepEqual(
    request.args
      .filter(message => message.role == "tool")
      .slice(-5)
      .map(message => JSON.parse(message.content)),
    [
      "../widget.js is not a valid file_path. Use a relative path with / between folders, such as lib/units.js.",
      "lib/../widget.js is not a valid file_path. Use a relative path with / between folders, such as lib/units.js.",
      "lib\\units.js is not a valid file_path. Use a relative path with / between folders, such as lib/units.js.",
      "lib/ is not a valid file_path. Use a relative path with / between folders, such as lib/units.js.",
      "meta-inf/manifest.mf is in META-INF, which is reserved for signatures.",
    ],
    "Path errors"
  );
  respond("Done.");
  Assert.deepEqual(
    [...readZip(await making).keys()].sort(),
    ["lib/units.js", "widget.html", "widget.js", "widget.json"],
    "Packaged files"
  );
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
  let kept = readZip(await making).get("widget.js");
  is(kept, FILES["widget.js"], "Unchanged file");
});

add_task(async function test_replace_all_edits_every_match() {
  let path = await generate(
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
    readZip(path).get("widget.js"),
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
    let files = readZip(
      await generate(
        { prompt: PROMPT },
        simulateWriteFileResponse(FILES),
        "Done."
      )
    );
    Assert.equal(files.get("widget.js"), FILES["widget.js"], "Files");
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_the_endpoint_pref_sets_the_engine_url() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.widgets.generated.endpoint", "http://localhost:2/"]],
  });
  build.resetHistory();
  try {
    await generate(
      { prompt: PROMPT },
      simulateWriteFileResponse(FILES),
      "Done."
    );
    Assert.equal(build.lastCall.args[0].baseURL, "http://localhost:2/", "URL");
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_the_custom_endpoint_needs_no_account() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.widgets.generated.customEndpoint", "http://localhost:1/"]],
  });
  let [, getToken] = mockEngine.mocks;
  getToken.resetHistory();
  try {
    let files = readZip(
      await generate(
        { prompt: PROMPT },
        simulateWriteFileResponse(FILES),
        "Done."
      )
    );
    Assert.equal(files.get("widget.js"), FILES["widget.js"], "Files");
    Assert.ok(getToken.notCalled, "No account token requested");
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_the_custom_endpoint_is_still_blocked() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.widgets.generated.customEndpoint", "http://localhost:1/"],
      ["browser.ai.control.generatedWidgets", "blocked"],
    ],
  });
  try {
    await Assert.rejects(
      WidgetGenerator.generate({ prompt: PROMPT }),
      error => error.code == GENERATION_ERRORS.BLOCKED,
      "The custom endpoint respects the block"
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
  cleanup(await generating);
  Assert.equal(
    withoutDate(request.args.find(message => message.role == "system").content),
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

add_task(async function test_a_rejected_request_has_its_service_code() {
  for (let [status, mlpaCode, code] of [
    [401, undefined, GENERATION_ERRORS.AUTH],
    [403, 4, GENERATION_ERRORS.CAPACITY],
    [406, undefined, GENERATION_ERRORS.NETWORK],
    [413, 3, GENERATION_ERRORS.GENERATION],
    [429, 1, GENERATION_ERRORS.BUDGET],
    [429, 2, GENERATION_ERRORS.RATE_LIMIT],
    [429, 10, GENERATION_ERRORS.CAPACITY],
    [429, undefined, GENERATION_ERRORS.RATE_LIMIT],
    [500, 10, GENERATION_ERRORS.CAPACITY],
    [500, undefined, GENERATION_ERRORS.SERVICE],
  ]) {
    let generating = WidgetGenerator.generate({ prompt: PROMPT });
    let { reject } = await mockEngine.captureRequest({
      purpose: PURPOSES.WIDGET_GENERATION,
    });
    reject(
      Object.assign(new Error(`${status} status code`), {
        status,
        error: mlpaCode,
      })
    );
    await Assert.rejects(
      generating,
      error => error.code == code,
      `${status} ${mlpaCode ?? ""}`
    );
  }
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
  await mockEngine.captureRequest({
    purpose: PURPOSES.WIDGET_GENERATION,
  });
  controller.abort();
  await Assert.rejects(
    generating,
    error => error.name == "AbortError",
    "Aborted while generating"
  );
});

add_task(async function test_one_creation_is_pending_at_a_time() {
  let started = BrowserTestUtils.waitForEvent(
    WidgetCreator,
    "WidgetCreator:Creating"
  );
  let creating = WidgetCreator.create({ prompt: PROMPT });
  let { detail } = await started;
  is(detail.pending, creating, "Event promise");
  is(WidgetCreator.pending, creating, "Pending");
  is(WidgetCreator.prompt, PROMPT, "Prompt");

  let listener = sinon.spy();
  WidgetCreator.addEventListener("WidgetCreator:Creating", listener);
  await Assert.rejects(
    WidgetCreator.create({ prompt: PROMPT }),
    error => error.code == CREATION_ERRORS.BUSY,
    "Second creation"
  );
  WidgetCreator.removeEventListener("WidgetCreator:Creating", listener);
  ok(listener.notCalled, "No event");

  let install = sinon
    .stub(GeneratedWidgetsProvider, "install")
    .callsFake(path => {
      cleanup(path);
      WidgetCreator.cancel();
      is(WidgetCreator.pending, creating, "Cancel ignored while installing");
      return install.wrappedMethod.call(GeneratedWidgetsProvider, path);
    });
  try {
    await answer(simulateWriteFileResponse(FILES), "Done.");
    let { id } = await creating;
    registerCleanupFunction(() => GeneratedWidgetsProvider.uninstall(id));
  } finally {
    install.restore();
  }
  is(WidgetCreator.pending, null, "Cleared");
  is(WidgetCreator.prompt, null, "Prompt cleared");
});

add_task(async function test_a_failed_creation_is_cleared() {
  let failed = BrowserTestUtils.waitForEvent(
    WidgetCreator,
    "WidgetCreator:Error"
  );
  let creating = WidgetCreator.create({ prompt: PROMPT });
  await answer("Done.");
  await Assert.rejects(
    creating,
    error => error.code == GENERATION_ERRORS.GENERATION,
    "No files"
  );
  let { detail } = await failed;
  is(detail.error.code, GENERATION_ERRORS.GENERATION, "Event error");
  is(detail.prompt, PROMPT, "Event prompt");
  is(WidgetCreator.pending, null, "Cleared");
  is(WidgetCreator.prompt, null, "Prompt cleared");
});

add_task(async function test_cancel_rejects_during_a_turn() {
  let creating = WidgetCreator.create({ prompt: PROMPT });
  await mockEngine.captureRequest({
    purpose: PURPOSES.WIDGET_GENERATION,
  });
  let listener = sinon.spy();
  WidgetCreator.addEventListener("WidgetCreator:Error", listener);
  WidgetCreator.cancel();
  await Assert.rejects(
    creating,
    error => error.name == "AbortError",
    "Canceled"
  );
  WidgetCreator.removeEventListener("WidgetCreator:Error", listener);
  ok(listener.notCalled, "No error event");
  is(WidgetCreator.pending, null, "Cleared");
  is(WidgetCreator.prompt, null, "Prompt cleared");
});

add_task(async function test_cancel_after_generating_skips_the_install() {
  let generating = sinon
    .stub(WidgetGenerator, "generate")
    .callsFake(async () => {
      await TestUtils.waitForTick();
      WidgetCreator.cancel();
      return "widget.zip";
    });
  let install = sinon.spy(GeneratedWidgetsProvider, "install");
  try {
    await Assert.rejects(
      WidgetCreator.create({ prompt: PROMPT }),
      error => error.name == "AbortError",
      "Canceled"
    );
    await TestUtils.waitForTick();
    ok(install.notCalled, "Not installed");
  } finally {
    generating.restore();
    install.restore();
  }
});

/**
 * Stubs IOUtils.createUniqueFile in the generator's global to run a callback
 * once the zip file is created.
 *
 * @param {Function} onCreated - called with the new zip file's path
 */
function onZipCreated(onCreated) {
  let { IOUtils: moduleIOUtils } = Cu.getGlobalForObject(WidgetGenerator);
  let stub = sinon
    .stub(moduleIOUtils, "createUniqueFile")
    .callsFake(async (...args) => {
      let path = await stub.wrappedMethod.apply(moduleIOUtils, args);
      onCreated(path);
      return path;
    });
  return stub;
}

add_task(async function test_aborting_while_packaging_removes_the_zip() {
  let controller = new AbortController();
  let path;
  let stub = onZipCreated(created => {
    path = created;
    controller.abort();
  });
  try {
    await Assert.rejects(
      generate(
        { prompt: PROMPT, signal: controller.signal },
        simulateWriteFileResponse(FILES),
        "Done."
      ),
      error => error.name == "AbortError",
      "Aborted while packaging"
    );
  } finally {
    stub.restore();
  }
  ok(!(await IOUtils.exists(path)), "Zip removed");
});

add_task(async function test_blocking_while_packaging_removes_the_zip() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ai.control.generatedWidgets", "available"]],
  });
  let path;
  let stub = onZipCreated(created => {
    path = created;
    Services.prefs.setStringPref(
      "browser.ai.control.generatedWidgets",
      "blocked"
    );
  });
  try {
    await Assert.rejects(
      generate({ prompt: PROMPT }, simulateWriteFileResponse(FILES), "Done."),
      error => error.code == GENERATION_ERRORS.BLOCKED,
      "Blocked while packaging"
    );
  } finally {
    stub.restore();
    await SpecialPowers.popPrefEnv();
  }
  ok(!(await IOUtils.exists(path)), "Zip removed");
});

add_task(async function test_an_incomplete_widget_fails_validation() {
  let unnamed = { ...FILES };
  delete unnamed["manifest.json"];
  let unwritten = { ...FILES };
  delete unwritten["widget.html"];
  for (let [files, pattern, message] of [
    [unnamed, /manifest/, "No manifest"],
    [
      { ...FILES, "manifest.json": JSON.stringify({ name: "  " }) },
      /no name/,
      "Blank name",
    ],
    [unwritten, /page/, "Page not written"],
  ]) {
    await Assert.rejects(
      generate({ prompt: PROMPT }, simulateWriteFileResponse(files), "Done."),
      error =>
        error.code == GENERATION_ERRORS.GENERATION &&
        pattern.test(error.message),
      message
    );
  }
});

add_task(async function test_a_long_prompt_is_rejected() {
  let [createEngine, getToken] = mockEngine.mocks;
  createEngine.resetHistory();
  getToken.resetHistory();
  await Assert.rejects(
    WidgetGenerator.generate({ prompt: "x".repeat(2001) }),
    error =>
      error.code == GENERATION_ERRORS.GENERATION &&
      /prompt/.test(error.message),
    "Long prompt"
  );
  Assert.ok(getToken.notCalled, "No token");
  Assert.ok(createEngine.notCalled, "No engine");
  await generate(
    { prompt: "x".repeat(2000) },
    simulateWriteFileResponse(FILES),
    "Done."
  );
});

add_task(async function test_a_long_name_is_packaged() {
  let files = {
    ...FILES,
    "manifest.json": JSON.stringify({ name: "x".repeat(76) }),
  };
  let path = await generate(
    { prompt: PROMPT },
    simulateWriteFileResponse(files),
    "Done."
  );
  let { name } = JSON.parse(readZip(path).get("widget.json"));
  Assert.equal(name.length, 76, "Long name");
});

add_task(async function test_a_package_installs_and_its_page_loads() {
  let path = await generate(
    { prompt: PROMPT },
    simulateWriteFileResponse(FILES),
    "Done."
  );
  let { id } = await GeneratedWidgetsProvider.install(path);
  registerCleanupFunction(() => GeneratedWidgetsProvider.uninstall(id));
  let policy = WebExtensionPolicy.getByID(id);
  Assert.ok(policy?.active, "Running");
  let text = await BrowserTestUtils.withNewTab(
    policy.getURL("widget.html"),
    browser =>
      SpecialPowers.spawn(browser, [], async () => {
        await ContentTaskUtils.waitForCondition(
          () => content.document.body?.textContent,
          "Waiting for the widget"
        );
        return content.document.body.textContent;
      })
  );
  Assert.equal(text, "21.5°C", "Page");
});
