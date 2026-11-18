/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.test.uiautomator.UiSelector
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
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
     * Long-presses the Firefox icon until its static app-shortcut popup is up.
     *
     * The launcher's response to the first long-press is unreliable: on a fresh device it shows a one-time "App
     * suggestions added to empty space" dialog (pinning the icon instead of opening the popup), and other times the
     * popup opens but is gone again by the time the caller reaches it. So each long-press is checked for the popup
     * itself (the "New tab" row) rather than for the dialog: if the popup is up we return immediately so the caller can
     * click it before it times out; otherwise Back dismisses whatever opened (dialog or drag) and the long-press is
     * retried, up to [MAX_LONG_PRESS_ATTEMPTS] times. If every attempt is exhausted without the popup appearing, an
     * [AssertionError] is thrown so the failure points here rather than at the caller's later click on a missing row.
     */
    fun openAppShortcutMenu(): LauncherPage {
        // Wait for the icon so the long-press acts on ground truth rather than racing the launcher's layout.
        mozVerify(LauncherSelectors.FIREFOX_APP_ICON, timeout = waitingTime)
        repeat(MAX_LONG_PRESS_ATTEMPTS) {
            mozLongClick(LauncherSelectors.FIREFOX_APP_ICON)
            val popupShown =
                mDevice
                    .findObject(UiSelector().text(LauncherSelectors.NEW_TAB_SHORTCUT.value))
                    .waitForExists(waitingTimeShort)
            if (popupShown) {
                return this
            }
            mDevice.pressBack()
            mDevice.waitForIdle()
        }
        throw AssertionError("App-shortcut popup never appeared after $MAX_LONG_PRESS_ATTEMPTS long-press attempts")
    }

    private companion object {
        const val MAX_LONG_PRESS_ATTEMPTS = 3
    }
}
