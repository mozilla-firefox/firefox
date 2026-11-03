/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.top.sites.presenter

import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.feature.top.sites.TopSitesConfig
import mozilla.components.feature.top.sites.TopSitesStorage
import mozilla.components.feature.top.sites.view.TopSitesView

/**
 * Default implementation of [TopSitesPresenter]. Connects the [TopSitesView] with the [TopSitesStorage] to update the
 * view whenever the storage is updated.
 *
 * @param view An implementor of [TopSitesView] that will be notified of changes to the storage.
 * @param storage The top sites storage that stores pinned and frecent sites.
 * @param config Lambda expression that returns [TopSitesConfig] which species the number of top sites to return and
 *   whether or not to include frequently visited sites.
 */
class DefaultTopSitesPresenter(
    override val view: TopSitesView,
    override val storage: TopSitesStorage,
    private val config: () -> TopSitesConfig,
    coroutineContext: CoroutineContext = Dispatchers.IO,
) : TopSitesPresenter, TopSitesStorage.Observer {

    private val scope = CoroutineScope(coroutineContext)
    private var job: Job? = null

    override fun start() {
        onStorageUpdated()

        storage.register(this)
    }

    override fun stop() {
        storage.unregister(this)
        job?.cancel()
    }

    override fun onStorageUpdated() {
        val innerConfig = config.invoke()

        // Cancel any existing job before starting a new one since onStorageUpdated can be called
        // multiple times in quick succession. Ensure only the latest job is running.
        job?.cancel()

        job = scope.launch {
            val topSites =
                storage.getTopSites(
                    targetSitesCount = innerConfig.targetSitesCount,
                    frecencyConfig = innerConfig.frecencyConfig,
                    providerConfig = innerConfig.providerConfig,
                )

            withContext(Dispatchers.Main) {
                view.displayTopSites(topSites)
            }
        }
    }
}
