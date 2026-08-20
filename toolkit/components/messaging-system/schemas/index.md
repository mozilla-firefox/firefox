# Messaging System Schemas

## Docs

More information about [Messaging System](/browser/components/asrouter/docs).

## Messages

There are JSON schemas for each type of message that the Firefox Messaging
System handles:

- {searchfox}`Action Only <browser/components/asrouter/content-src/templates/OnboardingMessage/ActionOnlyMessage.schema.json>`
- {searchfox}`CFR URLBar Chiclet <browser/components/asrouter/content-src/templates/CFR/templates/CFRUrlbarChiclet.schema.json>`
- {searchfox}`Extension Doorhanger <browser/components/asrouter/content-src/templates/CFR/templates/ExtensionDoorhanger.schema.json>`
- {searchfox}`Infobar <browser/components/asrouter/content-src/templates/CFR/templates/InfoBar.schema.json>`
- {searchfox}`Spotlight <browser/components/asrouter/content-src/templates/OnboardingMessage/Spotlight.schema.json>`
- {searchfox}`Toast Notification <browser/components/asrouter/content-src/templates/ToastNotification/ToastNotification.schema.json>`
- {searchfox}`Toolbar Badge <browser/components/asrouter/content-src/templates/OnboardingMessage/ToolbarBadgeMessage.schema.json>`
- {searchfox}`Update Action <browser/components/asrouter/content-src/templates/OnboardingMessage/UpdateAction.schema.json>`
- {searchfox}`Whats New <browser/components/asrouter/content-src/templates/OnboardingMessage/WhatsNewMessage.schema.json>`
- {searchfox}`Private Browsing Newtab Promo Message <browser/components/asrouter/content-src/templates/PBNewtab/NewtabPromoMessage.schema.json>`

Together, they are combined into the {searchfox}`Messaging Experiments <browser/components/asrouter/content-src/schemas/MessagingExperiment.schema.json>` via a {searchfox}`script <browser/components/asrouter/content-src/schemas/make-schemas.py>`. This
is the schema used for Nimbus experiments that target messaging features. All
incoming messaging experiments will be validated against this schema.

### MultiStageProtonScreen

There is a JSON schema for a multistage screen content, which the Messaging System renders Spotlights and Feature Callouts from, and even shares with about:welcome onboarding. However, this JSON schema isn't used by Nimbus to be enforced on messaging experiments and is only served for the purpose of documenting a catalog of the growing set of content properties.

See JSON schema [here](https://searchfox.org/firefox-main/source/toolkit/components/messaging-system/schemas/MultiStageProtonScreenSchemas/MultiStageProtonScreenSchemas.json).

## Schema Changes

To add a new message type to the Messaging Experiments schema:

1. Add your message template schema.

   Your message template schema only needs to define the following fields at a
   minimum:

   - `template`: a string field that defines an identifier for your message.
     This must be either a `const` or `enum` field.

     For example, the `template` field of Spotlight looks like:

     ```json
     { "type": "string", "const": "spotlight" }
     ```

   - `content`: an object field that defines your per-message unique content.

   If your message requires `targeting`, you must add a targeting field.

   If your message supports triggering, there is a definition you can reference
   the `MessageTrigger` [shared definition](#shared-definitions).

   The `groups`, `frequency`, and `priority` fields will automatically be
   inherited by your message.

2. Ensure the schema has an `$id` member. This allows for references (e.g.,
   `{ "$ref": "#!/$defs/Foo" }`) to work in the bundled schema. See docs on
   [bundling JSON schemas][jsonschema_bundling] for more information.

3. Add the new schema to the list in {searchfox}`make-schemas.py <browser/components/asrouter/content-src/schemas/make-schemas.py>`.

4. Build the new schema by running:

   ```shell
   cd browser/components/asrouter/content-src/schemas/
   ../../../../../mach python make-schemas.py
   ```

5. Commit the results.

Likewise, if you are modifying a message schema you must rebuild the generated
schema:

```shell
cd browser/components/asrouter/content-src/schemas/
../../../../../mach python make-schemas.py
```

If you do not, the {searchfox}`Firefox MS Schemas CI job <taskcluster/kinds/source-test/python.yml#425-438>` will fail.

(run-make-schemas)=

You can run this locally via:

```shell
cd browser/components/asrouter/content-src/schemas/
../../../../../mach xpcshell extract-test-corpus.js
../../../../../mach python make-schemas.py --check
```

This test will re-generate the schema and compare it to
`MessagingExperiment.schema.json`. If there is a difference, it will fail.
The test will also validate the list of in-tree messages with the same schema
validator that Experimenter uses to ensure that our schemas are compatible with
Experimenter.

## Shared Definitions

Some definitions are shared across multiple schemas. Instead of copying and
pasting the definitions between them and then having to manually keep them up to
date, we keep them in a common schema that contains these defintitions:
{searchfox}`FxMsCommon.schema.json <browser/components/asrouter/content-src/schemas/FxMSCommon.schema.json>`. Any definition that will be re-used
across multiple schemas should be added to the common schema, which will have
its definitions bundled into the generated schema. All references to the common
schema will be rewritten in the generated schema.

The definitions listed in this file are:

- `Message`, which defines the common fields present in each FxMS message;
- `MessageTrigger`, which defines a method that may trigger the message to be
  presented to the user;
- `localizableText`, for when either a string or a string ID (for translation
  purposes) can be used; and
- `localizedText`, for when a string ID is required.

An example of using the `localizableText` definition in a message schema follows:

```json
{
  "type": "object",
  "properties": {
    "message": {
      "$ref": "file:///FxMSCommon.schema.json#/$defs/localizableText"
      "description": "The message as a string or string ID"
    },
  }
}
```

## Schema Tests

We have in-tree tests ({searchfox}`Test_CFRMessageProvider <browser/components/asrouter/tests//xpcshell/test_CFMessageProvider.js>`,
{searchfox}`Test_OnboardingMessageProvider <browser/components/asrouter/tests//xpcshell/test_OnboardingMessageProvider.js>`, and {searchfox}`Test_PanelTestProvider <browser/components/asrouter/tests//xpcshell/test_PanelTestProvider.js>`), which
validate existing messages with the generated schema.

We also have compatibility tests for ensuring that our schemas work in
[Experimenter]. [Experimenter] uses a different JSON schema validation
library, which is reused in the {searchfox}`Firefox MS Schemas CI job <taskcluster/kinds/source-test/python.yml#425-438>`. This test validates a test corpus from
{searchfox}`CFRMessageProvider <browser/components/asrouter/modules/CFRMessageProvider.sys.mjs>`, {searchfox}`OnboardingMessageProvider <browser/components/asrouter/modules/OnboardingMessageProvider.sys.mjs>`, and {searchfox}`PanelTestProvider <browser/components/asrouter/modules/PanelTestProvider.sys.mjs>`
with the same JSON schema validation library and configuration as Experimenter.

See how to run these tests {ref}`above <run_make_schemas>`.

## Triggers and actions

```{toctree}
:maxdepth: 2

SpecialMessageActionSchemas/index
TriggerActionSchemas/index
```

[experimenter]: https://experimenter.info
[jsonschema_bundling]: https://json-schema.org/understanding-json-schema/structuring.html#bundling
[newtab_message_schema]: https://searchfox.org/mozilla-central/source/browser/components/asrouter/content-src/templates/OnboardingMessage/NewtabMessage.schema.json
[protections_panel_schema]: https://searchfox.org/mozilla-central/source/browser/components/asrouter/content-src/templates/OnboardingMessage/ProtectionsPanelMessage.schema.json
