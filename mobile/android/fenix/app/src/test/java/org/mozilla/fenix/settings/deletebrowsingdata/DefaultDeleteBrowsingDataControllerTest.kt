/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.deletebrowsingdata

import io.mockk.coVerify
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import kotlinx.coroutines.DelicateCoroutinesApi
import kotlinx.coroutines.ExperimentalCoroutinesApi
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
import mozilla.components.support.test.rule.MainCoroutineRule
import mozilla.components.support.test.rule.runTestOnMain
import org.junit.Before
import org.junit.Ignore
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.components.PermissionStorage

@OptIn(ExperimentalCoroutinesApi::class)
class DefaultDeleteBrowsingDataControllerTest {

    @get:Rule
    val coroutinesTestRule = MainCoroutineRule()

    private var removeAllTabs: TabsUseCases.RemoveAllTabsUseCase = mockk(relaxed = true)
    private var removeAllDownloads: DownloadsUseCases.RemoveAllDownloadsUseCase = mockk(relaxed = true)
    private var historyStorage: HistoryStorage = mockk(relaxed = true)
    private var permissionStorage: PermissionStorage = mockk(relaxed = true)
    private var store: BrowserStore = mockk(relaxed = true)
    private val engine: Engine = mockk(relaxed = true)
    private lateinit var controller: DefaultDeleteBrowsingDataController

    @Before
    @OptIn(DelicateCoroutinesApi::class) // coroutineContext usage
    fun setup() {
        controller = DefaultDeleteBrowsingDataController(
            removeAllTabs = removeAllTabs,
            removeAllDownloads = removeAllDownloads,
            historyStorage = historyStorage,
            store = store,
            permissionStorage = permissionStorage,
            engine = engine,
            coroutineContext = coroutinesTestRule.testDispatcher,
        )
    }

    @Test
    fun deleteTabs() = runTestOnMain {
        controller.deleteTabs()

        verify {
            removeAllTabs.invoke(false)
        }
    }

    @Test
    fun deleteBrowsingHistory() = runTestOnMain {
        controller = spyk(controller)
        controller.deleteBrowsingHistory()

        coVerify {
            historyStorage.deleteEverything()
            store.dispatch(EngineAction.PurgeHistoryAction)
            store.dispatch(RecentlyClosedAction.RemoveAllClosedTabAction)
        }
    }

    @Test
    fun deleteCookiesAndSiteData() = runTestOnMain {
        controller.deleteCookiesAndSiteData()

        verify {
            engine.clearData(
                Engine.BrowsingData.select(
                    Engine.BrowsingData.COOKIES,
                    Engine.BrowsingData.AUTH_SESSIONS,
                ),
            )
            engine.clearData(Engine.BrowsingData.select(Engine.BrowsingData.DOM_STORAGES))
        }
    }

    @Ignore("Disabled: Fails if new tests are added: https://bugzilla.mozilla.org/show_bug.cgi?id=1956618")
    @Test
    fun deleteCachedFiles() = runTestOnMain {
        controller.deleteCachedFiles()

        verify {
            engine.manageTranslationsLanguageModel(
                options = ModelManagementOptions(
                    operation = ModelOperation.DELETE,
                    operationLevel = OperationLevel.CACHE,
                ),
                onSuccess = any(),
                onError = any(),
            )
            engine.clearData(Engine.BrowsingData.select(Engine.BrowsingData.ALL_CACHES))
        }
    }

    @Test
    fun deleteSitePermissions() = runTestOnMain {
        controller.deleteSitePermissions()

        coVerify {
            engine.clearData(Engine.BrowsingData.select(Engine.BrowsingData.ALL_SITE_SETTINGS))
            permissionStorage.deleteAllSitePermissions()
        }
    }

    @Test
    fun deleteDownloads() = runTestOnMain {
        controller.deleteDownloads()

        verify {
            removeAllDownloads.invoke()
        }
    }
}
