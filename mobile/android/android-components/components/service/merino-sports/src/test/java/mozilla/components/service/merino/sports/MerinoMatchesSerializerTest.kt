/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import androidx.datastore.core.CorruptionException
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import kotlin.test.assertFailsWith
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class MerinoMatchesSerializerTest {

    @Test
    fun `WHEN matches are written and read back THEN the same matches are returned`() = runTest {
        val output = ByteArrayOutputStream()

        MerinoMatchesSerializer.writeTo(MATCHES, output)

        assertEquals(MATCHES, MerinoMatchesSerializer.readFrom(ByteArrayInputStream(output.toByteArray())))
    }

    @Test
    fun `WHEN null is written THEN nothing is written`() = runTest {
        val output = ByteArrayOutputStream()

        MerinoMatchesSerializer.writeTo(null, output)

        assertTrue(output.toByteArray().isEmpty())
    }

    @Test
    fun `GIVEN an empty input WHEN read THEN return null`() = runTest {
        assertNull(MerinoMatchesSerializer.readFrom(ByteArrayInputStream(ByteArray(0))))
    }

    @Test
    fun `GIVEN an input that is not JSON WHEN read THEN a CorruptionException is thrown`() = runTest {
        assertFailsWith<CorruptionException> {
            MerinoMatchesSerializer.readFrom(ByteArrayInputStream("not json".encodeToByteArray()))
        }
    }

    @Test
    fun `GIVEN a match missing a required field WHEN read THEN a CorruptionException is thrown`() = runTest {
        assertFailsWith<CorruptionException> {
            MerinoMatchesSerializer.readFrom(ByteArrayInputStream("""{"current": [{"id": 1}]}""".encodeToByteArray()))
        }
    }

    @Test
    fun `GIVEN unknown fields WHEN read THEN they are ignored`() = runTest {
        val input = """{"previous": [], "current": [], "upcoming": [], "new_field": true}"""

        assertEquals(MerinoMatches(), MerinoMatchesSerializer.readFrom(ByteArrayInputStream(input.encodeToByteArray())))
    }

    private companion object {
        val MATCHES =
            MerinoMatches(
                upcoming =
                    listOf(
                        MerinoMatch(
                            id = 1L,
                            competition = MerinoCompetition(id = 34L, name = "Test League"),
                            kickoff = "2026-10-17T14:00:00Z",
                            homeTeam = MerinoTeam(id = 1L, name = "Team A", logoUrl = "https://example.com/a.png"),
                            awayTeam = MerinoTeam(id = 2L, name = "Team B"),
                            status = MerinoMatchStatus.SCHEDULED,
                            updatedAt = "2026-10-10T09:00:00Z",
                        )
                    )
            )
    }
}
