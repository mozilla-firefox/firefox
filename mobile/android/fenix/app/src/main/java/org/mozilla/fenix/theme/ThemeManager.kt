/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.theme

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.util.TypedValue
import android.view.Window
import androidx.annotation.AnyRes
import androidx.annotation.StyleRes
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.colorResource
import com.google.android.material.R as materialR
import mozilla.components.compose.base.theme.Theme
import mozilla.components.support.ktx.android.content.getColorFromAttr
import mozilla.components.support.ktx.android.content.getStatusBarColor
import mozilla.components.support.ktx.android.view.createWindowInsetsController
import mozilla.components.support.ktx.android.view.setNavigationBarColorCompat
import mozilla.components.support.ktx.android.view.setStatusBarColorCompat
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.customtabs.ExternalAppBrowserActivity

abstract class ThemeManager {

    abstract var currentTheme: Theme

    /** Whether the [currentTheme] is [Theme.Private]. */
    val isPrivate: Boolean
        get() = currentTheme == Theme.Private

    /** Returns the style resource corresponding to the [currentTheme]. */
    @get:StyleRes
    val currentThemeResource
        get() =
            when (currentTheme) {
                Theme.Private -> R.style.PrivateTheme
                Theme.Oled -> R.style.OledTheme
                Theme.Light,
                Theme.Dark -> R.style.NormalTheme
            }

    /**
     * Handles status bar theme change since the window does not dynamically recreate
     *
     * @param activity The activity to apply the status bar theme to.
     */
    fun applyStatusBarTheme(activity: Activity) = applyStatusBarTheme(activity.window, activity)

    private fun applyStatusBarTheme(window: Window, context: Context) {
        when (currentTheme) {
            Theme.Light -> {
                updateLightSystemBars(window, context)
            }
            Theme.Dark,
            Theme.Oled,
            Theme.Private -> {
                clearLightSystemBars(window)
                setStatusBarColor(window, context)
                updateNavigationBar(window, context)
            }
        }
    }

    fun setActivityTheme(activity: Activity) {
        activity.setTheme(currentThemeResource)
    }

    companion object {
        /**
         * Resolves the attribute to a resource ID.
         *
         * @param attribute The attribute to resolve.
         * @param context Any context.
         * @return The resource ID of the resolved attribute.
         */
        @AnyRes
        fun resolveAttribute(attribute: Int, context: Context): Int {
            val typedValue = TypedValue()
            val theme = context.theme
            theme.resolveAttribute(attribute, typedValue, true)

            return typedValue.resourceId
        }

        /**
         * Resolves the attribute to a color.
         *
         * @param attribute The attribute to resolve.
         * @return The [Color] of the resolved attribute.
         */
        @Composable
        fun resolveAttributeColor(attribute: Int): Color {
            val resourceId = resolveAttribute(attribute, LocalContext.current)
            return colorResource(resourceId)
        }

        private fun updateLightSystemBars(window: Window, context: Context) {
            setStatusBarColor(window, context)
            window.createWindowInsetsController().isAppearanceLightStatusBars = true

            // display handle light navigation bar color
            window.createWindowInsetsController().isAppearanceLightNavigationBars = true

            updateNavigationBar(window, context)
        }

        private fun clearLightSystemBars(window: Window) {
            window.createWindowInsetsController().isAppearanceLightStatusBars = false

            // display handle light navigation bar color
            window.createWindowInsetsController().isAppearanceLightNavigationBars = false
        }

        private fun updateNavigationBar(window: Window, context: Context) {
            window.setNavigationBarColorCompat(context.getColorFromAttr(materialR.attr.colorSurface))
        }

        private fun setStatusBarColor(window: Window, context: Context) {
            context.getStatusBarColor()?.let { window.setStatusBarColorCompat(it) }
        }
    }
}

class DefaultThemeManager(
    currentTheme: Theme,
    private val activity: Activity,
) : ThemeManager() {

    override var currentTheme: Theme = currentTheme
        set(value) {
            if (currentTheme != value) {
                // ExternalAppBrowserActivity doesn't need to switch between private and non-private.
                if (activity is ExternalAppBrowserActivity) return
                // Don't recreate if activity is finishing
                if (activity.isFinishing) return

                field = value

                val intent = activity.intent ?: Intent().also { activity.intent = it }
                intent.putExtra(HomeActivity.PRIVATE_BROWSING_MODE, value == Theme.Private)

                activity.recreate()
            }
        }
}

/**
 * Returns the [Theme] to apply for the given [browsingMode].
 *
 * @param browsingMode The current [BrowsingMode].
 * @param isSystemInDarkTheme Whether the device is in dark mode.
 * @param shouldUseOledTheme Whether the OLED theme is selected.
 */
fun getTheme(browsingMode: BrowsingMode, isSystemInDarkTheme: Boolean, shouldUseOledTheme: Boolean): Theme =
    when {
        browsingMode.isPrivate -> Theme.Private
        !isSystemInDarkTheme -> Theme.Light
        shouldUseOledTheme -> Theme.Oled
        else -> Theme.Dark
    }
