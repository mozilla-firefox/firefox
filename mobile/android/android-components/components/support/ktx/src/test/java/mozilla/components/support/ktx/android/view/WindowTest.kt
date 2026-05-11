/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.ktx.android.view

import android.graphics.Color
import android.view.View
import android.view.Window
import android.view.WindowInsetsController
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.support.test.robolectric.testContext
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mock
import org.mockito.Mockito.inOrder
import org.mockito.Mockito.never
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.Mockito.verifyNoMoreInteractions
import org.mockito.Mockito.`when`
import org.mockito.MockitoAnnotations.openMocks
import org.robolectric.annotation.Config

/**
 * **Note** Tests for isAppearanceLightStatusBars are in WindowKtTest.
 */
@RunWith(AndroidJUnit4::class)
class WindowTest {

    @Mock private lateinit var window: Window

    @Mock private lateinit var decorView: View

    @Mock
    lateinit var windowInsetsController: WindowInsetsController

    @Before
    fun setup() {
        openMocks(this)

        `when`(window.decorView).thenAnswer { decorView }
        `when`(window.context).thenAnswer { testContext }
    }

    @Suppress("DEPRECATION")
    @Test
    @Config(sdk = [29])
    fun `GIVEN a color WHEN setStatusBarTheme THEN sets the status bar color`() {
        `when`(window.insetsController).thenReturn(windowInsetsController)
        window.setStatusBarTheme(Color.BLUE)
        verify(window).statusBarColor = Color.BLUE

        window.setStatusBarTheme(Color.RED)
        verify(window).statusBarColor = Color.RED
    }

    @Test
    @Config(sdk = [27])
    fun `GIVEN Android 8 & no args WHEN setNavigationBarTheme THEN no colors are set`() {
        window.setNavigationBarTheme()

        verifyNoMoreInteractions(window)
    }

    @Test
    @Suppress("DEPRECATION")
    @Config(sdk = [27])
    fun `GIVEN Android 8 & has nav bar color WHEN setNavigationBarTheme THEN only the nav bar color is set`() {
        window.setNavigationBarTheme(navBarColor = Color.MAGENTA)

        // We can't verify against the navigationBarDividerColor directly due to using SDK O_MR1 so we'll verify using ordering.
        val inOrder = inOrder(window)
        inOrder.verify(window).navigationBarColor = Color.MAGENTA
        // Called for createWindowInsetsController()
        inOrder.verify(window, times(2)).decorView
        inOrder.verifyNoMoreInteractions()
    }

    @Test
    @Config(sdk = [27])
    fun `GIVEN Android 8 & has nav bar divider color WHEN setNavigationBarTheme THEN no colors are set`() {
        window.setNavigationBarTheme(navBarDividerColor = Color.DKGRAY)

        verifyNoMoreInteractions(window)
    }

    @Test
    @Suppress("DEPRECATION")
    @Config(sdk = [27])
    fun `GIVEN Android 8 & all args WHEN setNavigationBarTheme THEN only the nav bar color is set`() {
        window.setNavigationBarTheme(navBarColor = Color.MAGENTA, navBarDividerColor = Color.DKGRAY)

        // We can't verify against the navigationBarDividerColor directly due to using SDK O_MR1 so we'll verify using ordering.
        val inOrder = inOrder(window)
        inOrder.verify(window).navigationBarColor = Color.MAGENTA
        // Called for createWindowInsetsController()
        inOrder.verify(window, times(2)).decorView
        inOrder.verifyNoMoreInteractions()
    }

    @Test
    @Suppress("DEPRECATION")
    @Config(sdk = [28])
    fun `GIVEN Android 9 & no args WHEN setNavigationBarTheme THEN the nav bar divider color is set to default`() {
        window.setNavigationBarTheme()

        verify(window, never()).navigationBarColor
        verify(window).navigationBarDividerColor = 0
    }

    @Test
    @Suppress("DEPRECATION")
    @Config(sdk = [28])
    fun `GIVEN Android 9 has nav bar color WHEN setNavigationBarTheme THEN the nav bar color is set & nav bar divider color set to default`() {
        window.setNavigationBarTheme(navBarColor = Color.BLUE)

        verify(window).navigationBarColor = Color.BLUE
        verify(window).navigationBarDividerColor = 0
    }

    @Test
    @Suppress("DEPRECATION")
    @Config(sdk = [28])
    fun `GIVEN Android 9 has nav bar divider color WHEN setNavigationBarTheme THEN only the nav bar divider color is set`() {
        window.setNavigationBarTheme(navBarDividerColor = Color.GREEN)

        verify(window, never()).navigationBarColor
        verify(window).navigationBarDividerColor = Color.GREEN
    }

    @Test
    @Suppress("DEPRECATION")
    @Config(sdk = [28])
    fun `GIVEN Android 9 & all args WHEN setNavigationBarTheme THEN the nav bar & nav bar divider colors are set`() {
        window.setNavigationBarTheme(navBarColor = Color.YELLOW, navBarDividerColor = Color.CYAN)

        verify(window).navigationBarColor = Color.YELLOW
        verify(window).navigationBarDividerColor = Color.CYAN
    }
}
