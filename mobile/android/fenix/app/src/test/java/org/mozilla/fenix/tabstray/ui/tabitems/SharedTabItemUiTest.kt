/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui.tabitems

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import org.mozilla.fenix.compose.swipeFadeAlpha

class SharedTabItemUiTest {
    @Test
    fun `WHEN an item has no fade offset THEN tab item is fully opaque`() =
        assertEquals(1f, swipeFadeAlpha(offset = 0f, width = 1000f))

    @Test
    fun `WHEN offset increases THEN tab item fade increases`() {
        (0..10).map { swipeFadeAlpha(it * 100f, 1000f) }.zipWithNext().forEach { (a, b) -> assertTrue(b <= a) }
    }

    @Test
    fun `WHEN offset is high THEN the tab item fade won't drop below the minimum value`() =
        assertEquals(0.1f, swipeFadeAlpha(offset = 5000f, width = 1000f))

    @Test
    fun `GIVEN LTR or RTL fade direction THEN the tab fade performs identically`() =
        assertEquals(swipeFadeAlpha(100f, 1000f), swipeFadeAlpha(-100f, 1000f))

    @Test
    fun `WHEN width is unknown THEN tab item is fully opaque`() =
        assertEquals(1f, swipeFadeAlpha(offset = 100f, width = 0f))
}
