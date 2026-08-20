/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import android.content.pm.PackageManager
import android.os.SystemClock
import android.util.Log
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.test.uiautomator.By
import org.junit.Assert.assertTrue
import org.mozilla.fenix.helpers.AppAndSystemHelper
import org.mozilla.fenix.helpers.Constants
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.SystemPickerCapabilities
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.navigation.NavigationRegistry
import org.mozilla.fenix.ui.efficiency.navigation.NavigationStep
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SystemSettingsSelectors

class SystemSettingsPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) : BasePage(composeRule) {
    override val pageName = "SystemSettingsPage"

    init {
        NavigationRegistry.register(
            from = "HomePage",
            to = pageName,
            steps =
                listOf(
                    NavigationStep.Click(HomeSelectors.MAIN_MENU_BUTTON),
                    NavigationStep.Click(MainMenuSelectors.SETTINGS_BUTTON),
                    NavigationStep.Swipe(SettingsSelectors.NOTIFICATIONS_BUTTON),
                    NavigationStep.Click(SettingsSelectors.NOTIFICATIONS_BUTTON),
                ),
        )
    }

    /** Open the Permissions list from the Android App info screen. */
    fun openAppPermissions(): SystemSettingsPage {
        // Waits first: the caller has just left Fenix via an intent, and the Settings activity may not have
        // drawn yet. The legacy helper clicks immediately and races that launch.
        mozVerify(SystemSettingsSelectors.APP_INFO_PERMISSIONS_ROW, timeout = waitingTime)
        mozClick(SystemSettingsSelectors.APP_INFO_PERMISSIONS_ROW)
        return this
    }

    /**
     * Grant every runtime permission dialog currently queued, and return how many were granted.
     *
     * AppAndSystemHelper.grantSystemPermission handles exactly one dialog, which is not enough when a single app action
     * fans out into a chain of requests -- tapping a bare <input type="file"> asks for audio and then for
     * music-and-audio, so granting once leaves the flow sitting on the second dialog with nothing to indicate anything
     * is wrong. Returns the count so a caller can assert the chain actually appeared rather than silently accepting
     * zero.
     */
    fun grantAllPendingSystemPermissions(maxDialogs: Int = 5): Int {
        var granted = 0
        repeat(maxDialogs) {
            if (!mozIsElementPresent(SystemSettingsSelectors.PERMISSION_GRANT_DIALOG)) return granted
            AppAndSystemHelper.grantSystemPermission()
            granted++
        }
        return granted
    }

    /** Grant [permissionName] from the app-permissions list. */
    fun allowAppPermission(permissionName: String): SystemSettingsPage {
        mozVerify(SystemSettingsSelectors.APP_PERMISSION_ROW(permissionName), timeout = waitingTime)
        mozClick(SystemSettingsSelectors.APP_PERMISSION_ROW(permissionName))
        mozVerify(SystemSettingsSelectors.APP_PERMISSION_ALLOW_OPTION, timeout = waitingTime)
        mozClick(SystemSettingsSelectors.APP_PERMISSION_ALLOW_OPTION)
        return this
    }

    /**
     * Click the content-picker row of Gecko's file chooser and return the package that actually came up.
     *
     * Both halves are resolved from the device rather than hard-coded, because both vary by OS release:
     * * the row LABEL -- API 37 shows "Files", API 34 shows "Media" and has no "Files" row at all;
     * * the package that then OPENS -- clicking "Media" on API 34 lands in documentsui, because the photo picker
     *   forwards a `*&#47;*` GET_CONTENT on rather than handling it, so the row's own package never appears.
     *
     * The window is therefore accepted against the whole set of apps that can service the intent, which still cannot
     * pass vacuously: Fenix, the chooser itself and the launcher are all outside that set, so a chooser that was never
     * dismissed or a picker that never opened fails here, naming what was in front instead.
     */
    fun openResolvedContentPicker(): String {
        val candidates = SystemPickerCapabilities.contentPickerCandidates()
        val rowDeadline = System.currentTimeMillis() + waitingTime
        var visible = emptyList<String>()
        while (System.currentTimeMillis() < rowDeadline) {
            visible =
                mDevice
                    .findObjects(By.clazz("android.widget.TextView"))
                    .mapNotNull { it.text }
                    .filter { it.isNotBlank() }
            if (candidates.any { it.label in visible }) break
            SystemClock.sleep(POLL_INTERVAL_MS)
        }
        val row = SystemPickerCapabilities.contentPickerRow(visible, candidates)
        Log.i(Constants.TAG, "openResolvedContentPicker: rows=$visible -> tapping '${row.label}'")
        mozClick(SystemSettingsSelectors.FILE_CHOOSER_OPTION(row.label))

        val acceptable = candidates.map { it.packageName }.toSet()
        val appDeadline = System.currentTimeMillis() + waitingTime
        var current = ""
        while (System.currentTimeMillis() < appDeadline) {
            current = mDevice.currentPackageName ?: ""
            if (current in acceptable) {
                Log.i(Constants.TAG, "openResolvedContentPicker: '${row.label}' opened $current")
                return current
            }
            SystemClock.sleep(POLL_INTERVAL_MS)
        }
        throw AssertionError(
            "Tapped the '${row.label}' chooser row but no content picker came to the foreground.\n" +
                "  foreground now : $current\n" +
                "  acceptable     : $acceptable"
        )
    }

    override fun mozGetSelectorsByGroup(group: String): List<Selector> {
        return SystemSettingsSelectors.all.filter { it.groups.contains(group) }
    }

    /**
     * Assert [permissionName] is granted, both in the settings UI and according to the OS.
     *
     * Legacy branched on Build.VERSION here: up to API 30 it asserted the row title AND its "Only while app is in use"
     * summary, and above that only the title -- which is present whether the permission is allowed or denied, so on a
     * modern release that assertion could not fail. The row check is kept for parity, and the OS's own state is what
     * decides: it needs no version branch and cannot pass for a denied permission.
     */
    fun verifySystemPermissionGranted(permissionName: String, androidPermission: String): SystemSettingsPage {
        mozVerify(SystemSettingsSelectors.APP_PERMISSION_ROW(permissionName), timeout = waitingTime)
        assertTrue(
            "$permissionName still reads as denied from the OS (checked $androidPermission)",
            appContext.checkSelfPermission(androidPermission) == PackageManager.PERMISSION_GRANTED,
        )
        return this
    }

    private companion object {
        const val POLL_INTERVAL_MS = 250L
    }
}
