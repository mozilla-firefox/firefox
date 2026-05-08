/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/// <reference path="head.js" />

// The runner.js script-blob exposes exactly one entry point — `run(done, args)`.
// Every test below routes its operation through this surface and asserts on
// the resulting `{ name, result }` / `{ name, error }` envelope. Internal
// helpers (serializeEntry, serializeError, normalizeLanguagePair, etc.) are
// verified implicitly via the observable envelope shape.

const { run } = ChromeUtils.importESModule(
  "resource://gre/modules/firefox_inference/Runner.sys.mjs"
);

function dispatch(...args) {
  return new Promise(resolve => run(resolve, args));
}

// ---------------------------------------------------------------------------
// Happy path: full create -> run -> destroy lifecycle through `run`.
// Implicitly verifies serializeInferenceResponse and serializeEntry — the
// engine's response object reaches the test as plain JSON-friendly data.
// ---------------------------------------------------------------------------

add_task(async function test_run_envelope_lifecycle() {
  const { cleanup, remoteClients } = await setup();

  info("create_ml_engine via dispatch");
  const create = await dispatch("create_ml_engine", {
    taskName: "moz-echo",
    timeoutMS: -1,
  });
  Assert.equal(create.name, "success", "create envelope: success");
  Assert.ok(
    create.result?.engineId,
    "create envelope: engineId is returned"
  );
  const engineId = create.result.engineId;

  info("run_ml_engine via dispatch");
  const runPromise = dispatch("run_ml_engine", engineId, { data: "hello" });
  await remoteClients["ml-onnx-runtime"].resolvePendingDownloads(1);
  const ran = await runPromise;
  Assert.equal(ran.name, "success", "run envelope: success");
  Assert.equal(
    ran.result.output.echo,
    "hello",
    "run envelope: moz-echo round-trips the input"
  );

  info("destroy_ml_engine via dispatch");
  const destroyed = await dispatch("destroy_ml_engine", engineId, {
    shutdown: true,
  });
  Assert.equal(destroyed.name, "success", "destroy envelope: success");
  Assert.equal(
    destroyed.result.engineId,
    engineId,
    "destroy envelope: engineId is echoed back"
  );
  Assert.equal(
    destroyed.result.status,
    "closed",
    "destroy envelope: status is closed after termination"
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
});

// ---------------------------------------------------------------------------
// Unknown dispatch name -> error envelope. Covers the handleMessage default
// arm + serializeError + envelope shape in one shot.
// ---------------------------------------------------------------------------

add_task(async function test_run_unknown_message_envelope() {
  const env = await dispatch("not_a_real_message");
  Assert.equal(env.name, "error", "envelope: error");
  Assert.ok(
    env.error.message.includes("Unknown message"),
    "envelope.error.message identifies the bad dispatch"
  );
  Assert.equal(env.error.name, "Error", "envelope.error.name is 'Error'");
  Assert.ok(
    typeof env.error.stack === "string" && env.error.stack.length,
    "envelope.error.stack is a non-empty string"
  );
});

// ---------------------------------------------------------------------------
// run_ml_engine against an unknown engineId -> error envelope.
// Verifies the unknown-engineId throw path through the public surface.
// ---------------------------------------------------------------------------

add_task(async function test_run_engine_not_found_envelope() {
  const env = await dispatch("run_ml_engine", "does-not-exist", {});
  Assert.equal(env.name, "error", "envelope: error");
  Assert.ok(
    /Engine does-not-exist not found/.test(env.error.message),
    "envelope.error.message identifies the missing engine"
  );
});

// ---------------------------------------------------------------------------
// run_ml_engine with a blank engineId -> error envelope (validation runs
// before the registry lookup).
// ---------------------------------------------------------------------------

add_task(async function test_run_engine_blank_id_envelope() {
  const env = await dispatch("run_ml_engine", "", {});
  Assert.equal(env.name, "error", "envelope: error");
  Assert.ok(
    /engineId is required/.test(env.error.message),
    "envelope.error.message rejects empty engineId"
  );
});

// ---------------------------------------------------------------------------
// create_translations_session with a missing language field -> error
// envelope. Validates that normalizeLanguagePair runs before any
// TranslationsParent call (no port is requested).
// ---------------------------------------------------------------------------

add_task(async function test_run_translations_missing_target_envelope() {
  const env = await dispatch("create_translations_session", {
    sourceLanguage: "en",
  });
  Assert.equal(env.name, "error", "envelope: error");
  Assert.ok(
    /requires both sourceLanguage and targetLanguage/.test(env.error.message),
    "envelope.error.message identifies the missing targetLanguage"
  );
});

add_task(async function test_run_translations_missing_source_envelope() {
  const env = await dispatch("create_translations_session", {
    targetLanguage: "fr",
  });
  Assert.equal(env.name, "error", "envelope: error");
  Assert.ok(
    /requires both sourceLanguage and targetLanguage/.test(env.error.message),
    "envelope.error.message identifies the missing sourceLanguage"
  );
});
