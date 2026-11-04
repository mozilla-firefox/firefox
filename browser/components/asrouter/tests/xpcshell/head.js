/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

/* import-globals-from ../../../../../toolkit/profile/test/xpcshell/head.js */
/* import-globals-from ../../../../../browser/components/profiles/tests/unit/head.js */

"use strict";

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  JsonSchema: "resource://gre/modules/JsonSchema.sys.mjs",
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
});

function assertValidates(validator, obj, msg) {
  const result = validator.validate(obj);
  Assert.ok(
    result.valid && result.errors.length === 0,
    `${msg} - errors = ${JSON.stringify(result.errors, undefined, 2)}`
  );
}

async function fetchSchema(uri) {
  try {
    dump(`URI: ${uri}\n`);
    return fetch(uri, { credentials: "omit" }).then(rsp => rsp.json());
  } catch (e) {
    throw new Error(`Could not fetch ${uri}`);
  }
}

async function schemaValidatorFor(uri, { common = false } = {}) {
  const schema = await fetchSchema(uri);
  const validator = new JsonSchema.Validator(schema);

  if (common) {
    const commonSchema = await fetchSchema(
      "resource://testing-common/FxMSCommon.schema.json"
    );
    validator.addSchema(commonSchema);
  }

  return validator;
}

async function makeValidators() {
  const experimentValidator = await schemaValidatorFor(
    "chrome://browser/content/asrouter/schemas/MessagingExperiment.schema.json"
  );

  const messageValidators = {
    action_only: await schemaValidatorFor(
      "resource://testing-common/ActionOnlyMessage.schema.json",
      { common: true }
    ),
    bookmarks_bar_button: await schemaValidatorFor(
      "resource://testing-common/BookmarksBarButton.schema.json",
      { common: true }
    ),
    infobar: await schemaValidatorFor(
      "resource://testing-common/InfoBar.schema.json",
      { common: true }
    ),
    menu_message: await schemaValidatorFor(
      "resource://testing-common/MenuMessage.schema.json",
      { common: true }
    ),
    newtab_message: await schemaValidatorFor(
      "resource://testing-common/NewtabMessage.schema.json",
      { common: true }
    ),
    pb_newtab: await schemaValidatorFor(
      "resource://testing-common/NewtabPromoMessage.schema.json",
      { common: true }
    ),
    sidebar_chatbot_promo: await schemaValidatorFor(
      "resource://testing-common/SidebarChatBotPromo.schema.json",
      { common: true }
    ),
    smart_window_newtab_promo: await schemaValidatorFor(
      "resource://testing-common/SmartWindowNewtabPromo.schema.json",
      { common: true }
    ),
    spotlight: await schemaValidatorFor(
      "resource://testing-common/Spotlight.schema.json",
      { common: true }
    ),
    toast_notification: await schemaValidatorFor(
      "resource://testing-common/ToastNotification.schema.json",
      { common: true }
    ),
    toolbar_badge: await schemaValidatorFor(
      "resource://testing-common/ToolbarBadgeMessage.schema.json",
      { common: true }
    ),
    update_action: await schemaValidatorFor(
      "resource://testing-common/UpdateAction.schema.json",
      { common: true }
    ),
    feature_callout: await schemaValidatorFor(
      // For now, Feature Callout and Spotlight share a common schema
      "resource://testing-common/Spotlight.schema.json",
      { common: true }
    ),
  };

  return { experimentValidator, messageValidators };
}
