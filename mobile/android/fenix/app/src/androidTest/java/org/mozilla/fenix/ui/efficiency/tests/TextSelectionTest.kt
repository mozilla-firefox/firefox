/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import org.junit.Test
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.selectors.ToolbarSelectors

class TextSelectionTest : BaseTest() {

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326832
    @SmokeTest
    @Test
    fun verifySelectAllTextOptionTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        on.home.navigateToPage()
        on.browserPage
            .navigateToPage(genericURL.url.toString())
            .longClickWebContentText("content")
            .clickContextMenuItem("Select all")
            .clickContextMenuItem("Copy")
            .longClickToolbar()
            .mozClick(ToolbarSelectors.DISPLAY_MODE_TOOLBAR_PASTE)

        // With Select all, white spaces are copied.
        // Potential bug https://bugzilla.mozilla.org/show_bug.cgi?id=1821310
        on.searchBar.verifyTypedToolbarText("  Page content: 1 ", exists = true)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326831
    @SmokeTest
    @Test
    fun verifyPrivateSearchTextTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        on.home.navigateToPage()
        on.home.switchToPrivateBrowsingMode()

        on.browserPage
            .navigateToPage(genericURL.url.toString())
            .verifyPageContent(genericURL.content)
            .searchSelectedWebText(
                webText = "content",
                menuItem = "Private Search",
                expectedTabCount = "2",
                isPrivateBrowsingEnabled = true,
            )
            .verifyUrl("content")
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/243839
    @SmokeTest
    @Test
    fun verifyCopyPDFTextOptionTest() {
        val genericURL = mockWebServer.getGenericAsset(3)

        on.home.navigateToPage()
        on.browserPage
            .navigateToPage(genericURL.url.toString())
            .clickPageContent("PDF form file")
            .clickStayInAppPromptButtonIfPresent()
            // Exact-match the caption run so the long-press can't resolve to the painting (a Wikipedia link whose
            // accessible name also contains "Crossing"); its centre glyph is still "Crossing".
            .longClickWebContentText(PDF_CAPTION, exact = true)
            .clickContextMenuItem("Copy")

        on.searchBar
            .navigateToPage()
            .clickClearButton()
            .verifySearchBarPlaceholder()
            .longClickToolbar()
            .clickContextMenuItem("Paste")
            .verifyTypedToolbarText("Crossing", exists = true)
    }

    @Test
    fun verifyCopyPDFTextOptionWithinAddressBarInFocusedModeTest() {
        composeRule.activityRule.applySettingsExceptions { it.showAddressBarInFocusMode = true }
        val genericURL = mockWebServer.getGenericAsset(3)

        on.home.navigateToPage()
        on.browserPage
            .navigateToPage(genericURL.url.toString())
            .clickPageContent("PDF form file")
            .clickStayInAppPromptButtonIfPresent()
            .longClickWebContentText(PDF_CAPTION, exact = true)
            .clickContextMenuItem("Copy")

        on.searchBar
            .navigateToPage()
            .verifySearchBarPlaceholder()
            .longClickToolbar()
            .clickContextMenuItem("Paste")
            .verifyTypedToolbarText("Crossing", exists = true)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/414316
    @Critical
    @Test
    fun urlBarQuickActionsTest() {
        val firstWebsite = mockWebServer.getGenericAsset(1)
        val secondWebsite = mockWebServer.getGenericAsset(2)

        on.home.navigateToPage()
        on.browserPage.navigateToPage(firstWebsite.url.toString()).longClickToolbar()
        on.browserPage.mozClick(ToolbarSelectors.DISPLAY_MODE_TOOLBAR_COPY)
        on.browserPage.loadUrlFromToolbar(secondWebsite.url.toString()).longClickToolbar()
        on.browserPage.mozClick(ToolbarSelectors.DISPLAY_MODE_TOOLBAR_PASTE)

        on.searchBar.verifyTypedToolbarText(firstWebsite.url.toString(), exists = true)
        on.searchBar.mozPressBack()

        on.browserPage.verifyUrl(secondWebsite.url.toString()).longClickToolbar()
        on.browserPage.mozClick(ToolbarSelectors.DISPLAY_MODE_TOOLBAR_PASTE_AND_GO)
        // Paste & Go navigates, so wait the full navigation time, not the verifyUrl 3s default.
        on.browserPage.verifyUrl(firstWebsite.url.toString(), timeout = waitingTime)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326836
    @SmokeTest
    @Test
    fun verifySearchPDFTextOptionTest() {
        val genericURL = mockWebServer.getGenericAsset(3)

        on.home.navigateToPage()
        on.browserPage
            .navigateToPage(genericURL.url.toString())
            .clickPageContent("PDF form file")
            .clickStayInAppPromptButtonIfPresent()
            .longClickWebContentText(PDF_CAPTION, exact = true)
            .clickContextMenuItem("Search")
            .verifyUrl("Crossing")
            .verifyTabCounter("2")
    }

    private companion object {
        // The caption under pdfForm.pdf's painting; long-pressing its centre selects "Crossing".
        const val PDF_CAPTION = "Washington Crossing the Delaware"
    }
}
