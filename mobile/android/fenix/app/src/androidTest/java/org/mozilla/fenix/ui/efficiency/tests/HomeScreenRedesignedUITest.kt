/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import org.junit.Test
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.navigation.LaunchConfig
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsHomepageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.StoriesSelectors
import org.mozilla.fenix.ui.efficiency.selectors.ToolbarSelectors

/**
 * Verifies the homepage UI: the header (stories button, Firefox wordmark, private-mode toggle), the navigation bar
 * (search box, tab counter, main menu), the default shortcuts, and the blocked-trackers widget.
 *
 * The stories button requires Pocket (isPocketEnabled), so it is enabled here. This is why the test lives in its own
 * class rather than in HomeTest, which runs with Pocket disabled.
 */
class HomeScreenRedesignedUITest :
    BaseTest(
        LaunchConfig(
            isPocketEnabled = true,
            isRecentlyVisitedFeatureEnabled = false,
        )
    ) {

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3938228
    @SmokeTest
    @Test
    fun verifyHomeScreenUITest() {
        on.home.navigateToPage()

        // Header: stories (left), private-mode toggle (right), Firefox wordmark (below)
        on.home
            .mozVerify(HomeSelectors.PRIVATE_MODE_BUTTON)
            .mozVerify(HomeSelectors.HOME_WORDMARK_LOGO)
            .mozVerify(HomeSelectors.HOME_WORDMARK_TEXT)
            .mozVerify(HomeSelectors.STORIES_BUTTON)

        // Navigation bar: search box, tab counter showing 0, and the three-dot main menu
        on.home
            .mozVerify(ToolbarSelectors.TOOLBAR_URL_BOX)
            .mozVerify(HomeSelectors.TAB_COUNTER_ZERO)
            .mozVerify(HomeSelectors.MAIN_MENU_BUTTON)

        // Page content: default shortcuts and the blocked-trackers widget
        on.home.mozVerify(HomeSelectors.TOP_SITES_LIST_COMPOSE).mozVerify(HomeSelectors.TRACKERS_BLOCKED_CARD)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3938214
    @SmokeTest
    @Test
    fun verifyStoriesViewUITest() {
        on.home.navigateToPage().mozVerify(HomeSelectors.STORIES_BUTTON)

        // Tap the stories button to open the Stories view. Only the static chrome is asserted (back button
        // top-left, "News" header): the story items (thumbnail, title, source) are dynamic and network-backed
        // and expose no stable handles, so asserting them would be flaky.
        on.stories.navigateToPage().mozVerify(StoriesSelectors.NEWS_HEADER).mozVerify(StoriesSelectors.BACK_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3938215
    @Critical
    @Test
    fun verifyHomeButtonOnNewsPageTest() {
        on.home.navigateToPage().mozVerify(HomeSelectors.STORIES_BUTTON)
        on.stories.navigateToPage().mozVerify(StoriesSelectors.NEWS_HEADER)
        on.stories.mozClick(StoriesSelectors.BACK_BUTTON)
        on.home
            .mozVerify(HomeSelectors.HOMEPAGE_VIEW)
            .mozVerify(HomeSelectors.TOP_SITES_LIST_COMPOSE)
            .mozVerify(HomeSelectors.STORIES_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3939270
    @Critical
    @Test
    fun verifyStoriesSettingTogglesNewsButtonTest() {
        on.settingsHomepage
            .navigateToPage()
            .mozVerifyOptionSwitchIsChecked(SettingsHomepageSelectors.STORIES_SWITCH)
            .mozClick(SettingsHomepageSelectors.STORIES_SWITCH)
            .mozVerifyOptionSwitchIsNotChecked(SettingsHomepageSelectors.STORIES_SWITCH)

        on.home.navigateToPage().mozVerifyElementStaysAbsent(HomeSelectors.STORIES_BUTTON)

        on.settingsHomepage
            .navigateToPage()
            .mozClick(SettingsHomepageSelectors.STORIES_SWITCH)
            .mozVerifyOptionSwitchIsChecked(SettingsHomepageSelectors.STORIES_SWITCH)

        on.home.navigateToPage().mozVerify(HomeSelectors.STORIES_BUTTON)
    }
}
