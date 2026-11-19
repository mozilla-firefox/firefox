/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.collections.migration

import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Ignore
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.CollectionsMigration
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.home.collections.migration.fake.FakeCollectionsMigrationRepository
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class CollectionsMigrationCardMiddlewareTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    @Test
    fun `WHEN ViewTabGroupsClicked action is dispatched THEN update card visibility preference`() {
        val repository = FakeCollectionsMigrationRepository(cardVisible = true)
        val store =
            CollectionsMigrationCardStore(
                initialState = CollectionsMigrationCardState(visible = true),
                middleware = listOf(CollectionsMigrationCardMiddleware(repository)),
            )

        store.dispatch(CollectionsMigrationCardAction.ViewTabGroupsClicked)

        assertFalse(repository.shouldShowCollectionsMigrationCard())
    }

    @Ignore("Bug 2095162: collections_migration probes expired in 163")
    @Test
    fun `WHEN ViewTabGroupsClicked action is dispatched THEN record promo click telemetry`() {
        val store =
            CollectionsMigrationCardStore(
                initialState = CollectionsMigrationCardState(visible = true),
                middleware =
                    listOf(CollectionsMigrationCardMiddleware(FakeCollectionsMigrationRepository(cardVisible = true))),
            )
        assertNull(CollectionsMigration.promoClick.testGetValue())

        store.dispatch(CollectionsMigrationCardAction.ViewTabGroupsClicked)

        assertEquals(1, CollectionsMigration.promoClick.testGetValue()!!.size)
    }
}
