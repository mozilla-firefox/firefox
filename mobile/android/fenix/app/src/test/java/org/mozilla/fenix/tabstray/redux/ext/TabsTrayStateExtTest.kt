/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.redux.ext

import kotlin.test.assertEquals
import kotlin.test.assertSame
import org.junit.Test
import org.mozilla.fenix.tabstray.data.createTabGroup
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState

class TabsTrayStateExtTest {

    private val group = createTabGroup(id = "group-1", title = "Group 1")
    private val state =
        TabsTrayState(
            selectedPage = Page.PrivateTabs,
            tabGroupState = TabsTrayState.TabGroupState(groups = listOf(createTabGroup(id = "other"), group)),
        )

    @Test
    fun `GIVEN no group ID WHEN expanding a tab group THEN the state is unchanged`() {
        assertSame(state, state.withExpandedTabGroup(groupId = null))
    }

    @Test
    fun `GIVEN a group ID that does not match any group WHEN expanding a tab group THEN the state is unchanged`() {
        assertSame(state, state.withExpandedTabGroup(groupId = "missing"))
    }

    @Test
    fun `GIVEN a matching group ID WHEN expanding a tab group THEN the expanded group is pushed onto the back stack`() {
        val result = state.withExpandedTabGroup(groupId = group.id)

        assertEquals(
            listOf(TabManagerNavDestination.Root, TabManagerNavDestination.ExpandedTabGroup(group = group)),
            result.backStack,
        )
    }

    @Test
    fun `GIVEN a matching group ID WHEN expanding a tab group THEN the normal tabs page is selected`() {
        val result = state.withExpandedTabGroup(groupId = group.id)

        assertEquals(Page.NormalTabs, result.selectedPage)
    }
}
