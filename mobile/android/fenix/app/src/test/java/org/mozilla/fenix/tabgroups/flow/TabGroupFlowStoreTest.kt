/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.flow

import kotlin.test.Test
import kotlin.test.assertNotNull
import mozilla.components.support.test.robolectric.testContext
import org.junit.Rule
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.TabsTray
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class TabGroupFlowStoreTest {
    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    @Test
    fun `WHEN store is initialized with the telemetry middleware, THEN its telemetry side effects are active`() {
        val store =
            TabGroupFlowStore(
                initialState =
                    TabGroupFlowState(
                        formState = TabGroupFormState(tabGroupId = null, name = "Test group"),
                        entryPoint = TabGroupFlowEntryPoint.AddTabToGroup("123"),
                    ),
                middlewares = listOf(TabGroupFlowTelemetryMiddleware()),
            )

        store.dispatch(TabGroupAction.SaveClicked)

        assertNotNull(TabsTray.tabGroupNamed.testGetValue())
        assertNotNull(TabsTray.tabGroupCreated.testGetValue())
    }
}
