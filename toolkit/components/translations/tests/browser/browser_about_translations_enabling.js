/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const FEATURE_ENABLED_VISIBILITY_EXPECTATIONS =
  aboutTranslationsVisibilityExpectations();

const FEATURE_BLOCKED_UI_VISIBILITY_EXPECTATIONS =
  aboutTranslationsVisibilityExpectations({
    featureBlockedInfoMessage: true,
  });

/**
 * Asserts that the main controls are enabled or disabled.
 *
 * @param {boolean} enabled
 */
async function assertMainUserInterfaceEnabledState(enabled) {
  // TODO: Switch to SpecialPowers.spawn
  // eslint-disable-next-line mozilla/reject-contenttask-spawn
  const controlStates = await ContentTask.spawn(
    gBrowser.selectedBrowser,
    {},
    function () {
      const { document } = content;
      return {
        sourceLanguageSelectorDisabled: document
          .querySelector("#about-translations-source-select")
          .hasAttribute("disabled"),
        targetLanguageSelectorDisabled: document
          .querySelector("#about-translations-target-select")
          .hasAttribute("disabled"),
        sourceTextAreaDisabled: document
          .querySelector("#about-translations-source-textarea")
          .hasAttribute("disabled"),
        targetTextAreaDisabled: document
          .querySelector("#about-translations-target-textarea")
          .hasAttribute("disabled"),
        copyButtonDisabled: document
          .querySelector("#about-translations-copy-button")
          .hasAttribute("disabled"),
        swapLanguagesButtonDisabled: document
          .querySelector("#about-translations-swap-languages-button")
          .hasAttribute("disabled"),
      };
    }
  );

  const expectedDisabled = !enabled;
  is(
    controlStates.sourceLanguageSelectorDisabled,
    expectedDisabled,
    `Expected source selector disabled state to be ${expectedDisabled}.`
  );
  is(
    controlStates.targetLanguageSelectorDisabled,
    expectedDisabled,
    `Expected target selector disabled state to be ${expectedDisabled}.`
  );
  is(
    controlStates.sourceTextAreaDisabled,
    expectedDisabled,
    `Expected source textarea disabled state to be ${expectedDisabled}.`
  );
  is(
    controlStates.targetTextAreaDisabled,
    expectedDisabled,
    `Expected target textarea disabled state to be ${expectedDisabled}.`
  );

  if (!enabled) {
    ok(
      controlStates.copyButtonDisabled,
      "Expected copy button to be disabled."
    );
    ok(
      controlStates.swapLanguagesButtonDisabled,
      "Expected swap-languages button to be disabled."
    );
  }
}

/**
 * Checks the about:translations UI updates with AIFeature enable/disable starting from disabled.
 */
add_task(
  async function test_about_translations_ai_feature_toggle_from_disabled() {
    const { aboutTranslationsTestUtils, cleanup } = await openAboutTranslations(
      {
        featureEnabled: false,
        autoDownloadFromRemoteSettings: true,
      }
    );

    await aboutTranslationsTestUtils.assertIsVisible(
      FEATURE_BLOCKED_UI_VISIBILITY_EXPECTATIONS
    );

    await aboutTranslationsTestUtils.assertEvents(
      {
        expected: [
          [
            AboutTranslationsTestUtils.Events.EnabledStateChanged,
            { enabled: true },
          ],
        ],
      },
      async () => {
        await TranslationsFeature.enable();
      }
    );
    await aboutTranslationsTestUtils.assertIsVisible(
      FEATURE_ENABLED_VISIBILITY_EXPECTATIONS
    );

    await aboutTranslationsTestUtils.assertEvents(
      {
        expected: [
          [
            AboutTranslationsTestUtils.Events.EnabledStateChanged,
            { enabled: false },
          ],
        ],
      },
      async () => {
        await TranslationsFeature.block();
      }
    );
    await aboutTranslationsTestUtils.assertIsVisible(
      FEATURE_BLOCKED_UI_VISIBILITY_EXPECTATIONS
    );

    await cleanup();
  }
);

/**
 * Checks the about:translations UI updates with AIFeature disable/enable starting from enabled.
 */
add_task(
  async function test_about_translations_ai_feature_toggle_from_enabled() {
    const { aboutTranslationsTestUtils, cleanup } = await openAboutTranslations(
      {
        featureEnabled: true,
        autoDownloadFromRemoteSettings: true,
        prefs: [
          ["browser.translations.enable", true],
          ["browser.ai.control.default", "available"],
          ["browser.ai.control.translations", "default"],
        ],
      }
    );

    await aboutTranslationsTestUtils.assertIsVisible(
      FEATURE_ENABLED_VISIBILITY_EXPECTATIONS
    );

    await aboutTranslationsTestUtils.assertEvents(
      {
        expected: [
          [
            AboutTranslationsTestUtils.Events.EnabledStateChanged,
            { enabled: false },
          ],
        ],
      },
      async () => {
        await TranslationsFeature.block();
      }
    );
    await aboutTranslationsTestUtils.assertIsVisible(
      FEATURE_BLOCKED_UI_VISIBILITY_EXPECTATIONS
    );

    await aboutTranslationsTestUtils.assertEvents(
      {
        expected: [
          [
            AboutTranslationsTestUtils.Events.EnabledStateChanged,
            { enabled: true },
          ],
        ],
      },
      async () => {
        await TranslationsFeature.enable();
      }
    );
    await aboutTranslationsTestUtils.assertIsVisible(
      FEATURE_ENABLED_VISIBILITY_EXPECTATIONS
    );

    await cleanup();
  }
);

/**
 * Checks that the page loads correctly when the global Translations pref is enabled.
 */
add_task(async function test_about_translations_engine_unsupported() {
  const { aboutTranslationsTestUtils, cleanup } = await openAboutTranslations({
    autoDownloadFromRemoteSettings: true,
    prefs: [["browser.translations.simulateUnsupportedEngine", true]],
  });

  await aboutTranslationsTestUtils.assertIsVisible(
    aboutTranslationsStandaloneMessageVisibilityExpectations({
      unsupportedInfoMessage: true,
    })
  );

  await cleanup();
});

/**
 * Checks that a policy-locked disabled feature shows the policy info message.
 */
add_task(async function test_about_translations_feature_blocked_by_policy() {
  const { aboutTranslationsTestUtils, cleanup } = await openAboutTranslations({
    featureEnabled: false,
    lockEnabledState: true,
    autoDownloadFromRemoteSettings: true,
  });

  await aboutTranslationsTestUtils.assertIsVisible(
    aboutTranslationsStandaloneMessageVisibilityExpectations({
      policyDisabledInfoMessage: true,
    })
  );

  await cleanup();
});

/**
 * Checks that clicking unblock from an initially disabled state enables the feature.
 */
add_task(
  async function test_about_translations_feature_unblock_from_disabled() {
    const { aboutTranslationsTestUtils, cleanup } = await openAboutTranslations(
      {
        featureEnabled: false,
        autoDownloadFromRemoteSettings: true,
      }
    );

    await aboutTranslationsTestUtils.assertIsVisible(
      FEATURE_BLOCKED_UI_VISIBILITY_EXPECTATIONS
    );
    await assertMainUserInterfaceEnabledState(false);

    await aboutTranslationsTestUtils.assertEvents(
      {
        expected: [
          [
            AboutTranslationsTestUtils.Events.EnabledStateChanged,
            { enabled: true },
          ],
        ],
      },
      async () => {
        await aboutTranslationsTestUtils.invokeUnblockFeatureButton();
      }
    );

    await aboutTranslationsTestUtils.assertIsVisible(
      FEATURE_ENABLED_VISIBILITY_EXPECTATIONS
    );
    await assertMainUserInterfaceEnabledState(true);
    await aboutTranslationsTestUtils.assertEvents(
      {
        expected: [
          [
            AboutTranslationsTestUtils.Events.TranslationRequested,
            ({ translationId }) => translationId >= 1,
          ],
          [
            AboutTranslationsTestUtils.Events.TranslationComplete,
            ({ translationId }) => translationId >= 1,
          ],
        ],
      },
      async () => {
        await aboutTranslationsTestUtils.updateCurrentPageHash({
          sourceLanguage: "en",
          targetLanguage: "fr",
          sourceText: "Hello",
        });
      }
    );

    await aboutTranslationsTestUtils.assertTranslatedText({
      sourceLanguage: "en",
      targetLanguage: "fr",
      sourceText: "Hello",
    });

    await cleanup();
  }
);

/**
 * Checks that clicking unblock after disabling at runtime enables the feature without reload.
 */
add_task(
  async function test_about_translations_feature_unblock_after_runtime_disable() {
    const { aboutTranslationsTestUtils, cleanup } = await openAboutTranslations(
      {
        autoDownloadFromRemoteSettings: true,
      }
    );

    await aboutTranslationsTestUtils.assertIsVisible(
      FEATURE_ENABLED_VISIBILITY_EXPECTATIONS
    );
    await assertMainUserInterfaceEnabledState(true);

    await aboutTranslationsTestUtils.assertEvents(
      {
        expected: [
          [
            AboutTranslationsTestUtils.Events.EnabledStateChanged,
            { enabled: false },
          ],
        ],
      },
      async () => {
        await TranslationsFeature.block();
      }
    );

    await aboutTranslationsTestUtils.assertIsVisible(
      FEATURE_BLOCKED_UI_VISIBILITY_EXPECTATIONS
    );
    await assertMainUserInterfaceEnabledState(false);

    await aboutTranslationsTestUtils.assertEvents(
      {
        expected: [
          [
            AboutTranslationsTestUtils.Events.EnabledStateChanged,
            { enabled: true },
          ],
        ],
      },
      async () => {
        await aboutTranslationsTestUtils.invokeUnblockFeatureButton();
      }
    );

    await aboutTranslationsTestUtils.assertIsVisible(
      FEATURE_ENABLED_VISIBILITY_EXPECTATIONS
    );
    await assertMainUserInterfaceEnabledState(true);
    await aboutTranslationsTestUtils.assertEvents(
      {
        expected: [
          [
            AboutTranslationsTestUtils.Events.TranslationRequested,
            ({ translationId }) => translationId >= 1,
          ],
          [
            AboutTranslationsTestUtils.Events.TranslationComplete,
            ({ translationId }) => translationId >= 1,
          ],
        ],
      },
      async () => {
        await aboutTranslationsTestUtils.updateCurrentPageHash({
          sourceLanguage: "en",
          targetLanguage: "fr",
          sourceText: "Hello",
        });
      }
    );

    await aboutTranslationsTestUtils.assertTranslatedText({
      sourceLanguage: "en",
      targetLanguage: "fr",
      sourceText: "Hello",
    });

    await cleanup();
  }
);
