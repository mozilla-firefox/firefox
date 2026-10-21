/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.search.storage

import java.util.Locale
import java.util.UUID
import java.util.concurrent.Executors
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.appservices.remotesettings.RemoteSettingsClient
import mozilla.appservices.search.RefinedSearchConfig
import mozilla.appservices.search.RefinedSearchConfigV3
import mozilla.appservices.search.SearchApplicationNameV3
import mozilla.appservices.search.SearchDeviceTypeV3
import mozilla.appservices.search.SearchEngineSelector
import mozilla.appservices.search.SearchUpdateChannelV3
import mozilla.appservices.search.SearchUserEnvironmentV3
import mozilla.components.browser.state.search.RegionState
import mozilla.components.feature.search.SearchApplicationName
import mozilla.components.feature.search.SearchDeviceType
import mozilla.components.feature.search.SearchUpdateChannel
import mozilla.components.feature.search.icons.SearchConfigIconsUpdateService
import mozilla.components.feature.search.middleware.SearchExtraParams
import mozilla.components.feature.search.middleware.SearchMiddleware
import mozilla.components.support.remotesettings.RemoteSettingsService
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.mockito.ArgumentMatchers.anyBoolean
import org.mockito.ArgumentMatchers.eq
import org.mockito.Mockito.doNothing
import org.mockito.Mockito.never
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`

class SearchEngineSelectorRepositoryTest {

    private lateinit var mockSelector: SearchEngineSelector
    private lateinit var mockService: RemoteSettingsService
    private lateinit var mockClient: RemoteSettingsClient
    private lateinit var mockConfig: SearchEngineSelectorConfig
    private lateinit var repository: SearchEngineSelectorRepository
    private lateinit var searchConfigIconsUpdateService: SearchConfigIconsUpdateService

    @Before
    fun setUp() {
        mockSelector = mock<SearchEngineSelector>()
        mockService = mock<RemoteSettingsService>()
        mockClient = mock<RemoteSettingsClient>()
        searchConfigIconsUpdateService = mock<SearchConfigIconsUpdateService>()

        // Mocking SearchEngineSelectorConfig with a fake app configuration
        mockConfig =
            SearchEngineSelectorConfig(
                appName = SearchApplicationName.FIREFOX_ANDROID,
                appVersion = "1.0.0",
                deviceType = SearchDeviceType.SMARTPHONE,
                experiment = "test_experiment",
                updateChannel = SearchUpdateChannel.RELEASE,
                service = mockService,
            )

        // Mock the useRemoteSettingsServer to avoid API calls
        doNothing().`when`(mockSelector).useRemoteSettingsServer(service = any(), applyEngineOverrides = eq(false))

        // Instantiate the repository with the mocked config
        repository =
            SearchEngineSelectorRepository(
                context = mock(),
                searchEngineSelectorConfig = mockConfig,
                defaultSearchEngineIcon = mock(),
                selector = mockSelector,
                client = mockClient,
                searchConfigIconsUpdateService = searchConfigIconsUpdateService,
            )
    }

    @Test
    fun `test repository initialization calls useRemoteSettingsServer`() {
        // Verify that useRemoteSettingsServer was called once with correct arguments
        verify(mockSelector, times(1))
            .useRemoteSettingsServer(service = mockService.remoteSettingsService, applyEngineOverrides = false)
    }

    @Test
    fun `test load returns expected RefinedSearchConfig`() = runTest {
        val fakeRegion = RegionState("US", "US")
        val fakeLocale = Locale.US
        val fakeDistribution = "test_distribution"
        val fakeSearchExtraParams: SearchExtraParams? = null
        val fakeCoroutineContext: CoroutineContext = coroutineContext

        val expectedBundle =
            SearchMiddleware.BundleStorage.Bundle(
                emptyList(),
                defaultSearchEngineId = "",
                searchEnvironmentId = null,
            )

        val expectedConfig =
            RefinedSearchConfig(
                emptyList(),
                appDefaultEngineId = null,
                appPrivateDefaultEngineId = null,
            ) // Fake response

        // Mock the filterEngineConfiguration to return our fake config
        `when`(mockSelector.filterEngineConfiguration(any())).thenReturn(expectedConfig)

        // mock the image loading
        `when`(searchConfigIconsUpdateService.fetchIconsRecords(any())).thenReturn(emptyList())

        // Run the repository load function
        val result =
            repository.load(fakeRegion, fakeLocale, fakeDistribution, fakeSearchExtraParams, fakeCoroutineContext)

        // Verify that filterEngineConfiguration was called once
        verify(mockSelector, times(1)).filterEngineConfiguration(any())

        // Verify that the search engine icons were fetched
        verify(searchConfigIconsUpdateService, times(1)).fetchIconsRecords(mockService)

        // Assert that the returned configuration matches the expected one
        assertEquals(expectedBundle, result)
    }

    @Test
    fun `load handles null distribution`() = runTest {
        val fakeRegion = RegionState("US", "US")
        val fakeLocale = Locale.US
        val fakeDistribution = null
        val fakeSearchExtraParams: SearchExtraParams? = null
        val fakeCoroutineContext: CoroutineContext = coroutineContext

        val expectedBundle =
            SearchMiddleware.BundleStorage.Bundle(
                emptyList(),
                defaultSearchEngineId = "",
                searchEnvironmentId = null,
            )

        val expectedConfig =
            RefinedSearchConfig(
                emptyList(),
                appDefaultEngineId = null,
                appPrivateDefaultEngineId = null,
            ) // Fake response

        // Mock the filterEngineConfiguration to return our fake config
        `when`(mockSelector.filterEngineConfiguration(any())).thenReturn(expectedConfig)

        // mock the image loading
        `when`(searchConfigIconsUpdateService.fetchIconsRecords(any())).thenReturn(emptyList())

        // Run the repository load function
        val result =
            repository.load(fakeRegion, fakeLocale, fakeDistribution, fakeSearchExtraParams, fakeCoroutineContext)

        // Verify that filterEngineConfiguration was called once
        verify(mockSelector, times(1)).filterEngineConfiguration(any())

        // Assert that the returned configuration matches the expected one
        assertEquals(expectedBundle, result)
    }

    @Test
    fun `load uses provided coroutine context`() = runTest {
        val fakeRegion = RegionState("US", "US")
        val fakeLocale = Locale.US
        val fakeDistribution = null
        val threadName = "SearchEngineSelectorRepositoryTest-${UUID.randomUUID()}"
        val dispatcher =
            Executors.newSingleThreadExecutor { runnable ->
                    Thread(runnable, threadName)
                }
                .asCoroutineDispatcher()
        var observedThreadName: String? = null

        try {
            `when`(mockSelector.filterEngineConfiguration(any())).thenAnswer {
                observedThreadName = Thread.currentThread().name
                RefinedSearchConfig(
                    emptyList(),
                    appDefaultEngineId = null,
                    appPrivateDefaultEngineId = null,
                )
            }

            repository.load(fakeRegion, fakeLocale, fakeDistribution, null, dispatcher)

            assertEquals(
                threadName,
                observedThreadName,
            )
        } finally {
            dispatcher.close()
        }
    }

    @Test
    fun `GIVEN search-config-v3 is enabled WHEN the repository is created THEN useRemoteSettingsServerV3 is called`() {
        val selector = mock<SearchEngineSelector>()

        buildV3Repository(selector)

        verify(selector, times(1)).useRemoteSettingsServerV3(service = mockService.remoteSettingsService)
        verify(selector, never()).useRemoteSettingsServer(service = any(), applyEngineOverrides = anyBoolean())
    }

    @Test
    fun `GIVEN search-config-v3 is enabled WHEN load is called THEN filterEngineConfigurationV3 is called`() = runTest {
        val selector = mock<SearchEngineSelector>()
        val v3Repository = buildV3Repository(selector)

        `when`(selector.filterEngineConfigurationV3(any()))
            .thenReturn(
                RefinedSearchConfigV3(
                    emptyList(),
                    appDefaultEngineId = null,
                    appPrivateDefaultEngineId = null,
                )
            )

        val result = v3Repository.load(RegionState("US", "US"), Locale.US, null, null, coroutineContext)

        verify(selector, times(1)).filterEngineConfigurationV3(any())
        verify(selector, never()).filterEngineConfiguration(any())
        verify(searchConfigIconsUpdateService, times(1)).fetchIconsRecords(mockService)
        assertEquals(
            SearchMiddleware.BundleStorage.Bundle(
                emptyList(),
                defaultSearchEngineId = "",
                searchEnvironmentId = null,
            ),
            result,
        )
    }

    @Test
    fun `GIVEN search-config-v3 is enabled THEN the environment id is computed from the v3 environment`() {
        val selector = mock<SearchEngineSelector>()
        val v3Repository = buildV3Repository(selector)

        val expected =
            SearchUserEnvironmentV3(
                locale = "en-US",
                region = "US",
                experiment = "test_experiment",
                version = "1.0.0",
                updateChannel = SearchUpdateChannelV3.RELEASE,
                distributionId = "test_distribution",
                appName = SearchApplicationNameV3.FIREFOX_ANDROID,
                deviceType = SearchDeviceTypeV3.SMARTPHONE,
            )

        assertEquals(
            expected.hashCode(),
            v3Repository.computeNewSearchEnvironmentId(RegionState("US", "US"), Locale.US, "test_distribution"),
        )
    }

    private fun buildV3Repository(selector: SearchEngineSelector): SearchEngineSelectorRepository =
        SearchEngineSelectorRepository(
            context = mock(),
            searchEngineSelectorConfig = mockConfig.copy(useSearchConfigV3 = true),
            defaultSearchEngineIcon = mock(),
            selector = selector,
            client = mockClient,
            searchConfigIconsUpdateService = searchConfigIconsUpdateService,
        )
}
