/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.IOException
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlinx.coroutines.Job
import kotlinx.serialization.SerializationException
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.file.loadResourceAsString
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doThrow
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class MerinoSportsProviderTest {

    private lateinit var client: Client
    private lateinit var crashReporter: FakeCrashReporter

    @Before
    fun setup() {
        client = getClient()
        crashReporter = FakeCrashReporter()
    }

    @Test
    fun `GIVEN a successful status response WHEN the matches are fetched THEN return the matches grouped by state`() {
        val matches = assertNotNull(MerinoSportsProvider(client).getMatches())

        assertEquals(1, matches.previous.size)
        assertEquals(1, matches.current.size)
        assertEquals(2, matches.upcoming.size)
    }

    @Test
    fun `GIVEN a live match WHEN the matches are fetched THEN all of its fields are parsed`() {
        val match = MerinoSportsProvider(client).getMatches()!!.current.first()

        assertEquals(116351L, match.id)
        assertEquals(MerinoCompetition(id = 34L, name = "Test League"), match.competition)
        assertEquals("2026-10-10T18:00:00Z", match.kickoff)
        assertEquals(
            MerinoTeam(
                id = 90002059L,
                name = "Team C",
                logoUrl = "https://example.com/logos/90002059.png",
            ),
            match.homeTeam,
        )
        assertEquals(
            MerinoTeam(
                id = 90002327L,
                name = "Team A",
                logoUrl = "https://example.com/logos/90002327.png",
            ),
            match.awayTeam,
        )
        assertEquals(MerinoMatchPeriod.REGULAR, match.period)
        assertEquals(MerinoMatchStatus.IN_PROGRESS, match.status)
        assertEquals("90+3", match.clock)
        assertEquals(1, match.homeScore)
        assertEquals(1, match.awayScore)
        assertNull(match.homePenaltyScore)
        assertNull(match.awayPenaltyScore)
        assertEquals("2026-10-10T18:45:00Z", match.updatedAt)
    }

    @Test
    fun `GIVEN a match decided by a penalty shootout WHEN the matches are fetched THEN the period and penalty scores are parsed`() {
        val match = MerinoSportsProvider(client).getMatches()!!.previous.first()

        assertEquals(MerinoMatchPeriod.PENALTY_SHOOTOUT, match.period)
        assertEquals(MerinoMatchStatus.FINAL, match.status)
        assertNull(match.clock)
        assertEquals(2, match.homeScore)
        assertEquals(2, match.awayScore)
        assertEquals(5, match.homePenaltyScore)
        assertEquals(4, match.awayPenaltyScore)
    }

    @Test
    fun `GIVEN upcoming matches WHEN the matches are fetched THEN their nullable fields are null`() {
        val upcoming = MerinoSportsProvider(client).getMatches()!!.upcoming

        assertEquals(MerinoMatchStatus.SCHEDULED, upcoming[0].status)
        assertEquals(MerinoMatchStatus.POSTPONED, upcoming[1].status)
        upcoming.forEach { match ->
            assertNull(match.period)
            assertNull(match.clock)
            assertNull(match.homeScore)
            assertNull(match.awayScore)
            assertNull(match.homePenaltyScore)
            assertNull(match.awayPenaltyScore)
        }
    }

    @Test
    fun `GIVEN every group is empty WHEN the matches are fetched THEN return empty matches`() {
        client = getClient(jsonResponse = """{"previous": [], "current": [], "upcoming": []}""")

        assertEquals(MerinoMatches(), MerinoSportsProvider(client).getMatches())
    }

    @Test
    fun `GIVEN a match with unknown fields WHEN the matches are fetched THEN the unknown fields are ignored`() {
        client = getClient(jsonResponse = matchJson(status = "InProgress", period = "Regular", extraField = true))

        val match = MerinoSportsProvider(client).getMatches()!!.current.first()

        assertEquals(MerinoMatchStatus.IN_PROGRESS, match.status)
        assertEquals(MerinoMatchPeriod.REGULAR, match.period)
    }

    @Test
    fun `GIVEN an unknown status WHEN the matches are fetched THEN return null and the exception is reported`() {
        client = getClient(jsonResponse = matchJson(status = "Abandoned", period = "Regular"))
        val provider = MerinoSportsProvider(client = client, crashReporter = crashReporter)

        assertNull(provider.getMatches())
        assertEquals(1, crashReporter.breadcrumbs.size)
        assertEquals(1, crashReporter.exceptions.size)
        assertIs<SerializationException>(crashReporter.exceptions.first())
    }

    @Test
    fun `GIVEN an unknown period WHEN the matches are fetched THEN return null and the exception is reported`() {
        client = getClient(jsonResponse = matchJson(status = "InProgress", period = "GoldenGoal"))
        val provider = MerinoSportsProvider(client = client, crashReporter = crashReporter)

        assertNull(provider.getMatches())
        assertEquals(1, crashReporter.breadcrumbs.size)
        assertEquals(1, crashReporter.exceptions.size)
        assertIs<SerializationException>(crashReporter.exceptions.first())
    }

    @Test
    fun `GIVEN a 503 status response WHEN the matches are fetched THEN return null and no crash report is submitted`() {
        client =
            getClient(
                jsonResponse = """{"statusCode": 503, "message": "Data is temporarily unavailable."}""",
                status = 503,
            )
        val provider = MerinoSportsProvider(client = client, crashReporter = crashReporter)

        assertNull(provider.getMatches())
        assertTrue(crashReporter.breadcrumbs.isEmpty())
        assertTrue(crashReporter.exceptions.isEmpty())
    }

    @Test
    fun `GIVEN the request fails with a network error WHEN the matches are fetched THEN return null and no crash report is submitted`() {
        client = mock()
        doThrow(IOException("offline")).`when`(client).fetch(any())
        val provider = MerinoSportsProvider(client = client, crashReporter = crashReporter)

        assertNull(provider.getMatches())
        assertTrue(crashReporter.breadcrumbs.isEmpty())
        assertTrue(crashReporter.exceptions.isEmpty())
    }

    @Test
    fun `GIVEN the response body is malformed WHEN the matches are fetched THEN return null and the exception is reported`() {
        client = getClient(jsonResponse = "not json")
        val provider = MerinoSportsProvider(client = client, crashReporter = crashReporter)

        assertNull(provider.getMatches())

        assertEquals(1, crashReporter.breadcrumbs.size)
        assertEquals(
            "MerinoSportsProvider - Failed to deserialize matches",
            crashReporter.breadcrumbs.first().message,
        )

        assertEquals(1, crashReporter.exceptions.size)
        assertIs<SerializationException>(crashReporter.exceptions.first())
    }

    @Test
    fun `GIVEN a match is missing a required field WHEN the matches are fetched THEN return null and the exception is reported`() {
        client = getClient(jsonResponse = """{"current": [{"id": 1}]}""")
        val provider = MerinoSportsProvider(client = client, crashReporter = crashReporter)

        assertNull(provider.getMatches())
        assertEquals(1, crashReporter.breadcrumbs.size)
        assertEquals(1, crashReporter.exceptions.size)
        assertIs<SerializationException>(crashReporter.exceptions.first())
    }

    @Test
    fun `GIVEN no limit is provided WHEN the matches are fetched THEN no query string is sent`() {
        MerinoSportsProvider(client).getMatches()

        val request = capturedRequest()
        assertEquals(MATCHES_ENDPOINT_URL, request.url)
        assertTrue(request.conservative)
    }

    @Test
    fun `GIVEN a limit is provided WHEN the matches are fetched THEN the limit is added to the request URL`() {
        MerinoSportsProvider(client).getMatches(limit = 3)

        val request = capturedRequest()
        assertEquals("$MATCHES_ENDPOINT_URL?limit=3", request.url)
        assertTrue(request.conservative)
    }

    @Test
    fun `GIVEN the endpoint URL has a query string WHEN the matches are fetched with a limit THEN the limit is appended to it`() {
        MerinoSportsProvider(client = client, matchesEndPointUrl = "https://example.com/matches?session=1")
            .getMatches(limit = 3)

        val request = capturedRequest()
        assertEquals("https://example.com/matches?session=1&limit=3", request.url)
        assertTrue(request.conservative)
    }

    private fun matchJson(status: String, period: String, extraField: Boolean = false): String {
        val extra = if (extraField) """, "new_field": true""" else ""
        return """
            {
              "current": [
                {
                  "id": 1,
                  "competition": {"id": 34, "name": "Test League"},
                  "kickoff": "2026-10-10T18:00:00Z",
                  "home_team": {"id": 1, "name": "Home"},
                  "away_team": {"id": 2, "name": "Away"},
                  "period": "$period",
                  "status": "$status",
                  "updated_at": "2026-10-10T18:45:00Z"$extra
                }
              ]
            }
            """
    }

    private fun getClient(
        jsonResponse: String = loadResourceAsString("/matches.json"),
        status: Int = 200,
    ): Client {
        val mockedClient = mock<Client>()
        val mockedResponse = mock<Response>()
        val mockedBody = mock<Response.Body>()

        whenever(mockedBody.string(any())).thenReturn(jsonResponse)
        whenever(mockedResponse.body).thenReturn(mockedBody)
        whenever(mockedResponse.status).thenReturn(status)
        whenever(mockedClient.fetch(any())).thenReturn(mockedResponse)

        return mockedClient
    }

    private fun capturedRequest(): Request {
        val request = argumentCaptor<Request>()
        verify(client).fetch(request.capture())
        return request.value
    }

    private class FakeCrashReporter : CrashReporting {
        val breadcrumbs = mutableListOf<Breadcrumb>()
        val exceptions = mutableListOf<Throwable>()

        override fun recordCrashBreadcrumb(breadcrumb: Breadcrumb) {
            breadcrumbs.add(breadcrumb)
        }

        override fun submitCaughtException(throwable: Throwable): Job {
            exceptions.add(throwable)
            return Job()
        }
    }
}
