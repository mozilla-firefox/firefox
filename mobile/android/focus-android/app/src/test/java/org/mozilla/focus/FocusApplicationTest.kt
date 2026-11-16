/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus

import android.os.UserManager
import androidx.test.core.app.ApplicationProvider
import androidx.work.testing.WorkManagerTestInitHelper
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SearchState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertThrows
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.focus.utils.Settings
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class FocusApplicationTest {

    private val application: FocusApplication = ApplicationProvider.getApplicationContext()

    @Test
    fun `GIVEN the user is locked WHEN WorkManager initializes THEN it does not throw`() {
        shadowOf(application.getSystemService(UserManager::class.java)).setUserUnlocked(false)

        WorkManagerTestInitHelper.initializeTestWorkManager(application, application.workManagerConfiguration)
        WorkManagerTestInitHelper.closeWorkDatabase()
    }

    @Test
    fun `GIVEN the user is unlocked WHEN WorkManager fails to initialize THEN the error is rethrown`() {
        val handler = application.workManagerConfiguration.initializationExceptionHandler!!

        assertThrows(IllegalStateException::class.java) { handler.accept(IllegalStateException()) }
    }

    @Test
    fun `WHEN the search configuration is updated in remote settings THEN set a new search configuration being available`() {
        val browserStore = BrowserStore(BrowserState(search = SearchState(isNewSearchConfigurationAvailable = false)))

        application.setupRefreshingSearchEngines(listOf("search-config-v2"), browserStore = browserStore)

        assertTrue(browserStore.state.search.isNewSearchConfigurationAvailable)
    }

    @Test
    fun `WHEN the search configuration overrides are updated in remote settings THEN set a new search configuration being available`() {
        val browserStore = BrowserStore(BrowserState(search = SearchState(isNewSearchConfigurationAvailable = false)))

        application.setupRefreshingSearchEngines(listOf("search-config-overrides-v2"), browserStore = browserStore)

        assertTrue(browserStore.state.search.isNewSearchConfigurationAvailable)
    }

    @Test
    fun `WHEN the search engine icons are updated in remote settings THEN set a new search configuration being available`() {
        val browserStore = BrowserStore(BrowserState(search = SearchState(isNewSearchConfigurationAvailable = false)))

        application.setupRefreshingSearchEngines(listOf("search-config-icons"), browserStore = browserStore)

        assertTrue(browserStore.state.search.isNewSearchConfigurationAvailable)
    }

    @Test
    fun `GIVEN search config v3 is enabled WHEN the search engines are refreshed THEN set a new search configuration being available`() {
        val settings: Settings = mock()
        whenever(settings.useSearchConfigV3).thenReturn(true)
        val browserStore = BrowserStore(BrowserState(search = SearchState(isNewSearchConfigurationAvailable = false)))

        application.setupRefreshingSearchEngines(listOf("search-config-v3"), settings, browserStore)

        assertTrue(browserStore.state.search.isNewSearchConfigurationAvailable)
    }

    @Test
    fun `GIVEN search config v3 is disabled WHEN the search engines are refreshed THEN do not set a new search configuration being available`() {
        val settings: Settings = mock()
        whenever(settings.useSearchConfigV3).thenReturn(false)
        val browserStore = BrowserStore(BrowserState(search = SearchState(isNewSearchConfigurationAvailable = false)))

        application.setupRefreshingSearchEngines(listOf("search-config-v3"), settings, browserStore)

        assertFalse(browserStore.state.search.isNewSearchConfigurationAvailable)
    }
}
