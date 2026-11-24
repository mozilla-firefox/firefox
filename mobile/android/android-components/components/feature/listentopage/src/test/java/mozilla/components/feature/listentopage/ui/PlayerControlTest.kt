/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class PlayerControlTest {

    @Test
    fun `test that controls only in the expanded player hand over to the nearest compact control`() {
        assertEquals(PlayerControl.Rewind, PlayerControl.Seek.compactCounterpart)
        assertEquals(PlayerControl.Rewind, PlayerControl.Voices.compactCounterpart)
        assertEquals(PlayerControl.PlayPause, PlayerControl.Forward.compactCounterpart)
        assertEquals(PlayerControl.PlayPause, PlayerControl.Speed.compactCounterpart)
    }

    @Test
    fun `test that controls in both players hand over to themselves`() {
        listOf(PlayerControl.Close, PlayerControl.Heading, PlayerControl.Rewind, PlayerControl.PlayPause).forEach {
            assertEquals(it, it.compactCounterpart)
        }
    }

    @Test
    fun `test that a control is found by its test tag`() {
        PlayerControl.entries.forEach { assertEquals(it, PlayerControl.fromTestTag(it.testTag)) }
        assertNull(PlayerControl.fromTestTag("unknown"))
        assertNull(PlayerControl.fromTestTag(null))
    }
}
