#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Firefox Messaging System Messaging Experiment schema generator

The Firefox Messaging System handles several types of messages. This program
patches and combines those schemas into a single schema
(MessagingExperiment.schema.json) which is used to validate messaging
experiments coming from Nimbus.

Definitions from FxMsCommon.schema.json are bundled into this schema. This
allows all of the FxMS schemas to reference common definitions, e.g.
`localizableText` for translatable strings, via referencing the common schema.
The bundled schema will be re-written so that the references now point at the
top-level, generated schema.

Additionally, all self-references in each messaging schema will be rewritten
into absolute references, referencing each sub-schemas `$id`. This is requried
due to the JSONSchema validation library used by Experimenter not fully
supporting self-references and bundled schema.
"""

from __future__ import annotations

import sys
from argparse import ArgumentParser
from itertools import chain
from pathlib import Path
from typing import Any, NamedTuple, Union
from urllib.parse import urlparse

import jsonschema
from mozfile import json


class SchemaDefinition(NamedTuple):
    """A definition of a schema that is to be bundled."""

    #: The $id of the generated schema.
    schema_id: str

    #: The path of the generated schema.
    schema_path: Path

    #: The message types that will be bundled into the schema.
    message_types: dict[str, Path]

    #: What common definitions to bundle into the schema.
    #:
    #: If `True`, all definitions will be bundled.
    #: If `False`, no definitons will be bundled.
    #: If a list, only the named definitions will be bundled.
    bundle_common: Union[bool, list[str]]

    #: The testing corpus for the schema.
    test_corpus: dict[str, Path]


SCHEMA_DIR = Path("..", "templates")

SCHEMAS = [
    SchemaDefinition(
        schema_id="chrome://browser/content/asrouter/schemas/MessagingExperiment.schema.json",
        schema_path=Path("MessagingExperiment.schema.json"),
        message_types={
            "ActionOnlyMessage": (
                SCHEMA_DIR / "OnboardingMessage" / "ActionOnlyMessage.schema.json"
            ),
            "BookmarksBarButton": (
                SCHEMA_DIR / "OnboardingMessage" / "BookmarksBarButton.schema.json"
            ),
            "InfoBar": SCHEMA_DIR / "InfoBar" / "InfoBar.schema.json",
            "MenuMessage": (
                SCHEMA_DIR / "OnboardingMessage" / "MenuMessage.schema.json"
            ),
            "NewtabPromoMessage": (
                SCHEMA_DIR / "PBNewtab" / "NewtabPromoMessage.schema.json"
            ),
            "NewtabMessage": (
                SCHEMA_DIR / "OnboardingMessage" / "NewtabMessage.schema.json"
            ),
            "SidebarChatBotPromo": (
                SCHEMA_DIR / "OnboardingMessage" / "SidebarChatBotPromo.schema.json"
            ),
            "SmartWindowNewtabPromo": (
                SCHEMA_DIR / "OnboardingMessage" / "SmartWindowNewtabPromo.schema.json"
            ),
            "Spotlight": SCHEMA_DIR / "OnboardingMessage" / "Spotlight.schema.json",
            "ToastNotification": (
                SCHEMA_DIR / "ToastNotification" / "ToastNotification.schema.json"
            ),
            "ToolbarBadgeMessage": (
                SCHEMA_DIR / "OnboardingMessage" / "ToolbarBadgeMessage.schema.json"
            ),
            "UpdateAction": (
                SCHEMA_DIR / "OnboardingMessage" / "UpdateAction.schema.json"
            ),
        },
        bundle_common=True,
        test_corpus={
            "ReachExperiments": Path("corpus", "ReachExperiments.messages.json"),
            # These are generated via extract-test-corpus.js
            "OnboardingMessageProvider": Path(
                "corpus", "OnboardingMessageProvider.messages.json"
            ),
            "PanelTestProvider": Path("corpus", "PanelTestProvider.messages.json"),
        },
    ),
    SchemaDefinition(
        schema_id=(
            "chrome://browser/content/asrouter/schemas/"
            "BackgroundTaskMessagingExperiment.schema.json"
        ),
        schema_path=Path("BackgroundTaskMessagingExperiment.schema.json"),
        message_types={
            "ToastNotification": (
                SCHEMA_DIR / "ToastNotification" / "ToastNotification.schema.json"
            ),
        },
        bundle_common=True,
        # These are generated via extract-test-corpus.js
        test_corpus={
            # Just the "toast_notification" messages.
            "PanelTestProvider": Path(
                "corpus", "PanelTestProvider_toast_notification.messages.json"
            ),
        },
    ),
]

COMMON_SCHEMA_NAME = "FxMSCommon.schema.json"
COMMON_SCHEMA_PATH = Path(COMMON_SCHEMA_NAME)


class NestedRefResolver(jsonschema.RefResolver):
    """A custom ref resolver that handles bundled schema.

    This is the resolver used by Experimenter.
    """

    def __init__(self, schema):
        super().__init__(base_uri=None, referrer=None)

        if "$id" in schema:
            self.store[schema["$id"]] = schema

        if "$defs" in schema:
            for dfn in schema["$defs"].values():
                if "$id" in dfn:
                    self.store[dfn["$id"]] = dfn


def read_schema(path):
    """Read a schema from disk and parse it as JSON."""
    with path.open("r") as f:
        return json.load(f)


def extract_template_values(template):
    """Extract the possible template values (either via JSON Schema enum or const)."""
    enum = template.get("enum")
    if enum:
        return enum

    const = template.get("const")
    if const:
        return [const]


def patch_schema(schema, bundled_id, schema_id=None):
    """Patch the given schema.

    The JSON schema validator that Experimenter uses
    (https://pypi.org/project/jsonschema/) does not support relative references,
    nor does it support bundled schemas. We rewrite the schema so that all
    relative refs are transformed into absolute refs via the schema's `$id`.

    Additionally, we merge in the contents of FxMSCommon.schema.json, so all
    refs relative to that schema will be transformed to become relative to this
    schema.

    See-also: https://github.com/python-jsonschema/jsonschema/issues/313
    """
    if schema_id is None:
        schema_id = schema["$id"]

    def patch_impl(schema):
        ref = schema.get("$ref")

        if ref:
            uri = urlparse(ref)
            if (
                uri.scheme == ""
                and uri.netloc == ""
                and uri.path == ""
                and uri.fragment != ""
            ):
                schema["$ref"] = f"{schema_id}#{uri.fragment}"
            elif (uri.scheme, uri.path) == ("file", f"/{COMMON_SCHEMA_NAME}"):
                schema["$ref"] = f"{bundled_id}#{uri.fragment}"

        # If `schema` is object-like, inspect each of its indivual properties
        # and patch them.
        properties = schema.get("properties")
        if properties:
            for prop in properties.keys():
                patch_impl(properties[prop])

        # If `schema` is array-like, inspect each of its items and patch them.
        items = schema.get("items")
        if items:
            patch_impl(items)

        # Patch each `if`, `then`, `else`, and `not` sub-schema that is present.
        for key in ("if", "then", "else", "not"):
            if key in schema:
                patch_impl(schema[key])

        # Patch the items of each `oneOf`, `allOf`, and `anyOf` sub-schema that
        # is present.
        for key in ("oneOf", "allOf", "anyOf"):
            subschema = schema.get(key)
            if subschema:
                for i, alternate in enumerate(subschema):
                    patch_impl(alternate)

    # Patch the top-level type defined in the schema.
    patch_impl(schema)

    # Patch each named definition in the schema.
    for key in ("$defs", "definitions"):
        defns = schema.get(key)
        if defns:
            for defn_name, defn_value in defns.items():
                patch_impl(defn_value)

    return schema


def bundle_schema(schema_def: SchemaDefinition):
    """Create a bundled schema based on the schema definition."""
    # Patch each message type schema to resolve all self-references to be
    # absolute and rewrite # references to FxMSCommon.schema.json to be relative
    # to the new schema (because we are about to bundle its definitions).
    defs = {
        name: patch_schema(read_schema(path), bundled_id=schema_def.schema_id)
        for name, path in schema_def.message_types.items()
    }

    # Bundle the definitions from FxMSCommon.schema.json into this schema.
    if schema_def.bundle_common:

        def dfn_filter(name):
            if schema_def.bundle_common is True:
                return True

            return name in schema_def.bundle_common

        common_schema = patch_schema(
            read_schema(COMMON_SCHEMA_PATH),
            bundled_id=schema_def.schema_id,
            schema_id=schema_def.schema_id,
        )

        # patch_schema mutates the given schema, so we read a new copy in for
        # each bundle operation.
        defs.update({
            name: dfn
            for name, dfn in common_schema["$defs"].items()
            if dfn_filter(name)
        })

    # Ensure all bundled schemas have an $id so that $refs inside the
    # bundled schema work correctly (i.e, they will reference the subschema
    # and not the bundle).
    for name in schema_def.message_types.keys():
        subschema = defs[name]
        if "$id" not in subschema:
            raise ValueError(f"Schema {name} is missing an $id")

        props = subschema["properties"]
        if "template" not in props:
            raise ValueError(f"Schema {name} is missing a template")

        template = props["template"]
        if "enum" not in template and "const" not in template:
            raise ValueError(f"Schema {name} should have const or enum template")

    templates = {
        name: extract_template_values(defs[name]["properties"]["template"])
        for name in schema_def.message_types.keys()
    }

    # Ensure that each schema has a unique set of template values.
    for a in templates.keys():
        a_keys = set(templates[a])

        for b in templates.keys():
            if a == b:
                continue

            b_keys = set(templates[b])
            intersection = a_keys.intersection(b_keys)

            if len(intersection):
                raise ValueError(
                    f"Schema {a} and {b} have overlapping template values: "
                    f"{', '.join(intersection)}"
                )

    all_templates = list(chain.from_iterable(templates.values()))

    # Enforce that one of the templates must match (so that one of the if
    # branches will match).
    defs["Message"]["properties"]["template"]["enum"] = all_templates
    defs["TemplatedMessage"] = {
        "description": "An FxMS message of one of a variety of types.",
        "type": "object",
        "allOf": [
            # Ensure each message has all the fields defined in the base
            # Message type.
            #
            # This is slightly redundant because each message should
            # already inherit from this message type, but it is easier
            # to add this requirement here than to verify that each
            # message's schema is properly inheriting.
            {"$ref": f"{schema_def.schema_id}#/$defs/Message"},
            # For each message type, create a subschema that says if the
            # template field matches a value for a message type defined
            # in MESSAGE_TYPES, then the message must also match the
            # schema for that message type.
            #
            # This is done using `allOf: [{ if, then }]` instead of `oneOf: []`
            # because it provides better error messages. Using `if-then`
            # will only show validation errors for the sub-schema that
            # matches template, whereas using `oneOf` will show
            # validation errors for *all* sub-schemas, which makes
            # debugging messages much harder.
            *(
                {
                    "if": {
                        "type": "object",
                        "properties": {
                            "template": {
                                "type": "string",
                                "enum": templates[message_type],
                            },
                        },
                        "required": ["template"],
                    },
                    "then": {"$ref": f"{schema_def.schema_id}#/$defs/{message_type}"},
                }
                for message_type in schema_def.message_types
            ),
        ],
    }
    defs["MultiMessage"] = {
        "description": "An object containing an array of messages.",
        "type": "object",
        "properties": {
            "template": {"type": "string", "const": "multi"},
            "messages": {
                "type": "array",
                "description": "An array of messages.",
                "items": {"$ref": f"{schema_def.schema_id}#/$defs/TemplatedMessage"},
            },
        },
        "required": ["template", "messages"],
    }

    # Generate the combined schema.
    return {
        "$schema": "https://json-schema.org/draft/2019-09/schema",
        "$id": schema_def.schema_id,
        "title": "Messaging Experiment",
        "description": "A Firefox Messaging System message.",
        # A message must be one of:
        # - An object that contains id, template, and content fields
        # - An object that contains none of the above fields (empty message)
        # - An array of messages like the above
        "if": {
            "type": "object",
            "properties": {"template": {"const": "multi"}},
            "required": ["template"],
        },
        "then": {
            "$ref": f"{schema_def.schema_id}#/$defs/MultiMessage",
        },
        "else": {
            "$ref": f"{schema_def.schema_id}#/$defs/TemplatedMessage",
        },
        "$defs": defs,
    }


def check_diff(schema_def: SchemaDefinition, schema: dict[str, Any]):
    """Check the generated schema matches the on-disk schema."""
    print(f"  Checking {schema_def.schema_path} for differences...")

    with schema_def.schema_path.open("r") as f:
        on_disk = json.load(f)

    if on_disk != schema:
        print(f"{schema_def.schema_path} does not match generated schema:")
        print("Generated schema:")
        json.dump(schema, sys.stdout, indent=2)
        print("\n\nOn Disk schema:")
        json.dump(on_disk, sys.stdout, indent=2)
        print("\n\n")

        raise ValueError("Schemas do not match!")


def validate_corpus(schema_def: SchemaDefinition, schema: dict[str, Any]):
    """Check that the schema validates.

    This uses the same validation configuration that is used in Experimenter.
    """
    print("  Validating messages with Experimenter JSON Schema validator...")

    resolver = NestedRefResolver(schema)

    for provider, provider_path in schema_def.test_corpus.items():
        print(f"    Validating messages from {provider}:")

        try:
            with provider_path.open("r", encoding="utf-8") as f:
                messages = json.load(f)
        except FileNotFoundError as e:
            if not provider_path.parent.exists():
                new_exc = Exception(
                    f"Could not find {provider_path}: Did you run "
                    "`mach xpcshell extract-test-corpus.js` ?"
                )
                raise new_exc from e

            raise e

        for i, message in enumerate(messages):
            template = message.get("template", "(no template)")
            msg_id = message.get("id", f"index {i}")

            print(
                f"      Validating {msg_id} {template} message with {schema_def.schema_path}..."
            )
            jsonschema.validate(instance=message, schema=schema, resolver=resolver)

        print()


def main(check=False):
    """Generate Nimbus feature schemas for Firefox Messaging System."""
    for schema_def in SCHEMAS:
        print(f"Generating {schema_def.schema_path} ...")
        schema = bundle_schema(schema_def)

        if check:
            print(f"Checking {schema_def.schema_path} ...")
            check_diff(schema_def, schema)
            validate_corpus(schema_def, schema)
        else:
            with schema_def.schema_path.open("wb") as f:
                print(f"Writing {schema_def.schema_path} ...")
                f.write(json.dumps(schema, indent=2).encode("utf-8"))
                f.write(b"\n")


if __name__ == "__main__":
    parser = ArgumentParser(description=main.__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="Check that the generated schemas have not changed and run validation tests.",
        default=False,
    )
    args = parser.parse_args()

    main(args.check)
