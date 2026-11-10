/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.theme

import mozilla.components.compose.base.theme.Theme
import org.junit.Assert.assertEquals
import org.junit.Test
import org.mozilla.fenix.browser.browsingmode.BrowsingMode

class ThemeManagerTest {

    @Test
    fun `GIVEN private browsing mode WHEN the device is not in dark mode THEN the private theme is returned`() {
        assertEquals(
            Theme.Private,
            getTheme(BrowsingMode.Private, isSystemInDarkTheme = false, shouldUseOledTheme = false),
        )
    }

    @Test
    fun `GIVEN private browsing mode WHEN the device is in dark mode THEN the private theme is returned`() {
        assertEquals(
            Theme.Private,
            getTheme(BrowsingMode.Private, isSystemInDarkTheme = true, shouldUseOledTheme = false),
        )
    }

    @Test
    fun `GIVEN private browsing mode WHEN the OLED theme is selected THEN the private theme is returned`() {
        assertEquals(
            Theme.Private,
            getTheme(BrowsingMode.Private, isSystemInDarkTheme = true, shouldUseOledTheme = true),
        )
    }

    @Test
    fun `GIVEN the OLED theme is not selected WHEN the device is not in dark mode THEN the light theme is returned`() {
        assertEquals(
            Theme.Light,
            getTheme(BrowsingMode.Normal, isSystemInDarkTheme = false, shouldUseOledTheme = false),
        )
    }

    @Test
    fun `GIVEN the OLED theme is selected WHEN the device is not in dark mode THEN the light theme is returned`() {
        assertEquals(
            Theme.Light,
            getTheme(BrowsingMode.Normal, isSystemInDarkTheme = false, shouldUseOledTheme = true),
        )
    }

    @Test
    fun `GIVEN the OLED theme is selected WHEN the device is in dark mode THEN the OLED theme is returned`() {
        assertEquals(
            Theme.Oled,
            getTheme(BrowsingMode.Normal, isSystemInDarkTheme = true, shouldUseOledTheme = true),
        )
    }

    @Test
    fun `GIVEN the OLED theme is not selected WHEN the device is in dark mode THEN the dark theme is returned`() {
        assertEquals(
            Theme.Dark,
            getTheme(BrowsingMode.Normal, isSystemInDarkTheme = true, shouldUseOledTheme = false),
        )
    }
}
