/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.test.uiautomator.UiScrollable
import androidx.test.uiautomator.UiSelector
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeShort
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.selectors.LauncherSelectors

/**
 * The device launcher (home screen), used to exercise Firefox's static app shortcuts.
 *
 * No navigation edge is registered and this page is intentionally NOT part of PageContext: the launcher is a separate
 * app whose home screen is reached by pressing the device Home button, not a screen the navigation graph can route to,
 * and registering it would pull the launcher's (inherently flaky) presence into the shared reachability suite. Tests
 * instantiate it locally and drive it via [pressDeviceHomeScreen].
 */
class LauncherPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) : BasePage(composeRule) {
    override val pageName = "LauncherPage"

    override val selectorCatalog = LauncherSelectors

    /** Drops to the device launcher (its home screen) by pressing the device Home button. */
    fun pressDeviceHomeScreen(): LauncherPage {
        mDevice.pressHome()
        mDevice.waitForIdle()
        return this
    }

    /**
     * Opens the app drawer and long-presses the Firefox icon until its static app-shortcut popup is up. The icon lives
     * in the drawer, not the home screen (the launcher doesn't reliably pin it to the workspace), and the long-press
     * can start a drag or the popup can vanish, so each attempt re-opens the drawer and retries up to
     * [MAX_SHORTCUT_MENU_ATTEMPTS] times before failing here.
     */
    fun openAppShortcutMenu(): LauncherPage {
        repeat(MAX_SHORTCUT_MENU_ATTEMPTS) {
            if (openDrawerAndScrollToFirefoxIcon()) {
                // mozLongClick (not UiObject.longClick) so the press honors the test harness' long-press timeout.
                mozLongClick(LauncherSelectors.FIREFOX_APP_ICON)
                val popupShown =
                    mDevice
                        .findObject(UiSelector().text(LauncherSelectors.NEW_TAB_SHORTCUT.value))
                        .waitForExists(waitingTimeShort)
                if (popupShown) {
                    return this
                }
            }
            mDevice.pressBack()
            mDevice.waitForIdle()
        }
        throw AssertionError("App-shortcut popup never appeared after $MAX_SHORTCUT_MENU_ATTEMPTS attempts")
    }

    /** Swipes up to open the app drawer and scrolls the Firefox icon into view; returns whether it is present. */
    private fun openDrawerAndScrollToFirefoxIcon(): Boolean {
        val startY = (mDevice.displayHeight * 0.8).toInt()
        val endY = (mDevice.displayHeight * 0.2).toInt()
        mDevice.swipe(mDevice.displayWidth / 2, startY, mDevice.displayWidth / 2, endY, SWIPE_STEPS)
        mDevice.waitForIdle()

        val appName = LauncherSelectors.FIREFOX_APP_ICON.value
        val drawer = UiScrollable(UiSelector().scrollable(true)).apply { setAsVerticalList() }
        runCatching { drawer.scrollTextIntoView(appName) }
        return mDevice.findObject(UiSelector().text(appName)).waitForExists(waitingTimeShort)
    }

    private companion object {
        const val MAX_SHORTCUT_MENU_ATTEMPTS = 3
        const val SWIPE_STEPS = 25
    }
}
