/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ext

import androidx.core.content.ContextCompat
import io.mockk.every
import io.mockk.mockk
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class TabCollectionTest {

    @Test
    fun getIconColor() {
        val color = mockTabCollection(100L).getIconColor(testContext)
        // Color does not change
        repeat(100) {
            assertEquals(color, mockTabCollection(100L).getIconColor(testContext))
        }

        // Returns a color for negative IDs
        val defaultColor = ContextCompat.getColor(testContext, R.color.fx_mobile_icon_color_oncolor)
        assertNotEquals(defaultColor, mockTabCollection(-123L).getIconColor(testContext))
    }

    private fun mockTabCollection(id: Long): TabCollection {
        val collection: TabCollection = mockk()
        every { collection.id } returns id
        return collection
    }
}
