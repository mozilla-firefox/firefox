/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.preference.R as preferenceR
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.ViewMatchers.Visibility
import androidx.test.espresso.matcher.ViewMatchers.hasDescendant
import androidx.test.espresso.matcher.ViewMatchers.withChild
import androidx.test.espresso.matcher.ViewMatchers.withClassName
import androidx.test.espresso.matcher.ViewMatchers.withContentDescription
import androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withResourceName
import androidx.test.espresso.matcher.ViewMatchers.withText
import org.hamcrest.CoreMatchers.allOf
import org.hamcrest.CoreMatchers.containsString
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.assertIsChecked
import org.mozilla.fenix.helpers.atPosition
import org.mozilla.fenix.helpers.click
import org.mozilla.fenix.helpers.isChecked

/** Implementation of Robot Pattern for the settings Delete Browsing Data On Quit sub menu. */
class SettingsSubMenuDeleteBrowsingDataOnQuitRobot {

    fun verifyNavigationToolBarHeader() {
        Log.i(
            TAG,
            "verifyNavigationToolBarHeader: Trying to verify that the \"Delete browsing data on quit\" toolbar title is visible",
        )
        onView(
                allOf(
                    withId(R.id.navigationToolbar),
                    withChild(withText(R.string.preferences_delete_browsing_data_on_quit)),
                )
            )
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(
            TAG,
            "verifyNavigationToolBarHeader: Verified that the \"Delete browsing data on quit\" toolbar title is visible",
        )
    }

    fun verifyDeleteBrowsingOnQuitEnabled(enabled: Boolean) {
        Log.i(
            TAG,
            "verifyDeleteBrowsingOnQuitEnabled: Trying to verify that the  \"Delete browsing data on quit\" toggle is checked: $enabled",
        )
        deleteBrowsingOnQuitButton().assertIsChecked(enabled)
        Log.i(
            TAG,
            "verifyDeleteBrowsingOnQuitEnabled: Verified that the  \"Delete browsing data on quit\" toggle is checked: $enabled",
        )
    }

    fun verifyDeleteBrowsingOnQuitButtonSummary() {
        Log.i(
            TAG,
            "verifyDeleteBrowsingOnQuitButtonSummary: Trying to verify that the \"Delete browsing data on quit\" option summary is visible",
        )
        onView(withText(R.string.preference_summary_delete_browsing_data_on_quit_2))
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(
            TAG,
            "verifyDeleteBrowsingOnQuitButtonSummary: Verified that the \"Delete browsing data on quit\" option summary is visible",
        )
    }

    fun clickDeleteBrowsingOnQuitButtonSwitch() {
        Log.i(TAG, "clickDeleteBrowsingOnQuitButtonSwitch: Trying to click the \"Delete browsing data on quit\" toggle")
        onView(withResourceName("switchWidget")).click()
        Log.i(TAG, "clickDeleteBrowsingOnQuitButtonSwitch: Clicked the \"Delete browsing data on quit\" toggle")
    }

    fun verifyAllTheCheckBoxesText() {
        Log.i(TAG, "verifyAllTheCheckBoxesText: Trying to verify that the \"Open tabs\" option is visible")
        openTabsCheckbox().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyAllTheCheckBoxesText: Verified that the \"Open tabs\" option is visible")
        Log.i(TAG, "verifyAllTheCheckBoxesText: Trying to verify that the \"Browsing history\" option is visible")
        browsingHistoryCheckbox().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyAllTheCheckBoxesText: Verified that the \"Browsing history\" option is visible")
        Log.i(TAG, "verifyAllTheCheckBoxesText: Trying to verify that the \"Cookies and site data\" option is visible")
        cookiesAndSiteDataCheckbox().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyAllTheCheckBoxesText: Verified that the \"Cookies and site data\" option is visible")
        Log.i(
            TAG,
            "verifyAllTheCheckBoxesText: Trying to verify that the \"Cookies and site data\" option summary is visible",
        )
        onView(withText(R.string.preferences_delete_browsing_data_cookies_subtitle))
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyAllTheCheckBoxesText: Verified that the \"Cookies and site data\" option summary is visible")
        Log.i(
            TAG,
            "verifyAllTheCheckBoxesText: Trying to verify that the \"Cached images and files\" option is visible",
        )
        cachedFilesCheckbox().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyAllTheCheckBoxesText: Verified that the \"Cached images and files\" option is visible")
        Log.i(
            TAG,
            "verifyAllTheCheckBoxesText: Trying to verify that the \"Cached images and files\" option summary is visible",
        )
        onView(withText(R.string.preferences_delete_browsing_data_cached_files_subtitle))
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(
            TAG,
            "verifyAllTheCheckBoxesText: Verified that the \"Cached images and files\" option summary is visible",
        )
        Log.i(TAG, "verifyAllTheCheckBoxesText: Trying to verify that the \"Site permissions\" option is visible")
        sitePermissionsCheckbox().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyAllTheCheckBoxesText: Verified that the \"Site permissions\" option is visible")
    }

    fun verifyAllTheCheckBoxesChecked(checked: Boolean) {
        for (index in 2..7) {
            Log.i(
                TAG,
                "verifyAllTheCheckBoxesChecked: Trying to verify that the the check box at position ${index - 1} is checked: $checked",
            )
            onView(withId(preferenceR.id.recycler_view))
                .check(
                    matches(
                        atPosition(
                            index,
                            hasDescendant(
                                allOf(
                                    withResourceName(containsString("checkbox")),
                                    isChecked(checked),
                                )
                            ),
                        )
                    )
                )
            Log.i(
                TAG,
                "verifyAllTheCheckBoxesChecked: Verified that the the check box at position ${index - 1} is checked: $checked",
            )
        }
    }

    class Transition {
        fun goBack(interact: SettingsRobot.() -> Unit): SettingsRobot.Transition {
            Log.i(TAG, "goBack: Trying to click navigate up toolbar button")
            goBackButton().click()
            Log.i(TAG, "goBack: Clicked navigate up toolbar button")

            SettingsRobot().interact()
            return SettingsRobot.Transition()
        }
    }
}

private fun goBackButton() = onView(withContentDescription("Navigate up"))

private fun deleteBrowsingOnQuitButton() =
    onView(withClassName(containsString("com.google.android.material.materialswitch.MaterialSwitch")))

private fun openTabsCheckbox() = onView(withText(R.string.preferences_delete_browsing_data_tabs_title_2))

private fun browsingHistoryCheckbox() =
    onView(withText(R.string.preferences_delete_browsing_data_browsing_history_title))

private fun cookiesAndSiteDataCheckbox() =
    onView(withText(R.string.preferences_delete_browsing_data_cookies_and_site_data))

private fun cachedFilesCheckbox() = onView(withText(R.string.preferences_delete_browsing_data_cached_files))

private fun sitePermissionsCheckbox() = onView(withText(R.string.preferences_delete_browsing_data_site_permissions))
