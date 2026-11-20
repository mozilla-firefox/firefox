/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.playback

import org.junit.Assert.assertEquals
import org.junit.Test

class PlaybackSpeedTest {

    @Test
    fun `test that an article opens at the speed the voice reads at`() {
        assertEquals(PlaybackSpeed.X1, PlaybackSpeed.Default)
        assertEquals(1f, PlaybackSpeed.Default.multiplier, 0f)
    }

    @Test
    fun `test that every step is reported as itself`() {
        PlaybackSpeed.entries.forEach { speed ->
            assertEquals(speed, PlaybackSpeed.nearest(speed.multiplier))
        }
    }

    @Test
    fun `test that stepping through every speed comes back to where it started`() {
        var stepped = PlaybackSpeed.Default
        repeat(PlaybackSpeed.entries.size) { stepped = stepped.next() }

        assertEquals(PlaybackSpeed.Default, stepped)
    }

    // The player is what the speed is read back from, and another app's controller can set it to anything at all.
    @Test
    fun `test that a speed of no step of ours is reported as the nearest one`() {
        assertEquals(PlaybackSpeed.X1_5, PlaybackSpeed.nearest(1.44f))
        assertEquals(PlaybackSpeed.X0_25, PlaybackSpeed.nearest(0.1f))
        assertEquals(PlaybackSpeed.X2, PlaybackSpeed.nearest(3.5f))
    }
}
