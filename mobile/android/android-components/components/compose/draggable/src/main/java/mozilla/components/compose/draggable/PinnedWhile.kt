/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.draggable

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.ui.layout.LocalPinnableContainer

/**
 * Keep the lazy list or grid item this is called from composed while [isPinned], even when it is scrolled out of view.
 *
 * @param isPinned Whether to keep the item composed.
 */
@Composable
internal fun PinnedWhile(isPinned: Boolean) {
    val pinnableContainer = LocalPinnableContainer.current
    DisposableEffect(isPinned) {
        val handle = if (isPinned) pinnableContainer?.pin() else null
        onDispose { handle?.release() }
    }
}
