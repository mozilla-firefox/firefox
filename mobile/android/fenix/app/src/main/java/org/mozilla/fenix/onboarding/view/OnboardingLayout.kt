/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

import android.content.res.Configuration
import androidx.compose.foundation.layout.BoxWithConstraintsScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.isLargeScreenSize

/** The small device max height. The value comes from [org.mozilla.fenix.ext.isTallWindow]. */
private val SMALL_SCREEN_MAX_HEIGHT = 570.dp

@Composable
internal fun getOnboardingLayout(scope: BoxWithConstraintsScope): OnboardingLayout {
    val context = LocalContext.current
    val config = LocalConfiguration.current
    val isSmall = scope.maxHeight <= SMALL_SCREEN_MAX_HEIGHT
    val isLarge = context.isLargeScreenSize()
    val isLandscape = config.orientation == Configuration.ORIENTATION_LANDSCAPE

    val pagerWidth =
        pageContentWidth(
            scope = scope,
            isLandscape = isLandscape,
            isSmallScreen = isSmall,
            isLargeScreen = isLarge,
        )
    val pagerHeight =
        pageContentHeight(
            scope = scope,
            isLargeScreen = isLarge,
            isSmallScreen = isSmall,
            isLandscape = isLandscape,
        )

    // Ensure the adjacent card always peeks by 12dp. Since pageSpacing shares the same space, the
    // configured peek must be at least 24dp to leave a 12dp reveal after 12dp page spacing.
    val peek = ((scope.maxWidth - pagerWidth) / 2).coerceAtLeast(FirefoxTheme.layout.size.static300)

    val padding =
        when {
            isSmall && !isLandscape -> PaddingValues(0.dp)
            !isLarge && isLandscape -> PaddingValues(0.dp)
            else -> PaddingValues(horizontal = peek)
        }

    return OnboardingLayout(
        pagerHeight = pagerHeight,
        contentPadding = padding,
        pagePeekWidth = peek,
        isSmall = isSmall,
        isLarge = isLarge,
        isLandscape = isLandscape,
    )
}

private object PageContentLayout {
    val MIN_HEIGHT_DP = 650.dp
    val MIN_WIDTH_DP = 360.dp
    val MIN_HEIGHT_SMALL_SCREEN_DP = 430.dp
    val MIN_WIDTH_SMALL_SCREEN_DP = 300.dp
    val MIN_HEIGHT_TABLET_DP = 620.dp
    val MIN_WIDTH_TABLET_DP = 440.dp
    const val HEIGHT_RATIO = 0.6f
    const val WIDTH_RATIO = 0.85f
    const val TABLET_WIDTH_RATIO = 0.35f
    const val TABLET_HEIGHT_RATIO = 0.50f
    const val HEIGHT_RATIO_LANDSCAPE_NON_LARGE_SCREEN = 1f
    const val WIDTH_RATIO_LANDSCAPE_NON_LARGE_SCREEN = 1f
    const val HEIGHT_RATIO_SMALL_SCREEN = 0.9f
    const val WIDTH_RATIO_SMALL_SCREEN = 0.9f
}

private fun pageContentHeight(
    scope: BoxWithConstraintsScope,
    isLargeScreen: Boolean,
    isSmallScreen: Boolean,
    isLandscape: Boolean,
): Dp {
    val minHeight = minHeight(isLargeScreen, isSmallScreen)
    val heightRatio = heightRatio(isLargeScreen, isSmallScreen, isLandscape)

    return scope.maxHeight.times(heightRatio).coerceAtLeast(minHeight)
}

private fun minHeight(
    isLargeScreen: Boolean,
    isSmallScreen: Boolean,
): Dp =
    when {
        isLargeScreen -> PageContentLayout.MIN_HEIGHT_TABLET_DP
        isSmallScreen -> PageContentLayout.MIN_HEIGHT_SMALL_SCREEN_DP
        else -> PageContentLayout.MIN_HEIGHT_DP
    }

private fun heightRatio(
    isLargeScreen: Boolean,
    isSmallScreen: Boolean,
    isLandscape: Boolean,
): Float =
    when {
        isLargeScreen -> PageContentLayout.TABLET_HEIGHT_RATIO
        isSmallScreen -> PageContentLayout.HEIGHT_RATIO_SMALL_SCREEN
        !isLargeScreen && isLandscape -> PageContentLayout.HEIGHT_RATIO_LANDSCAPE_NON_LARGE_SCREEN
        else -> PageContentLayout.HEIGHT_RATIO
    }

private fun pageContentWidth(
    scope: BoxWithConstraintsScope,
    isLargeScreen: Boolean,
    isSmallScreen: Boolean,
    isLandscape: Boolean,
): Dp {
    val minWidth = minWidth(isLargeScreen, isSmallScreen)
    val widthRatio = widthRatio(isLargeScreen, isSmallScreen, isLandscape)

    return scope.maxWidth.times(widthRatio).coerceAtLeast(minWidth)
}

private fun widthRatio(
    isLargeScreen: Boolean,
    isSmallScreen: Boolean,
    isLandscape: Boolean,
): Float =
    when {
        isLargeScreen -> PageContentLayout.TABLET_WIDTH_RATIO
        isSmallScreen -> PageContentLayout.WIDTH_RATIO_SMALL_SCREEN
        !isLargeScreen && isLandscape -> PageContentLayout.WIDTH_RATIO_LANDSCAPE_NON_LARGE_SCREEN
        else -> PageContentLayout.WIDTH_RATIO
    }

private fun minWidth(
    isLargeScreen: Boolean,
    isSmallScreen: Boolean,
): Dp =
    when {
        isLargeScreen -> PageContentLayout.MIN_WIDTH_TABLET_DP
        isSmallScreen -> PageContentLayout.MIN_WIDTH_SMALL_SCREEN_DP
        else -> PageContentLayout.MIN_WIDTH_DP
    }

internal fun isNonLargeScreenLandscape(isLargeScreen: Boolean, isLandscape: Boolean) = (isLandscape && !isLargeScreen)

@Composable
internal fun pageSpacing(isLargeScreen: Boolean, isSmallScreen: Boolean, pagePeekWidth: Dp) =
    when {
        isLargeScreen -> pagePeekWidth
        isSmallScreen -> 0.dp
        else -> FirefoxTheme.layout.size.static150
    }

internal data class OnboardingLayout(
    val pagerHeight: Dp,
    val contentPadding: PaddingValues,
    val pagePeekWidth: Dp,
    val isSmall: Boolean,
    val isLarge: Boolean,
    val isLandscape: Boolean,
)
