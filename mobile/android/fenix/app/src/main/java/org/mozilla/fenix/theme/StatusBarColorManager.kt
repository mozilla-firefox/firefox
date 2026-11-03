/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.theme

import android.app.Activity
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentManager
import androidx.fragment.app.FragmentManager.FragmentLifecycleCallbacks
import androidx.navigation.fragment.NavHostFragment
import org.mozilla.fenix.customtabs.ExternalAppBrowserFragment
import org.mozilla.fenix.tabstray.ui.TabManagementFragment

/**
 * Uses the [ThemeManager] to set the status bar color based on the current fragment.
 *
 * @param themeManager The [ThemeManager] to use for setting the status bar color.
 * @param activity The [Activity] to set the status bar color on.
 */
class StatusBarColorManager(
    private val themeManager: ThemeManager,
    private val activity: Activity,
) : FragmentLifecycleCallbacks() {

    override fun onFragmentResumed(fragmentManager: FragmentManager, fragment: Fragment) {
        if (fragment is NavHostFragment || fragment is DialogFragment) {
            return
        }

        // These fragments theme their own system bars independently of the browsing mode,
        // so the browsing-mode status bar theming must not run for them.
        if (fragment is ExternalAppBrowserFragment || fragment is TabManagementFragment) {
            return
        }

        themeManager.applyStatusBarTheme(activity)
    }
}
