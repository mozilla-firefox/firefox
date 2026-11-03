/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites.middleware

import androidx.annotation.VisibleForTesting
import androidx.core.net.toUri
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.feature.top.sites.TopSitesUseCases
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.lib.state.ext.flow
import mozilla.components.service.merino.manifest.MerinoManifestProvider
import mozilla.components.support.ktx.android.net.hostWithoutCommonPrefixes
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.ShortcutAction
import org.mozilla.fenix.home.topsites.AddShortcutEntryPoint
import org.mozilla.fenix.home.topsites.AddShortcutSource
import org.mozilla.fenix.home.topsites.store.ShortcutsAction
import org.mozilla.fenix.home.topsites.store.ShortcutsState
import org.mozilla.fenix.home.topsites.store.ShortcutsStore
import org.mozilla.fenix.home.topsites.store.toPopularSite

@VisibleForTesting internal const val POPULAR_SITES_LIMIT = 8

/**
 * [Middleware] implementation for handling [ShortcutsAction] and managing the [ShortcutsState] for the shortcuts
 * screen.
 *
 * @param appStore The [AppStore] to observe for top site updates and dispatching actions.
 * @param topSitesUseCases The [TopSitesUseCases] used to persist new pinned shortcuts.
 * @param merinoManifestProvider The [MerinoManifestProvider] used to read popular site suggestions.
 * @param scope The lifecycle-aware [CoroutineScope] used to launch coroutines. The consumer is responsible for
 *   providing a scope that gets canceled when the consuming component is destroyed to avoid leaking the
 *   [ShortcutsStore].
 * @param ioDispatcher [CoroutineDispatcher] used for the IO operations.
 */
class ShortcutsMiddleware(
    private val appStore: AppStore,
    private val topSitesUseCases: TopSitesUseCases,
    private val merinoManifestProvider: MerinoManifestProvider,
    private val scope: CoroutineScope,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : Middleware<ShortcutsState, ShortcutsAction> {

    override fun invoke(
        store: Store<ShortcutsState, ShortcutsAction>,
        next: (ShortcutsAction) -> Unit,
        action: ShortcutsAction,
    ) {
        when (action) {
            is ShortcutsAction.InitAction -> initialize(store = store)

            is ShortcutsAction.ShowAddShortcutBottomSheet -> {
                appStore.dispatch(
                    ShortcutAction.AddShortcutSheetShown(entryPoint = AddShortcutEntryPoint.SHORTCUTS_LIBRARY)
                )
            }

            is ShortcutsAction.ShowAddShortcutDialog -> {
                appStore.dispatch(ShortcutAction.AddWebsiteDialogShown)
            }

            is ShortcutsAction.SaveShortcut -> {
                saveShortcut(
                    store = store,
                    title = action.title,
                    url = action.url,
                    source = action.source,
                )
            }

            else -> Unit
        }

        next(action)
    }

    private fun initialize(store: Store<ShortcutsState, ShortcutsAction>) {
        scope.launch {
            appStore
                .flow()
                .map { it.topSites }
                .distinctUntilChanged()
                .collect { topSites ->
                    store.dispatch(ShortcutsAction.UpdateTopSites(topSites))

                    val popularSites =
                        withContext(ioDispatcher) {
                            merinoManifestProvider
                                .getTopDomains(
                                    limit = POPULAR_SITES_LIMIT,
                                    excludedDomains =
                                        topSites.mapNotNullTo(mutableSetOf()) {
                                            it.url.toUri().hostWithoutCommonPrefixes
                                        },
                                )
                                .map { it.toPopularSite() }
                        }
                    store.dispatch(ShortcutsAction.UpdatePopularSites(popularSites))
                }
        }
    }

    private fun saveShortcut(
        store: Store<ShortcutsState, ShortcutsAction>,
        title: String,
        url: String,
        source: AddShortcutSource,
    ) = scope.launch {
        topSitesUseCases.addPinnedSites(title = title, url = url)
        appStore.dispatch(
            ShortcutAction.ShortcutAdded(
                url = url,
                source = source,
                entryPoint = AddShortcutEntryPoint.SHORTCUTS_LIBRARY,
            )
        )
        store.dispatch(ShortcutsAction.CloseDialog)
    }
}
