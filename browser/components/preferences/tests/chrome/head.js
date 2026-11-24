/** @import { Setting } from "chrome://global/content/preferences/Setting.mjs"; */

/**
 * @callback TestSettingControlCommonPropertiesFunction
 * @param {(config: Record<string, any>) => Promise<HTMLElement>} renderTemplateFunction
 */

/**
 * Asserts all properties on the element
 * that is returned from the provided function.
 *
 * @type {TestSettingControlCommonPropertiesFunction}
 */
async function testCommonSettingControlPropertiesSet(renderTemplateFunction) {
  const l10nId = "l10n-test-id";
  const l10nArgs = { foo: "bar" };
  const iconSrc = "anicon.png";
  const supportPage = "https://support.page";
  const label = "foo-bar";
  const slot = "foo";

  const element = await renderTemplateFunction({
    l10nId,
    l10nArgs,
    iconSrc,
    supportPage,
    slot,
    controlAttrs: {
      label,
    },
  });

  is(
    element.getAttribute("data-l10n-id"),
    l10nId,
    "sets data-l10n-id attribute"
  );

  is(
    element.getAttribute("data-l10n-args"),
    JSON.stringify(l10nArgs),
    "converts data-l10n-args to stringified JSON object"
  );

  is(element.getAttribute("label"), label, "sets controlAttrs.label");

  is(element.iconSrc, iconSrc, "sets iconSrc");

  is(element.supportPage, supportPage, "sets supportPage");

  is(element.slot, slot, "sets slot");
}

/**
 * Asserts all unset properties on the element
 * that is returned from the provided function.
 *
 * @type {TestSettingControlCommonPropertiesFunction}
 */
async function testCommonSettingControlPropertiesUnset(renderTemplateFunction) {
  info("Test common properties when unset");
  const element = await renderTemplateFunction({});
  ok(!element.hasAttribute("data-l10n-id"), "no data-l10n-id attribute");
  ok(!element.hasAttribute("data-l10n-args"), "no data-l10n-args");
  ok(!element.dataset.subcategory, "no subcategory");
  ok(!element.hasAttribute("label"), "no controlAttrs.label");
  ok(!element.iconSrc, "no iconSrc");
  ok(!element.supportPage, "no supportPage");
  ok(!element.slot, "no slot");
}

/**
 * Asserts all properties set on the element
 * that is returned from the provided function.
 *
 * @type {TestSettingControlCommonPropertiesFunction}
 */
async function testCommonSettingControlProperties(renderTemplateFunction) {
  await testCommonSettingControlPropertiesSet(renderTemplateFunction);
  await testCommonSettingControlPropertiesUnset(renderTemplateFunction);
}

/**
 * Waits for a setting to emit a "change" event.
 *
 * @param {Setting} setting - The setting object to watch for changes.
 * @returns {Promise<void>} A promise that resolves when the setting emits a
   "change" event
 */
function waitForSettingChange(setting) {
  return new Promise(resolve => {
    setting.on("change", function handler() {
      setting.off("change", handler);
      resolve();
    });
  });
}
