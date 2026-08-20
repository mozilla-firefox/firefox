/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Validates policies-schema.json against policies-schema.meta.json, so every
// policy has metadata that downstream tooling relies on. A new rule that JSON
// Schema can express belongs in policies-schema.meta.json.

const { JsonSchema } = ChromeUtils.importESModule(
  "resource://gre/modules/JsonSchema.sys.mjs"
);

let metaSchema;

function readSupportJSON(name) {
  return IOUtils.readJSON(PathUtils.join(do_get_cwd().path, name));
}

add_setup(async function () {
  metaSchema = await readSupportJSON("policies-schema.meta.json");
});

add_task(async function test_policies_schema_has_required_metadata() {
  let policiesSchema = await readSupportJSON("policies-schema.json");

  let validator = new JsonSchema.Validator(metaSchema, { shortCircuit: false });
  let result = validator.validate(policiesSchema);

  let message = "policies-schema.json should satisfy policies-schema.meta.json";
  if (!result.valid) {
    message += `:\n${JSON.stringify(result.errors, null, 2)}`;
  }
  Assert.ok(result.valid, message);
});

// Guards against accidentally weakening the meta-schema.
// Removing a validation rule from the meta-schema causes this
// test to fail instead of reducing schema coverage.
add_task(async function test_meta_schema_catches_violations() {
  let description = "A description long enough to satisfy minLength.";
  let compat = {
    firefox: { version_added: "60" },
    firefox_esr: { version_added: "60" },
    firefox_enterprise: { version_added: false },
  };
  let bad = {
    properties: {
      MissingDescription: {
        "x-category": "Miscellaneous",
        "x-compatibility": compat,
        examples: ["example"],
        "x-restart-required": true,
      },
      ShortDescription: {
        description: "Too short.",
        "x-category": "Miscellaneous",
        "x-compatibility": compat,
        examples: ["example"],
        "x-restart-required": true,
      },
      EmptyCategory: {
        description,
        "x-category": "",
        "x-compatibility": compat,
        examples: ["example"],
        "x-restart-required": true,
      },
      EmptyExamples: {
        description,
        "x-category": "Miscellaneous",
        "x-compatibility": compat,
        examples: [],
        "x-restart-required": true,
      },
      MissingCompatibility: {
        description,
        "x-category": "Miscellaneous",
        examples: ["example"],
        "x-restart-required": true,
      },
      MissingChannel: {
        description,
        "x-category": "Miscellaneous",
        "x-compatibility": {
          firefox: { version_added: "60" },
          firefox_enterprise: { version_added: false },
        },
        examples: ["example"],
        "x-restart-required": true,
      },
      MissingVersionAdded: {
        description,
        "x-category": "Miscellaneous",
        "x-compatibility": { ...compat, firefox: {} },
        examples: ["example"],
        "x-restart-required": true,
      },
      BadVersionString: {
        description,
        "x-category": "Miscellaneous",
        "x-compatibility": {
          ...compat,
          firefox: { version_added: "fifty" },
        },
        examples: ["example"],
        "x-restart-required": true,
      },
      BadVersionType: {
        description,
        "x-category": "Miscellaneous",
        "x-compatibility": {
          ...compat,
          firefox: { version_added: 2 },
        },
        examples: ["example"],
        "x-restart-required": true,
      },
      UnknownChannel: {
        description,
        "x-category": "Miscellaneous",
        "x-compatibility": {
          ...compat,
          firefox_galactic_edition: { version_added: "60" },
        },
        examples: ["example"],
        "x-restart-required": true,
      },
      MissingRestartRequired: {
        description,
        "x-category": "Miscellaneous",
        "x-compatibility": compat,
        examples: ["example"],
      },
      BadRestartRequiredType: {
        description,
        "x-category": "Miscellaneous",
        "x-compatibility": compat,
        examples: ["example"],
        "x-restart-required": "true",
      },
    },
  };

  let validator = new JsonSchema.Validator(metaSchema, { shortCircuit: false });
  let result = validator.validate(bad);
  Assert.ok(!result.valid, "A schema with bad metadata should be rejected.");

  for (let [policy, keyword] of [
    ["MissingDescription", "required"],
    ["ShortDescription", "minLength"],
    ["EmptyCategory", "minLength"],
    ["EmptyExamples", "minItems"],
    ["MissingCompatibility", "required"],
    ["MissingChannel", "required"],
    ["MissingVersionAdded", "required"],
    ["BadVersionString", "pattern"],
    ["BadVersionType", "type"],
    ["UnknownChannel", "additionalProperties"],
    ["MissingRestartRequired", "required"],
    ["BadRestartRequiredType", "type"],
  ]) {
    Assert.ok(
      result.errors.some(
        e => e.keyword == keyword && e.instanceLocation.includes(policy)
      ),
      `${policy} should be flagged by "${keyword}".`
    );
  }
});
