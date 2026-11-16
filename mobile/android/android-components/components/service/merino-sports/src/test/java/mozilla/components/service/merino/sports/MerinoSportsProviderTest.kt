/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlinx.coroutines.Job
import kotlinx.serialization.SerializationException
import mozilla.appservices.merino.MerinoWrexhamApiException
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.service.merino.sports.fake.FakeWrexhamClient
import mozilla.components.support.test.file.loadResourceAsString
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class MerinoSportsProviderTest {

    private lateinit var client: FakeWrexhamClient
    private lateinit var crashReporter: FakeCrashReporter

    @Before
    fun setup() {
        client = FakeWrexhamClient(matchesResponse = loadResourceAsString("/matches.json"))
        crashReporter = FakeCrashReporter()
    }

    @Test
    fun `GIVEN a successful status response WHEN the matches are fetched THEN return the matches grouped by state`() {
        val matches = assertNotNull(provider().getMatches())

        assertEquals(1, matches.previous.size)
        assertEquals(1, matches.current.size)
        assertEquals(2, matches.upcoming.size)
    }

    @Test
    fun `GIVEN a live match WHEN the matches are fetched THEN all of its fields are parsed`() {
        val match = provider().getMatches()!!.current.first()

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
        val match = provider().getMatches()!!.previous.first()

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
        val upcoming = provider().getMatches()!!.upcoming

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
        client = FakeWrexhamClient(matchesResponse = """{"previous": [], "current": [], "upcoming": []}""")

        assertEquals(
            MerinoMatches(previous = emptyList(), current = emptyList(), upcoming = emptyList()),
            provider().getMatches(),
        )
    }

    @Test
    fun `GIVEN a match with unknown fields WHEN the matches are fetched THEN the unknown fields are ignored`() {
        client =
            FakeWrexhamClient(matchesResponse = matchJson(status = "InProgress", period = "Regular", extraField = true))

        val match = provider().getMatches()!!.current.first()

        assertEquals(MerinoMatchStatus.IN_PROGRESS, match.status)
        assertEquals(MerinoMatchPeriod.REGULAR, match.period)
    }

    @Test
    fun `GIVEN an unknown status WHEN the matches are fetched THEN return null and the exception is reported`() {
        client = FakeWrexhamClient(matchesResponse = matchJson(status = "Abandoned", period = "Regular"))
        val provider = provider()

        assertNull(provider.getMatches())
        assertEquals(1, crashReporter.breadcrumbs.size)
        assertEquals(1, crashReporter.exceptions.size)
        assertIs<SerializationException>(crashReporter.exceptions.first())
    }

    @Test
    fun `GIVEN an unknown period WHEN the matches are fetched THEN return null and the exception is reported`() {
        client = FakeWrexhamClient(matchesResponse = matchJson(status = "InProgress", period = "GoldenGoal"))
        val provider = provider()

        assertNull(provider.getMatches())
        assertEquals(1, crashReporter.breadcrumbs.size)
        assertEquals(1, crashReporter.exceptions.size)
        assertIs<SerializationException>(crashReporter.exceptions.first())
    }

    @Test
    fun `GIVEN Merino has no data it can serve and returns 503 status response WHEN the matches are fetched THEN return null and no crash report is submitted`() {
        client = FakeWrexhamClient(exception = MerinoWrexhamApiException.Unavailable(reason = "503"))

        assertNull(provider().getMatches())
        assertEquals(1, crashReporter.breadcrumbs.size)
        assertTrue(crashReporter.exceptions.isEmpty())
    }

    @Test
    fun `GIVEN the request fails with a network error WHEN the matches are fetched THEN return null and no crash report is submitted`() {
        client = FakeWrexhamClient(exception = MerinoWrexhamApiException.Network(reason = "offline"))

        assertNull(provider().getMatches())
        assertEquals(1, crashReporter.breadcrumbs.size)
        assertTrue(crashReporter.exceptions.isEmpty())
    }

    @Test
    fun `GIVEN the request fails with an unexpected error WHEN the matches are fetched THEN return null and the crash report is submitted`() {
        val exception = MerinoWrexhamApiException.Other(code = 500.toUShort(), reason = "boom")
        client = FakeWrexhamClient(exception = exception)

        assertNull(provider().getMatches())

        assertEquals(1, crashReporter.breadcrumbs.size)
        assertEquals("MerinoSportsProvider - Failed to fetch matches", crashReporter.breadcrumbs.first().message)

        assertEquals(listOf<Throwable>(exception), crashReporter.exceptions)
    }

    @Test
    fun `GIVEN the response body is malformed WHEN the matches are fetched THEN return null and the crash report is submitted`() {
        client = FakeWrexhamClient(matchesResponse = "not json")
        val provider = provider()

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
    fun `GIVEN a match is missing a required field WHEN the matches are fetched THEN return null and the crash report is submitted`() {
        client = FakeWrexhamClient(matchesResponse = """{"current": [{"id": 1}]}""")
        val provider = provider()

        assertNull(provider.getMatches())
        assertEquals(1, crashReporter.breadcrumbs.size)
        assertEquals(1, crashReporter.exceptions.size)
        assertIs<SerializationException>(crashReporter.exceptions.first())
    }

    @Test
    fun `GIVEN no limit is provided WHEN the matches are fetched THEN no limit is sent`() {
        provider().getMatches()

        assertNull(assertNotNull(client.options).limit)
    }

    @Test
    fun `GIVEN a limit is provided WHEN the matches are fetched THEN the limit is sent`() {
        provider().getMatches(limit = 3)

        assertEquals(3u, assertNotNull(client.options).limit)
    }

    @Test
    fun `GIVEN a non-positive limit is provided WHEN the matches are fetched THEN no limit is sent`() {
        provider().getMatches(limit = 0)
        assertNull(assertNotNull(client.options).limit)

        provider().getMatches(limit = -1)
        assertNull(assertNotNull(client.options).limit)
    }

    @Test
    fun `GIVEN a limit above the maximum is provided WHEN the matches are fetched THEN no limit is sent`() {
        provider().getMatches(limit = MerinoSportsProvider.MAX_LIMIT + 1)

        assertNull(assertNotNull(client.options).limit)
    }

    @Test
    fun `GIVEN a response missing a match group WHEN the matches are fetched THEN return null and the crash report is submitted`() {
        client = FakeWrexhamClient(matchesResponse = """{"previous": [], "current": []}""")

        assertNull(provider().getMatches())
        assertEquals(1, crashReporter.breadcrumbs.size)
        assertIs<SerializationException>(crashReporter.exceptions.single())
    }

    @Test
    fun `GIVEN a team missing its logo URL WHEN the matches are fetched THEN return null and the crash report is submitted`() {
        client =
            FakeWrexhamClient(
                matchesResponse = matchJson(status = "InProgress", period = "Regular", homeLogoUrl = null)
            )

        assertNull(provider().getMatches())
        assertEquals(1, crashReporter.breadcrumbs.size)
        assertIs<SerializationException>(crashReporter.exceptions.single())
    }

    private fun matchJson(
        status: String,
        period: String,
        extraField: Boolean = false,
        homeLogoUrl: String? = "https://example.com/home.png",
        awayLogoUrl: String? = "https://example.com/away.png",
    ): String {
        val extra = if (extraField) """, "new_field": true""" else ""
        val homeLogo = logoJson(homeLogoUrl)
        val awayLogo = logoJson(awayLogoUrl)
        return """
            {
              "previous": [],
              "upcoming": [],
              "current": [
                {
                  "id": 1,
                  "competition": {"id": 34, "name": "Test League"},
                  "kickoff": "2026-10-10T18:00:00Z",
                  "home_team": {"id": 1, "name": "Home"$homeLogo},
                  "away_team": {"id": 2, "name": "Away"$awayLogo},
                  "period": "$period",
                  "status": "$status",
                  "updated_at": "2026-10-10T18:45:00Z"$extra
                }
              ]
            }
            """
    }

    private fun logoJson(logoUrl: String?) = logoUrl?.let { ", \"logo_url\": \"$it\"" }.orEmpty()

    private fun provider() = MerinoSportsProvider(client = lazyOf(client), crashReporter = crashReporter)

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
