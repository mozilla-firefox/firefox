/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import org.junit.Test
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.TabDrawerSelectors

class PrivateBrowsingTest : BaseTest() {

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3938236
    @Critical
    @Test
    fun verifyPrivateBrowsingHomeScreenItemsAccessedFromTabsTrayTest() {
        // Given: the app is on the home screen in normal browsing mode

        // When: we open the tabs tray and switch to the private browsing tabs tray
        on.tabDrawer.navigateToPage()
        on.tabDrawer
            .mozClick(TabDrawerSelectors.PRIVATE_TABS_PAGE_BUTTON)
            // Then: the (empty) private browsing tabs tray is displayed
            .mozVerify(TabDrawerSelectors.EMPTY_PRIVATE_TABS_LIST)

        // When: we tap the "+" button to open a new private tab
        on.tabDrawer.mozClick(TabDrawerSelectors.FAB)

        // Then: the private browsing home screen shows the private toggle and info card (title + description + link)
        on.home
            .mozVerify(HomeSelectors.HOMEPAGE_VIEW)
            .mozVerify(HomeSelectors.PRIVATE_MODE_BUTTON)
            .mozVerifyElementsByGroup(HomeSelectors.Group.PRIVATE_BROWSING_HOME_SCREEN)
            .mozVerify(HomeSelectors.PRIVATE_BROWSING_LEARN_MORE_LINK)

        // And: tapping the learn more link opens the private browsing myths support article
        on.home.mozClick(HomeSelectors.PRIVATE_BROWSING_LEARN_MORE_LINK)
        on.browserPage.verifyUrl("common-myths-about-private-browsing")
    }
}
