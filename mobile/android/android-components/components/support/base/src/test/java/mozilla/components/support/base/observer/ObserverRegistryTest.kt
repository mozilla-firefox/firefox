/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.base.observer

import android.app.Activity
import android.os.Looper.getMainLooper
import android.view.View
import android.view.WindowManager
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.robolectric.Robolectric
import org.robolectric.Shadows.shadowOf

@RunWith(AndroidJUnit4::class)
class ObserverRegistryTest {

    @Test
    fun `registered observer gets notified`() {
        val registry = ObserverRegistry<TestObserver>()

        val observer = TestObserver()
        registry.register(observer)

        assertFalse(observer.notified)

        registry.notifyObservers {
            somethingChanged()
        }

        assertTrue(observer.notified)
    }

    @Test
    fun `observer gets queued notifications when registered`() {
        val registry = ObserverRegistry<TestIntObserver>()

        val observer = TestIntObserver()
        val anotherObserver = TestIntObserver()
        registry.notifyAtLeastOneObserver {
            somethingChanged(1)
        }
        registry.notifyAtLeastOneObserver {
            somethingChanged(2)
        }
        registry.notifyAtLeastOneObserver {
            somethingChanged(3)
        }
        assertEquals(emptyList<Int>(), observer.notified)

        registry.register(observer)
        registry.register(anotherObserver)
        assertEquals(listOf(1, 2, 3), observer.notified)
        assertEquals(emptyList<Int>(), anotherObserver.notified)
    }

    @Test
    fun `observer does not get notified again after unregistering`() {
        val registry = ObserverRegistry<TestObserver>()

        val observer = TestObserver()
        registry.register(observer)

        assertFalse(observer.notified)

        registry.notifyObservers {
            somethingChanged()
        }

        assertTrue(observer.notified)

        observer.notified = false
        registry.unregister(observer)

        assertFalse(observer.notified)

        registry.notifyObservers {
            somethingChanged()
        }

        assertFalse(observer.notified)

        assertTrue(registry.checkInternalCollectionsAreEmpty())
    }

    @Test
    fun `observer gets notified multiple times`() {
        val registry = ObserverRegistry<TestObserver>()

        val observer = TestObserver()
        registry.register(observer)

        assertFalse(observer.notified)

        registry.notifyObservers {
            somethingChanged()
        }

        assertTrue(observer.notified)
        observer.notified = false

        registry.notifyObservers {
            somethingChanged()
        }

        assertTrue(observer.notified)
    }

    @Test
    fun `observer does not get notified when unregistered immediately`() {
        val registry = ObserverRegistry<TestObserver>()

        val observer = TestObserver()

        registry.register(observer)
        registry.unregister(observer)

        assertFalse(observer.notified)

        registry.notifyObservers {
            somethingChanged()
        }

        assertFalse(observer.notified)

        assertTrue(registry.checkInternalCollectionsAreEmpty())
    }

    @Test
    fun `observer will not get registered if lifecycle state is DESTROYED`() {
        val owner = MockedLifecycleOwner(Lifecycle.State.STARTED)

        // We cannot set initial DESTROYED state for LifecycleRegistry
        // so we simulate lifecycle getting destroyed.
        owner.lifecycleRegistry.currentState = Lifecycle.State.DESTROYED

        val registry = ObserverRegistry<TestObserver>()
        val observer = TestObserver()

        registry.register(observer, owner)

        assertFalse(observer.notified)

        registry.notifyObservers {
            somethingChanged()
        }

        assertFalse(observer.notified)

        assertTrue(registry.checkInternalCollectionsAreEmpty())
    }

    @Test
    fun `observer will get removed if lifecycle gets destroyed`() {
        val owner = MockedLifecycleOwner(Lifecycle.State.STARTED)

        val registry = ObserverRegistry<TestObserver>()
        val observer = TestObserver()

        registry.register(observer, owner)

        // Observer gets notified
        assertFalse(observer.notified)
        registry.notifyObservers { somethingChanged() }
        assertTrue(observer.notified)

        // Pretend lifecycle gets destroyed
        owner.lifecycleRegistry.currentState = Lifecycle.State.DESTROYED

        observer.notified = false
        registry.notifyObservers { somethingChanged() }
        assertFalse(observer.notified)

        assertTrue(registry.checkInternalCollectionsAreEmpty())
    }

    @Test
    fun `observer registered as autopause will get removed if lifecycle gets destroyed`() {
        val owner = MockedLifecycleOwner(Lifecycle.State.RESUMED)

        val registry = ObserverRegistry<TestObserver>()
        val observer = TestObserver()

        registry.register(observer = observer, owner = owner, autoPause = true)

        // Observer gets notified
        assertFalse(observer.notified)
        registry.notifyObservers { somethingChanged() }
        assertTrue(observer.notified)

        // Pretend lifecycle gets destroyed
        owner.lifecycleRegistry.currentState = Lifecycle.State.DESTROYED

        observer.notified = false
        registry.notifyObservers { somethingChanged() }
        assertFalse(observer.notified)

        assertTrue(registry.checkInternalCollectionsAreEmpty())
    }

    @Test
    fun `non-destroy lifecycle changes do not affect observer`() {
        val owner = MockedLifecycleOwner(Lifecycle.State.STARTED)

        val registry = ObserverRegistry<TestObserver>()
        val observer = TestObserver()

        registry.register(observer, owner)

        // STARTED
        assertFalse(observer.notified)
        registry.notifyObservers { somethingChanged() }
        assertTrue(observer.notified)

        // RESUMED
        owner.lifecycleRegistry.currentState = Lifecycle.State.RESUMED
        observer.notified = false
        registry.notifyObservers { somethingChanged() }
        assertTrue(observer.notified)

        // CREATED
        owner.lifecycleRegistry.currentState = Lifecycle.State.CREATED
        observer.notified = false
        registry.notifyObservers { somethingChanged() }
        assertTrue(observer.notified)
    }

    @Test
    fun `unregisterObservers unregisters all observers`() {
        val registry = ObserverRegistry<TestObserver>()
        val activity = Robolectric.buildActivity(Activity::class.java).create().get()
        val view = View(testContext)

        val observer1 = TestObserver()
        val observer2 = TestObserver()
        val observer3 = TestObserver()
        val observer4 = TestObserver()

        registry.register(observer1)
        registry.register(observer2)
        registry.register(observer3, MockedLifecycleOwner(Lifecycle.State.CREATED))
        registry.register(observer4, view)
        activity.windowManager.addView(view, WindowManager.LayoutParams(100, 100))
        shadowOf(getMainLooper()).idle()

        assertFalse(observer1.notified)
        assertFalse(observer2.notified)

        registry.notifyObservers { somethingChanged() }

        assertTrue(observer1.notified)
        assertTrue(observer2.notified)
        assertTrue(observer3.notified)
        assertTrue(observer4.notified)

        observer1.notified = false
        observer2.notified = false
        observer3.notified = false
        observer4.notified = false

        registry.unregisterObservers()

        registry.notifyObservers { somethingChanged() }

        assertFalse(observer1.notified)
        assertFalse(observer2.notified)
        assertFalse(observer3.notified)
        assertFalse(observer4.notified)

        assertTrue(registry.checkInternalCollectionsAreEmpty())
    }

    @Test
    fun `unregisterObservers clears references to all observers`() {
        val registry = ObserverRegistry<TestObserver>()

        val observer1 = TestObserver()
        val observer2 = TestObserver()
        val observer3 = TestObserver()
        val observer4 = TestObserver()

        registry.register(observer1)
        registry.register(observer2)
        registry.register(observer3, MockedLifecycleOwner(Lifecycle.State.CREATED))
        registry.register(observer4, View(testContext))

        registry.unregisterObservers()

        assertFalse(registry.isObserved())

        assertTrue(registry.checkInternalCollectionsAreEmpty())
    }

    @Test
    fun `unregister removes observers from observers map`() {
        val registry = ObserverRegistry<String>()
        val observer = "Observer"

        registry.unregister(observer)

        assertFalse(registry.isObserved())
        assertTrue(registry.checkInternalCollectionsAreEmpty())
    }

    @Test
    fun `unregister removes observers from lifecycle observers map`() {
        val registry = ObserverRegistry<String>()
        val observer = "Observer"
        val lifecycleOwner = MockedLifecycleOwner(Lifecycle.State.CREATED)

        registry.register(observer, lifecycleOwner)
        registry.unregister(observer)

        assertFalse(registry.isObserved())
        assertTrue(registry.checkInternalCollectionsAreEmpty())
    }

    @Test
    fun `unregister removes observers from view observers map`() {
        val registry = ObserverRegistry<String>()
        val observer = "Observer"
        val view = View(testContext)

        registry.register(observer, view)
        registry.unregister(observer)

        assertFalse(registry.isObserved())
        assertTrue(registry.checkInternalCollectionsAreEmpty())
    }

    @Test
    fun `observer will not get added if view is detached`() {
        val view = mock<View>()

        val registry = ObserverRegistry<TestObserver>()
        val observer = TestObserver()

        @Suppress("UsePropertyAccessSyntax") doReturn(false).`when`(view).isAttachedToWindow()

        registry.register(observer, view)

        assertFalse(observer.notified)

        registry.notifyObservers {
            somethingChanged()
        }

        assertFalse(observer.notified)
    }

    @Test
    fun `observer will get added once view is attached`() {
        val activity = Robolectric.buildActivity(Activity::class.java).create().get()
        val view = View(testContext)

        val registry = ObserverRegistry<TestObserver>()
        val observer = TestObserver()

        assertFalse(view.isAttachedToWindow)
        registry.register(observer, view)

        registry.notifyObservers {
            somethingChanged()
        }

        assertFalse(observer.notified)

        activity.windowManager.addView(view, WindowManager.LayoutParams(100, 100))
        shadowOf(getMainLooper()).idle()
        assertTrue(view.isAttachedToWindow)

        registry.notifyObservers {
            somethingChanged()
        }

        assertTrue(observer.notified)
    }

    @Test
    fun `observer will get unregistered if view gets detached`() {
        val activity = Robolectric.buildActivity(Activity::class.java).create().get()
        val view = View(testContext)
        activity.windowManager.addView(view, WindowManager.LayoutParams(100, 100))
        shadowOf(getMainLooper()).idle()

        val registry = ObserverRegistry<TestObserver>()
        val observer = TestObserver()

        assertTrue(view.isAttachedToWindow)

        registry.register(observer, view)

        assertFalse(observer.notified)

        registry.notifyObservers {
            somethingChanged()
        }

        assertTrue(observer.notified)

        observer.notified = false

        activity.windowManager.removeView(view)
        shadowOf(getMainLooper()).idle()

        assertFalse(view.isAttachedToWindow)

        registry.notifyObservers {
            somethingChanged()
        }

        assertFalse(observer.notified)

        assertTrue(registry.checkInternalCollectionsAreEmpty())
    }

    @Test
    fun `unregisterObservers will unregister from view`() {
        val view: View = mock()
        doReturn(true).`when`(view).isAttachedToWindow

        val registry = ObserverRegistry<TestObserver>()
        val observer = TestObserver()

        registry.register(observer, view)
        verify(view).addOnAttachStateChangeListener(any())

        registry.unregisterObservers()
        verify(view).removeOnAttachStateChangeListener(any())

        assertTrue(registry.checkInternalCollectionsAreEmpty())
    }

    @Test
    fun `unregisterObserver will remove attach listener`() {
        val view: View = mock()
        doReturn(true).`when`(view).isAttachedToWindow

        val registry = ObserverRegistry<TestObserver>()
        val observer = TestObserver()

        registry.register(observer, view)
        verify(view).addOnAttachStateChangeListener(any())

        registry.unregister(observer)
        verify(view).removeOnAttachStateChangeListener(any())

        assertTrue(registry.checkInternalCollectionsAreEmpty())
    }

    @Test
    fun `wrapConsumers will return list of lambdas calling observers`() {
        val observer1 = TestConsumingObserver(shouldConsume = false)
        val observer2 = TestConsumingObserver(shouldConsume = true)
        val observer3 = TestConsumingObserver(shouldConsume = false)

        val registry = ObserverRegistry<TestConsumingObserver>()
        registry.register(observer1)
        registry.register(observer2)
        registry.register(observer3)

        val consumers: List<(Int) -> Boolean> = registry.wrapConsumers { value -> consumeSomething(value) }

        assertFalse(observer1.notified)
        assertFalse(observer2.notified)
        assertFalse(observer3.notified)

        val valueToConsume = 23
        assertEquals(listOf(false, true, false), consumers.map { it(valueToConsume) })

        assertTrue(observer1.notified)
        assertTrue(observer2.notified)
        assertTrue(observer3.notified)

        assertEquals(valueToConsume, observer1.notifiedWith!!)
        assertEquals(valueToConsume, observer2.notifiedWith!!)
        assertEquals(valueToConsume, observer3.notifiedWith!!)
    }

    @Test
    fun `observer is paused on lifecycle event (ON_PAUSE)`() {
        val owner = MockedLifecycleOwner(Lifecycle.State.RESUMED)

        val registry = spy(ObserverRegistry<TestObserver>())

        val observer = TestObserver()
        registry.register(observer, owner, autoPause = false)

        val autoPausedObserver = TestObserver()
        registry.register(autoPausedObserver, owner, autoPause = true)

        // Both observers get notified
        assertFalse(observer.notified)
        assertFalse(autoPausedObserver.notified)
        registry.notifyObservers { somethingChanged() }
        assertTrue(observer.notified)
        assertTrue(autoPausedObserver.notified)

        // Pause observers
        owner.lifecycleRegistry.handleLifecycleEvent(Lifecycle.Event.ON_PAUSE)

        observer.notified = false
        autoPausedObserver.notified = false
        registry.notifyObservers { somethingChanged() }

        // (Regular) observer still gets notified
        verify(registry, never()).pauseObserver(observer)
        assertTrue(observer.notified)

        // (Auto-paused) observer is now paused and no longer gets notified
        verify(registry).pauseObserver(autoPausedObserver)
        assertFalse(autoPausedObserver.notified)
    }

    @Test
    fun `observer is resumed on lifecycle event (ON_RESUME)`() {
        val owner = MockedLifecycleOwner(Lifecycle.State.RESUMED)

        val registry = spy(ObserverRegistry<TestIntObserver>())
        val observer = TestIntObserver()
        registry.register(observer, owner, autoPause = true)

        // Called once on register, since the lifecycle is already resumed
        verify(registry, times(1)).resumeObserver(observer)

        // Pause observer
        owner.lifecycleRegistry.handleLifecycleEvent(Lifecycle.Event.ON_PAUSE)
        registry.notifyObservers { somethingChanged(1) }
        registry.notifyObservers { somethingChanged(2) }
        registry.notifyObservers { somethingChanged(3) }
        assertEquals(emptyList<Int>(), observer.notified)

        // Resume observer
        owner.lifecycleRegistry.handleLifecycleEvent(Lifecycle.Event.ON_RESUME)

        // Resumed observer gets notified
        registry.notifyObservers { somethingChanged(4) }
        assertEquals(listOf(4), observer.notified)
        verify(registry, times(2)).resumeObserver(observer)
    }

    @Test
    fun `observer starts paused if needed`() {
        // Start in state other than RESUMED
        val owner = MockedLifecycleOwner(Lifecycle.State.STARTED)

        val registry = spy(ObserverRegistry<TestObserver>())

        val observer = TestObserver()
        registry.register(observer, owner, autoPause = false)

        val autoPausedObserver = TestObserver()
        registry.register(autoPausedObserver, owner, autoPause = true)

        verify(registry, never()).pauseObserver(observer)
        verify(registry).pauseObserver(autoPausedObserver)
    }

    @Test
    fun `register observer only once, notify once`() {
        // Given
        val observer = TestIntObserver()
        val registry = ObserverRegistry<TestIntObserver>()

        // When
        registry.register(observer)
        registry.register(observer)

        registry.notifyObservers { somethingChanged(1) }

        // Then
        assertEquals(1, observer.notified.size)
    }

    @Test
    fun `register observer only once, don't notify after unregister`() {
        // Given
        val observer = TestIntObserver()
        val registry = ObserverRegistry<TestIntObserver>()

        // When
        registry.register(observer)
        registry.register(observer)
        registry.unregister(observer)

        registry.notifyObservers { somethingChanged(1) }

        // Then
        assertEquals(0, observer.notified.size)
    }

    @Test
    fun `isObserved is true if observers is empty`() {
        val registry = ObserverRegistry<TestIntObserver>()
        val observer = TestIntObserver()

        assertFalse(registry.isObserved())

        registry.register(observer)

        assertTrue(registry.isObserved())

        registry.unregister(observer)

        assertFalse(registry.isObserved())
    }

    @Test
    fun `isObserved is true if there is still a view observer that may register an observer for a view`() {
        val registry = ObserverRegistry<TestIntObserver>()
        val observer: TestIntObserver = mock()

        val view: View = mock()
        doReturn(false).`when`(view).isAttachedToWindow

        registry.register(observer, view)

        // observer is not registered since the view is not attached yet
        registry.notifyObservers { somethingChanged(42) }
        verify(observer, never()).somethingChanged(42)

        // But it still counts as being observed
        assertTrue(registry.isObserved())

        registry.unregister(observer)
        assertFalse(registry.isObserved())
    }

    private class TestObserver {
        var notified: Boolean = false

        fun somethingChanged() {
            notified = true
        }
    }

    private class TestIntObserver {
        val notified = mutableListOf<Int>()

        fun somethingChanged(value: Int) {
            notified += value
        }
    }

    private class TestConsumingObserver(private val shouldConsume: Boolean) {
        var notified: Boolean = false
        var notifiedWith: Int? = null

        fun consumeSomething(value: Int): Boolean {
            notifiedWith = value
            notified = true
            return shouldConsume
        }
    }

    private class MockedLifecycleOwner(initialState: Lifecycle.State) : LifecycleOwner {
        val lifecycleRegistry =
            LifecycleRegistry(this).apply {
                currentState = initialState
            }

        override val lifecycle: Lifecycle = lifecycleRegistry
    }
}
