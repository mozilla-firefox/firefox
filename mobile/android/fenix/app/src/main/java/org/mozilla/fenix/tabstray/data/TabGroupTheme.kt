/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.data

import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

/** Value class representing the possible themes for Tab Groups. */
enum class TabGroupTheme {
    Yellow {
        override val primary: Color
            @ReadOnlyComposable @Composable get() = FirefoxTheme.tabGroupColors.yellow.primary

        override val onPrimary: Color
            @ReadOnlyComposable @Composable get() = FirefoxTheme.tabGroupColors.yellow.onPrimary

        override val contentLabel: String
            @ReadOnlyComposable @Composable get() = stringResource(id = R.string.tab_group_color_yellow)
    },
    Orange {
        override val primary: Color
            @ReadOnlyComposable @Composable get() = FirefoxTheme.tabGroupColors.orange.primary

        override val onPrimary: Color
            @ReadOnlyComposable @Composable get() = FirefoxTheme.tabGroupColors.orange.onPrimary

        override val contentLabel: String
            @ReadOnlyComposable @Composable get() = stringResource(id = R.string.tab_group_color_orange)
    },
    Red {
        override val primary: Color
            @ReadOnlyComposable @Composable get() = FirefoxTheme.tabGroupColors.red.primary

        override val onPrimary: Color
            @ReadOnlyComposable @Composable get() = FirefoxTheme.tabGroupColors.red.onPrimary

        override val contentLabel: String
            @ReadOnlyComposable @Composable get() = stringResource(id = R.string.tab_group_color_red)
    },
    Pink {
        override val primary: Color
            @ReadOnlyComposable @Composable get() = FirefoxTheme.tabGroupColors.pink.primary

        override val onPrimary: Color
            @ReadOnlyComposable @Composable get() = FirefoxTheme.tabGroupColors.pink.onPrimary

        override val contentLabel: String
            @ReadOnlyComposable @Composable get() = stringResource(id = R.string.tab_group_color_pink)
    },
    Purple {
        override val primary: Color
            @Composable get() = FirefoxTheme.tabGroupColors.purple.primary

        override val onPrimary: Color
            @Composable get() = FirefoxTheme.tabGroupColors.purple.onPrimary

        override val contentLabel: String
            @Composable get() = stringResource(id = R.string.tab_group_color_purple)
    },
    Blue {
        override val primary: Color
            @ReadOnlyComposable @Composable get() = FirefoxTheme.tabGroupColors.blue.primary

        override val onPrimary: Color
            @ReadOnlyComposable @Composable get() = FirefoxTheme.tabGroupColors.blue.onPrimary

        override val contentLabel: String
            @ReadOnlyComposable @Composable get() = stringResource(id = R.string.tab_group_color_blue)
    },
    Cyan {
        override val primary: Color
            @ReadOnlyComposable @Composable get() = FirefoxTheme.tabGroupColors.cyan.primary

        override val onPrimary: Color
            @Composable get() = FirefoxTheme.tabGroupColors.cyan.onPrimary

        override val contentLabel: String
            @Composable get() = stringResource(id = R.string.tab_group_color_cyan)
    },
    Green {
        override val primary: Color
            @ReadOnlyComposable @Composable get() = FirefoxTheme.tabGroupColors.green.primary

        override val onPrimary: Color
            @ReadOnlyComposable @Composable get() = FirefoxTheme.tabGroupColors.green.onPrimary

        override val contentLabel: String
            @ReadOnlyComposable @Composable get() = stringResource(id = R.string.tab_group_color_green)
    },
    Grey {
        override val primary: Color
            @ReadOnlyComposable @Composable get() = FirefoxTheme.tabGroupColors.grey.primary

        override val onPrimary: Color
            @ReadOnlyComposable @Composable get() = FirefoxTheme.tabGroupColors.grey.onPrimary

        override val contentLabel: String
            @ReadOnlyComposable @Composable get() = stringResource(id = R.string.tab_group_color_grey)
    };

    /** The primary color of the tab group. */
    @get:Composable @get:ReadOnlyComposable abstract val primary: Color

    /** The color of content displayed on top of [primary]. */
    @get:Composable @get:ReadOnlyComposable abstract val onPrimary: Color

    /** The accessibility label for this theme. //todo replace with localized text */
    @get:Composable @get:ReadOnlyComposable abstract val contentLabel: String

    /** Returns the next [TabGroupTheme] in the list, cycling back to the start if the current theme is the last one. */
    fun next(): TabGroupTheme {
        val themes = TabGroupTheme.entries
        return themes[(ordinal + 1) % themes.size]
    }

    companion object {
        /** The color of content displayed on top of [primary]. */
        val default: TabGroupTheme = Yellow
    }
}

/** Returns the next [TabGroupTheme] that follows the last used theme or [TabGroupTheme.default] if there is none. */
fun TabGroupTheme?.nextOrDefault(): TabGroupTheme = this?.next() ?: TabGroupTheme.default
