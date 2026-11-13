/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import org.junit.Ignore
import org.junit.Test
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.MockBrowserDataHelper.createHistoryItem
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.HistorySelectors
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SearchBarSelectors

class HistoryTest : BaseTest() {

    // TODO (I. RIOS 3/20/2026): add to BaseTest for State Machine
    @Ignore("Covered by verifyNavigationReachability[0: HistoryPage (TBD) — Navigation Reachability]")
    @Test
    fun verifyHistorySectionTest() {
        on.history.navigateToPage()
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903590
    @SmokeTest
    @Test
    fun noHistoryInPrivateBrowsingTest() {
        val website = mockWebServer.getGenericAsset(1)
        on.home.navigateToPage().mozClick(HomeSelectors.PRIVATE_BROWSING_BUTTON)
        on.browserPage.navigateToPage(website.url.toString())
        on.history.navigateToPage().mozVerifyElementsByGroup(HistorySelectors.Group.EMPTY_HISTORY_MENU_VIEW)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2302742
    @SmokeTest
    @Test
    fun verifyHistoryMenuWithHistoryItemsTest() {
        val website = mockWebServer.getGenericAsset(1)
        on.browserPage.navigateToPage(website.url.toString())
        on.history
            .navigateToPage()
            .mozVerifyElementsByGroup(HistorySelectors.Group.HISTORY_MENU_VIEW_WITH_HISTORY_ITEMS)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1848881
    @SmokeTest
    @Test
    fun deleteAllHistoryTest() {
        val website = mockWebServer.getGenericAsset(1)
        on.browserPage.navigateToPage(website.url.toString())
        on.history
            .navigateToPage()
            .mozVerify(HistorySelectors.HISTORY_LIST)
            .mozClick(HistorySelectors.DELETE_ALL_HISTORY_BUTTON)
            .mozVerifyElementsByGroup(HistorySelectors.Group.DELETE_CONFIRMATION)
            .mozClick(HistorySelectors.DELETE_EVERYTHING_OPTION_BUTTON)
            .mozClick(HistorySelectors.DELETE_CONFIRM_BUTTON)
            .mozVerifyElementsByGroup(HistorySelectors.Group.EMPTY_HISTORY_MENU_VIEW)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/243287
    @Critical
    @Test
    fun openHistoryItemTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)
        val secondWebPage = mockWebServer.getGenericAsset(2)

        createHistoryItem(firstWebPage.url.toString())
        createHistoryItem(secondWebPage.url.toString())

        on.history.navigateToPage()
        on.history.openHistoryItem(firstWebPage.url.toString())
        on.browserPage.verifyUrl(firstWebPage.url.toString())
        // Re-opening firstWebPage records a fresh visit, so it now sorts above secondWebPage in the
        // recency-ordered list.
        on.history
            .navigateToPage()
            .mozVerifyElementIsAbove(
                HistorySelectors.HISTORY_ITEM_WITH_TEXT(firstWebPage.url.toString()),
                HistorySelectors.HISTORY_ITEM_WITH_TEXT(secondWebPage.url.toString()),
            )
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3310499
    @Critical
    @Test
    fun deleteHistoryItemViaSearchXButtonTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)
        val secondWebPage = mockWebServer.getGenericAsset(2)

        createHistoryItem(firstWebPage.url.toString())
        createHistoryItem(secondWebPage.url.toString())

        // The History search bar runs in History-engine mode and can't load URLs, so it is driven inline rather than
        // through a navigation edge: an edge into the shared SearchBarComponent would let findPath route any
        // History->BrowserPage navigation through it and never load a page.
        on.history
            .navigateToPage()
            .mozClick(HistorySelectors.SEARCH_HISTORY_BUTTON)
            .mozEnterText("generic1", SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozVerifyAnyContainsText(SearchBarSelectors.SEARCH_SUGGESTION, firstWebPage.url.toString())
            .mozClick(SearchBarSelectors.AWESOMEBAR_SUGGESTION_REMOVE_BUTTON)
            .mozVerify(BrowserPageSelectors.SNACKBAR_ACTION_BUTTON)
            .mozVerifyNoneContainText(SearchBarSelectors.SEARCH_SUGGESTION, firstWebPage.url.toString())
            .mozClick(BrowserPageSelectors.SNACKBAR_ACTION_BUTTON)
            .mozVerifyAnyContainsText(SearchBarSelectors.SEARCH_SUGGESTION, firstWebPage.url.toString())
    }
}
