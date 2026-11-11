/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.ui.graphics.Color
import mozilla.components.compose.base.theme.information
import mozilla.components.compose.menu.ui.MenuItemState.ACTIVE
import mozilla.components.compose.menu.ui.MenuItemState.DEFAULT
import mozilla.components.compose.menu.ui.MenuItemState.DISABLED
import mozilla.components.compose.menu.ui.MenuItemState.WARNING
import mozilla.components.compose.menu.ui.utils.DISABLED_ALPHA

/** Color of the primary content - titles and badge labels - of a menu item element in this state. */
internal val MenuItemState.contentColor: Color
    @Composable
    @ReadOnlyComposable
    get() =
        when (this) {
            DEFAULT -> MaterialTheme.colorScheme.onSurface
            ACTIVE -> MaterialTheme.colorScheme.tertiary
            DISABLED -> MaterialTheme.colorScheme.onSurface.copy(alpha = DISABLED_ALPHA)
            WARNING -> MaterialTheme.colorScheme.error
        }

/**
 * Color of the secondary content - summaries and icons - of a menu item element in this state.
 *
 * Identical to [contentColor] except in [DEFAULT], where it is muted.
 */
internal val MenuItemState.secondaryContentColor: Color
    @Composable
    @ReadOnlyComposable
    get() =
        when (this) {
            DEFAULT -> MaterialTheme.colorScheme.onSurfaceVariant
            else -> contentColor
        }

/** Background color of the badge of a menu item element in this state. */
internal val MenuItemState.badgeContainerColor: Color
    @Composable
    @ReadOnlyComposable
    get() =
        when (this) {
            ACTIVE -> MaterialTheme.colorScheme.tertiaryContainer
            else -> MaterialTheme.colorScheme.surfaceContainerHigh
        }

/** Background color of the "new" indicator of a menu item in this state. */
internal val MenuItemState.newIndicatorContainerColor: Color
    @Composable
    @ReadOnlyComposable
    get() =
        when (this) {
            DISABLED -> MaterialTheme.colorScheme.information.copy(alpha = DISABLED_ALPHA)
            else -> MaterialTheme.colorScheme.information
        }
