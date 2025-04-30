/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import androidx.fragment.app.Fragment
import kotlinx.coroutines.CoroutineScope
import mozilla.components.lib.state.Action
import mozilla.components.lib.state.State
import mozilla.components.lib.state.Store
import mozilla.components.support.test.robolectric.createAddedTestFragment
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.helpers.FenixRobolectricTestRunner

@RunWith(FenixRobolectricTestRunner::class)
class StoreProviderTest {

    private class BasicState : State

    private val basicStore = Store(BasicState(), { state, _: Action -> state })

    @Test
    fun `factory returns store provider`() {
        var createCalled = false
        val factory = StoreProviderFactory {
            createCalled = true
            basicStore
        }

        assertFalse(createCalled)

        assertEquals(basicStore, factory.create(StoreProvider::class.java).store)

        assertTrue(createCalled)
    }

    @Test
    fun `get returns store`() {
        val fragment = createAddedTestFragment { Fragment() }

        val store = StoreProvider.get(fragment) { basicStore }
        assertEquals(basicStore, store)
    }

    @Test
    fun `get only calls createStore if needed`() {
        val fragment = createAddedTestFragment { Fragment() }

        var createCalled = false
        val createStore: (CoroutineScope) -> Store<BasicState, Action> = {
            createCalled = true
            basicStore
        }

        StoreProvider.get(fragment, createStore)
        assertTrue(createCalled)

        createCalled = false
        StoreProvider.get(fragment, createStore)
        assertFalse(createCalled)
    }

    @Test
    fun `WHEN store is created lazily THEN createStore is only invoked on access`() {
        val fragment = createAddedTestFragment { Fragment() }

        var createCalled = false
        val createStore: (CoroutineScope) -> Store<BasicState, Action> = {
            createCalled = true
            basicStore
        }

        val store by fragment.lazyStore(createStore)
        // The store is not created yet.
        assertFalse(createCalled)

        assertEquals(basicStore, store)
        // The store is only created when it's used.
        assertTrue(createCalled)

        // The store is not created again.
        createCalled = false
        fragment.lazyStore(createStore).value
        assertFalse(createCalled)
    }

    @Test
    fun `GIVEN different stores are persisted WHEN requesting them THEN get their unique instances`() {
        val fragment = createAddedTestFragment { Fragment() }
        var createACalled = false
        val storeAFactory: (CoroutineScope) -> Store<BasicState, Action> = {
            createACalled = true
            basicStore
        }
        var createBCalled = false
        val storeBFactory: (CoroutineScope) -> StoreB = {
            createBCalled = true
            StoreB(BasicState())
        }

        val storeA: Store<BasicState, Action> = StoreProvider.get(fragment, storeAFactory)
        val storeB: StoreB = StoreProvider.get(fragment, storeBFactory)
        assertTrue(createACalled)
        assertTrue(createBCalled)

        createACalled = false
        createBCalled = false
        assertSame(storeA, StoreProvider.get(fragment, storeAFactory))
        assertSame(storeB, StoreProvider.get(fragment, storeBFactory))
        assertFalse(createACalled)
        assertFalse(createBCalled)
    }

    private class StoreB(initialState: BasicState) : Store<BasicState, Action>(
        initialState,
        { state, _: Action -> state },
    )
}
