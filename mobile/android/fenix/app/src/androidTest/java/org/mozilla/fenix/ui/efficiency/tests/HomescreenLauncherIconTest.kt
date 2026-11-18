/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import org.junit.Test
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.pageObjects.LauncherPage
import org.mozilla.fenix.ui.efficiency.selectors.LauncherSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SearchBarSelectors

class HomescreenLauncherIconTest : BaseTest() {

    // LauncherPage is not part of PageContext (the device launcher is a separate app the navigation
    // graph cannot route to), so instantiate it locally. The test drops to the launcher explicitly
    // via launcher.pressDeviceHomeScreen() below.
    private val launcher
        get() = LauncherPage(composeRule)

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2793514
    @Critical
    @Test
    fun verifyTheNewTabAppShortcutTest() {
        on.home.navigateToPage()
        launcher.pressDeviceHomeScreen().openAppShortcutMenu().mozClick(LauncherSelectors.NEW_TAB_SHORTCUT)
        on.searchBar
            .mozVerify(SearchBarSelectors.SEARCH_ENGINE_SELECTOR)
            .mozVerify(SearchBarSelectors.SEARCH_BAR_PLACEHOLDER)
            .mozVerify(SearchBarSelectors.VOICE_SEARCH_BUTTON)
            .mozVerify(SearchBarSelectors.GOOGLE_LENS_BUTTON)
            .mozVerifyKeyboardVisible()
    }
}
