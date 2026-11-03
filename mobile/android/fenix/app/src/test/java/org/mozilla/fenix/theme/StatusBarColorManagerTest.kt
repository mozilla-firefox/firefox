/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.theme

import android.app.Activity
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentManager
import androidx.navigation.fragment.NavHostFragment
import io.mockk.mockk
import io.mockk.verify
import org.junit.Test
import org.mozilla.fenix.browser.BrowserFragment
import org.mozilla.fenix.customtabs.ExternalAppBrowserFragment
import org.mozilla.fenix.home.HomeFragment
import org.mozilla.fenix.tabstray.ui.TabManagementFragment

class StatusBarColorManagerTest {

    private val themeManager: ThemeManager = mockk(relaxed = true)
    private val activity: Activity = mockk(relaxed = true)
    private val fragmentManager: FragmentManager = mockk(relaxed = true)
    private val manager = StatusBarColorManager(themeManager = themeManager, activity = activity)

    @Test
    fun `WHEN the home fragment resumes THEN the status bar theme is applied`() {
        manager.onFragmentResumed(fragmentManager, mockk<HomeFragment>())

        verify { themeManager.applyStatusBarTheme(activity) }
    }

    @Test
    fun `WHEN the browser fragment resumes THEN the status bar theme is applied`() {
        manager.onFragmentResumed(fragmentManager, mockk<BrowserFragment>())

        verify { themeManager.applyStatusBarTheme(activity) }
    }

    @Test
    fun `WHEN any other fragment resumes THEN the status bar theme is applied`() {
        manager.onFragmentResumed(fragmentManager, mockk<Fragment>())

        verify { themeManager.applyStatusBarTheme(activity) }
    }

    @Test
    fun `GIVEN NavHostFragment, DialogFragment, ExternalAppBrowserFragment and TabManagementFragment as ignored fragments WHEN an ignored fragment resumes THEN do nothing`() {
        manager.onFragmentResumed(fragmentManager, mockk<NavHostFragment>())
        manager.onFragmentResumed(fragmentManager, mockk<DialogFragment>())
        manager.onFragmentResumed(fragmentManager, mockk<ExternalAppBrowserFragment>())
        manager.onFragmentResumed(fragmentManager, mockk<TabManagementFragment>())

        verify(exactly = 0) { themeManager.applyStatusBarTheme(any()) }
    }
}
