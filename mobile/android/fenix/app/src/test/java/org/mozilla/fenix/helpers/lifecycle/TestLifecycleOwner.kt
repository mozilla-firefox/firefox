/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.helpers.lifecycle

import androidx.lifecycle.Lifecycle
import androidx.lifecycle.Lifecycle.State
import androidx.lifecycle.LifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry

/**
 * [LifecycleOwner] to be used for testing
 *
 * @param initialState Initial state. Defaults to [State.CREATED]
 */
class TestLifecycleOwner(initialState: State = State.CREATED) : LifecycleOwner {

    private val registry = LifecycleRegistry.createUnsafe(this)

    init {
        registry.currentState = initialState
    }

    override val lifecycle: Lifecycle
        get() = registry

    /**
     * Registers a [LifecycleObserver] for this [LifecycleOwner]
     */
    fun registerObserver(observer: LifecycleObserver) {
        registry.addObserver(observer)
    }

    /**
     * Simulates the `onCreate()` event
     */
    fun onCreate() = registry.handleLifecycleEvent(Lifecycle.Event.ON_CREATE)

    /**
     * Simulates the on `onStart()` event
     */
    fun onStart() = registry.handleLifecycleEvent(Lifecycle.Event.ON_START)

    /**
     * Simulates the on `onResume()` event
     */
    fun onResume() = registry.handleLifecycleEvent(Lifecycle.Event.ON_RESUME)

    /**
     * Simulates the on `onPause()` event
     */
    fun onPause() = registry.handleLifecycleEvent(Lifecycle.Event.ON_PAUSE)

    /**
     * Simulates the on `onStop()` event
     */
    fun onStop() = registry.handleLifecycleEvent(Lifecycle.Event.ON_STOP)

    /**
     * Simulates the on `onDestroy()` event
     */
    fun onDestroy() = registry.handleLifecycleEvent(Lifecycle.Event.ON_DESTROY)
}
