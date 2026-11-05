/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { buildChatSystemPrompt, loadPrompt } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs"
);

const {
  checkMajorVersion,
  FEATURE_MAJOR_VERSIONS,
  getRemoteClient,
  MODEL_FEATURES,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

const CHAT_MODEL = "generic";
const REQUIRED_CHAT_MODULES = new Set(["identity", "response-rules"]);
let gChatParams;
let gChatModuleRecords;
let gSkillNames;

add_setup(async function read_real_chat_records() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.aitab.enabled", true]],
  });
  const records = await getRemoteClient().get();
  gChatParams = records.find(
    record =>
      record.kind === "params" &&
      record.feature === MODEL_FEATURES.CHAT &&
      record.model === CHAT_MODEL &&
      checkMajorVersion(
        record.version,
        FEATURE_MAJOR_VERSIONS[MODEL_FEATURES.CHAT]
      )
  );
  gChatModuleRecords = records.filter(
    record => record.kind === "module" && record.feature === MODEL_FEATURES.CHAT
  );
  gSkillNames = records
    .filter(record => record.kind === "skill" && record.model === "generic")
    .map(record => record.name)
    .sort((a, b) => a.localeCompare(b));

  Assert.ok(gChatParams, "Remote Settings provides chat params");
  Assert.ok(
    gChatParams.modules.map(module => module.name).length,
    "Modules are not empty"
  );
  Assert.ok(gSkillNames.length, "Skills are not empty");
});

function getManifestModuleMarker({ name, version }) {
  const record =
    gChatModuleRecords.find(
      moduleRecord =>
        moduleRecord.module === name &&
        moduleRecord.version === version &&
        moduleRecord.model === CHAT_MODEL
    ) ||
    gChatModuleRecords.find(
      moduleRecord =>
        moduleRecord.module === name &&
        moduleRecord.version === version &&
        moduleRecord.model === "generic"
    );
  if (!record) {
    Assert.ok(
      !REQUIRED_CHAT_MODULES.has(name),
      `${name} optional module missing`
    );
    return null;
  }
  return record.prompts.trim().split("\n")[0];
}

add_task(async function test_buildChatSystemPrompt_uses_manifest_order() {
  const { prompt, version } = await buildChatSystemPrompt(CHAT_MODEL);

  Assert.equal(
    version,
    gChatParams.version,
    "Uses params version from Remote Settings"
  );

  let previousIndex = -1;
  for (const module of gChatParams.modules) {
    const marker = getManifestModuleMarker(module);
    if (!marker) {
      continue;
    }
    const index = prompt.indexOf(marker);
    Assert.greater(index, previousIndex, `${marker} appears in manifest order`);
    previousIndex = index;
  }
});

add_task(
  async function test_buildChatSystemPrompt_renders_real_substitutions() {
    const { prompt } = await buildChatSystemPrompt(CHAT_MODEL);

    for (const placeholder of [
      "{skill_list}",
      "{locale}",
      "{timezone}",
      "{location}",
      "{isoTimestamp}",
      "{todayDate}",
    ]) {
      Assert.ok(!prompt.includes(placeholder), `${placeholder} is rendered`);
    }

    let previousIndex = -1;
    for (const skillName of gSkillNames) {
      const index = prompt.indexOf(`<name>${skillName}</name>`);
      Assert.greater(
        index,
        previousIndex,
        `${skillName} appears in skill order`
      );
      previousIndex = index;
    }
  }
);

/**
 * Test that the pages module follows the AITab preference.
 */
add_task(async function test_buildChatSystemPrompt_skips_gated_module() {
  const AITAB_PREF = "browser.smartwindow.aitab.enabled";

  await SpecialPowers.pushPrefEnv({
    set: [[AITAB_PREF, false]],
  });
  const records = await getRemoteClient().get();
  const params = records.find(
    record =>
      record.kind === "params" &&
      record.feature === MODEL_FEATURES.CHAT &&
      record.model === CHAT_MODEL &&
      checkMajorVersion(
        record.version,
        FEATURE_MAJOR_VERSIONS[MODEL_FEATURES.CHAT]
      )
  );
  const entry = params?.modules.find(module => module.name === "pages");
  Assert.ok(entry, "Chat manifest names the gated pages module");
  const pagesRecord = records.find(
    record =>
      record.kind === "module" &&
      record.feature === MODEL_FEATURES.CHAT &&
      record.module === "pages" &&
      record.model === CHAT_MODEL &&
      checkMajorVersion(record.version, parseInt(entry.version, 10))
  );
  Assert.ok(pagesRecord, "Remote Settings provides the pages module");
  const marker = pagesRecord.prompts.trim().split("\n")[0];

  let { prompt } = await buildChatSystemPrompt(CHAT_MODEL);
  Assert.ok(
    !prompt.includes(marker),
    "pages module is left out while the aitab pref is off"
  );
  await SpecialPowers.popPrefEnv();

  await SpecialPowers.pushPrefEnv({
    set: [[AITAB_PREF, true]],
  });
  ({ prompt } = await buildChatSystemPrompt(CHAT_MODEL));
  Assert.ok(
    prompt.includes(marker),
    "pages module is assembled while the aitab pref is on"
  );
  await SpecialPowers.popPrefEnv();
});
