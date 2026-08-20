/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import mozilla.components.feature.app.links.R as applinksR
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.snackbar.SNACKBAR_BUTTON_TEST_TAG
import org.mozilla.fenix.compose.snackbar.SNACKBAR_TEST_TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.TestHelper.shortAppName
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object BrowserPageSelectors {
    val ENGINE_VIEW =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "engineView",
            description = "Engine view",
            groups = listOf("requiredForPage"),
        )

    val PAGE_CONTENT =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "mozilla",
            description = "Page content",
            groups = listOf(""),
        )

    // The Play control on the media test pages (video's custom <button>Play</button> and audio's native
    // <audio controls>). Two deliberate choices, both learned from failing runs:
    //  - EXACT text (By.text), not contains: the audio page's "Page content: audio player" paragraph also
    //    contains "play", so a contains-match clicks that non-clickable paragraph and playback never starts.
    //  - UiObject2 (fire-and-forget click): starting playback is a slow reaction that produces no app-window
    //    update, so a UiObject clickAndSync reports a false failure even though the tap landed and media
    //    started (same reasoning as NotificationSelectors.MEDIA_NOTIFICATION_CONTROL_BUTTON).
    val MEDIA_PLAY_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_TEXT,
            value = "Play",
            description = "Web media Play button",
            groups = listOf(),
        )

    // The snackbar's single action button. The tag is generic and the label varies with the snackbar
    // ("Edit", "SWITCH", "UNDO"), which is why the legacy helper matches the tag OR the label rather than
    // both. Named for the element, not for one of its labels.
    val SNACKBAR_ACTION_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = SNACKBAR_BUTTON_TEST_TAG,
            description = "Snackbar action button",
            groups = listOf("snackbar"),
        )

    val SNACKBAR =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = SNACKBAR_TEST_TAG,
            description = "Snackbar container",
            groups = listOf(),
        )

    val MAIN_MENU_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = getStringResource(R.string.content_description_menu),
            description = "Three Dot Menu",
            // NOT an arrival anchor: the menu button lives on the top toolbar in the default layout but moves to
            // the bottom navigation bar when shouldUseExpandedToolbar is on — so it's layout-dependent (gotcha B7).
            // ENGINE_VIEW (the web content) is the layout-invariant "browser is loaded" signal for requiredForPage.
            groups = listOf(),
        )

    val TAB_CRASH_REPORTER_IMAGE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "crash_tab_image",
            description = "Tab crash reporter image",
            groups = listOf("tabCrashReporter"),
        )

    val TAB_CRASH_REPORTER_TITLE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.tab_crash_title_2),
            description = "Tab crash reporter title",
            groups = listOf("tabCrashReporter"),
        )

    val TAB_CRASH_REPORTER_MESSAGE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.tab_crash_send_report),
            description = "Tab crash reporter send crash message",
            groups = listOf("tabCrashReporter"),
        )

    val TAB_CRASH_REPORTER_RESTORE_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "restoreTabButton",
            description = "Tab crash reporter restore button",
            groups = listOf("tabCrashReporter"),
        )

    val TAB_CRASH_REPORTER_CLOSE_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "closeTabButton",
            description = "Tab crash reporter close button",
            groups = listOf("tabCrashReporter"),
        )

    @Suppress("FunctionName")
    fun PAGE_CONTENT(text: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = text,
            description = "Page content '$text'",
            groups = listOf(),
        )

    // An item on the GeckoView text-selection floating action bar (and the app's paste popup),
    // matched by exact visible text. Mirrors the legacy clickContextMenuItem, which located items
    // with By.text(item) — covers "Select all", "Copy", "Search", "Private Search", "Paste".
    @Suppress("FunctionName")
    fun TEXT_SELECTION_CONTEXT_MENU_ITEM(item: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_TEXT,
            value = item,
            description = "Text selection context menu item '$item'",
            groups = listOf(),
        )

    // A link in web content, keyed on its exact text like the legacy MatcherHelper.itemWithText. Exact and
    // not CONTAINS on purpose: PAGE_CONTENT(text) above cannot tell "Link 1" from "Link 10", and the
    // search-group tests long-press "Link 1" and "Link 2" on the same page.
    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun PAGE_LINK(linkText: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = linkText,
            description = "'$linkText' link in web content",
            groups = listOf(),
        )

    // An item in the web-content long-press context menu ("Open link in new tab", "Open link in private
    // tab"). UiObject2 rather than UiObject, matching the legacy By.text: these items open a tab without
    // necessarily updating the window, and UiObject's clickAndSync reports a slow-but-successful click as a
    // failure.
    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun CONTEXT_MENU_ITEM(itemText: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_TEXT,
            value = itemText,
            description = "'$itemText' context menu item",
            groups = listOf(),
        )

    val TRANSLATION_SHEET =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_RES,
            value = "design_bottom_sheet",
            description = "Translation bottom sheet",
            groups = listOf("notTranslatedPageTranslationSheet"),
        )

    val TRANSLATION_SHEET_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translations_bottom_sheet_title_first_time, argument = shortAppName),
            description = "Translation bottom sheet title",
            groups = listOf("notTranslatedPageTranslationSheet"),
        )

    val TRANSLATION_SHEET_TRANSLATE_FROM =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translations_bottom_sheet_translate_from),
            description = "Translation bottom sheet translate from dropdown",
            groups = listOf("notTranslatedPageTranslationSheet"),
        )

    val TRANSLATION_SHEET_TRANSLATE_TO =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translations_bottom_sheet_translate_to),
            description = "Translation bottom sheet translate to dropdown",
            groups = listOf("notTranslatedPageTranslationSheet"),
        )

    val TRANSLATION_SHEET_TRANSLATE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translations_bottom_sheet_positive_button),
            description = "Translation bottom sheet translate button",
            groups = listOf("notTranslatedPageTranslationSheet"),
        )

    val TRANSLATION_SHEET_NOT_NOW_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translations_bottom_sheet_negative_button),
            description = "Translation bottom sheet not now button",
            groups = listOf("notTranslatedPageTranslationSheet"),
        )

    val TRANSLATION_SHEET_SHOW_ORIGINAL_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translations_bottom_sheet_negative_button_restore),
            description = "Translation bottom sheet show original button",
            groups = listOf("translatedPageTranslationSheet"),
        )

    val ADDED_TO_SHORTCUTS_SNACKBAR_TEXT =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.snackbar_added_to_shortcuts),
            description = "Added to shortcuts snackbar text",
            groups = listOf("addedToShortcutsSnackbar"),
        )

    // Web form submit button. The value is a raw web DOM id, NOT a Compose tag — but GeckoView exposes web
    // element ids unprefixed in the accessibility tree, exactly like Compose's testTagsAsResourceId, so
    // UIAUTOMATOR_WITH_COMPOSE_TAG's un-namespaced resourceId lookup is the mechanism that matches it.
    val SUBMIT_LOGIN_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "submit",
            description = "Web form submit/login button",
            groups = listOf(),
        )

    val UPLOAD_FILE_WEB_INPUT =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RAW_RES_ID,
            value = "upload_file",
            description = "Web form file upload input",
            groups = listOf(),
        )

    val USERNAME_WEB_FIELD =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "username",
            description = "Web form username field",
            groups = listOf(),
        )

    val PASSWORD_WEB_FIELD =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "password",
            description = "Web form password field",
            groups = listOf(),
        )

    val TOGGLE_PASSWORD_WEB_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "togglePassword",
            description = "Web form show-password toggle",
            groups = listOf(),
        )

    @Suppress("FunctionName")
    fun PREFILLED_USERNAME(text: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_WEB_ID_AND_TEXT,
            value = "username",
            secondaryValue = text,
            description = "Web form username prefilled with '$text'",
            groups = listOf(),
        )

    @Suppress("FunctionName")
    fun PREFILLED_PASSWORD(text: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_WEB_ID_AND_TEXT,
            value = "password",
            secondaryValue = text,
            description = "Web form password prefilled with '$text'",
            groups = listOf(),
        )

    val SUGGESTED_LOGINS_BAR =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_RES,
            value = "loginSelectBar",
            description = "Suggested logins bar",
            groups = listOf(),
        )

    @Suppress("FunctionName")
    fun SUGGESTED_LOGIN(username: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = username,
            description = "Suggested login '$username'",
            groups = listOf(),
        )

    // Save-login prompt is an app View (package-prefixed res-id).
    val SAVE_LOGIN_PROMPT =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "feature_prompt_login_fragment",
            description = "Save-login prompt",
            groups = listOf(),
        )

    val SAVE_LOGIN_PROMPT_CONFIRM_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_RES,
            value = "save_confirm",
            description = "Save-login prompt: confirm (Save/Update) button",
            groups = listOf(),
        )

    // --- Address autofill on a web form (GeckoView content + the app's autofill prompt) ---

    // Web DOM ids on the address form page. Not Compose tags — GeckoView exposes web element ids
    // unprefixed in the accessibility tree, so UIAUTOMATOR_WITH_COMPOSE_TAG's un-namespaced resourceId
    // lookup is what matches them (same mechanism as SUBMIT_LOGIN_BUTTON above).
    val ADDRESS_STREET_WEB_FIELD =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "streetAddress",
            description = "Web address form: street address field",
            groups = listOf(),
        )

    val ADDRESS_CITY_WEB_FIELD =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "city",
            description = "Web address form: city field",
            groups = listOf(),
        )

    val ADDRESS_COUNTRY_WEB_FIELD =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "country",
            description = "Web address form: country field",
            groups = listOf(),
        )

    // NOTE: the Android stylus-handwriting prompt that can cover this page is handled centrally via
    // OverlayRegistry + BasePage.dismissKnownOverlaysIfPresent(), not with a per-page selector here.

    // The "Select address" header of the autofill prompt (an app View, package-prefixed res-id).
    val SELECT_ADDRESS_HEADER =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "select_address_header",
            description = "Autofill prompt: 'Select address' header",
            groups = listOf(),
        )

    // A saved-address suggestion row in the autofill prompt, keyed by the substring shown in its
    // name/title (e.g. the street address). App View: package-prefixed res-id + textContains.
    @Suppress("FunctionName")
    fun ADDRESS_SUGGESTION(text: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_CONTAINING_TEXT,
            value = "address_name",
            secondaryValue = text,
            description = "Autofill suggestion containing '$text'",
            groups = listOf(),
        )

    // Assertion helper: the web street-address field is populated with the expected value.
    // Raw web DOM id + exact text.
    @Suppress("FunctionName")
    fun AUTOFILLED_STREET_ADDRESS(text: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_WEB_ID_AND_TEXT,
            value = "streetAddress",
            secondaryValue = text,
            description = "Web address form: street address autofilled with '$text'",
            groups = listOf(),
        )

    // --- Credit card autofill on a web form (GeckoView content + the app's autofill prompt) ---

    // Web DOM id on the credit card form page. Not a Compose tag — GeckoView exposes web element ids
    // unprefixed in the accessibility tree, so UIAUTOMATOR_WITH_COMPOSE_TAG's un-namespaced resourceId
    // lookup is what matches it (same mechanism as ADDRESS_STREET_WEB_FIELD above).
    val CREDIT_CARD_NUMBER_WEB_FIELD =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "cardNumber",
            description = "Web credit card form: card number field",
            groups = listOf(),
        )

    // The "Select credit card" header of the autofill prompt (an app View, package-prefixed res-id).
    val SELECT_CREDIT_CARD_HEADER =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "select_credit_card_header",
            description = "Autofill prompt: 'Select credit card' header",
            groups = listOf(),
        )

    // A saved-card suggestion row in the autofill prompt, keyed by the last digits shown in its masked
    // number. App View: package-prefixed res-id + textContains.
    @Suppress("FunctionName")
    fun CREDIT_CARD_SUGGESTION(lastDigits: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_CONTAINING_TEXT,
            value = "credit_card_number",
            secondaryValue = lastDigits,
            description = "Autofill suggestion for card ending '$lastDigits'",
            groups = listOf(),
        )

    // Assertion helper: the web card-number field is populated with the expected value. Raw web DOM id
    // + exact text.
    @Suppress("FunctionName")
    fun AUTOFILLED_CREDIT_CARD(number: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_WEB_ID_AND_TEXT,
            value = "cardNumber",
            secondaryValue = number,
            description = "Web credit card form: card number autofilled with '$number'",
            groups = listOf(),
        )

    // "Set cookies" button on the storage_write.html test page. Web DOM id, exposed unprefixed by
    // GeckoView (same mechanism as SUBMIT_LOGIN_BUTTON), so UIAUTOMATOR_WITH_COMPOSE_TAG matches it.
    val SET_COOKIES_WEB_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "setCookies",
            description = "Web page 'Set cookies' button",
            groups = listOf(),
        )

    // UiObject2 (By.textContains): clicking these applinks-prompt buttons only dismisses the in-app
    // sheet, which the legacy UiObject.click() misreports as a failed click (gotcha: no post-click
    // window-change event for its sync to latch). UiObject2.click() does not gate on that sync.
    val STAY_IN_FIREFOX_PROMPT_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_TEXT_CONTAINS,
            value = "Stay in",
            description = "Applinks prompt 'Stay in Firefox' button",
            groups = listOf(),
        )

    val OPEN_IN_APP_PROMPT_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_TEXT_CONTAINS,
            value = getStringResource(applinksR.string.mozac_feature_applinks_confirm_dialog_confirm_2),
            description = "Applinks prompt 'Open in App' button",
            groups = listOf(),
        )

    // Title of the "open link in another app" prompt, parameterized by the target app name.
    @Suppress("FunctionName")
    fun OPEN_IN_APP_PROMPT(appName: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value =
                getStringResource(
                    applinksR.string.mozac_feature_applinks_normal_confirm_dialog_title_with_app_name_2,
                    appName,
                ),
            description = "Open link in '$appName' app prompt",
            groups = listOf(),
        )

    val all =
        listOf(
            ADDED_TO_SHORTCUTS_SNACKBAR_TEXT,
            ADDRESS_CITY_WEB_FIELD,
            ADDRESS_COUNTRY_WEB_FIELD,
            ADDRESS_STREET_WEB_FIELD,
            ADDRESS_SUGGESTION(),
            AUTOFILLED_STREET_ADDRESS(),
            AUTOFILLED_CREDIT_CARD(),
            CREDIT_CARD_NUMBER_WEB_FIELD,
            CREDIT_CARD_SUGGESTION(),
            SELECT_CREDIT_CARD_HEADER,
            ENGINE_VIEW,
            MAIN_MENU_BUTTON,
            MEDIA_PLAY_BUTTON,
            OPEN_IN_APP_PROMPT(),
            OPEN_IN_APP_PROMPT_BUTTON,
            PAGE_CONTENT,
            PAGE_CONTENT(),
            PASSWORD_WEB_FIELD,
            PREFILLED_PASSWORD(),
            PREFILLED_USERNAME(),
            SAVE_LOGIN_PROMPT,
            SAVE_LOGIN_PROMPT_CONFIRM_BUTTON,
            SELECT_ADDRESS_HEADER,
            SET_COOKIES_WEB_BUTTON,
            STAY_IN_FIREFOX_PROMPT_BUTTON,
            SUBMIT_LOGIN_BUTTON,
            TEXT_SELECTION_CONTEXT_MENU_ITEM(),
            SUGGESTED_LOGIN(),
            SUGGESTED_LOGINS_BAR,
            TAB_CRASH_REPORTER_CLOSE_BUTTON,
            SNACKBAR,
            MAIN_MENU_BUTTON,
            TAB_CRASH_REPORTER_IMAGE,
            TAB_CRASH_REPORTER_MESSAGE,
            TAB_CRASH_REPORTER_RESTORE_BUTTON,
            TAB_CRASH_REPORTER_TITLE,
            TOGGLE_PASSWORD_WEB_BUTTON,
            TRANSLATION_SHEET,
            TRANSLATION_SHEET_NOT_NOW_BUTTON,
            TRANSLATION_SHEET_SHOW_ORIGINAL_BUTTON,
            TRANSLATION_SHEET_TITLE,
            TRANSLATION_SHEET_TRANSLATE_BUTTON,
            TRANSLATION_SHEET_TRANSLATE_FROM,
            TRANSLATION_SHEET_TRANSLATE_TO,
            UPLOAD_FILE_WEB_INPUT,
            USERNAME_WEB_FIELD,
            SNACKBAR_ACTION_BUTTON,
        )
}
