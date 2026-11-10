/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalContext
import mozilla.components.compose.base.theme.Theme
import mozilla.components.compose.base.theme.Theme.Dark
import mozilla.components.compose.base.theme.Theme.Light
import mozilla.components.compose.base.utils.inComposePreview

/** Abstraction for providing the current [Theme] that is to be displayed. */
interface ThemeProvider {
    /** Returns the current [Theme] that is to be displayed. */
    @Composable fun provideTheme(): Theme

    /** Returns the [Theme] that is to be displayed ignoring private theme. */
    @Composable fun provideNonPrivateTheme(): Theme
}

/** The default [ThemeProvider]. Used when [Theme.Private] is not needed or when in a Compose Preview. */
object DefaultThemeProvider : ThemeProvider {
    @Composable
    override fun provideTheme() =
        if (isSystemInDarkTheme()) {
            Dark
        } else {
            Light
        }

    @Composable override fun provideNonPrivateTheme() = provideTheme()
}

/** Gets the [ThemeProvider] for the current context. */
@Composable
fun getThemeProvider(): ThemeProvider {
    return if (inComposePreview) {
        DefaultThemeProvider
    } else {
        LocalContext.current.applicationContext as ThemeProvider
    }
}
