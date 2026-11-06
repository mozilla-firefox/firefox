/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import androidx.compose.ui.semantics.SemanticsConfiguration
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.test.uiautomator.UiSelector
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_SEARCH_BOX
import org.junit.Assert.assertTrue
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy
import org.mozilla.fenix.ui.efficiency.navigation.NavigationGraph
import org.mozilla.fenix.ui.efficiency.navigation.NavigationOptions
import org.mozilla.fenix.ui.efficiency.navigation.NavigationStep
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SearchBarSelectors
import org.mozilla.fenix.ui.efficiency.selectors.TabDrawerSelectors
import org.mozilla.fenix.ui.efficiency.selectors.ToolbarSelectors

class SearchBarComponent(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) : BasePage(composeRule) {
    override val pageName = "SearchBarComponent"

    internal override fun registerNavigation(builder: NavigationGraph.Builder) {
        // Click empty Search bar to enter a URL
        builder.register(
            from = "HomePage",
            to = pageName,
            steps = listOf(NavigationStep.Click(ToolbarSelectors.TOOLBAR_URL_BOX)),
        )

        // Click search bar to edit or replace a URL
        // Use UIAutomator selector to avoid Compose sync hanging when GeckoView is active.
        builder.register(
            from = "BrowserPage",
            to = pageName,
            steps = listOf(NavigationStep.Click(ToolbarSelectors.TOOLBAR_URL_BOX_UIAUTOMATOR2)),
        )

        // The FAB opens a new tab in whichever tray page is selected; only the normal and private pages show it.
        builder.register(
            from = "TabDrawerPage",
            to = pageName,
            steps = listOf(NavigationStep.Click(TabDrawerSelectors.FAB)),
        )

        // Dismiss the search bar (edit mode) back to Home. Without an outbound edge the graph can enter
        // the search bar but never route out of it.
        builder.register(
            from = pageName,
            to = "HomePage",
            steps = listOf(NavigationStep.PressBack),
        )
    }

    override val selectorCatalog = SearchBarSelectors

    override fun navigateToPage(
        url: String,
        forceNavigation: Boolean,
        navigationOptions: NavigationOptions,
    ): SearchBarComponent {
        super.navigateToPage(url, forceNavigation = forceNavigation, navigationOptions = navigationOptions)
        return this
    }

    fun verifyUrl(url: String): SearchBarComponent {
        mozVerify(
            Selector(
                strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
                value = url,
                description = "URL bar contains '$url'",
            )
        )
        return this
    }

    /** Clear the edit-mode toolbar text (the "X" button). Ports SearchRobot.clickClearButton. */
    fun clickClearButton(): SearchBarComponent {
        mozClick(SearchBarSelectors.CLEAR_BUTTON)
        return this
    }

    /** Assert the empty search bar shows its "Search or enter address" hint. */
    fun verifySearchBarPlaceholder(): SearchBarComponent {
        mozVerify(SearchBarSelectors.SEARCH_BAR_PLACEHOLDER)
        return this
    }

    /** Long-press the edit-mode search box to raise the paste popup. Ports SearchRobot.longClickToolbar. */
    fun longClickToolbar(): SearchBarComponent {
        mozLongClick(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        return this
    }

    /** Click an item on the paste/text-selection popup over the search box (e.g. "Paste"). */
    fun clickContextMenuItem(item: String): SearchBarComponent {
        val selector = BrowserPageSelectors.TEXT_SELECTION_CONTEXT_MENU_ITEM(item)
        mozVerify(selector, timeout = waitingTime)
        mozClick(selector)
        return this
    }

    /**
     * Assert the edit-mode toolbar's text [contains][exists] [expectedText] (whitespace-normalized). Ports
     * SearchRobot.verifyTypedToolbarText: the box exposes both a Text and an EditableText semantics property, and the
     * check collapses runs of whitespace so, for example, the leading/trailing spaces that "Select all" copies compare
     * equal.
     */
    fun verifyTypedToolbarText(expectedText: String, exists: Boolean = true): SearchBarComponent {
        mozVerify(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE, timeout = waitingTime)
        val normalizedExpected = normalizeWhitespace(expectedText)
        val actual =
            composeRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).fetchSemanticsNode().config.toNormalizedToolbarText()
        assertTrue(
            "Expected toolbar text '$normalizedExpected' to ${if (exists) "exist" else "not exist"} in '$actual'",
            if (exists) actual.contains(normalizedExpected) else !actual.contains(normalizedExpected),
        )
        return this
    }

    private fun normalizeWhitespace(text: String): String = text.replace(Regex("\\s+"), " ").trim()

    private fun SemanticsConfiguration.toNormalizedToolbarText(): String {
        val textParts = buildList {
            getOrNull(SemanticsProperties.Text)?.let { annotations -> addAll(annotations.map { it.text }) }
            getOrNull(SemanticsProperties.EditableText)?.let { editableText -> add(editableText.text) }
        }
        return normalizeWhitespace(textParts.joinToString(" "))
    }

    /**
     * Assert the QR scanner opened, accepting the Lens camera's QR title, the view finder or the camera-error view.
     *
     * The OR is deliberate and carried over from the legacy robot: on a device or emulator with no usable camera the
     * app shows an error instead of a view finder, and the test still means "the scanner was launched". The Lens camera
     * shows its title in both cases, and the legacy scanner is still used when the Google Lens integration is disabled.
     * mozVerify* cannot express alternation, hence the direct UIAutomator check.
     */
    fun verifyScannerOpen(): SearchBarComponent {
        assertTrue(
            "Neither the QR scanner title, the QR view finder nor the camera error view was displayed",
            mDevice
                .findObject(UiSelector().text(getStringResource(R.string.lens_camera_title_qr)))
                .waitForExists(waitingTime) ||
                mDevice.findObject(UiSelector().resourceId("$packageName:id/view_finder")).exists() ||
                mDevice.findObject(UiSelector().resourceId("$packageName:id/camera_error")).exists(),
        )
        return this
    }
}
