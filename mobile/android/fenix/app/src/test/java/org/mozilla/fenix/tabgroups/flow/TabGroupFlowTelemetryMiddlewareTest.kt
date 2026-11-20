/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.flow

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import mozilla.components.support.test.robolectric.testContext
import org.junit.Rule
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.TabsTray
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.tabstray.data.createTabGroup
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class TabGroupFlowTelemetryMiddlewareTest {
    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private val store =
        TabGroupFlowStore(
            initialState =
                TabGroupFlowState(
                    entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "123"),
                    groups = listOf(createTabGroup(id = "source", tabs = listOf(createTab("mozilla.org", id = "123")))),
                ),
            middlewares = listOf(TabGroupFlowTelemetryMiddleware()),
        )

    @Test
    fun `WHEN moving the last tab and deleting its group is confirmed THEN a tab added to group event is recorded`() {
        store.dispatch(
            TabGroupAction.MoveTabAndDeleteGroupConfirmed(
                sourceGroupId = "source",
                destinationGroupId = "destination",
                destinationGroupTitle = "Work",
            )
        )

        val event = TabsTray.tabAddedToGroup.testGetValue()!!.single()
        assertEquals("1", event.extra?.get("tab_count"))
    }

    @Test
    fun `GIVEN the tab is the last in its group WHEN an existing group is selected and the dialog canceled THEN no tab added to group event is recorded`() {
        store.dispatch(
            TabGroupAction.ExistingGroupSelectedForMove(tabId = "123", groupId = "destination", groupTitle = "Work")
        )
        store.dispatch(TabGroupAction.MoveTabAndDeleteGroupCanceled)

        assertNull(TabsTray.tabAddedToGroup.testGetValue())
    }

    @Test
    fun `WHEN the last tab of a group is added to a new group THEN a tab added to group event is recorded`() {
        store.dispatch(TabGroupAction.TabAddedToNewTabGroup(tabId = "123"))

        val event = TabsTray.tabAddedToGroup.testGetValue()!!.single()
        assertEquals("1", event.extra?.get("tab_count"))
    }
}
