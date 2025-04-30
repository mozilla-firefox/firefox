/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.deletebrowsingdata

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.RecentlyClosedAction
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.translate.ModelManagementOptions
import mozilla.components.concept.engine.translate.ModelOperation
import mozilla.components.concept.engine.translate.OperationLevel
import mozilla.components.concept.storage.HistoryStorage
import mozilla.components.feature.downloads.DownloadsUseCases
import mozilla.components.feature.tabs.TabsUseCases
import org.mozilla.fenix.components.PermissionStorage
import kotlin.coroutines.CoroutineContext

interface DeleteBrowsingDataController {
    suspend fun deleteTabs()
    suspend fun deleteBrowsingHistory()
    suspend fun deleteCookiesAndSiteData()
    suspend fun deleteCachedFiles()
    suspend fun deleteSitePermissions()
    suspend fun deleteDownloads()
}

@Suppress("LongParameterList")
class DefaultDeleteBrowsingDataController(
    private val removeAllTabs: TabsUseCases.RemoveAllTabsUseCase,
    private val removeAllDownloads: DownloadsUseCases.RemoveAllDownloadsUseCase,
    private val historyStorage: HistoryStorage,
    private val permissionStorage: PermissionStorage,
    private val store: BrowserStore,
    private val engine: Engine,
    private val coroutineContext: CoroutineContext = Dispatchers.Main,
) : DeleteBrowsingDataController {

    override suspend fun deleteTabs() {
        withContext(coroutineContext) {
            removeAllTabs.invoke(false)
        }
    }

    override suspend fun deleteBrowsingHistory() {
        withContext(coroutineContext) {
            historyStorage.deleteEverything()
            store.dispatch(EngineAction.PurgeHistoryAction)
            store.dispatch(RecentlyClosedAction.RemoveAllClosedTabAction)
        }
    }

    override suspend fun deleteCookiesAndSiteData() {
        withContext(coroutineContext) {
            engine.clearData(
                Engine.BrowsingData.select(
                    Engine.BrowsingData.COOKIES,
                    Engine.BrowsingData.AUTH_SESSIONS,
                ),
            )
            engine.clearData(Engine.BrowsingData.select(Engine.BrowsingData.DOM_STORAGES))
        }
    }

    override suspend fun deleteCachedFiles() {
        withContext(coroutineContext) {
            engine.manageTranslationsLanguageModel(
                options = ModelManagementOptions(
                    operation = ModelOperation.DELETE,
                    operationLevel = OperationLevel.CACHE,
                ),
                onSuccess = { },
                onError = { },
            )
            engine.clearData(
                Engine.BrowsingData.select(Engine.BrowsingData.ALL_CACHES),
            )
        }
    }

    override suspend fun deleteSitePermissions() {
        withContext(coroutineContext) {
            engine.clearData(
                Engine.BrowsingData.select(Engine.BrowsingData.ALL_SITE_SETTINGS),
            )
        }
        permissionStorage.deleteAllSitePermissions()
    }

    override suspend fun deleteDownloads() {
        withContext(coroutineContext) {
            removeAllDownloads.invoke()
        }
    }
}
