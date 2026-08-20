/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import android.content.Intent
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.core.net.toUri
import androidx.test.platform.app.InstrumentationRegistry
import org.mozilla.fenix.IntentReceiverActivity
import org.mozilla.fenix.helpers.DataGenerationHelper.createCustomTabIntent
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.helpers.PageStateTracker
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.navigation.NavigationRegistry
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.CustomTabsSelectors

class CustomTabsPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) : BasePage(composeRule) {
    override val pageName = "CustomTabsPage"

    // Custom tabs are LAUNCH-reached: they run in their own activity (CustomTabActivity), started by firing
    // a custom-tabs intent at IntentReceiverActivity — not by navigating a click-path from HomePage. So there
    // is no INBOUND NavigationRegistry edge; use launchCustomTab() as the entry point (it sets the page state).

    init {
        // Outbound 0-step edge for the "Open in Firefox" flow: the menu click that converts the custom tab
        // into a normal tab is performed by the test, and this edge only carries navigateToPage() past its
        // single-pass mozIsOnPageNow() check into the polling mozWaitForPageToLoad(). That poll is what
        // absorbs the ~1s CustomTabActivity -> HomeActivity transition, during which the engineView anchor
        // isn't in the tree yet — without the edge, a slow transition makes path-finding fail with
        // "No navigation path found to 'BrowserPage'" (intermittent). Mirrors MainMenuPage -> BrowserPage.
        NavigationRegistry.register(
            from = pageName,
            to = "BrowserPage",
            steps = listOf(),
        )
    }

    fun launchCustomTab(url: String, customMenuItemLabel: String = ""): CustomTabsPage {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val intent =
            createCustomTabIntent(url, customMenuItemLabel).apply {
                setClass(context, IntentReceiverActivity::class.java)
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
        context.startActivity(intent)
        mozVerify(CustomTabsSelectors.MAIN_MENU_BUTTON) // wait for the custom-tab toolbar to settle
        PageStateTracker.currentPageName = pageName
        return this
    }

    fun openMainMenu(): CustomTabsPage {
        mozClick(CustomTabsSelectors.MAIN_MENU_BUTTON)
        return this
    }

    /**
     * Leave the custom tab the way an external app would: an ACTION_VIEW intent scoped to Firefox, which opens the URL
     * in the full browser. Distinct from the custom tab's own "Open in Firefox" menu item (MENU_OPEN_IN_APP), which
     * hands over the existing tab — a test that means to exercise the external entry point cannot substitute one for
     * the other.
     *
     * Not a NavigationRegistry edge for the same reason launchCustomTab is not: it is an intent rather than a
     * click-path. Sets the page state to BrowserPage, since that is where it lands; the caller should then assert on
     * `on.browserPage` rather than continuing to chain off the custom tab.
     */
    fun openUrlFromExternalLink(url: String) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val intent =
            Intent(Intent.ACTION_VIEW, url.toUri()).apply {
                `package` = context.packageName
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
        context.startActivity(intent)
        PageStateTracker.currentPageName = "BrowserPage"
    }

    // Web content is rendered by GeckoView and matched device-wide (UIAutomator text), so the BrowserPage
    // page-content locator works inside a custom tab too. Wait for the content to render before tapping:
    // mozClick resolves once with no polling, and GeckoView's accessibility node for freshly-loaded text
    // can lag the toolbar settling, so an immediate tap intermittently misses it (element not found).
    fun clickWebContent(text: String): CustomTabsPage {
        mozVerify(BrowserPageSelectors.PAGE_CONTENT(text), timeout = waitingTime)
        mozClick(BrowserPageSelectors.PAGE_CONTENT(text))
        return this
    }

    fun verifyWebContent(text: String): CustomTabsPage {
        mozVerify(BrowserPageSelectors.PAGE_CONTENT(text))
        return this
    }

    override fun mozGetSelectorsByGroup(group: String): List<Selector> {
        return CustomTabsSelectors.all.filter { it.groups.contains(group) }
    }
}
