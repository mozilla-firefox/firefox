/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.recentvisits

import io.mockk.coEvery
import io.mockk.coVerify
import io.mockk.every
import io.mockk.mockk
import io.mockk.slot
import io.mockk.spyk
import kotlin.random.Random
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.storage.sync.PlacesHistoryStorage
import mozilla.components.concept.storage.DocumentType
import mozilla.components.concept.storage.HistoryHighlight
import mozilla.components.concept.storage.HistoryHighlightWeights
import mozilla.components.concept.storage.HistoryMetadata
import mozilla.components.concept.storage.HistoryMetadataKey
import mozilla.components.concept.storage.HistoryMetadataStorage
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem.RecentHistoryGroup
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem.RecentHistoryHighlight
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItemInternal.HistoryGroupInternal
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItemInternal.HistoryHighlightInternal
import org.mozilla.fenix.utils.Settings

class RecentVisitsFeatureTest {

    private lateinit var historyHighlightsStorage: PlacesHistoryStorage
    private lateinit var historyMetadataStorage: HistoryMetadataStorage

    private val middleware = CaptureActionsMiddleware<AppState, AppAction>()
    private val appStore = AppStore(middlewares = listOf(middleware))
    private val testDispatcher = StandardTestDispatcher()

    @Before
    fun setup() {
        historyHighlightsStorage = mockk(relaxed = true)
        historyMetadataStorage = mockk(relaxed = true)
        Settings.searchGroupMinimumSites = 1
    }

    @Test
    fun `GIVEN no recent visits WHEN feature starts THEN fetch history metadata and highlights then notify store`() =
        runTest(testDispatcher) {
            val historyEntry =
                HistoryMetadata(
                    key = HistoryMetadataKey("http://www.mozilla.com", "mozilla", null),
                    title = "mozilla",
                    createdAt = System.currentTimeMillis(),
                    updatedAt = System.currentTimeMillis(),
                    totalViewTime = 10,
                    documentType = DocumentType.Regular,
                    previewImageUrl = null,
                )
            val recentHistoryGroup =
                RecentHistoryGroup(
                    title = "mozilla",
                    historyMetadata = listOf(historyEntry),
                )
            val highlightEntry = HistoryHighlight(1.0, 1, "https://firefox.com", "firefox", null)
            val recentHistoryHighlight = RecentHistoryHighlight("firefox", "https://firefox.com")
            coEvery { historyMetadataStorage.getHistoryMetadataSince(any()) }
                .coAnswers {
                    listOf(historyEntry)
                }
            coEvery { historyHighlightsStorage.getHistoryHighlights(any(), any()) }
                .coAnswers {
                    listOf(highlightEntry)
                }

            startRecentVisitsFeature(scope = this)

            middleware.assertLastAction(AppAction.RecentHistoryChange::class) {
                assertEquals(listOf(recentHistoryGroup, recentHistoryHighlight), it.recentHistory)
            }
        }

    @Test
    fun `WHEN asking for history highlights THEN use a specific query`() {
        runTest(testDispatcher) {
            val highlightWeights = slot<HistoryHighlightWeights>()
            val highlightsAskedForNumber = slot<Int>()

            startRecentVisitsFeature(scope = this)

            coVerify {
                historyHighlightsStorage.getHistoryHighlights(
                    capture(highlightWeights),
                    capture(highlightsAskedForNumber),
                )
            }

            assertEquals(MIN_VIEW_TIME_OF_HIGHLIGHT, highlightWeights.captured.viewTime, 0.0)
            assertEquals(MIN_FREQUENCY_OF_HIGHLIGHT, highlightWeights.captured.frequency, 0.0)
            assertEquals(MAX_RESULTS_TOTAL, highlightsAskedForNumber.captured)
        }
    }

    @Test
    fun `GIVEN groups containing history metadata items with the same url WHEN they are added to store THEN entries are deduped`() =
        runTest(testDispatcher) {
            val historyEntry1 =
                HistoryMetadata(
                    key = HistoryMetadataKey("http://www.mozilla.com", "mozilla", null),
                    title = "mozilla",
                    createdAt = System.currentTimeMillis(),
                    updatedAt = 1,
                    totalViewTime = 10,
                    documentType = DocumentType.Regular,
                    previewImageUrl = null,
                )

            val historyEntry2 =
                HistoryMetadata(
                    key = HistoryMetadataKey("http://firefox.com", "mozilla", null),
                    title = "firefox",
                    createdAt = System.currentTimeMillis(),
                    updatedAt = 2,
                    totalViewTime = 20,
                    documentType = DocumentType.Regular,
                    previewImageUrl = "http://firefox.com/image1",
                )

            val historyEntry3 =
                HistoryMetadata(
                    key = HistoryMetadataKey("http://www.mozilla.com", "mozilla", null),
                    title = "mozilla",
                    createdAt = System.currentTimeMillis(),
                    updatedAt = 3,
                    totalViewTime = 30,
                    documentType = DocumentType.Regular,
                    previewImageUrl = null,
                )

            val expectedHistoryGroup =
                RecentHistoryGroup(
                    title = "mozilla",
                    historyMetadata =
                        listOf(
                            // Expected total view time to be summed up for deduped entries
                            historyEntry1.copy(
                                totalViewTime = historyEntry1.totalViewTime + historyEntry3.totalViewTime,
                                updatedAt = historyEntry3.updatedAt,
                            ),
                            historyEntry2,
                        ),
                )

            coEvery { historyMetadataStorage.getHistoryMetadataSince(any()) }
                .coAnswers {
                    listOf(
                        historyEntry1,
                        historyEntry2,
                        historyEntry3,
                    )
                }

            startRecentVisitsFeature(scope = this)

            middleware.assertLastAction(AppAction.RecentHistoryChange::class) {
                assertEquals(listOf(expectedHistoryGroup), it.recentHistory)
            }
        }

    @Test
    fun `GIVEN different groups containing history metadata items with the same url WHEN they are added to store THEN entries are not deduped`() =
        runTest(testDispatcher) {
            val now = System.currentTimeMillis()
            val historyEntry1 =
                HistoryMetadata(
                    key = HistoryMetadataKey("http://www.mozilla.com", "mozilla", null),
                    title = "mozilla",
                    createdAt = now,
                    updatedAt = now + 3,
                    totalViewTime = 10,
                    documentType = DocumentType.Regular,
                    previewImageUrl = null,
                )

            val historyEntry2 =
                HistoryMetadata(
                    key = HistoryMetadataKey("http://firefox.com", "mozilla", null),
                    title = "firefox",
                    createdAt = now,
                    updatedAt = now + 2,
                    totalViewTime = 20,
                    documentType = DocumentType.Regular,
                    previewImageUrl = null,
                )

            val historyEntry3 =
                HistoryMetadata(
                    key = HistoryMetadataKey("http://www.mozilla.com", "firefox", null),
                    title = "mozilla",
                    createdAt = now,
                    updatedAt = now + 1,
                    totalViewTime = 30,
                    documentType = DocumentType.Regular,
                    previewImageUrl = null,
                )

            val expectedHistoryGroup1 =
                RecentHistoryGroup(
                    title = "mozilla",
                    historyMetadata = listOf(historyEntry1, historyEntry2),
                )

            val expectedHistoryGroup2 =
                RecentHistoryGroup(
                    title = "firefox",
                    historyMetadata = listOf(historyEntry3),
                )

            coEvery { historyMetadataStorage.getHistoryMetadataSince(any()) }
                .coAnswers {
                    listOf(
                        historyEntry1,
                        historyEntry2,
                        historyEntry3,
                    )
                }

            startRecentVisitsFeature(scope = this)

            middleware.assertLastAction(AppAction.RecentHistoryChange::class) {
                assertEquals(listOf(expectedHistoryGroup1, expectedHistoryGroup2), it.recentHistory)
            }
        }

    @Test
    fun `GIVEN history groups WHEN they are added to store THEN they are sorted descending by last updated timestamp`() =
        runTest(testDispatcher) {
            val now = System.currentTimeMillis()
            val historyEntry1 =
                HistoryMetadata(
                    key = HistoryMetadataKey("http://www.mozilla.com", "mozilla", null),
                    title = "mozilla",
                    createdAt = now,
                    updatedAt = now + 1,
                    totalViewTime = 10,
                    documentType = DocumentType.Regular,
                    previewImageUrl = null,
                )

            val historyEntry2 =
                HistoryMetadata(
                    key = HistoryMetadataKey("http://firefox.com", "mozilla", null),
                    title = "firefox",
                    createdAt = now,
                    updatedAt = now + 2,
                    totalViewTime = 20,
                    documentType = DocumentType.Regular,
                    previewImageUrl = null,
                )

            val historyEntry3 =
                HistoryMetadata(
                    key = HistoryMetadataKey("http://www.mozilla.com", "firefox", null),
                    title = "mozilla",
                    createdAt = now,
                    updatedAt = now + 3,
                    totalViewTime = 30,
                    documentType = DocumentType.Regular,
                    previewImageUrl = null,
                )

            val expectedHistoryGroup1 =
                RecentHistoryGroup(
                    title = "mozilla",
                    historyMetadata = listOf(historyEntry1, historyEntry2),
                )

            val expectedHistoryGroup2 =
                RecentHistoryGroup(
                    title = "firefox",
                    historyMetadata = listOf(historyEntry3),
                )

            coEvery { historyMetadataStorage.getHistoryMetadataSince(any()) }
                .coAnswers {
                    listOf(
                        historyEntry1,
                        historyEntry2,
                        historyEntry3,
                    )
                }

            startRecentVisitsFeature(scope = this)

            middleware.assertLastAction(AppAction.RecentHistoryChange::class) {
                assertEquals(listOf(expectedHistoryGroup2, expectedHistoryGroup1), it.recentHistory)
            }
        }

    @Test
    fun `GIVEN multiple groups exist but no highlights WHEN they are added to store THEN only MAX_RESULTS_TOTAL are sent`() =
        runTest(testDispatcher) {
            val visitsFromSearch = getSearchFromHistoryMetadataItems(10)
            val expectedRecentHistoryGroups =
                visitsFromSearch
                    // Expect to only have the last accessed 9 groups.
                    .subList(1, 10)
                    .toIndividualRecentHistoryGroups()
            coEvery { historyMetadataStorage.getHistoryMetadataSince(any()) }.coAnswers { visitsFromSearch }

            startRecentVisitsFeature(scope = this)

            middleware.assertLastAction(AppAction.RecentHistoryChange::class) {
                assertEquals(
                    // The 9 most recent groups.
                    expectedRecentHistoryGroups,
                    it.recentHistory,
                )
            }
        }

    @Test
    fun `GIVEN multiple highlights exist but no history groups WHEN they are added to store THEN only MAX_RESULTS_TOTAL are sent`() =
        runTest(testDispatcher) {
            val highlights = getHistoryHighlightsItems(10)
            val expectedRecentHighlights =
                highlights
                    // Expect to only have 9 highlights
                    .subList(0, 9)
                    .toRecentHistoryHighlights()
            coEvery { historyHighlightsStorage.getHistoryHighlights(any(), any()) }.coAnswers { highlights }

            startRecentVisitsFeature(scope = this)

            middleware.assertLastAction(AppAction.RecentHistoryChange::class) {
                assertEquals(
                    expectedRecentHighlights,
                    it.recentHistory,
                )
            }
        }

    @Test
    fun `GIVEN multiple history highlights and history groups WHEN they are added to store THEN only last accessed are added`() =
        runTest(testDispatcher) {
            val visitsFromSearch = getSearchFromHistoryMetadataItems(10)
            val directVisits = getDirectVisitsHistoryMetadataItems(10)
            val expectedRecentHistoryGroups =
                visitsFromSearch
                    // Expect only 4 groups. Take 5 here for using in the below zip() and be dropped after.
                    .subList(5, 10)
                    .toIndividualRecentHistoryGroups()
            val expectedRecentHistoryHighlights = directVisits.reversed().toRecentHistoryHighlights()
            val expectedItems =
                expectedRecentHistoryHighlights
                    .zip(expectedRecentHistoryGroups)
                    .flatMap {
                        listOf(it.first, it.second)
                    }
                    .take(9)
            coEvery { historyMetadataStorage.getHistoryMetadataSince(any()) }
                .coAnswers { visitsFromSearch + directVisits }
            coEvery { historyHighlightsStorage.getHistoryHighlights(any(), any()) }
                .coAnswers {
                    directVisits.toHistoryHighlights()
                }

            startRecentVisitsFeature(scope = this)

            middleware.assertLastAction(AppAction.RecentHistoryChange::class) {
                assertEquals(expectedItems, it.recentHistory)
            }
        }

    @Test
    fun `GIVEN history highlights exist as history metadata WHEN they are added to store THEN don't add highlight dupes`() =
        runTest(testDispatcher) {
            // To know if a highlight appears in a search group each visit's url should be checked.
            val visitsFromSearch = getSearchFromHistoryMetadataItems(10)
            val directDistinctVisits = getDirectVisitsHistoryMetadataItems(10).takeLast(2)
            val directDupeVisits =
                visitsFromSearch.takeLast(2).map {
                    // Erase the search term for this to not be mapped to a search group.
                    // The url remains the same as the item from a group so it should be skipped.
                    it.copy(key = it.key.copy(searchTerm = null))
                }
            val expectedRecentHistoryGroups = visitsFromSearch.subList(3, 10).toIndividualRecentHistoryGroups()
            val expectedRecentHistoryHighlights = directDistinctVisits.reversed().toRecentHistoryHighlights()
            val expectedItems =
                listOf(
                    expectedRecentHistoryHighlights.first(),
                    expectedRecentHistoryGroups.first(),
                    expectedRecentHistoryHighlights[1],
                ) + expectedRecentHistoryGroups.subList(1, expectedRecentHistoryGroups.size)
            coEvery { historyMetadataStorage.getHistoryMetadataSince(any()) }
                .coAnswers {
                    visitsFromSearch + directDistinctVisits + directDupeVisits
                }
            coEvery { historyHighlightsStorage.getHistoryHighlights(any(), any()) }
                .coAnswers {
                    directDistinctVisits.toHistoryHighlights() + directDupeVisits.toHistoryHighlights()
                }

            startRecentVisitsFeature(scope = this)

            middleware.assertLastAction(AppAction.RecentHistoryChange::class) {
                assertEquals(expectedItems, it.recentHistory)
            }
        }

    @Test
    fun `GIVEN a list of history highlights and groups WHEN updateState is called THEN emit RecentHistoryChange`() {
        val feature = spyk(RecentVisitsFeature(appStore, mockk(), mockk(), mockk(), mockk()))
        val expected = List<RecentHistoryHighlight>(1) { mockk() }
        every { feature.getCombinedHistory(any(), any()) } returns expected

        feature.updateState(emptyList(), emptyList())

        middleware.assertLastAction(AppAction.RecentHistoryChange::class) {
            assertEquals(expected, it.recentHistory)
        }
    }

    @Test
    fun `GIVEN highlights visits exist in search groups WHEN getCombined is called THEN remove the highlights already in groups`() {
        val feature = RecentVisitsFeature(mockk(), mockk(), mockk(), mockk(), mockk())
        val visitsFromSearch = getSearchFromHistoryMetadataItems(4)
        val directVisits = getDirectVisitsHistoryMetadataItems(4)
        val directDupeVisits =
            getSearchFromHistoryMetadataItems(2).map {
                // Erase the search term for this to not be mapped to a search group.
                // The url remains the same as the item from a group so it should be skipped.
                it.copy(key = it.key.copy(searchTerm = null))
            }
        val expected =
            directVisits
                .reversed()
                .toRecentHistoryHighlights()
                .zip(visitsFromSearch.toIndividualRecentHistoryGroups())
                .flatMap {
                    listOf(it.first, it.second)
                }

        val result =
            feature.getCombinedHistory(
                (directVisits + directDupeVisits).toHistoryHighlightsInternal(),
                visitsFromSearch.toHistoryGroupsInternal(),
            )

        assertEquals(expected, result)
    }

    @Test
    fun `GIVEN fewer than needed highlights and search groups WHEN getCombined is called THEN the result is sorted by date`() {
        val feature = RecentVisitsFeature(mockk(), mockk(), mockk(), mockk(), mockk())
        val visitsFromSearch = getSearchFromHistoryMetadataItems(4)
        val directVisits = getDirectVisitsHistoryMetadataItems(4)
        val expected =
            directVisits
                .reversed()
                .toRecentHistoryHighlights()
                .zip(visitsFromSearch.toIndividualRecentHistoryGroups())
                .flatMap {
                    listOf(it.first, it.second)
                }

        val result =
            feature.getCombinedHistory(
                directVisits.toHistoryHighlightsInternal(),
                visitsFromSearch.toHistoryGroupsInternal(),
            )

        assertEquals(expected, result)
    }

    @Test
    fun `GIVEN more highlights are newer than search groups WHEN getCombined is called THEN then return an even split then sorted by date`() {
        val feature = RecentVisitsFeature(mockk(), mockk(), mockk(), mockk(), mockk())
        val visitsFromSearch = getSearchFromHistoryMetadataItems(5)
        val directVisits = getDirectVisitsHistoryMetadataItems(14)
        val expected =
            directVisits.takeLast(5).reversed().toRecentHistoryHighlights() +
                visitsFromSearch.takeLast(4).toIndividualRecentHistoryGroups()

        val result =
            feature.getCombinedHistory(
                directVisits.toHistoryHighlightsInternal(),
                visitsFromSearch.toHistoryGroupsInternal(),
            )

        assertEquals(expected, result)
    }

    @Test
    fun `GIVEN more search groups are newer than highlights WHEN getCombined is called THEN then return an even split then sorted by date`() {
        val feature = RecentVisitsFeature(mockk(), mockk(), mockk(), mockk(), mockk())
        val visitsFromSearch = getSearchFromHistoryMetadataItems(14)
        val directVisits = getDirectVisitsHistoryMetadataItems(5)
        val expected =
            visitsFromSearch.takeLast(4).toIndividualRecentHistoryGroups() +
                directVisits.takeLast(5).reversed().toRecentHistoryHighlights()

        val result =
            feature.getCombinedHistory(
                directVisits.toHistoryHighlightsInternal(),
                visitsFromSearch.toHistoryGroupsInternal(),
            )

        assertEquals(expected, result)
    }

    @Test
    fun `GIVEN all highlights have metadata WHEN getHistoryHighlights is called THEN return a list of highlights with an inferred last access time`() {
        val feature = RecentVisitsFeature(mockk(), mockk(), mockk(), mockk(), mockk())
        val visitsFromSearch = getSearchFromHistoryMetadataItems(10)
        val directVisits = getDirectVisitsHistoryMetadataItems(10)

        val result =
            feature.getHistoryHighlights(
                directVisits.toHistoryHighlights(),
                visitsFromSearch + directVisits,
            )

        assertEquals(
            directVisits.toHistoryHighlightsInternal(),
            result,
        )
    }

    @Test
    fun `GIVEN not all highlights have metadata WHEN getHistoryHighlights is called THEN set 0 for the highlights with not found last access time`() {
        val feature = RecentVisitsFeature(mockk(), mockk(), mockk(), mockk(), mockk())
        val visitsFromSearch = getSearchFromHistoryMetadataItems(10)
        val directVisits = getDirectVisitsHistoryMetadataItems(10)
        val highlightsWithUnknownAccessTime =
            directVisits.toHistoryHighlightsInternal().take(5).map {
                it.copy(lastAccessedTime = 0)
            }
        val highlightsWithInferredAccessTime = directVisits.toHistoryHighlightsInternal().takeLast(5)

        val result =
            feature.getHistoryHighlights(
                directVisits.toHistoryHighlights(),
                visitsFromSearch + directVisits.takeLast(5),
            )

        assertEquals(
            highlightsWithUnknownAccessTime + highlightsWithInferredAccessTime,
            result,
        )
    }

    @Test
    fun `GIVEN multiple metadata records for the same highlight WHEN getHistoryHighlights is called THEN set the latest access time from multiple available`() {
        val feature = RecentVisitsFeature(mockk(), mockk(), mockk(), mockk(), mockk())
        val visitsFromSearch = getSearchFromHistoryMetadataItems(10)
        val directVisits = getDirectVisitsHistoryMetadataItems(10)
        val newerDirectVisits = directVisits.mapIndexed { index, item ->
            item.copy(updatedAt = item.updatedAt * ((index % 2) + 1))
        }

        val result =
            feature.getHistoryHighlights(
                directVisits.toHistoryHighlights(),
                visitsFromSearch + directVisits + newerDirectVisits,
            )

        assertEquals(
            directVisits.mapIndexed { index, item ->
                item.toHistoryHighlightInternal(item.updatedAt * ((index % 2) + 1))
            },
            result,
        )
    }

    @Test
    fun `GIVEN multiple metadata entries only for direct accessed pages WHEN getHistorySearchGroups is called THEN return an empty list`() {
        val feature = RecentVisitsFeature(mockk(), mockk(), mockk(), mockk(), mockk())
        val directVisits = getDirectVisitsHistoryMetadataItems(10)

        val result = feature.getHistorySearchGroups(directVisits)

        assertTrue(result.isEmpty())
    }

    @Test
    fun `GIVEN multiple metadata entries WHEN getHistorySearchGroups is called THEN group all entries by their search term`() {
        val feature = RecentVisitsFeature(mockk(), mockk(), mockk(), mockk(), mockk())
        val visitsFromSearch = getSearchFromHistoryMetadataItems(10)
        val directVisits = getDirectVisitsHistoryMetadataItems(10)

        val result = feature.getHistorySearchGroups(visitsFromSearch + directVisits)

        assertEquals(10, result.size)
        assertEquals(visitsFromSearch.map { it.key.searchTerm }, result.map { it.groupName })
        assertEquals(visitsFromSearch.map { listOf(it) }, result.map { it.groupItems })
    }

    @Test
    fun `GIVEN multiple metadata entries for the same url WHEN getHistorySearchGroups is called THEN entries are deduped`() {
        val feature = RecentVisitsFeature(mockk(), mockk(), mockk(), mockk(), mockk())
        val visitsFromSearch = getSearchFromHistoryMetadataItems(10)
        val newerVisitsFromSearch = visitsFromSearch.map { it.copy(updatedAt = it.updatedAt * 2) }
        val directVisits = getDirectVisitsHistoryMetadataItems(10)

        val result = feature.getHistorySearchGroups(visitsFromSearch + directVisits + newerVisitsFromSearch)

        assertEquals(10, result.size)
        assertEquals(newerVisitsFromSearch.map { it.key.searchTerm }, result.map { it.groupName })
        assertEquals(
            newerVisitsFromSearch.map {
                listOf(it.copy(totalViewTime = it.totalViewTime * 2))
            },
            result.map { it.groupItems },
        )
    }

    @Test
    fun `GIVEN highlights and search groups WHEN getSortedHistory is called THEN sort descending all items based on the last access time`() {
        val feature = RecentVisitsFeature(mockk(), mockk(), mockk(), mockk(), mockk())
        val visitsFromSearch = getSearchFromHistoryMetadataItems(10)
        val directVisits = getDirectVisitsHistoryMetadataItems(10)
        val expected =
            directVisits
                .reversed()
                .toRecentHistoryHighlights()
                .zip(visitsFromSearch.toIndividualRecentHistoryGroups())
                .flatMap {
                    listOf(it.first, it.second)
                }

        val result =
            feature.getSortedHistory(
                directVisits.toHistoryHighlightsInternal(),
                visitsFromSearch.toHistoryGroupsInternal(),
            )

        assertEquals(expected, result)
    }

    @Test
    fun `GIVEN highlights don't have a valid title WHEN getSortedHistory is called THEN the url is set as title`() {
        val feature = RecentVisitsFeature(mockk(), mockk(), mockk(), mockk(), mockk())
        val visitsFromSearch = getSearchFromHistoryMetadataItems(10)
        val directVisits =
            getDirectVisitsHistoryMetadataItems(10).mapIndexed { index, item ->
                when (index % 3) {
                    0 -> item
                    1 -> item.copy(title = null)
                    else -> item.copy(title = " ".repeat(Random.nextInt(3)))
                }
            }
        val sortedByDateHighlights = directVisits.reversed()

        val result =
            feature
                .getSortedHistory(
                    directVisits.toHistoryHighlightsInternal(),
                    visitsFromSearch.toHistoryGroupsInternal(),
                )
                .filterIsInstance<RecentHistoryHighlight>()

        assertEquals(10, result.size)
        result.forEachIndexed { index, item ->
            when (index % 3) {
                0 -> assertEquals(sortedByDateHighlights[index].title, item.title)
                1 -> assertEquals(sortedByDateHighlights[index].key.url, item.title)
                2 -> assertEquals(sortedByDateHighlights[index].key.url, item.title)
            }
        }
    }

    @Test
    fun `GIVEN highlight visits also exist in search groups WHEN removeHighlightsAlreadyInGroups is called THEN filter out such highlights`() {
        val visitsFromSearch = getSearchFromHistoryMetadataItems(10)
        // To know if a highlight appears in a search group each visit's url should be checked.
        // Ensure we have the identical urls with the ones from a search group and also some random others.
        val directDupeVisits = visitsFromSearch.mapIndexed { index, item ->
            when (index % 2) {
                0 -> item
                else -> item.copy(key = item.key.copy(url = "https://mozilla.org"))
            }
        }
        val highlights = directDupeVisits.toHistoryHighlightsInternal()

        val result = highlights.removeHighlightsAlreadyInGroups(visitsFromSearch.toHistoryGroupsInternal())

        assertEquals(5, result.size)
        result.forEach { assertEquals("https://mozilla.org", it.historyHighlight.url) }
    }

    private fun startRecentVisitsFeature(scope: CoroutineScope) {
        val feature =
            RecentVisitsFeature(
                appStore,
                historyMetadataStorage,
                lazy { historyHighlightsStorage },
                scope,
                testDispatcher,
            )

        assertEquals(emptyList<RecentHistoryGroup>(), appStore.state.recentHistory)

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        coVerify {
            historyMetadataStorage.getHistoryMetadataSince(any())
        }
    }
}

/**
 * Get a list of [HistoryMetadata] representing visits following a search with [count] different elements. The elements
 * will have different `title`, `url`, `searchTerm` and an increasing `updatedAt` property based on their index in the
 * returned list.
 *
 * This items can be mapped to search groups.
 */
private fun getSearchFromHistoryMetadataItems(count: Int): List<HistoryMetadata> {
    return if (count > 0) {
        val historyEntry1 =
            HistoryMetadata(
                key = HistoryMetadataKey("https://searchurl1.test", "searchTerm1", null),
                title = "test1",
                createdAt = 0,
                updatedAt = 1,
                totalViewTime = 1,
                documentType = DocumentType.Regular,
                previewImageUrl = null,
            )
        mutableListOf(historyEntry1) +
            (2..count).map {
                historyEntry1.copy(
                    key = HistoryMetadataKey("https://searchurl$it.test", "searchTerm$it", null),
                    title = "test$it",
                    updatedAt = it.toLong(),
                )
            }
    } else {
        emptyList()
    }
}

/**
 * Get a list of [HistoryMetadata] representing directly accessed webpages with [count] different elements. The elements
 * will have different `title`, `url` and an increasing `updatedAt` property based on their index in the returned list.
 *
 * This items cannot be mapped to search groups since they don't contain a `searchTerm`.
 */
private fun getDirectVisitsHistoryMetadataItems(count: Int): List<HistoryMetadata> {
    return if (count > 0) {
        val historyEntry1 =
            HistoryMetadata(
                key = HistoryMetadataKey("https://url1.test", null),
                title = "test1",
                createdAt = 0,
                updatedAt = 1,
                totalViewTime = 1,
                documentType = DocumentType.Regular,
                previewImageUrl = null,
            )
        mutableListOf(historyEntry1) +
            (2..count).map {
                historyEntry1.copy(
                    key = HistoryMetadataKey("https://url$it.test", null),
                    title = "test$it",
                    updatedAt = it.toLong(),
                )
            }
    } else {
        emptyList()
    }
}

/**
 * Get a list of [HistoryHighlight] with [count] different elements. Each element will have unique value for all
 * properties based on their index in the returned list.
 */
private fun getHistoryHighlightsItems(count: Int): List<HistoryHighlight> =
    (1..count).map {
        HistoryHighlight(
            score = it.toDouble(),
            placeId = it,
            url = "https://url$it.test",
            title = "test$it",
            previewImageUrl = "https://previewImage$it.test",
        )
    }

private fun HistoryMetadata.toHistoryHighlight(): HistoryHighlight =
    HistoryHighlight(
        score = 3.0,
        placeId = 2,
        title = title,
        url = key.url,
        previewImageUrl = null,
    )

private fun HistoryMetadata.toRecentHistoryGroup(): RecentHistoryGroup =
    RecentHistoryGroup(
        title = key.searchTerm!!,
        historyMetadata = listOf(this),
    )

private fun List<HistoryMetadata>.toIndividualRecentHistoryGroups(): List<RecentHistoryGroup> = map {
    it.toRecentHistoryGroup()
}
    .sortedByDescending { it.lastUpdated() }

private fun HistoryMetadata.toRecentHistoryHighlight(): RecentHistoryHighlight =
    RecentHistoryHighlight(
        title = if (title.isNullOrBlank()) key.url else title!!,
        url = key.url,
    )

private fun List<HistoryMetadata>.toRecentHistoryHighlights(): List<RecentHistoryHighlight> = map {
    it.toRecentHistoryHighlight()
}

@JvmName("historyHighlightsToRecentHistoryHighlights") // avoid platform declaration clash with the above method
private fun List<HistoryHighlight>.toRecentHistoryHighlights(): List<RecentHistoryHighlight> = map {
    RecentHistoryHighlight(
        title = it.title!!,
        url = it.url,
    )
}

private fun List<HistoryMetadata>.toHistoryHighlights() = map { it.toHistoryHighlight() }

private fun HistoryMetadata.toHistoryHighlightInternal(lastAccessTime: Long) =
    HistoryHighlightInternal(
        historyHighlight = this.toHistoryHighlight(),
        lastAccessedTime = lastAccessTime,
    )

private fun List<HistoryMetadata>.toHistoryHighlightsInternal() = mapIndexed { index, item ->
    item.toHistoryHighlightInternal(index + 1L)
}

private fun HistoryMetadata.toHistoryGroupInternal() =
    HistoryGroupInternal(
        groupName = key.searchTerm!!,
        groupItems = listOf(this),
    )

private fun List<HistoryMetadata>.toHistoryGroupsInternal() = map { it.toHistoryGroupInternal() }
