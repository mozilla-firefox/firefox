/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.utils

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay

/**
 * Tells accessibility services about an update on the screen, even a structure change, politely and without moving
 * focus, through its [LiveRegion], which has to be drawn over the rest of the screen.
 */
internal class A11yUpdatesAnnouncer {
    // Empty for nothing to tell.
    private var announcement by mutableStateOf("")

    /** Tell accessibility services [announcement]. */
    fun announce(announcement: String) {
        this.announcement = announcement
    }

    /**
     * The node telling accessibility services each announcement. An announcement is cleared once it was available long
     * enough to be told, for it to not be read again when moving through the screen, and for the same to be told again
     * if announced next.
     */
    @Composable
    fun LiveRegion() {
        val current = announcement
        Box(
            modifier =
                Modifier.size(1.dp).clearAndSetSemantics {
                    liveRegion = LiveRegionMode.Polite
                    if (current.isNotEmpty()) contentDescription = current
                }
        )

        LaunchedEffect(current) {
            if (current.isNotEmpty()) {
                delay(A11Y_ANNOUNCEMENT_DURATION)
                announcement = ""
            }
        }
    }
}

/** Remember an [A11yUpdatesAnnouncer]. It announces nothing unless its [A11yUpdatesAnnouncer.LiveRegion] is shown. */
@Composable internal fun rememberA11yUpdatesAnnouncer(): A11yUpdatesAnnouncer = remember { A11yUpdatesAnnouncer() }
