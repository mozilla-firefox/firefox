/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import androidx.annotation.MainThread
import androidx.annotation.VisibleForTesting
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.ViewModelStoreOwner
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.CoroutineScope
import mozilla.components.lib.state.Store

/**
 * Generic ViewModel wrapper of a [Store] helping to persist it across process/activity recreations.
 *
 * @param createStore [Store] factory receiving also the [ViewModel.viewModelScope] associated with this [ViewModel].
 */
class StoreProvider<T : Store<*, *>>(
    createStore: (CoroutineScope) -> T,
) : ViewModel() {

    @VisibleForTesting
    @PublishedApi
    internal val store: T = createStore(viewModelScope)

    companion object {
        /**
         * Returns an existing [Store] instance or creates a new one scoped to a [ViewModelStoreOwner].
         *
         * @see [ViewModelProvider.get].
         */
        inline fun <reified T : Store<*, *>> get(
            owner: ViewModelStoreOwner,
            noinline createStore: (CoroutineScope) -> T,
        ): T {
            val factory = StoreProviderFactory(createStore)
            val viewModel: StoreProvider<*> =
                ViewModelProvider(owner, factory).get(T::class.java.name, StoreProvider::class.java)
            return viewModel.store as T
        }
    }
}

/**
 * [ViewModel] factory to create [StoreProvider] instances that will wrap a [Store] instance
 * helping to persist it across process/activity recreations.
 *
 * @param createStore [Store] factory receiving also the [ViewModel.viewModelScope] associated with this [ViewModel].
 */
@VisibleForTesting
class StoreProviderFactory<T : Store<*, *>>(
    private val createStore: (CoroutineScope) -> T,
) : ViewModelProvider.Factory {

    @Suppress("UNCHECKED_CAST")
    override fun <VM : ViewModel> create(modelClass: Class<VM>): VM {
        return StoreProvider(createStore) as VM
    }
}

/**
 * Helper function for lazy creation of a [Store] instance scoped to a [ViewModelStoreOwner].
 *
 * @param createStore [Store] factory receiving also the [ViewModel.viewModelScope] associated with this [ViewModel].
 *
 * Example:
 * ```
 * val store by lazy { scope ->
 *   MyStore(
 *     middleware = listOf(
 *       MyMiddleware(
 *         settings = requireComponents.settings,
 *         ...
 *         scope = scope,
 *       ),
 *     )
 *   )
 * }
 */
@MainThread
inline fun <reified T : Store<*, *>> ViewModelStoreOwner.lazyStore(
    noinline createStore: (CoroutineScope) -> T,
): Lazy<T> {
    return lazy(mode = LazyThreadSafetyMode.NONE) {
        StoreProvider.get(this, createStore)
    }
}
