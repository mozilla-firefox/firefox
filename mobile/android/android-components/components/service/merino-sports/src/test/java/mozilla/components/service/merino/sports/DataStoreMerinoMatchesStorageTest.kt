/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import androidx.datastore.core.DataStoreFactory
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class DataStoreMerinoMatchesStorageTest {

    @get:Rule val tempFolder = TemporaryFolder()

    private val file: File
        get() = File(tempFolder.root, ASSET_FILE_NAME)

    @Test
    fun `GIVEN nothing is stored WHEN the matches are read THEN return null`() = runTest {
        assertNull(createStorage(backgroundScope).matches.first())
    }

    @Test
    fun `WHEN matches are set THEN they are emitted`() = runTest {
        val storage = createStorage(backgroundScope)

        storage.setMatches(MATCHES)

        assertEquals(MATCHES, storage.matches.first())
    }

    @Test
    fun `GIVEN matches are stored WHEN null is set THEN null is emitted`() = runTest {
        val storage = createStorage(backgroundScope)
        storage.setMatches(MATCHES)

        storage.setMatches(null)

        assertNull(storage.matches.first())
    }

    @Test
    fun `GIVEN the stored file is corrupted WHEN the matches are read THEN return null`() = runTest {
        file.writeText("not json")

        assertNull(createStorage(backgroundScope).matches.first())
    }

    @Test
    fun `GIVEN the stored file is corrupted WHEN matches are set THEN they replace the corrupt data`() = runTest {
        file.writeText("not json")
        val storage = createStorage(backgroundScope)

        storage.setMatches(MATCHES)

        assertEquals(MATCHES, storage.matches.first())
    }

    private fun createStorage(scope: CoroutineScope) =
        DataStoreMerinoMatchesStorage(
            DataStoreFactory.create(
                serializer = MerinoMatchesSerializer,
                corruptionHandler = merinoMatchesCorruptionHandler,
                scope = scope,
                produceFile = { file },
            )
        )

    private companion object {
        val MATCHES =
            MerinoMatches(
                previous =
                    listOf(
                        MerinoMatch(
                            id = 1L,
                            competition = MerinoCompetition(id = 27L, name = "Test Cup"),
                            kickoff = "2026-10-04T14:00:00Z",
                            homeTeam = MerinoTeam(id = 1L, name = "Team A", logoUrl = "https://example.com/a.png"),
                            awayTeam = MerinoTeam(id = 2L, name = "Team B"),
                            period = MerinoMatchPeriod.PENALTY_SHOOTOUT,
                            status = MerinoMatchStatus.FINAL,
                            homeScore = 2,
                            awayScore = 2,
                            homePenaltyScore = 5,
                            awayPenaltyScore = 4,
                            updatedAt = "2026-10-04T16:20:00Z",
                        )
                    ),
                current =
                    listOf(
                        MerinoMatch(
                            id = 2L,
                            competition = MerinoCompetition(id = 34L, name = "Test League"),
                            kickoff = "2026-10-10T18:00:00Z",
                            homeTeam = MerinoTeam(id = 3L, name = "Team C"),
                            awayTeam = MerinoTeam(id = 1L, name = "Team A"),
                            period = MerinoMatchPeriod.REGULAR,
                            status = MerinoMatchStatus.IN_PROGRESS,
                            clock = "90+3",
                            homeScore = 1,
                            awayScore = 1,
                            updatedAt = "2026-10-10T18:45:00Z",
                        )
                    ),
                upcoming =
                    listOf(
                        MerinoMatch(
                            id = 3L,
                            competition = MerinoCompetition(id = 34L, name = "Test League"),
                            kickoff = "2026-10-17T14:00:00Z",
                            homeTeam = MerinoTeam(id = 1L, name = "Team A"),
                            awayTeam = MerinoTeam(id = 4L, name = "Team D"),
                            status = MerinoMatchStatus.SCHEDULED,
                            updatedAt = "2026-10-10T09:00:00Z",
                        )
                    ),
            )
    }
}
