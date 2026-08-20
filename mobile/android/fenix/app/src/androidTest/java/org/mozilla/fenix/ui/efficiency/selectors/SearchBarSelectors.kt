/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import mozilla.components.browser.toolbar.R as toolbarR
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_SEARCH_BOX
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.SEARCH_SELECTOR
import mozilla.components.feature.qr.R as qrR
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object SearchBarSelectors {
    // Page-identity anchor for the edit-mode search bar. Must be an EDIT-mode-only handle: the redesigned Home/browser
    // display toolbar shows the SEARCH_SELECTOR chip too, so anchoring on that alone makes mozIsOnPageNow() (and thus
    // navigateToPage) falsely believe edit mode is already open and skip the tap that opens it. ADDRESSBAR_SEARCH_BOX
    // exists only in edit mode (display mode uses ADDRESSBAR_URL_BOX), so it disambiguates the two.
    val TOOLBAR_IN_EDIT_MODE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = ADDRESSBAR_SEARCH_BOX,
            description = "Toolbar in edit mode",
            groups = listOf("requiredForPage"),
        )

    val URL_TEXT =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "mozac_browser_toolbar_url_view",
            description = "Page URL",
            groups = listOf("requiredForBrowserPage"),
        )

    val SEARCH_ENGINE_SELECTOR =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = SEARCH_SELECTOR,
            description = "Search engine selector button",
            groups = listOf("requiredForPage"),
        )

    // An engine row inside the search-selector popup menu. Each row exposes the plain engine name as
    // its content-description (mirrors the legacy verifySearchShortcutList / selectTemporarySearchMethod
    // which matched hasContentDescription(engineName)).
    @Suppress("FunctionName")
    fun SEARCH_SELECTOR_MENU_ENGINE(engineName: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = engineName,
            description = "Search selector menu engine: $engineName",
            groups = listOf(),
        )

    // A single awesomebar suggestion row. The tag is on each suggestion item (the container uses the
    // plural "mozac.awesomebar.suggestions"). Verified against ground truth as a collection with
    // mozVerifyAnyContainsText / mozVerifyNoneContainText.
    val AWESOMEBAR_SUGGESTION =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = "mozac.awesomebar.suggestion",
            description = "Awesomebar search suggestion",
            groups = listOf(),
        )

    // Text, not content-description, and deliberately so despite text being the last-resort handle: in edit
    // mode the hint is rendered as its own text node, and ADDRESSBAR_SEARCH_BOX carries no description at
    // all (verified from a ScreenDump of the search view). The browser-view address bar does expose the hint
    // as part of its description, but that is a different element in a different state. Keyed off the string
    // resource so it survives localization.
    val SEARCH_BAR_PLACEHOLDER =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.search_hint),
            description = "Search bar placeholder",
            groups = listOf(),
        )

    // A row in the search selector menu, keyed by engine/shortcut name ("Tabs", "Bookmarks", "History").
    // Content-description is the handle the menu row exposes; the name is not rendered as a text node.
    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun SEARCH_SHORTCUT(searchShortcutName: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = searchShortcutName,
            description = "'$searchShortcutName' search shortcut",
            groups = listOf(),
        )

    // A group header in the awesomebar, keyed by its text ("TestSearchEngine search", "Firefox Suggest").
    // Text is the only handle the header exposes; it carries no tag and no content description.
    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun SUGGESTIONS_HEADER(headerText: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = headerText,
            description = "'$headerText' suggestions header",
            groups = listOf(),
        )

    val FIREFOX_SUGGEST_HEADER =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.firefox_suggest_header),
            description = "Firefox Suggest suggestions header",
            groups = listOf(),
        )

    // One awesomebar suggestion row. The tag is an android-components literal with no exported constant,
    // so it is duplicated here the same way the legacy robots duplicate it. Match rows with
    // mozVerifyAnyContainsText / mozVerifyNoneContainText rather than adding a per-suggestion selector.
    val SEARCH_SUGGESTION =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = "mozac.awesomebar.suggestion",
            description = "Search suggestion",
            groups = listOf(),
        )

    // The same suggestion rows, but addressable individually so one can be clicked. Tag AND text: the
    // text alone is ambiguous because an open tab's URL also renders in the homepage "Jump back in" row
    // behind the search overlay. The row's text lives on a descendant, so this only resolves on the
    // merged tree — which resolveComposeNode reaches via its other-tree fallback.
    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun SEARCH_SUGGESTION_WITH_TEXT(suggestionText: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG_AND_TEXT,
            value = "mozac.awesomebar.suggestion",
            secondaryValue = suggestionText,
            description = "Search suggestion '$suggestionText'",
            groups = listOf(),
        )

    val SCAN_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(qrR.string.mozac_feature_qr_scanner),
            description = "QR scan button",
            groups = listOf(),
        )

    // Clear (X) button in the edit-mode toolbar. Content-description "Clear", keyed off the string
    // resource so it survives localization (mirrors the legacy SearchRobot.clickClearButton).
    // UiObject2 (By.descContains) rather than UiObject: UiObject.click() returns false on this button
    // even when the tap lands (the framework then throws "Failed to click UiObject"); UiObject2.click()
    // does not gate on that return value. Same gotcha as the applinks prompt buttons.
    val CLEAR_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_DESCRIPTION_CONTAINS,
            value = getStringResource(toolbarR.string.mozac_clear_button_description),
            description = "Search bar clear button",
            groups = listOf(),
        )

    // End-of-address-bar button in edit mode. The slot is mutually exclusive: the Google Lens button shows only when
    // the Lens integration is enabled (Nimbus + user pref), the session is non-private, and Google is the selected
    // engine; otherwise the QR scanner button takes the slot for general/custom engines. Both are Compose
    // ActionButtonRes with a content-description and NO testTag
    // (BrowserToolbarSearchMiddleware.updateSearchEndPageActions),
    // so we match on the content-description string, keyed off the resource so it survives localization.
    val GOOGLE_LENS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.lens_search_content_description),
            description = "Google Lens button (search with image)",
            groups = listOf(),
        )

    val all =
        listOf(
            TOOLBAR_IN_EDIT_MODE,
            URL_TEXT,
            SEARCH_ENGINE_SELECTOR,
            SEARCH_SELECTOR_MENU_ENGINE(),
            AWESOMEBAR_SUGGESTION,
            SEARCH_BAR_PLACEHOLDER,
            SEARCH_SHORTCUT(),
            SUGGESTIONS_HEADER(),
            FIREFOX_SUGGEST_HEADER,
            SEARCH_SUGGESTION,
            SEARCH_SUGGESTION_WITH_TEXT(),
            SCAN_BUTTON,
            CLEAR_BUTTON,
            GOOGLE_LENS_BUTTON,
        )
}
