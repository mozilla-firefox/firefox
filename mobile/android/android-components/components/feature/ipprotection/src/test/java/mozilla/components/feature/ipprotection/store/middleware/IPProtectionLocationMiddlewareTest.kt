/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ipprotection.store.middleware

import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds
import kotlinx.coroutines.delay
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler
import mozilla.components.feature.ipprotection.storage.IPProtectionLocationRepository
import mozilla.components.feature.ipprotection.storage.IPProtectionTestMiddleware
import mozilla.components.feature.ipprotection.store.CachedLocationStatus
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.AccountState
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.Country
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.Location
import mozilla.components.feature.ipprotection.store.state.LocationState
import mozilla.components.feature.ipprotection.store.state.ProxyStatus
import mozilla.components.feature.ipprotection.store.state.Recommended
import mozilla.components.feature.ipprotection.store.state.Uninitialized
import mozilla.components.lib.state.Middleware
import org.junit.Assert
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalAndroidComponentsApi::class)
class IPProtectionLocationMiddlewareTest {

    private lateinit var fakeRepository: FakeIPProtectionLocationRepository
    private lateinit var middleware: IPProtectionLocationMiddleware
    private val scope = TestScope(StandardTestDispatcher())

    @Before
    fun setUp() {
        fakeRepository = FakeIPProtectionLocationRepository()
        middleware = IPProtectionLocationMiddleware(fakeRepository, scope)
    }

    @Test
    fun `WHEN location changes THEN the selected location code is saved in the repository`() = scope.runTest {
        listOf(
                Recommended,
                Country(countryCode = "JP", available = true),
                Country(countryCode = "CA", available = true),
            )
            .forEach { location ->
                val fakeRepository = FakeIPProtectionLocationRepository()
                val store =
                    buildStore(
                        middleware =
                            listOf(
                                IPProtectionLocationMiddleware(
                                    repository = fakeRepository,
                                    coroutineScope = scope,
                                )
                            )
                    )

                Assert.assertEquals(null, fakeRepository.getSelectedLocationCode())
                Assert.assertEquals(null, store.state.locationState.selectedLocation.countryCode)

                store.dispatch(IPProtectionAction.LocationAction.LocationChanged(location, userAction = true))
                testScheduler.advanceUntilIdle()

                Assert.assertEquals(location.countryCode, fakeRepository.getSelectedLocationCode())
                Assert.assertEquals(location.countryCode, store.state.locationState.selectedLocation.countryCode)
            }
    }

    // simulates the start of the app when a country list update contains the previously cached code
    @Test
    fun `GIVEN a country list update contains a country with a code that matches the cached code WHEN country list updates THEN selected location equals the matching country`() =
        scope.runTest {
            val cachedLocationCode = "JP"
            val store = buildStore(locationState = LocationState(), middleware = listOf(middleware))
            fakeRepository.setSelectedLocationCode(cachedLocationCode)

            Assert.assertFalse(cachedLocationCode == store.state.locationState.selectedLocation.countryCode)

            store.dispatch(
                IPProtectionAction.LocationAction.CountryListChanged(
                    countries =
                        listOf(
                            IPProtectionHandler.Country(code = cachedLocationCode, available = true),
                            IPProtectionHandler.Country(code = "CA", available = true),
                            IPProtectionHandler.Country(code = "GB", available = true),
                            IPProtectionHandler.Country(code = "FR", available = true),
                        )
                )
            )
            testScheduler.advanceUntilIdle()

            Assert.assertEquals(cachedLocationCode, store.state.locationState.selectedLocation.countryCode)
        }

    // simulates the start of the app when a country list does not contain the previously cached code
    @Test
    fun `GIVEN default location state AND a country list update does not contain the selected country WHEN country list updates THEN cache is reset and the selected location is Recommended`() =
        scope.runTest {
            val cachedLocationCode = "JP"
            val store = buildStore(locationState = LocationState(), middleware = listOf(middleware))
            fakeRepository.setSelectedLocationCode(cachedLocationCode)

            store.dispatch(
                IPProtectionAction.LocationAction.CountryListChanged(
                    countries =
                        listOf(
                            IPProtectionHandler.Country(code = "FN", available = true),
                            IPProtectionHandler.Country(code = "CA", available = true),
                            IPProtectionHandler.Country(code = "GB", available = true),
                            IPProtectionHandler.Country(code = "FR", available = true),
                        )
                )
            )

            testScheduler.advanceUntilIdle()

            Assert.assertEquals(Recommended, store.state.locationState.selectedLocation)
            Assert.assertEquals(null, fakeRepository.getSelectedLocationCode())
        }

    // simulates a country list update after the app has been running for a bit
    @Test
    fun `GIVEN populated location state AND a country list update does not contain the selected country WHEN country list updates THEN cache is reset and selected country resets to default`() =
        scope.runTest {
            val cachedLocationCode = "JP"
            val selectedCountry = Country(countryCode = cachedLocationCode, available = true)
            val store =
                buildStore(
                    selectedLocation = selectedCountry,
                    locations =
                        listOf(
                            selectedCountry,
                            Country(countryCode = "CA", available = true),
                            Country(countryCode = "GB", available = true),
                            Country(countryCode = "FR", available = true),
                        ),
                    middleware = listOf(middleware),
                )
            fakeRepository.setSelectedLocationCode(cachedLocationCode)

            Assert.assertEquals(selectedCountry, store.state.locationState.selectedLocation)
            Assert.assertEquals(cachedLocationCode, fakeRepository.getSelectedLocationCode())

            store.dispatch(
                IPProtectionAction.LocationAction.CountryListChanged(
                    countries =
                        listOf(
                            IPProtectionHandler.Country(code = "CA", available = true),
                            IPProtectionHandler.Country(code = "GB", available = true),
                            IPProtectionHandler.Country(code = "FR", available = true),
                        )
                )
            )
            testScheduler.advanceUntilIdle()

            Assert.assertEquals(Recommended, store.state.locationState.selectedLocation)
            Assert.assertEquals(null, fakeRepository.getSelectedLocationCode())
        }

    // simulates a country list update after the app has been running for a bit
    @Test
    fun `GIVEN populated location state AND the previously selected country turns unavailable WHEN country list updates THEN cache is reset and selected country resets to default`() =
        scope.runTest {
            val cachedLocationCode = "JP"
            val selectedCountry = Country(countryCode = cachedLocationCode, available = true)
            val store =
                buildStore(
                    selectedLocation = selectedCountry,
                    locations =
                        listOf(
                            selectedCountry,
                            Country(countryCode = "CA", available = true),
                            Country(countryCode = "GB", available = true),
                            Country(countryCode = "FR", available = true),
                        ),
                    middleware = listOf(middleware),
                )
            fakeRepository.setSelectedLocationCode(cachedLocationCode)

            Assert.assertEquals(selectedCountry, store.state.locationState.selectedLocation)
            Assert.assertEquals(cachedLocationCode, fakeRepository.getSelectedLocationCode())

            store.dispatch(
                IPProtectionAction.LocationAction.CountryListChanged(
                    countries =
                        listOf(
                            IPProtectionHandler.Country(code = cachedLocationCode, available = false),
                            IPProtectionHandler.Country(code = "CA", available = true),
                            IPProtectionHandler.Country(code = "GB", available = true),
                            IPProtectionHandler.Country(code = "FR", available = true),
                        )
                )
            )
            testScheduler.advanceUntilIdle()

            Assert.assertEquals(Recommended, store.state.locationState.selectedLocation)
            Assert.assertEquals(null, fakeRepository.getSelectedLocationCode())
        }

    // simulates the start of the app when a country list update contains the previously cached location but the
    // location is no longer available
    @Test
    fun `GIVEN default location state AND the previously selected country turns unavailable WHEN country list updates THEN cache is reset and the selected location remains Recommended`() =
        scope.runTest {
            val cachedLocationCode = "JP"
            val store = buildStore(locationState = LocationState(), middleware = listOf(middleware))
            fakeRepository.setSelectedLocationCode(cachedLocationCode)

            Assert.assertFalse(cachedLocationCode == store.state.locationState.selectedLocation.countryCode)

            store.dispatch(
                IPProtectionAction.LocationAction.CountryListChanged(
                    countries =
                        listOf(
                            IPProtectionHandler.Country(code = cachedLocationCode, available = false),
                            IPProtectionHandler.Country(code = "CA", available = true),
                            IPProtectionHandler.Country(code = "GB", available = true),
                            IPProtectionHandler.Country(code = "FR", available = true),
                        )
                )
            )
            testScheduler.advanceUntilIdle()

            Assert.assertEquals(Recommended, store.state.locationState.selectedLocation)
            Assert.assertEquals(null, fakeRepository.getSelectedLocationCode())
        }

    @Test
    fun `GIVEN a cached selected location WHEN a user logs out THEN the cache is retained`() = scope.runTest {
        val cachedLocationCode = "JP"
        val selectedCountry = Country(countryCode = cachedLocationCode, available = true)
        val store =
            buildStore(
                accountState = AccountState(AccountStatus.EnrolledAndEntitled),
                selectedLocation = selectedCountry,
                locations =
                    listOf(
                        selectedCountry,
                        Country(countryCode = "CA", available = true),
                        Country(countryCode = "GB", available = true),
                        Country(countryCode = "FR", available = true),
                    ),
                middleware = listOf(middleware),
            )
        fakeRepository.setSelectedLocationCode(cachedLocationCode)

        Assert.assertEquals(selectedCountry, store.state.locationState.selectedLocation)
        Assert.assertEquals(cachedLocationCode, fakeRepository.getSelectedLocationCode())

        store.dispatch(IPProtectionAction.AccountAction.AccountManagerStateChanged(status = AccountStatus.NoAccount))
        testScheduler.advanceUntilIdle()

        Assert.assertEquals(cachedLocationCode, fakeRepository.getSelectedLocationCode())
    }

    // a country list push can arrive at any time, including while the proxy is running
    @Test
    fun `GIVEN the cached selection is already selected AND the proxy is active WHEN country list updates THEN no location switch is requested`() =
        scope.runTest {
            val cachedLocationCode = "JP"
            val selectedCountry = Country(countryCode = cachedLocationCode, available = true)
            val captureMiddleware = IPProtectionTestMiddleware()
            val store =
                buildStore(
                    selectedLocation = selectedCountry,
                    locations =
                        listOf(
                            selectedCountry,
                            Country(countryCode = "CA", available = true),
                        ),
                    proxyStatus = Authorized.Active,
                    middleware = listOf(middleware, captureMiddleware),
                )
            fakeRepository.setSelectedLocationCode(cachedLocationCode)

            store.dispatch(
                IPProtectionAction.LocationAction.CountryListChanged(
                    countries =
                        listOf(
                            IPProtectionHandler.Country(code = cachedLocationCode, available = true),
                            IPProtectionHandler.Country(code = "CA", available = true),
                        )
                )
            )
            testScheduler.advanceUntilIdle()

            captureMiddleware.assertNotDispatched(IPProtectionAction.LocationAction.LocationChanged::class)
            Assert.assertEquals(selectedCountry, store.state.locationState.selectedLocation)
        }

    @Test
    fun `GIVEN a cached selection is still available WHEN country list updates THEN the restore is not marked as a user action`() =
        scope.runTest {
            val cachedLocationCode = "JP"
            val captureMiddleware = IPProtectionTestMiddleware()
            val store = buildStore(middleware = listOf(middleware, captureMiddleware))
            fakeRepository.setSelectedLocationCode(cachedLocationCode)

            store.dispatch(
                IPProtectionAction.LocationAction.CountryListChanged(
                    countries = listOf(IPProtectionHandler.Country(code = cachedLocationCode, available = true))
                )
            )
            testScheduler.advanceUntilIdle()

            captureMiddleware.assertLastAction(IPProtectionAction.LocationAction.LocationChanged::class) { action ->
                Assert.assertFalse(action.userAction)
            }
        }

    @Test
    fun `GIVEN the cached country is gone before it was applied WHEN country list updates THEN its code is reported as missing`() =
        scope.runTest {
            val captureMiddleware = IPProtectionTestMiddleware()
            val store = buildStore(middleware = listOf(middleware, captureMiddleware))
            fakeRepository.setSelectedLocationCode("JP")

            store.dispatch(
                IPProtectionAction.LocationAction.CountryListChanged(
                    countries = listOf(IPProtectionHandler.Country(code = "CA", available = true))
                )
            )
            testScheduler.advanceUntilIdle()

            captureMiddleware.assertLastAction(IPProtectionAction.LocationAction.PersistedLocationUnavailable::class) {
                action ->
                Assert.assertEquals("JP", action.countryCode)
                Assert.assertEquals(CachedLocationStatus.Missing, action.status)
            }
            captureMiddleware.assertNotDispatched(IPProtectionAction.LocationAction.LocationReset::class)
        }

    @Test
    fun `GIVEN an applied selection disappears WHEN country list updates THEN LocationReset is dispatched instead`() =
        scope.runTest {
            val selected = Country(countryCode = "JP", available = true)
            val captureMiddleware = IPProtectionTestMiddleware()
            val store =
                buildStore(
                    selectedLocation = selected,
                    locations = listOf(selected),
                    middleware = listOf(middleware, captureMiddleware),
                )
            fakeRepository.setSelectedLocationCode("JP")

            store.dispatch(
                IPProtectionAction.LocationAction.CountryListChanged(
                    countries = listOf(IPProtectionHandler.Country(code = "CA", available = true))
                )
            )
            testScheduler.advanceUntilIdle()

            captureMiddleware.assertLastAction(IPProtectionAction.LocationAction.LocationReset::class) { action ->
                Assert.assertEquals("JP", action.countryCode)
                Assert.assertEquals(CachedLocationStatus.Missing, action.status)
            }
            captureMiddleware.assertNotDispatched(IPProtectionAction.LocationAction.PersistedLocationUnavailable::class)
        }

    @Test
    fun `GIVEN no cached selection WHEN country list updates THEN PersistedLocationUnavailable is not dispatched`() =
        scope.runTest {
            val captureMiddleware = IPProtectionTestMiddleware()
            val store = buildStore(middleware = listOf(middleware, captureMiddleware))

            store.dispatch(
                IPProtectionAction.LocationAction.CountryListChanged(
                    countries = listOf(IPProtectionHandler.Country(code = "CA", available = true))
                )
            )
            testScheduler.advanceUntilIdle()

            captureMiddleware.assertNotDispatched(IPProtectionAction.LocationAction.PersistedLocationUnavailable::class)
        }

    @Test
    fun `GIVEN a slow write WHEN a country list follows a selection THEN the selection is kept`() = scope.runTest {
        val japan = Country(countryCode = "JP", available = true)
        val france = Country(countryCode = "FR", available = true)
        val captureMiddleware = IPProtectionTestMiddleware()
        val store =
            buildStore(
                selectedLocation = japan,
                locations = listOf(Recommended, japan, france),
                middleware = listOf(middleware, captureMiddleware),
            )
        fakeRepository.setSelectedLocationCode("JP")
        fakeRepository.writeDelay = 100.milliseconds

        store.dispatch(IPProtectionAction.LocationAction.LocationChanged(france, userAction = true))
        store.dispatch(
            IPProtectionAction.LocationAction.CountryListChanged(
                countries =
                    listOf(
                        IPProtectionHandler.Country(code = "JP", available = true),
                        IPProtectionHandler.Country(code = "FR", available = true),
                    )
            )
        )
        testScheduler.advanceUntilIdle()

        Assert.assertEquals(france, store.state.locationState.selectedLocation)
        Assert.assertEquals("FR", fakeRepository.getSelectedLocationCode())
        captureMiddleware.assertLastAction(IPProtectionAction.LocationAction.LocationChanged::class) { action ->
            Assert.assertEquals(france, action.location)
        }
    }

    @Test
    fun `GIVEN a restore in progress WHEN a country is selected THEN the selection is kept`() = scope.runTest {
        val france = Country(countryCode = "FR", available = true)
        val captureMiddleware = IPProtectionTestMiddleware()
        val store = buildStore(middleware = listOf(middleware, captureMiddleware))
        fakeRepository.setSelectedLocationCode("JP")
        fakeRepository.readDelay = 100.milliseconds

        store.dispatch(
            IPProtectionAction.LocationAction.CountryListChanged(
                countries =
                    listOf(
                        IPProtectionHandler.Country(code = "JP", available = true),
                        IPProtectionHandler.Country(code = "FR", available = true),
                    )
            )
        )
        testScheduler.runCurrent()
        store.dispatch(IPProtectionAction.LocationAction.LocationChanged(france, userAction = true))
        testScheduler.advanceUntilIdle()

        Assert.assertEquals(france, store.state.locationState.selectedLocation)
        Assert.assertEquals("FR", fakeRepository.getSelectedLocationCode())
        captureMiddleware.assertLastAction(IPProtectionAction.LocationAction.LocationChanged::class) { action ->
            Assert.assertEquals(france, action.location)
        }
    }

    @Test
    fun `GIVEN the saved country is dropped WHEN another country is selected THEN the selection is kept`() =
        scope.runTest {
            val japan = Country(countryCode = "JP", available = true)
            val france = Country(countryCode = "FR", available = true)
            val captureMiddleware = IPProtectionTestMiddleware()
            val store =
                buildStore(
                    selectedLocation = japan,
                    locations = listOf(Recommended, japan, france),
                    middleware = listOf(middleware, captureMiddleware),
                )
            fakeRepository.setSelectedLocationCode("JP")
            fakeRepository.readDelay = 100.milliseconds

            store.dispatch(
                IPProtectionAction.LocationAction.CountryListChanged(
                    countries = listOf(IPProtectionHandler.Country(code = "FR", available = true))
                )
            )
            testScheduler.runCurrent()
            store.dispatch(IPProtectionAction.LocationAction.LocationChanged(france, userAction = true))
            testScheduler.advanceUntilIdle()

            Assert.assertEquals(france, store.state.locationState.selectedLocation)
            Assert.assertEquals("FR", fakeRepository.getSelectedLocationCode())
            captureMiddleware.assertNotDispatched(IPProtectionAction.LocationAction.LocationReset::class)
            captureMiddleware.assertLastAction(IPProtectionAction.LocationAction.LocationChanged::class) { action ->
                Assert.assertEquals(france, action.location)
            }
        }

    @Test
    fun `GIVEN a restore in progress WHEN an unavailable country is selected THEN the selection is reset`() =
        scope.runTest {
            val france = Country(countryCode = "FR", available = true)
            val captureMiddleware = IPProtectionTestMiddleware()
            val store = buildStore(middleware = listOf(middleware, captureMiddleware))
            fakeRepository.setSelectedLocationCode("JP")
            fakeRepository.readDelay = 100.milliseconds

            store.dispatch(
                IPProtectionAction.LocationAction.CountryListChanged(
                    countries =
                        listOf(
                            IPProtectionHandler.Country(code = "JP", available = true),
                            IPProtectionHandler.Country(code = "FR", available = false),
                        )
                )
            )
            testScheduler.runCurrent()
            store.dispatch(IPProtectionAction.LocationAction.LocationChanged(france, userAction = true))
            testScheduler.advanceUntilIdle()

            Assert.assertEquals(Recommended, store.state.locationState.selectedLocation)
            Assert.assertEquals(null, fakeRepository.getSelectedLocationCode())
            captureMiddleware.assertLastAction(IPProtectionAction.LocationAction.LocationReset::class) { action ->
                Assert.assertEquals("FR", action.countryCode)
                Assert.assertEquals(CachedLocationStatus.Unavailable, action.status)
            }
        }

    private fun buildStore(
        selectedLocation: Location = Recommended,
        locations: List<Location> = listOf(Recommended),
        locationState: LocationState =
            LocationState(
                selectedLocation = selectedLocation,
                locations = locations,
            ),
        accountState: AccountState = AccountState(),
        proxyStatus: ProxyStatus = Uninitialized,
        middleware: List<Middleware<IPProtectionState, IPProtectionAction>> = emptyList(),
    ) =
        IPProtectionStore(
            initialState =
                IPProtectionState(
                    locationState = locationState,
                    accountState = accountState,
                    proxyStatus = proxyStatus,
                ),
            middleware = middleware,
        )

    private class FakeIPProtectionLocationRepository : IPProtectionLocationRepository {
        var readDelay: Duration = Duration.ZERO
        var writeDelay: Duration = Duration.ZERO
        private var selectedLocationCode: String? = null

        override suspend fun getSelectedLocationCode(): String? {
            val code = selectedLocationCode
            delay(readDelay)
            return code
        }

        override suspend fun setSelectedLocationCode(code: String?) {
            delay(writeDelay)
            selectedLocationCode = code
        }
    }
}
