/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.state.helpers

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentActivity
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.lifecycle.ViewModelStore
import androidx.lifecycle.ViewModelStoreOwner
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.lib.state.Store
import mozilla.components.lib.state.TestAction
import mozilla.components.lib.state.TestState
import mozilla.components.lib.state.helpers.StoreProvider.Companion.buildPersistentStore
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import mozilla.components.lib.state.helpers.StoreProvider.Companion.storeProvider
import mozilla.components.lib.state.reducer
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric

@RunWith(AndroidJUnit4::class)
class StoreProviderTest {
    private val lifecycleOwner = MockedLifecycleOwner(Lifecycle.State.STARTED)

    @Test
    fun `GIVEN same store type was built before WHEN building a new store THEN reuse the state of the previous store`() {
        var store = lifecycleOwner.buildPersistentStore(TestState(23)) { TestStore(it) }.value
        assertEquals(23, store.state.counter)
        var store2 = lifecycleOwner.buildPersistentStore(TestState(123)) { TestStore2(it) }.value
        assertEquals(123, store2.state.counter)

        lifecycleOwner.lifecycleRegistry.currentState = Lifecycle.State.DESTROYED

        store = lifecycleOwner.buildPersistentStore(TestState(99)) { TestStore(it) }.value
        assertEquals(23, store.state.counter)
        store2 = lifecycleOwner.buildPersistentStore(TestState(999)) { TestStore2(it) }.value
        assertEquals(123, store2.state.counter)
    }

    @Test
    fun `GIVEN same store type was built before WHEN asking for it THEN return the previously built instance`() {
        val store = lifecycleOwner.buildPersistentStore(TestState(23)) { TestStore(it) }.value
        val store2 = lifecycleOwner.buildPersistentStore(TestState(123)) { TestStore2(it) }.value
        val store3 = lifecycleOwner.storeProvider.get<TestState, TestStore3> { TestStore3(TestState(1234)) }

        var result1 = lifecycleOwner.storeProvider.get<TestStore>()
        var result2: TestStore2? = lifecycleOwner.storeProvider.get()
        var result3: TestStore3 =
            lifecycleOwner.storeProvider.get<TestState, TestStore3> { TestStore3(TestState(9999)) }
        assertEquals(store, result1)
        assertEquals(store2, result2)
        assertEquals(store3, result3)

        lifecycleOwner.lifecycleRegistry.currentState = Lifecycle.State.DESTROYED

        result1 = lifecycleOwner.storeProvider.get<TestStore>()
        result2 = lifecycleOwner.storeProvider.get()
        result3 =
            lifecycleOwner.storeProvider.get<TestState, TestStore3> { restoredState ->
                TestStore3(restoredState ?: TestState(9999))
            }
        assertNull(result1)
        assertNull(result2)
        assertEquals(store3.state, result3.state)
    }

    @Test
    fun `GIVEN a store is updated after being built WHEN building a new store of the same type THEN reuse the latest state`() {
        val initialState = TestState(23)
        var store = lifecycleOwner.buildPersistentStore(initialState) { Store(it, ::reducer) }.value
        assertEquals(23, store.state.counter)

        store.dispatch(TestAction.IncrementAction)
        val updatedState = initialState.copy(counter = 24)

        lifecycleOwner.lifecycleRegistry.currentState = Lifecycle.State.DESTROYED

        store = lifecycleOwner.buildPersistentStore(initialState) { Store(it, ::reducer) }.value
        assertEquals(updatedState.counter, store.state.counter)
    }

    @Test
    fun `GIVEN a fragment with a view WHEN reading the store repeatedly THEN return the same store`() {
        val (_, fragment) = setupFragment()

        val store = fragment.store

        assertSame(store, fragment.store)
        assertEquals(1, fragment.factoryCalls)
    }

    @Test
    fun `GIVEN a fragment WHEN checking if the store is initialized THEN it is only after the first read and stays so after the view is destroyed`() {
        val (activity, fragment) = setupFragment()

        assertFalse(fragment.storeLazy.isInitialized())

        fragment.store

        assertTrue(fragment.storeLazy.isInitialized())

        activity.detach(fragment)

        assertTrue(fragment.storeLazy.isInitialized())
    }

    @Test
    fun `GIVEN a fragment whose view was destroyed WHEN its view is created again THEN build a new store from the last state`() {
        val (activity, fragment) = setupFragment()

        repeat(2) { cycle ->
            val previous = fragment.store
            previous.dispatch(TestAction.IncrementAction)

            activity.detach(fragment)
            activity.attach(fragment)

            assertNotSame(previous, fragment.store)
            assertEquals(previous.state.counter, fragment.store.state.counter)
            assertEquals(cycle + 2, fragment.factoryCalls)
        }

        val last = fragment.store
        val provider = fragment.storeProvider
        activity.detach(fragment)

        assertNull(provider.get<TestStore>())
        activity.attach(fragment)
        assertEquals(last.state.counter, fragment.store.state.counter)
    }

    @Test
    fun `GIVEN a fragment whose view is being destroyed WHEN reading the store THEN return the old one and build a new one for the next view`() {
        val (activity, fragment) = setupFragment()
        val previous = fragment.store
        fragment.readInOnDestroyView = true

        activity.detach(fragment)

        assertSame(previous, fragment.storeReadInOnDestroyView)
        assertSame(previous, fragment.store)
        assertEquals(1, fragment.factoryCalls)

        fragment.readInOnDestroyView = false
        activity.attach(fragment)

        assertNotSame(previous, fragment.store)
        assertEquals(2, fragment.factoryCalls)
    }

    @Test
    fun `GIVEN a fragment whose view is created again WHEN reading the store in onCreateView THEN return the new store`() {
        val (activity, fragment) = setupFragment()
        val previous = fragment.store
        fragment.readInOnCreateView = true

        activity.detach(fragment)
        activity.attach(fragment)

        assertNotSame(previous, fragment.storeReadInOnCreateView)
        assertSame(fragment.storeReadInOnCreateView, fragment.store)
    }

    @Test
    fun `GIVEN a store first read while the view is being destroyed WHEN the view is created again THEN build a new store and keep rebuilding on later cycles`() {
        val (activity, fragment) = setupFragment()
        fragment.readInOnDestroyView = true

        activity.detach(fragment)

        val readInDestroy = fragment.storeReadInOnDestroyView
        assertSame(readInDestroy, fragment.storeReadAgainInOnDestroyView)
        assertEquals(1, fragment.factoryCalls)
        fragment.readInOnDestroyView = false
        activity.attach(fragment)

        val afterFirstCycle = fragment.store
        assertNotSame(readInDestroy, afterFirstCycle)

        activity.detach(fragment)
        activity.attach(fragment)

        assertNotSame(afterFirstCycle, fragment.store)
    }

    @Test
    fun `GIVEN a fragment whose view was destroyed WHEN its old store changes before the view is created again THEN the new store starts from that state`() {
        val (activity, fragment) = setupFragment()
        val previous = fragment.store

        activity.detach(fragment)
        previous.dispatch(TestAction.IncrementAction)
        activity.attach(fragment)

        assertEquals(previous.state.counter, fragment.store.state.counter)
        assertEquals(2, fragment.store.state.counter)
    }

    @Test
    fun `GIVEN a fragment changing its store while its view is being destroyed WHEN the view is created again THEN the new store starts from that state`() {
        val (activity, fragment) = setupFragment()
        val previous = fragment.store
        fragment.dispatchInOnDestroyView = true

        activity.detach(fragment)
        fragment.dispatchInOnDestroyView = false
        activity.attach(fragment)

        assertEquals(2, previous.state.counter)
        assertEquals(2, fragment.store.state.counter)
    }

    private fun setupFragment(): Pair<FragmentActivity, StoreFragment> {
        val activity = Robolectric.buildActivity(FragmentActivity::class.java).setup().get()
        val fragment = StoreFragment()
        activity.supportFragmentManager.beginTransaction().add(fragment, null).commitNow()
        return activity to fragment
    }

    private fun FragmentActivity.detach(fragment: Fragment) {
        supportFragmentManager.beginTransaction().detach(fragment).commitNow()
    }

    private fun FragmentActivity.attach(fragment: Fragment) {
        supportFragmentManager.beginTransaction().attach(fragment).commitNow()
    }
}

internal class StoreFragment : Fragment() {
    var factoryCalls = 0
    var readInOnCreateView = false
    var readInOnDestroyView = false
    var dispatchInOnDestroyView = false
    var storeReadInOnCreateView: TestStore? = null
    var storeReadInOnDestroyView: TestStore? = null
    var storeReadAgainInOnDestroyView: TestStore? = null

    val storeLazy =
        fragmentStore(TestState(1)) {
            factoryCalls++
            TestStore(it)
        }
    val store by storeLazy

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        if (readInOnCreateView) {
            storeReadInOnCreateView = store
        }
        return View(inflater.context)
    }

    override fun onDestroyView() {
        if (readInOnDestroyView) {
            storeReadInOnDestroyView = store
            storeReadAgainInOnDestroyView = store
        }
        if (dispatchInOnDestroyView) {
            store.dispatch(TestAction.IncrementAction)
        }
        super.onDestroyView()
    }
}

private class MockedLifecycleOwner(initialState: Lifecycle.State) : LifecycleOwner, ViewModelStoreOwner {
    val lifecycleRegistry =
        LifecycleRegistry(this).apply {
            currentState = initialState
        }

    override val lifecycle: Lifecycle = lifecycleRegistry

    override val viewModelStore: ViewModelStore = ViewModelStore()
}

internal class TestStore(initialState: TestState = TestState(1)) :
    Store<TestState, TestAction>(
        initialState = initialState,
        reducer = ::reducer,
        middleware = emptyList(),
    )

private class TestStore2(initialState: TestState = TestState(2)) :
    Store<TestState, TestAction>(
        initialState = initialState,
        reducer = ::reducer,
        middleware = emptyList(),
    )

private class TestStore3(initialState: TestState = TestState(3)) :
    Store<TestState, TestAction>(
        initialState = initialState,
        reducer = ::reducer,
        middleware = emptyList(),
    )
