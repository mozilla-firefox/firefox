/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.draggable

import kotlin.test.assertEquals
import org.junit.Test

class ItemInteractionHandlerTest {
    @Test
    fun `GIVEN a handler which only handles cancelled drags WHEN a drag ends THEN it is handled as a cancelled drag`() {
        var cancelledDrags = 0
        val handler =
            object : ItemInteractionHandler {
                override fun onMove(sourceKey: Any, targetKey: Any?, placeAfter: Boolean) = Unit

                override fun onDrop(sourceKey: Any, targetKey: Any) = Unit

                override fun onDragCancel() {
                    cancelledDrags++
                }

                override fun onDragStart(sourceKey: Any, preserveSelectMode: Boolean) = Unit
            }

        handler.onDragEnd()

        assertEquals(1, cancelledDrags)
    }
}
