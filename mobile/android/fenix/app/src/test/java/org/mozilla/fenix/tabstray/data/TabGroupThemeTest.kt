/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.data

import org.junit.Assert.assertEquals
import org.junit.Test

class TabGroupThemeTest {

    @Test
    fun `GIVEN no last used theme WHEN nextOrDefault is called THEN the default theme is returned`() {
        val lastUsedTheme: TabGroupTheme? = null

        assertEquals(TabGroupTheme.default, lastUsedTheme.nextOrDefault())
    }

    @Test
    fun `WHEN nextOrDefault is called THEN the next theme is returned`() {
        assertEquals(TabGroupTheme.Orange, TabGroupTheme.Yellow.nextOrDefault())
    }

    @Test
    fun `GIVEN the last theme in the list WHEN nextOrDefault is called THEN the first theme is returned`() {
        assertEquals(TabGroupTheme.entries.first(), TabGroupTheme.entries.last().nextOrDefault())
    }
}
