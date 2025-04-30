/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.search.storage

import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.appservices.search.RefinedSearchConfig
import mozilla.appservices.search.SearchApplicationName
import mozilla.appservices.search.SearchDeviceType
import mozilla.appservices.search.SearchEngineSelector
import mozilla.appservices.search.SearchUpdateChannel
import mozilla.components.browser.state.search.RegionState
import mozilla.components.feature.search.icons.SearchConfigIconsModel
import mozilla.components.feature.search.icons.SearchConfigIconsUpdateService
import mozilla.components.feature.search.middleware.SearchExtraParams
import mozilla.components.feature.search.middleware.SearchMiddleware
import mozilla.components.support.remotesettings.RemoteSettingsService
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.mockito.ArgumentMatchers.eq
import org.mockito.Mockito.doNothing
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`
import java.util.Locale
import kotlin.coroutines.CoroutineContext

class SearchEngineSelectorRepositoryTest {

    private lateinit var mockSelector: SearchEngineSelector
    private lateinit var mockService: RemoteSettingsService
    private lateinit var repository: SearchEngineSelectorRepository
    private lateinit var mockConfig: SearchEngineSelectorConfig
    private lateinit var searchConfigIconsUpdateService: SearchConfigIconsUpdateService

    @Before
    fun setUp() {
        mockSelector = mock<SearchEngineSelector>()
        mockService = mock<RemoteSettingsService>()
        searchConfigIconsUpdateService = mock<SearchConfigIconsUpdateService>()

        // Mocking SearchEngineSelectorConfig with a fake app configuration
        mockConfig = SearchEngineSelectorConfig(
            appName = SearchApplicationName.FIREFOX_ANDROID,
            appVersion = "1.0.0",
            deviceType = SearchDeviceType.SMARTPHONE,
            experiment = "test_experiment",
            updateChannel = SearchUpdateChannel.RELEASE,
            selector = mockSelector,
            service = mockService,
        )

        // Mock the useRemoteSettingsServer to avoid API calls
        doNothing().`when`(mockConfig.selector).useRemoteSettingsServer(service = any(), applyEngineOverrides = eq(false))

        // Instantiate the repository with the mocked config
        repository = SearchEngineSelectorRepository(mockConfig)
    }

    @Test
    fun `test repository initialization calls useRemoteSettingsServer`() {
        // Verify that useRemoteSettingsServer was called once with correct arguments
        verify(mockConfig.selector, times(1)).useRemoteSettingsServer(service = mockService.remoteSettingsService, applyEngineOverrides = false)
    }

    @Test
    fun `test load returns expected RefinedSearchConfig`() = runTest {
        val fakeRegion = RegionState("US", "US")
        val fakeLocale = Locale.US
        val fakeDistribution = "test_distribution"
        val fakeSearchExtraParams: SearchExtraParams? = null
        val fakeCoroutineContext: CoroutineContext = StandardTestDispatcher()

        val expectedBundle = SearchMiddleware.BundleStorage.Bundle(
            emptyList(),
            defaultSearchEngineId = "",
        )

        val expectedConfig = RefinedSearchConfig(
            emptyList(),
            appDefaultEngineId = null,
            appPrivateDefaultEngineId = null,
        ) // Fake response

        // Mock the filterEngineConfiguration to return our fake config
        `when`(mockSelector.filterEngineConfiguration(any())).thenReturn(expectedConfig)

        // mock the image loading
        `when`(searchConfigIconsUpdateService.fetchIcons(any())).thenReturn(emptyList<SearchConfigIconsModel>())

        // Run the repository load function
        val result = repository.load(fakeRegion, fakeLocale, fakeDistribution, fakeSearchExtraParams, fakeCoroutineContext)

        // Verify that filterEngineConfiguration was called once
        verify(mockSelector, times(1)).filterEngineConfiguration(any())

        // Assert that the returned configuration matches the expected one
        assertEquals(expectedBundle, result)
    }

    @Test
    fun `load handles null distribution`() = runTest {
        val fakeRegion = RegionState("US", "US")
        val fakeLocale = Locale.US
        val fakeDistribution = null
        val fakeSearchExtraParams: SearchExtraParams? = null
        val fakeCoroutineContext: CoroutineContext = StandardTestDispatcher()

        val expectedBundle = SearchMiddleware.BundleStorage.Bundle(
            emptyList(),
            defaultSearchEngineId = "",
        )

        val expectedConfig = RefinedSearchConfig(
            emptyList(),
            appDefaultEngineId = null,
            appPrivateDefaultEngineId = null,
        ) // Fake response

        // Mock the filterEngineConfiguration to return our fake config
        `when`(mockSelector.filterEngineConfiguration(any())).thenReturn(expectedConfig)

        // mock the image loading
        `when`(searchConfigIconsUpdateService.fetchIcons(any())).thenReturn(emptyList<SearchConfigIconsModel>())

        // Run the repository load function
        val result = repository.load(fakeRegion, fakeLocale, fakeDistribution, fakeSearchExtraParams, fakeCoroutineContext)

        // Verify that filterEngineConfiguration was called once
        verify(mockSelector, times(1)).filterEngineConfiguration(any())

        // Assert that the returned configuration matches the expected one
        assertEquals(expectedBundle, result)
    }
}
