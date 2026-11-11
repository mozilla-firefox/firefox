/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.collections.migration

import androidx.room.RoomDatabase
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.tabgroups.storage.database.getTestTabGroupDatabase
import mozilla.components.feature.tabgroups.storage.repository.DefaultTabGroupRepository
import mozilla.components.feature.tabgroups.storage.repository.createTestTabGroupRepository
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.test.fakes.engine.FakeEngine
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.utils.FakeDateTimeProvider
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.components.TabCollectionStorage
import org.mozilla.fenix.helpers.perf.TestStrictModeManager
import org.mozilla.fenix.utils.Settings

@RunWith(AndroidJUnit4::class)
class CollectionsToTabGroupsMigrationIntegrationTest {

    private val testDispatcher = StandardTestDispatcher()
    private val dateTimeProvider = FakeDateTimeProvider()

    private lateinit var tabGroupDatabase: RoomDatabase
    private lateinit var tabGroupRepository: DefaultTabGroupRepository
    private lateinit var tabCollectionStorage: TabCollectionStorage
    private lateinit var browserStore: BrowserStore
    private lateinit var settings: Settings

    @Before
    fun setup() = runTest {
        tabGroupDatabase = getTestTabGroupDatabase(context = testContext)
        tabGroupRepository =
            createTestTabGroupRepository(
                database = tabGroupDatabase,
                ioDispatcher = testDispatcher,
                dateTimeProvider = dateTimeProvider,
            )
        browserStore = BrowserStore()
        tabCollectionStorage =
            TabCollectionStorage(
                context = testContext,
                strictMode = TestStrictModeManager(),
            )
        settings =
            Settings(testContext).apply {
                migrateCollectionsToTabGroupsEnabled = true
                tabGroupsEnabled = true
                hasMigratedCollectionsToTabGroups = false
                migratedCollectionIds = emptySet()
            }
    }

    @After
    fun teardown() =
        runTest(testDispatcher) {
            tabCollectionStorage.getCollectionsList().forEach { tabCollectionStorage.removeCollection(it) }
            tabGroupDatabase.close()
        }

    private fun migration() =
        CollectionsToTabGroupsMigration(
            tabCollectionStorage = tabCollectionStorage,
            restoreUseCase = TabsUseCases(browserStore).restore,
            tabGroupRepository = tabGroupRepository,
            collectionsMigrationRepository = DefaultCollectionsMigrationRepository(settings = settings),
            engine = FakeEngine(),
            filesDir = testContext.filesDir,
            dateTimeProvider = dateTimeProvider,
            ioDispatcher = testDispatcher,
        )

    @Test
    fun `GIVEN stored collections WHEN collections to tab groups migration runs THEN their tabs are restored into closed tab groups`() =
        runTest(testDispatcher) {
            tabCollectionStorage.createCollection(
                title = "Recipes",
                sessions =
                    listOf(
                        createTab(url = "https://example.com/pasta"),
                        createTab(url = "https://example.com/soup"),
                    ),
            )
            tabCollectionStorage.createCollection(
                title = "News",
                sessions = listOf(createTab(url = "https://example.com/news")),
            )

            assertTrue(migration().migrateIfNeeded())

            assertEquals(
                setOf("https://example.com/pasta", "https://example.com/soup", "https://example.com/news"),
                browserStore.state.tabs.map { it.content.url }.toSet(),
            )

            val tabGroupData = tabGroupRepository.tabGroupDataFlow.first()
            assertEquals(setOf("Recipes", "News"), tabGroupData.tabGroups.map { it.title }.toSet())
            assertTrue(tabGroupData.tabGroups.all { it.closed })

            val recipesTabGroupId = tabGroupData.tabGroups.first { it.title == "Recipes" }.id
            val newsTabGroupId = tabGroupData.tabGroups.first { it.title == "News" }.id
            val tabGroupIdsByUrl =
                browserStore.state.tabs.associate {
                    it.content.url to tabGroupData.tabGroupAssignments[it.id]
                }
            assertEquals(
                mapOf(
                    "https://example.com/pasta" to recipesTabGroupId,
                    "https://example.com/soup" to recipesTabGroupId,
                    "https://example.com/news" to newsTabGroupId,
                ),
                tabGroupIdsByUrl,
            )

            assertTrue(settings.hasMigratedCollectionsToTabGroups)
            assertTrue(settings.migratedCollectionIds.isEmpty())

            // The collections are retained as a backup.
            assertEquals(2, tabCollectionStorage.getCollectionsList().size)
        }

    @Test
    fun `GIVEN a collection was migrated WHEN collection to tab group migration runs again THEN the collection is not migrated again`() =
        runTest(testDispatcher) {
            tabCollectionStorage.createCollection(
                title = "Recipes",
                sessions =
                    listOf(
                        createTab(url = "https://example.com/pasta"),
                        createTab(url = "https://example.com/soup"),
                    ),
            )

            migration().migrateIfNeeded()

            // Simulate a subsequent launch where the migration is attempted again.
            assertTrue(migration().migrateIfNeeded())

            assertEquals(2, browserStore.state.tabs.size)

            val tabGroupData = tabGroupRepository.tabGroupDataFlow.first()
            assertEquals(1, tabGroupData.tabGroups.size)
            assertEquals(2, tabGroupData.tabGroupAssignments.size)
            assertTrue(settings.hasMigratedCollectionsToTabGroups)
        }

    @Test
    fun `GIVEN a collection was migrated in an interrupted run WHEN the migration runs again THEN only the remaining collection is migrated`() =
        runTest(testDispatcher) {
            val recipesId =
                requireNotNull(
                    tabCollectionStorage.createCollection(
                        title = "Recipes",
                        sessions = listOf(createTab(url = "https://example.com/pasta")),
                    )
                )
            tabCollectionStorage.createCollection(
                title = "News",
                sessions = listOf(createTab(url = "https://example.com/news")),
            )

            // Simulate a previous run that migrated the recipes collection before it was interrupted.
            settings.migratedCollectionIds = setOf(recipesId.toString())

            assertTrue(migration().migrateIfNeeded())
            assertEquals(listOf("https://example.com/news"), browserStore.state.tabs.map { it.content.url })

            val tabGroupData = tabGroupRepository.tabGroupDataFlow.first()
            assertEquals(listOf("News"), tabGroupData.tabGroups.map { it.title })
            assertEquals(1, tabGroupData.tabGroupAssignments.size)
            assertTrue(settings.hasMigratedCollectionsToTabGroups)
            assertTrue(settings.migratedCollectionIds.isEmpty())
        }

    @Test
    fun `GIVEN tab groups feature is disabled WHEN collection to tab group migration runs THEN the collections are left untouched`() =
        runTest(testDispatcher) {
            settings.tabGroupsEnabled = false
            tabCollectionStorage.createCollection(
                title = "Recipes",
                sessions = listOf(createTab(url = "https://example.com/pasta")),
            )

            assertTrue(migration().migrateIfNeeded())

            assertTrue(browserStore.state.tabs.isEmpty())
            assertTrue(tabGroupRepository.tabGroupDataFlow.first().tabGroups.isEmpty())
            assertFalse(settings.hasMigratedCollectionsToTabGroups)
            assertEquals(1, tabCollectionStorage.getCollectionsList().size)
        }
}
