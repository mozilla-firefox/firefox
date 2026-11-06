/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.awesomebar.provider

import java.time.LocalDateTime
import java.util.Locale
import kotlin.test.assertNotNull
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runTest
import mozilla.components.feature.awesomebar.optimizedsuggestions.FlightItem
import mozilla.components.feature.awesomebar.optimizedsuggestions.FlightSuggestionStatus
import mozilla.components.feature.session.SessionUseCases.LoadUrlUseCase
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.mockito.Mockito.verify

private const val ARTIFICIAL_DELAY = 350L

/**
 * Tests for [FlightsOnlineSuggestionProvider].
 *
 * Note: these tests use virtual time provided by kotlinx.coroutines.test.runTest so that the internal
 * [kotlinx.coroutines.delay(ARTIFICIAL_DELAY)] does not slow the tests.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class FlightsOnlineSuggestionProviderTest {
    private lateinit var fakeDataSource: FakeCombinedOnlineSuggestionDataSource
    private lateinit var provider: FlightsOnlineSuggestionProvider

    @Before
    fun setUp() {
        fakeDataSource = FakeCombinedOnlineSuggestionDataSource(flightResults = listOf(sampleFlightItem()))

        provider =
            FlightsOnlineSuggestionProvider(
                loadUrlUseCase = mock(),
                dataSource = fakeDataSource,
                suggestionsHeader = null,
                maxNumberOfSuggestions = DEFAULT_FLIGHT_SUGGESTION_LIMIT,
            )
    }

    @Test
    fun `returns empty list when text is empty and does not call data source`() = runTest {
        val results = provider.onInputChanged("")
        assertTrue(results.isEmpty())
        assertTrue(fakeDataSource.calls.isEmpty())
    }

    @Test
    fun `fetches and returns suggestions for any non-blank text`() = runTest {
        val deferred = async { provider.onInputChanged("test") }

        advanceTimeBy(ARTIFICIAL_DELAY)

        val results = deferred.await()
        assertTrue(results.isNotEmpty())

        assertEquals(listOf("test"), fakeDataSource.calls)

        val suggestion = results.single()
        assertEquals("AA123", suggestion.flightNumber)
        assertEquals(provider, suggestion.provider)
    }

    @Test
    fun `onSuggestionClicked invokes search use case with query`() = runTest {
        val url = "https://www.flightaware.com/live/flight/AAL123"
        val loadUrlUseCase: LoadUrlUseCase = mock()
        val localDateSource =
            FakeCombinedOnlineSuggestionDataSource(flightResults = listOf(sampleFlightItem(url = url)))
        val localProvider =
            FlightsOnlineSuggestionProvider(
                loadUrlUseCase = loadUrlUseCase,
                dataSource = localDateSource,
                suggestionsHeader = null,
                maxNumberOfSuggestions = DEFAULT_FLIGHT_SUGGESTION_LIMIT,
            )

        val deferred = async { localProvider.onInputChanged("aa123") }
        advanceTimeBy(ARTIFICIAL_DELAY)
        val results = deferred.await()

        val suggestion = results.single()
        assertNotNull(suggestion.onSuggestionClicked)
        suggestion.onSuggestionClicked.invoke()

        verify(loadUrlUseCase).invoke(url)
    }

    @Test
    fun `respects maxNumberOfSuggestions`() = runTest {
        val manyResults =
            listOf(
                sampleFlightItem(url = "https://www.flightaware.com/live/flight/AAL123", flightNumber = "A"),
                sampleFlightItem(url = "https://www.flightaware.com/live/flight/AAL105", flightNumber = "B"),
                sampleFlightItem(url = "https://www.flightaware.com/live/flight/AAL101", flightNumber = "C"),
            )

        val localDataSource = FakeCombinedOnlineSuggestionDataSource(flightResults = manyResults)

        val limitedProvider =
            FlightsOnlineSuggestionProvider(
                loadUrlUseCase = mock(),
                dataSource = localDataSource,
                suggestionsHeader = null,
                maxNumberOfSuggestions = 1,
            )

        val deferred = async { limitedProvider.onInputChanged("flight") }
        advanceTimeBy(ARTIFICIAL_DELAY)
        val results = deferred.await()

        assertEquals(1, results.size)
    }

    @Test
    fun `id is stable per instance`() = runTest {
        val p =
            FlightsOnlineSuggestionProvider(
                loadUrlUseCase = mock(),
                dataSource = fakeDataSource,
                suggestionsHeader = null,
                maxNumberOfSuggestions = 1,
            )

        val id1 = p.id
        val deferred = async { p.onInputChanged("sport") }
        advanceTimeBy(ARTIFICIAL_DELAY)
        deferred.await()
        val id2 = p.id

        assertEquals(id1, id2)
    }

    @Test
    fun `cancellation before delay prevents data source call`() = runTest {
        val localDataSource = fakeDataSource
        val cancellableProvider =
            FlightsOnlineSuggestionProvider(
                loadUrlUseCase = mock(),
                dataSource = localDataSource,
                suggestionsHeader = null,
                maxNumberOfSuggestions = 1,
            )

        val job = async { cancellableProvider.onInputChanged("flight") }

        job.cancel(CancellationException("test-cancel"))

        advanceTimeBy(ARTIFICIAL_DELAY)

        try {
            job.await()
            // If we get here, cancellation didn't happen as expected — fail the test.
            throw AssertionError("Expected cancellation to propagate")
        } catch (_: CancellationException) {
            // expected
        }

        assertTrue(localDataSource.calls.isEmpty())
    }

    // --- parseFlightStatus tests ---

    @Test
    fun `parseFlightStatus returns ON_TIME for Scheduled`() {
        assertEquals(FlightSuggestionStatus.ON_TIME, provider.parseFlightStatus(false, "Scheduled"))
    }

    @Test
    fun `parseFlightStatus returns IN_FLIGHT for En Route and not delayed`() {
        assertEquals(FlightSuggestionStatus.IN_FLIGHT, provider.parseFlightStatus(false, "En Route"))
    }

    @Test
    fun `parseFlightStatus returns DELAYED for En Route and delayed`() {
        assertEquals(FlightSuggestionStatus.DELAYED, provider.parseFlightStatus(true, "En Route"))
    }

    @Test
    fun `parseFlightStatus returns ARRIVED for Arrived`() {
        assertEquals(FlightSuggestionStatus.ARRIVED, provider.parseFlightStatus(false, "Arrived"))
    }

    @Test
    fun `parseFlightStatus returns CANCELLED for Cancelled`() {
        assertEquals(FlightSuggestionStatus.CANCELLED, provider.parseFlightStatus(false, "Cancelled"))
    }

    @Test
    fun `parseFlightStatus returns DELAYED for Delayed`() {
        assertEquals(FlightSuggestionStatus.DELAYED, provider.parseFlightStatus(true, "Delayed"))
    }

    @Test
    fun `parseFlightStatus returns null for unknown status`() {
        assertEquals(null, provider.parseFlightStatus(false, "Unknown"))
    }

    // --- parseFlightData tests ---

    @Test
    fun `parseFlightData uses estimatedTime when available`() {
        val airport = FlightItem.Airport(code = "LAX", city = "Los Angeles")
        val timing =
            FlightItem.Timing(
                scheduledTime = "2025-10-05T13:05:00-07:00",
                estimatedTime = "2025-10-05T15:05:00-07:00",
            )

        val result =
            provider.parseFlightData(
                airport = airport,
                time = timing,
                locale = Locale.US,
            )

        assertNotNull(result)
        assertEquals("LAX", result.airportCode)
        assertEquals("Los Angeles", result.airportCity)
        assertEquals(formatShortTime(LocalDateTime.of(2025, 10, 5, 15, 5), Locale.US), result.time)
        assertEquals("Oct 5", result.date)
    }

    @Test
    fun `parseFlightData uses scheduledTime when estimatedTime is not available`() {
        val airport = FlightItem.Airport(code = "JFK", city = "New York")
        val timing =
            FlightItem.Timing(
                scheduledTime = "2025-10-05T13:05:00-04:00",
                estimatedTime = null,
            )

        val result =
            provider.parseFlightData(
                airport = airport,
                time = timing,
                locale = Locale.US,
            )

        assertNotNull(result)
        assertEquals("JFK", result.airportCode)
        assertEquals("New York", result.airportCity)
        assertEquals(formatShortTime(LocalDateTime.of(2025, 10, 5, 13, 5), Locale.US), result.time)
        assertEquals("Oct 5", result.date)
    }

    @Test
    fun `parseFlightData returns null for invalid ISO date format`() {
        val airport = FlightItem.Airport(code = "LAX", city = "Los Angeles")
        val timing =
            FlightItem.Timing(
                scheduledTime = "invalid-date-format",
                estimatedTime = null,
            )

        val result =
            provider.parseFlightData(
                airport = airport,
                time = timing,
            )

        assertNull(result)
    }

    @Test
    fun `parseFlightData shows the time in the airport's local timezone`() {
        // The destination airport is one hour ahead of the departure airport. The arrival time
        // must be shown in the destination's local time, not re-projected onto another zone.
        val departureAirport = FlightItem.Airport(code = "BNA", city = "Nashville")
        val departureTiming =
            FlightItem.Timing(
                scheduledTime = "2025-10-05T22:13:00-06:00",
                estimatedTime = null,
            )
        val arrivalAirport = FlightItem.Airport(code = "PHL", city = "Philadelphia")
        val arrivalTiming =
            FlightItem.Timing(
                scheduledTime = "2025-10-06T00:13:00-05:00",
                estimatedTime = null,
            )

        val departureResult =
            provider.parseFlightData(
                airport = departureAirport,
                time = departureTiming,
                locale = Locale.US,
            )

        val arrivalResult =
            provider.parseFlightData(
                airport = arrivalAirport,
                time = arrivalTiming,
                locale = Locale.US,
            )

        assertNotNull(departureResult)
        assertNotNull(arrivalResult)
        assertEquals(formatShortTime(LocalDateTime.of(2025, 10, 5, 22, 13), Locale.US), departureResult.time)
        assertEquals("Oct 5", departureResult.date)
        assertEquals(formatShortTime(LocalDateTime.of(2025, 10, 6, 0, 13), Locale.US), arrivalResult.time)
        assertEquals("Oct 6", arrivalResult.date)
    }

    @Test
    fun `parseFlightData formats date with different locale`() {
        val airport = FlightItem.Airport(code = "CDG", city = "Paris")
        val timing =
            FlightItem.Timing(
                scheduledTime = "2025-10-05T13:05:00+02:00",
                estimatedTime = null,
            )

        val resultUS =
            provider.parseFlightData(
                airport = airport,
                time = timing,
                locale = Locale.US,
            )

        val resultFrance =
            provider.parseFlightData(
                airport = airport,
                time = timing,
                locale = Locale.FRANCE,
            )

        assertNotNull(resultUS)
        assertNotNull(resultFrance)
        assertEquals("Oct 5", resultUS.date)
        assertEquals("oct. 5", resultFrance.date)
    }
}

/** Convenience factory for creating sample [FlightItem] objects for tests. */
private fun sampleFlightItem(
    flightNumber: String = "AA123",
    destination: FlightItem.Airport = sampleDestination,
    origin: FlightItem.Airport = sampleOrigin,
    departure: FlightItem.Timing = sampleDeparture,
    arrival: FlightItem.Timing = sampleArrival,
    status: String = "En Route",
    progressPercent: Int = 72,
    timeLeftMinutes: Int? = 63,
    delayed: Boolean = false,
    url: String = "https://flightaware.com/live/flight/AAL123",
    airline: FlightItem.Airline = sampleAirline,
) =
    FlightItem(
        flightNumber = flightNumber,
        destination = destination,
        origin = origin,
        departure = departure,
        arrival = arrival,
        status = status,
        progressPercent = progressPercent,
        timeLeftMinutes = timeLeftMinutes,
        delayed = delayed,
        url = url,
        airline = airline,
    )

private val sampleDestination =
    FlightItem.Airport(
        code = "JFK",
        city = "New York",
    )
private val sampleOrigin =
    FlightItem.Airport(
        code = "LAX",
        city = "Los Angeles",
    )
private val sampleDeparture =
    FlightItem.Timing(
        scheduledTime = "2025-10-05T13:05:00-07:00",
        estimatedTime = "2025-10-05T15:05:00-07:00",
    )
private val sampleArrival =
    FlightItem.Timing(
        scheduledTime = "2025-10-05T18:20:00-04:00",
        estimatedTime = "2025-10-05T18:25:00-04:00",
    )
private val sampleAirline =
    FlightItem.Airline(
        code = "AAL",
        name = "American Airlines",
        color = "#0078D2",
        icon = null,
    )
