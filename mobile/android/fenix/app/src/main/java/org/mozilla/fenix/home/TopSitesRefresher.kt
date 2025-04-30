/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import mozilla.components.feature.top.sites.TopSitesProvider
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.utils.Settings

/**
 * Class to refresh the top sites using the [TopSitesProvider]
 *
 * @param settings Fenix [Settings]
 * @param topSitesProvider [TopSitesProvider] to refresh top sites
 * @param dispatcher [CoroutineDispatcher] to use launch the refresh job.
 * Default value is [Dispatchers.IO]. It is helpful to improve testability
 */
class TopSitesRefresher(
    private val settings: Settings,
    private val topSitesProvider: TopSitesProvider,
    private val dispatcher: CoroutineDispatcher = Dispatchers.IO,
) : DefaultLifecycleObserver {

    private val logger = Logger("TopSitesRefresher")
    private val scope = CoroutineScope(dispatcher)

    override fun onResume(owner: LifecycleOwner) {
        scope.launch(dispatcher) {
            runCatching {
                if (settings.showContileFeature) {
                    topSitesProvider.refreshTopSitesIfCacheExpired()
                }
            }.onFailure { exception ->
                logger.error("Failed to refresh contile top sites", exception)
            }
        }
    }

    override fun onPause(owner: LifecycleOwner) {
        scope.cancel()
    }
}
