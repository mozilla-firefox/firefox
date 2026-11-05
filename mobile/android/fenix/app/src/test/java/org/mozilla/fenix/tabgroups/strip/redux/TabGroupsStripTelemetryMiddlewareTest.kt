/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.strip.redux

import kotlin.test.assertEquals
import kotlin.test.assertNull
import mozilla.components.support.test.robolectric.testContext
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.TabsTray
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class) // for gleanTestRule
class TabGroupsStripTelemetryMiddlewareTest {
    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private var homepageAsNewTabEnabled = true

    @Test
    fun `GIVEN HNT is enabled and in a group WHEN AddTabClicked is dispatched THEN tab added to group is recorded`() {
        val store = buildStore(TabGroupsStripState.TabGroupStripVisible(groupId = "group", groupTitle = "Group"))

        store.dispatch(TabGroupsStripAction.AddTabClicked)

        val events = TabsTray.tabAddedToGroup.testGetValue()!!
        assertEquals(1, events.size)
    }

    @Test
    fun `GIVEN HNT is disabled WHEN AddTabClicked is dispatched THEN nothing is recorded`() {
        homepageAsNewTabEnabled = false
        val store = buildStore(TabGroupsStripState.TabGroupStripVisible(groupId = "group", groupTitle = "Group"))

        store.dispatch(TabGroupsStripAction.AddTabClicked)

        assertNull(TabsTray.tabAddedToGroup.testGetValue())
    }

    @Test
    fun `GIVEN not in a group WHEN AddTabClicked is dispatched THEN nothing is recorded`() {
        val store = buildStore(TabGroupsStripState.TabGroupStripHidden)

        store.dispatch(TabGroupsStripAction.AddTabClicked)

        assertNull(TabsTray.tabAddedToGroup.testGetValue())
    }

    private fun buildStore(initialState: TabGroupsStripState) =
        TabGroupsStripStore(
            initialState = initialState,
            middleware = listOf(TabGroupsStripTelemetryMiddleware({ homepageAsNewTabEnabled })),
        )
}
