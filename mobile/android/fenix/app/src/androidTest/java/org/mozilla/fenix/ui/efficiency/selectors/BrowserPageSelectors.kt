/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import mozilla.components.feature.app.links.R as applinksR
import mozilla.components.feature.prompts.R as promptsR
import mozilla.components.feature.summarize.R as summarizeR
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.snackbar.SNACKBAR_BUTTON_TEST_TAG
import org.mozilla.fenix.compose.snackbar.SNACKBAR_TEST_TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.TestHelper.shortAppName
import org.mozilla.fenix.pdf.PdfToolsTestTag
import org.mozilla.fenix.ui.efficiency.helpers.PageReadinessProfiles
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorContainer
import org.mozilla.fenix.ui.efficiency.helpers.SelectorGroup
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object BrowserPageSelectors : SelectorContainer {
    enum class Group : SelectorGroup {
        SNACKBAR,
        TAB_CRASH_REPORTER,
        NOT_TRANSLATED_PAGE_TRANSLATION_SHEET,
        TRANSLATED_PAGE_TRANSLATION_SHEET,
        ADDED_TO_SHORTCUTS_SNACKBAR,
        SHAKE_TO_SUMMARIZE_CFR,
        SIGN_IN_TO_SUMMARIZE_BOTTOM_SHEET,
        TRANSLATION_BANNER,
        OPEN_IN_APP_CFR,
    }

    val ENGINE_VIEW =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "engineView",
            description = "Engine view",
            readiness = PageReadinessProfiles.IDENTITY_ANCHOR,
        )

    val PAGE_CONTENT =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "mozilla",
            description = "Page content",
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
        )

    // The snackbar's single action button. The tag is generic and the label varies with the snackbar
    // ("Edit", "SWITCH", "UNDO"), which is why the legacy helper matches the tag OR the label rather than
    // both. Named for the element, not for one of its labels.
    val SNACKBAR_ACTION_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = SNACKBAR_BUTTON_TEST_TAG,
            description = "Snackbar action button",
            groups = setOf(Group.SNACKBAR),
        )

    val SNACKBAR =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = SNACKBAR_TEST_TAG,
            description = "Snackbar container",
        )

    // The summarize sheet's drag handle, present for every outcome (summary, error, or sign-in prompt).
    val SUMMARIZE_BOTTOM_SHEET =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = "shake.to.summarize.bottom.sheet.drag.handle",
            description = "the Summarize bottom sheet",
        )

    val MAIN_MENU_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = getStringResource(R.string.content_description_menu),
            description = "Three Dot Menu",
            // NOT an arrival anchor: the menu button lives on the top toolbar in the default layout but moves to
            // the bottom navigation bar when shouldUseExpandedToolbar is on — so it's layout-dependent (gotcha B7).
            // ENGINE_VIEW (the web content) is the layout-invariant browser-readiness signal.

        )

    val TAB_CRASH_REPORTER_IMAGE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "crash_tab_image",
            description = "Tab crash reporter image",
            groups = setOf(Group.TAB_CRASH_REPORTER),
        )

    val TAB_CRASH_REPORTER_TITLE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.tab_crash_title_2),
            description = "Tab crash reporter title",
            groups = setOf(Group.TAB_CRASH_REPORTER),
        )

    val TAB_CRASH_REPORTER_MESSAGE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.tab_crash_send_report),
            description = "Tab crash reporter send crash message",
            groups = setOf(Group.TAB_CRASH_REPORTER),
        )

    val TAB_CRASH_REPORTER_RESTORE_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "restoreTabButton",
            description = "Tab crash reporter restore button",
            groups = setOf(Group.TAB_CRASH_REPORTER),
        )

    val TAB_CRASH_REPORTER_CLOSE_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "closeTabButton",
            description = "Tab crash reporter close button",
            groups = setOf(Group.TAB_CRASH_REPORTER),
        )

    @Suppress("FunctionName")
    fun PAGE_CONTENT(text: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = text,
            description = "Page content '$text'",
        )

    // PAGE_CONTENT for a long-press. UiObject.longClick() reports failure when no window update lands
    // inside its acknowledgment timeout, which GeckoView text selection does not reliably produce even
    // though the selection toolbar opens. UiObject2.longClick() just injects the gesture.
    @Suppress("FunctionName")
    fun PAGE_CONTENT_LONG_PRESS_TARGET(text: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_TEXT_CONTAINS,
            value = text,
            description = "Page content '$text' long-press target",
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
        )

    // The link's target URL shown as the header of the web-content long-press context menu. Contains-match,
    // like the legacy contextMenuLinkUrl (itemContainingText): the menu renders the full URL and nothing
    // else on screen contains it, so a substring match is both sufficient and robust to how it's displayed.
    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun CONTEXT_MENU_LINK_URL(url: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = url,
            description = "Context menu link URL '$url'",
        )

    // A snackbar's message text (e.g. "New tab opened"), matched by substring like the legacy
    // verifySnackBarText (itemContainingText).
    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun SNACKBAR_MESSAGE(text: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = text,
            description = "Snackbar message '$text'",
        )

    // Compose variant of SNACKBAR_MESSAGE, for snackbars whose message is a Compose text node absent
    // from the UIAutomator text tree (e.g. the "Tab closed" undo snackbar).
    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun SNACKBAR_MESSAGE_COMPOSE(text: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = text,
            description = "Snackbar message '$text' (Compose)",
        )

    val TRANSLATION_SHEET =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_RES,
            value = "design_bottom_sheet",
            description = "Translation bottom sheet",
            groups = setOf(Group.NOT_TRANSLATED_PAGE_TRANSLATION_SHEET),
        )

    val TRANSLATION_SHEET_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translations_bottom_sheet_title_first_time, argument = shortAppName),
            description = "Translation bottom sheet title",
            groups = setOf(Group.NOT_TRANSLATED_PAGE_TRANSLATION_SHEET),
        )

    val TRANSLATION_SHEET_TRANSLATE_FROM =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translations_bottom_sheet_translate_from),
            description = "Translation bottom sheet translate from dropdown",
            groups = setOf(Group.NOT_TRANSLATED_PAGE_TRANSLATION_SHEET),
        )

    val TRANSLATION_SHEET_TRANSLATE_TO =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translations_bottom_sheet_translate_to),
            description = "Translation bottom sheet translate to dropdown",
            groups = setOf(Group.NOT_TRANSLATED_PAGE_TRANSLATION_SHEET, Group.TRANSLATED_PAGE_TRANSLATION_SHEET),
        )

    val TRANSLATION_SHEET_TRANSLATE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translations_bottom_sheet_positive_button),
            description = "Translation bottom sheet translate button",
            groups = setOf(Group.NOT_TRANSLATED_PAGE_TRANSLATION_SHEET, Group.TRANSLATED_PAGE_TRANSLATION_SHEET),
        )

    val TRANSLATION_SHEET_NOT_NOW_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translations_bottom_sheet_negative_button),
            description = "Translation bottom sheet not now button",
            groups = setOf(Group.NOT_TRANSLATED_PAGE_TRANSLATION_SHEET),
        )

    val TRANSLATED_SHEET_SHOW_ORIGINAL_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translations_bottom_sheet_negative_button_restore),
            description = "Translated bottom sheet show original button",
            groups = setOf(Group.TRANSLATED_PAGE_TRANSLATION_SHEET),
        )

    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun TRANSLATED_SHEET_TITLE(translatedFrom: String, translatedTo: String) =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value =
                getStringResource(
                    R.string.translations_bottom_sheet_title_translation_completed,
                    translatedFrom,
                    translatedTo,
                ),
            description = "Translated bottom sheet title translation completed",
        )

    val TRANSLATED_SHEET_TRANSLATION_OPTIONS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.translation_option_bottom_sheet_title_heading),
            description = "Translated bottom sheet translation options button",
            groups = setOf(Group.NOT_TRANSLATED_PAGE_TRANSLATION_SHEET, Group.TRANSLATED_PAGE_TRANSLATION_SHEET),
        )

    val TRANSLATED_SHEET_HANDLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.translation_option_bottom_sheet_close_content_description),
            description = "Translated bottom sheet handle",
            groups = setOf(Group.TRANSLATED_PAGE_TRANSLATION_SHEET),
        )

    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun TRANSLATION_BANNER_TRANSLATED_FROM_TO(translatedFrom: String, translatedTo: String) =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value =
                getStringResource(R.string.translation_toolbar_translated_from_and_to, translatedFrom, translatedTo),
            description = "Translation banner translated from $translatedFrom to $translatedTo",
        )

    val TRANSLATION_BANNER_OPEN_TRANSLATION_SHEET_CHEVRON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.translation_toolbar_expand_action),
            description = "Translation banner open translation sheet chevron",
            groups = setOf(Group.TRANSLATION_BANNER),
        )

    val TRANSLATION_BANNER_DISMISS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.translation_toolbar_close_action),
            description = "Translation banner dismiss button",
            groups = setOf(Group.TRANSLATION_BANNER),
        )

    val ADDED_TO_SHORTCUTS_SNACKBAR_TEXT =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.snackbar_added_to_shortcuts),
            description = "Added to shortcuts snackbar text",
            groups = setOf(Group.ADDED_TO_SHORTCUTS_SNACKBAR),
        )

    // Web form submit button. The value is a raw web DOM id, NOT a Compose tag — but GeckoView exposes web
    // element ids unprefixed in the accessibility tree, exactly like Compose's testTagsAsResourceId, so
    // UIAUTOMATOR_WITH_COMPOSE_TAG's un-namespaced resourceId lookup is the mechanism that matches it.
    val SUBMIT_LOGIN_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "submit",
            description = "Web form submit/login button",
        )

    val UPLOAD_FILE_WEB_INPUT =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RAW_RES_ID,
            value = "upload_file",
            description = "Web form file upload input",
        )

    val USERNAME_WEB_FIELD =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "username",
            description = "Web form username field",
        )

    val PASSWORD_WEB_FIELD =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "password",
            description = "Web form password field",
        )

    // Same field via UiObject2 for a coordinate-tap click: UiObject.click() on a GeckoView web input is flaky (A57).
    val PASSWORD_WEB_FIELD_CLICKABLE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_RAW_RES,
            value = "password",
            description = "Web form password field (clickable)",
        )

    val TOGGLE_PASSWORD_WEB_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "togglePassword",
            description = "Web form show-password toggle",
        )

    @Suppress("FunctionName")
    fun PREFILLED_USERNAME(text: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_WEB_ID_AND_TEXT,
            value = "username",
            secondaryValue = text,
            description = "Web form username prefilled with '$text'",
        )

    @Suppress("FunctionName")
    fun PREFILLED_PASSWORD(text: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_WEB_ID_AND_TEXT,
            value = "password",
            secondaryValue = text,
            description = "Web form password prefilled with '$text'",
        )

    val SUGGESTED_LOGINS_BAR =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_RES,
            value = "loginSelectBar",
            description = "Suggested logins bar",
        )

    @Suppress("FunctionName")
    fun SUGGESTED_LOGIN(username: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = username,
            description = "Suggested login '$username'",
        )

    // Save-login prompt is an app View (package-prefixed res-id).
    val SAVE_LOGIN_PROMPT =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "feature_prompt_login_fragment",
            description = "Save-login prompt",
        )

    val SAVE_LOGIN_PROMPT_CONFIRM_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_RES,
            value = "save_confirm",
            description = "Save-login prompt: confirm (Save/Update) button",
        )

    val SAVE_LOGIN_PROMPT_NEVER_SAVE_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_RES,
            value = "save_cancel",
            description = "Save-login prompt: never save button",
        )

    val SAVE_LOGIN_PROMPT_FAVICON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_RES,
            value = "host_icon",
            description = "Save-login prompt: accessed domain favicon",
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
        )

    val ADDRESS_CITY_WEB_FIELD =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "city",
            description = "Web address form: city field",
        )

    val ADDRESS_STATE_WEB_FIELD =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "state",
            description = "Web address form: state field",
        )

    val ADDRESS_COUNTRY_WEB_FIELD =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "country",
            description = "Web address form: country field",
        )

    val ADDRESS_ZIP_WEB_FIELD =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "zipCode",
            description = "Web address form: zip code field",
        )

    val ADDRESS_TELEPHONE_WEB_FIELD =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "telephone",
            description = "Web address form: telephone field",
        )

    val ADDRESS_EMAIL_WEB_FIELD =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "email",
            description = "Web address form: email field",
        )

    // The settings EditAddress form has no apartment (address-line2) field, so a saved address always leaves it empty.
    // Filling it on the web form while keeping every other field identical makes it a "superset" of the saved record,
    // which is what turns the capture into an Update (vs Save) prompt -- see FormAutofillParent._onAddressSubmit.
    val ADDRESS_APARTMENT_WEB_FIELD =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "apartment",
            description = "Web address form: apartment field",
        )

    val SAVE_ADDRESS_PROMPT =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(promptsR.string.mozac_feature_prompts_save_address_prompt_title),
            description = "Save-address capture prompt",
        )

    val SAVE_ADDRESS_PROMPT_NOT_NOW_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(promptsR.string.mozac_feature_prompt_not_now),
            description = "Save-address capture prompt: 'Not now' button",
        )

    // Shown instead of SAVE_ADDRESS_PROMPT when the submitted address is mergeable into an existing saved one
    // (no "different" field, at least one "superset"/"similar"). Compose dialog title, matched by text via UIAutomator.
    val UPDATE_ADDRESS_PROMPT =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(promptsR.string.mozac_feature_prompts_update_address_prompt_title),
            description = "Update-address capture prompt",
        )

    val UPDATE_ADDRESS_PROMPT_UPDATE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(promptsR.string.mozac_feature_prompt_update_confirmation),
            description = "Update-address capture prompt: 'Update' button",
        )

    val UPDATE_ADDRESS_PROMPT_NOT_NOW_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(promptsR.string.mozac_feature_prompt_dont_update_2),
            description = "Update-address capture prompt: 'Not now' button",
        )

    // NOTE: the Android stylus-handwriting prompt that can cover this page is handled centrally via
    // OverlayRegistry + BasePage.dismissKnownOverlaysIfPresent(), not with a per-page selector here.

    // The "Select address" header of the autofill prompt (an app View, package-prefixed res-id).
    val SELECT_ADDRESS_HEADER =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "select_address_header",
            description = "Autofill prompt: 'Select address' header",
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
        )

    // The "Select credit card" header of the autofill prompt (an app View, package-prefixed res-id).
    val SELECT_CREDIT_CARD_HEADER =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "select_credit_card_header",
            description = "Autofill prompt: 'Select credit card' header",
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
        )

    // "Set cookies" button on the storage_write.html test page. Web DOM id, exposed unprefixed by
    // GeckoView (same mechanism as SUBMIT_LOGIN_BUTTON), so UIAUTOMATOR_WITH_COMPOSE_TAG matches it.
    val SET_COOKIES_WEB_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = "setCookies",
            description = "Web page 'Set cookies' button",
        )

    // UiObject2 (By.textContains): clicking these applinks-prompt buttons only dismisses the in-app
    // sheet, which the legacy UiObject.click() misreports as a failed click (gotcha: no post-click
    // window-change event for its sync to latch). UiObject2.click() does not gate on that sync.
    val STAY_IN_FIREFOX_PROMPT_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_TEXT_CONTAINS,
            value = "Stay in",
            description = "Applinks prompt 'Stay in Firefox' button",
        )

    val OPEN_IN_APP_PROMPT_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_TEXT_CONTAINS,
            value = getStringResource(applinksR.string.mozac_feature_applinks_confirm_dialog_confirm_2),
            description = "Applinks prompt 'Open in App' button",
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
        )

    // "Open links in apps" CFR banner, shown on a page that a non-browser app can handle when
    // isOpenInAppBannerEnabled is on. The banner is an app View (package-prefixed res-ids), so each part is matched
    // with UIAUTOMATOR_WITH_RES_ID[_CONTAINING_TEXT], mirroring the legacy BrowserRobot.verifyOpenLinksInAppsCFRExists.
    val OPEN_IN_APP_CFR_CONTAINER =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "banner_container",
            description = "Open links in apps CFR banner container",
            groups = setOf(Group.OPEN_IN_APP_CFR),
        )

    val OPEN_IN_APP_CFR_INFO_MESSAGE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_CONTAINING_TEXT,
            value = "banner_info_message",
            secondaryValue = getStringResource(R.string.open_in_app_cfr_info_message_2),
            description = "Open links in apps CFR info message",
            groups = setOf(Group.OPEN_IN_APP_CFR),
        )

    val OPEN_IN_APP_CFR_DISMISS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_CONTAINING_TEXT,
            value = "dismiss",
            secondaryValue = getStringResource(R.string.open_in_app_cfr_negative_button_text),
            description = "Open links in apps CFR Dismiss button",
            groups = setOf(Group.OPEN_IN_APP_CFR),
        )

    val OPEN_IN_APP_CFR_ACTION_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_CONTAINING_TEXT,
            value = "action",
            secondaryValue = getStringResource(R.string.open_in_app_cfr_positive_button_text),
            description = "Open links in apps CFR 'Go to settings' button",
            groups = setOf(Group.OPEN_IN_APP_CFR),
        )

    val PDF_DOWNLOAD_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RAW_RES_ID_CONTAINING_TEXT,
            value = "download",
            secondaryValue = "Download",
            description = "PDF viewer download button",
        )

    val PDF_TOOLS_DOWNLOAD_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = PdfToolsTestTag.DOWNLOAD_BUTTON,
            description = "PDF tools download button",
        )

    val SHAKE_TO_SUMMARIZE_CFR_DISMISS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = "cfr.dismiss",
            description = "Shake to summarize CFR dismiss button",
            groups = setOf(Group.SHAKE_TO_SUMMARIZE_CFR),
        )

    val SHAKE_TO_SUMMARIZE_CFR_MESSAGE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_toolbar_summarize_cfr_description),
            description = "Shake to summarize CFR  message",
            groups = setOf(Group.SHAKE_TO_SUMMARIZE_CFR),
        )

    // The "Sign in to summarize" bottom sheet shown when an unauthenticated user taps Summarize page.
    val SIGN_IN_TO_SUMMARIZE_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(summarizeR.string.mozac_summarize_fxa_sign_in_title),
            description = "Sign in to summarize bottom sheet title",
            groups = setOf(Group.SIGN_IN_TO_SUMMARIZE_BOTTOM_SHEET),
        )

    // The message and its inline "Learn more" link render as a single text node, so match them joined.
    val SIGN_IN_TO_SUMMARIZE_MESSAGE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value =
                getStringResource(summarizeR.string.mozac_summarize_fxa_sign_in_message) +
                    " " +
                    getStringResource(summarizeR.string.mozac_summarize_learn_more_link),
            description = "Sign in to summarize bottom sheet message",
            groups = setOf(Group.SIGN_IN_TO_SUMMARIZE_BOTTOM_SHEET),
        )

    val SIGN_IN_TO_SUMMARIZE_SIGN_IN_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(summarizeR.string.mozac_summarize_fxa_sign_in_button_positive),
            description = "Sign in to summarize bottom sheet Sign in button",
            groups = setOf(Group.SIGN_IN_TO_SUMMARIZE_BOTTOM_SHEET),
        )

    val SIGN_IN_TO_SUMMARIZE_DISMISS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(summarizeR.string.mozac_summarize_error_dissmiss),
            description = "Sign in to summarize bottom sheet Dismiss button",
            groups = setOf(Group.SIGN_IN_TO_SUMMARIZE_BOTTOM_SHEET),
        )

    val SIGN_IN_TO_SUMMARIZE_HANDLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = "shake.to.summarize.bottom.sheet.drag.handle",
            description = "Sign in to summarize bottom sheet drag handle",
            groups = setOf(Group.SIGN_IN_TO_SUMMARIZE_BOTTOM_SHEET),
        )

    // --- Strong (generated) password prompt ---

    // The "Use strong password" bar above the keyboard; absent on the first engagement, where the sheet auto-opens.
    val SUGGEST_STRONG_PASSWORD_BAR =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_RES,
            value = "suggestStrongPasswordBar",
            description = "Suggest strong password bar",
        )

    // Title of the generated-password sheet; also distinguishes it from the save/update-login prompt it shares a
    // container with.
    val STRONG_PASSWORD_SHEET_TITLE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_TEXT,
            value = getStringResource(promptsR.string.mozac_feature_prompts_suggest_strong_password_title),
            description = "Strong password sheet title",
        )

    val STRONG_PASSWORD_USE_PASSWORD_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_TEXT,
            value = getStringResource(promptsR.string.mozac_feature_prompts_suggest_strong_password_use_password),
            description = "Strong password sheet 'Use password' button",
        )

    // Save-login headline for the add-username update. The save_message res-id is shared, so the text pins the case.
    val UPDATE_USERNAME_PROMPT_MESSAGE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_AND_TEXT,
            value = "save_message",
            secondaryValue = getStringResource(promptsR.string.mozac_feature_prompt_login_add_username_headline_2),
            description = "Update username prompt headline",
        )

    val EXTENSIONS_SETTINGS_ENGINE_VIEW =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "addonSettingsEngineView",
            description = "Extensions settings engine view",
        )
}
