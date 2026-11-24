/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.state.helpers

import androidx.activity.ComponentActivity
import androidx.annotation.MainThread
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.fragment.app.Fragment
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.ViewModelStoreOwner
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.viewmodel.compose.LocalViewModelStoreOwner
import androidx.navigation.NavBackStackEntry
import mozilla.components.lib.state.Action
import mozilla.components.lib.state.State
import mozilla.components.lib.state.Store

/**
 * Generic [ViewModel] wrapper for persisting the [State] of a particular [Store] between activity recreations.
 *
 * @param owner [ViewModelStoreOwner] whose lifecycle will be used to persist the various [State]s.
 */
class StoreProvider<T> private constructor(@PublishedApi internal var owner: T?) : ViewModel()
    where T : LifecycleOwner {
    private var isCleanupConfigured: Boolean = false

    init {
        setupCleanup()
    }

    /**
     * Helper for refreshing the [owner] instance if it was cleared in the meantime, for example in cases like the
     * ViewLifecycleOwner from a Fragment being cleared when the user navigates to another fragment but then a new one
     * created when the user comes back to the initial Fragment and uses this same ViewModel.
     */
    @PublishedApi
    internal fun refreshOwner(owner: T) {
        if (this.owner == null) {
            this.owner = owner
            setupCleanup()
        }
    }

    /**
     * Map of the current [Store]s built by this [StoreProvider]. This is needed to be able to access and persist the
     * latest [State] just before [owner] is destroyed.
     *
     * The key used is the name of the [Store<*, *>] java class.
     */
    @PublishedApi internal val stores: MutableMap<String, Store<*, *>> = mutableMapOf()

    /**
     * Map of the currently persisted [State]s.
     *
     * The key used is the name of the [Store<*, *>] java class.
     */
    @PublishedApi internal val states: MutableMap<String, State> = mutableMapOf()

    /**
     * Returns an existing [Store] of the requested type __if__ it was already build in the current scope (usually, a
     * fragment or an activity).
     *
     * This can return `null` if the requested [Store] was __not__ built again after activity recreations even if it has
     * it's state persisted.
     */
    @MainThread
    inline fun <reified ST : Store<*, *>> get(): ST? {
        return stores[ST::class.java.name] as? ST
    }

    /**
     * Returns an existing [Store] or allows creating a new one in the current scope (usually, a fragment or an
     * activity).
     *
     * The created Store is associated with the given scope and its will be retained as long as the scope is alive (e.g.
     * if it is an activity, until it is finished or process is killed).
     *
     * @param factory Custom builder for the new [Store] instance, used only if a [Store] of this type was not already
     *   built and persisted in the current scope. This will receive the persisted state (if available) of the
     *   previously built [Store] of the same type allowing to build build a new [Store] instance that will now be
     *   persisted.
     */
    @MainThread
    inline fun <reified S : State, reified ST : Store<S, *>> get(noinline factory: (S?) -> ST): ST {
        return stores[ST::class.java.name] as? ST
            ?: factory(states[ST::class.java.name] as? S).also {
                stores[ST::class.java.name] = it
            }
    }

    /**
     * Build a new [Store] with the provided [factory] and [initialState]. If this provider already built this store
     * type then the new instance will use the [State] of the previous.
     */
    @PublishedApi
    internal inline fun <reified S : State, reified ST : Store<S, *>> buildStore(
        initialState: S,
        factory: (S) -> ST,
    ): ST {
        val state =
            (stores[ST::class.java.name] as? ST)?.state // most up-to-date state
            ?: (states[ST::class.java.name] as? S) // persisted state
                ?: initialState

        return factory(state).also {
            stores[ST::class.java.name] = it
        }
    }

    @PublishedApi
    internal fun setupCleanup() {
        if (isCleanupConfigured) {
            return
        }

        val lifecycle = (owner as? Fragment)?.viewLifecycleOwner?.lifecycle ?: (owner as LifecycleOwner).lifecycle
        if (lifecycle.currentState == Lifecycle.State.DESTROYED) {
            owner = null
            return
        }
        isCleanupConfigured = true

        lifecycle.addObserver(
            object : DefaultLifecycleObserver {
                override fun onDestroy(owner: LifecycleOwner) {
                    this@StoreProvider.owner = null
                    isCleanupConfigured = false

                    states.clear()
                    stores.forEach { states[it.key] = it.value.state }
                    stores.clear()
                }
            }
        )
    }

    companion object {
        /** Get a [StoreProvider] for the current [LifecycleOwner]. */
        @Suppress("UNCHECKED_CAST")
        val <O> O.storeProvider: StoreProvider<O> where O : LifecycleOwner, O : ViewModelStoreOwner
            get() =
                (ViewModelProvider(this, DataPersisterFactory(this))[StoreProvider::class.java] as StoreProvider<O>)
                    .apply { refreshOwner(this@storeProvider) }

        /**
         * Build a [Store] with the provided parameters whose state lives as long as this [Fragment], surviving
         * configuration changes and activity recreations until this [Fragment] is removed from the backstack or until
         * process death. A new [Store] is built from that state for each view of this [Fragment].
         *
         * @param initialState The initial state until a dispatched [Action] creates a new state.
         * @param factory A function that receives [initialState] or the persisted state (if available) of the
         *   previously built [Store] of the same type allowing to build build a new [Store] instance to persist.
         *
         * This should be called only after this Fragment is attached i.e., after Fragment.onAttach(), and the result be
         * used i.e., by calling _.value()_ only in or after after Fragment.onCreateView() with access prior to that
         * resulting in IllegalStateException.
         *
         * Reads while this [Fragment] has no live view return the last [Store], if one was built. The [Store] of a
         * previous view is no longer used once the one of the next view is built, so its middleware should not outlive
         * its view.
         */
        @MainThread
        inline fun <reified S : State, A : Action, reified ST : Store<S, A>> Fragment.fragmentStore(
            initialState: S,
            noinline factory: (S) -> ST,
        ): Lazy<ST> =
            FragmentViewStoreLazy(this) { previous ->
                // The persisted state misses what the previous Store changed after its view lifecycle was destroyed.
                previous?.let { storeProvider.stores[ST::class.java.name] = it }
                storeProvider.buildStore(initialState, factory)
            }

        /**
         * Build a [Store] with the provided parameters which will live as long as this it's parent [AppCompatActivity],
         * surviving configuration changes and activity recreations until this [AppCompatActivity] is finished or until
         * process death.
         *
         * @param initialState The initial state until a dispatched [Action] creates a new state.
         * @param factory A function that receives [initialState] or the persisted state (if available) of the
         *   previously built [Store] of the same type allowing to build build a new [Store] instance to persist.
         *
         * This should be called only after this Fragment is attached i.e., after Fragment.onAttach(), with access prior
         * to that resulting in IllegalStateException.
         */
        @MainThread
        inline fun <reified S : State, A : Action, reified ST : Store<S, A>> Fragment.activityStore(
            initialState: S,
            noinline factory: (S) -> ST,
        ): Lazy<ST> = requireActivity().buildPersistentStore(initialState, factory)

        /**
         * Build a [Store] with the provided parameters which will live as long as this [AppCompatActivity], surviving
         * configuration changes and activity recreations until this [AppCompatActivity] is finished or until process
         * death.
         *
         * @param initialState The initial state until a dispatched [Action] creates a new state.
         * @param factory A function that receives [initialState] or the persisted state (if available) of the
         *   previously built [Store] of the same type allowing to build build a new [Store] instance to persist.
         *
         * This should be called only after this Activity is attached to the Application with access prior to that will
         * result in IllegalStateException.
         */
        @MainThread
        inline fun <reified S : State, A : Action, reified ST : Store<S, A>> ComponentActivity.activityStore(
            initialState: S,
            noinline factory: (S) -> ST,
        ): Lazy<ST> = buildPersistentStore(initialState, factory)

        /**
         * Build a [Store] with the provided parameters which will live as long as this the [Fragment] or
         * [AppCompatActivity] in which this composable is shown, surviving configuration changes and activity
         * recreations until this [AppCompatActivity] until the [Fragment] is removed from the backstack, the
         * [AppCompatActivity] is finished or until process death.
         *
         * The lifecycle of the created [Store] is managed by having it associated with the current
         * LocalViewModelStoreOwner and LocalLifecycleOwner available in the current composition.
         *
         * @param initialState The initial state until a dispatched [Action] creates a new state.
         * @param factory A function that receives [initialState] or the persisted state (if available) of the
         *   previously built [Store] of the same type allowing to build build a new [Store] instance to persist.
         */
        @Composable
        inline fun <reified S : State, A : Action, reified ST : Store<S, A>> composableStore(
            initialState: S,
            noinline factory: (S) -> ST,
        ): Lazy<ST> {
            val lifecycleOwner = LocalLifecycleOwner.current
            val viewModelStoreOwner =
                checkNotNull(LocalViewModelStoreOwner.current) {
                    "No ViewModelStoreOwner was provided via LocalViewModelStoreOwner"
                }

            return remember(viewModelStoreOwner) {
                ComposableStoreOwner(lifecycleOwner, viewModelStoreOwner).buildPersistentStore(initialState, factory)
            }
        }

        /**
         * Build a [Store] with the provided parameters which will live as long as this [NavBackStackEntry], which may
         * represent a single destination or a nested navigation graph allowing the store to survive configuration
         * changes and activity recreations until this [NavBackStackEntry] is removed from the navigation backstack or
         * until process death.
         *
         * @param initialState The initial state until a dispatched [Action] creates a new state.
         * @param factory A function that receives [initialState] or the persisted state (if available) of the
         *   previously built [Store] of the same type allowing to build build a new [Store] instance to persist.
         */
        @MainThread
        inline fun <reified S : State, A : Action, reified ST : Store<S, A>> NavBackStackEntry.navBackStackStore(
            initialState: S,
            noinline factory: (S) -> ST,
        ): Lazy<ST> = buildPersistentStore(initialState, factory)

        /**
         * Helper in building a new [Store] with the provided parameters that will have it's state persisted between
         * Activity recreations.
         *
         * If a [State] is already persisted this will be used instead of [initialState] for the new [Store].
         *
         * @param initialState The initial state until a dispatched [Action] creates a new state.
         * @param factory A function that receives [initialState] or the persisted state (if available) of the
         *   previously built [Store] of the same type allowing to build build a new [Store] instance to persist.
         */
        @Suppress("UNCHECKED_CAST")
        @MainThread
        @PublishedApi
        internal inline fun <reified S : State, A : Action, reified ST : Store<S, A>, O> O.buildPersistentStore(
            initialState: S,
            noinline factory: (S) -> ST,
        ): Lazy<ST> where O : LifecycleOwner, O : ViewModelStoreOwner {
            return lazy(mode = LazyThreadSafetyMode.NONE) {
                storeProvider.buildStore(initialState, factory)
            }
        }

        /**
         * [Lazy] returning the [Store] of a [Fragment], building a new one from the last state for each view of the
         * [Fragment]. Reads while the [Fragment] has no live view return the last [Store], if one was built.
         *
         * @param fragment The [Fragment] whose view lifecycle decides when a new [Store] is needed.
         * @param build Builds a new [Store], given the one it replaces, if there is one.
         */
        @PublishedApi
        internal class FragmentViewStoreLazy<ST : Store<*, *>>(
            private val fragment: Fragment,
            private val build: (previous: ST?) -> ST,
        ) : Lazy<ST> {
            private var store: ST? = null
            private var storeViewLifecycle: Lifecycle? = null

            override val value: ST
                get() {
                    val current = store
                    if (current != null && isCurrent()) {
                        return current
                    }

                    val viewLifecycle = viewLifecycleIfAlive()
                    if (current != null && viewLifecycle == null) {
                        return current
                    }

                    return build(current).also {
                        store = it
                        storeViewLifecycle = viewLifecycle
                    }
                }

            private fun isCurrent(): Boolean {
                val lifecycle = storeViewLifecycle
                return lifecycle != null && lifecycle.currentState != Lifecycle.State.DESTROYED
            }

            override fun isInitialized(): Boolean = store != null

            private fun viewLifecycleIfAlive(): Lifecycle? {
                return try {
                    fragment.viewLifecycleOwner.lifecycle.takeIf { it.currentState != Lifecycle.State.DESTROYED }
                } catch (_: IllegalStateException) {
                    null
                }
            }
        }

        @PublishedApi
        internal class DataPersisterFactory<T>(private val owner: T) : ViewModelProvider.Factory
            where T : LifecycleOwner {
            @Suppress("UNCHECKED_CAST")
            override fun <VM : ViewModel> create(modelClass: Class<VM>): VM {
                return StoreProvider(owner) as VM
            }
        }

        @PublishedApi
        internal class ComposableStoreOwner(
            lifecycleOwnerDelegate: LifecycleOwner,
            viewModelOwnerDelegate: ViewModelStoreOwner,
        ) : LifecycleOwner by lifecycleOwnerDelegate, ViewModelStoreOwner by viewModelOwnerDelegate
    }
}
