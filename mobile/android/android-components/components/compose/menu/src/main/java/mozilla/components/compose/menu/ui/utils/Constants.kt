/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.utils

import kotlin.time.Duration.Companion.seconds
import mozilla.components.compose.menu.ui.MenuItemState.DISABLED

/** Transparency level of [DISABLED] menu items or their elements. */
internal const val DISABLED_ALPHA = 0.38f

/** Transparency level of menu items while being dragged by the user. */
internal const val DRAGGED_GROUP_ALPHA = 0.7f

/** How long the announcement of a move stays available to accessibility services. */
internal val A11Y_ANNOUNCEMENT_DURATION = 1.seconds
