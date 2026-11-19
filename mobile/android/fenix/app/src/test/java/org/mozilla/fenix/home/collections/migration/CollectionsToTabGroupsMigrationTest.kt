/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.collections.migration

import android.content.Context
import android.database.sqlite.SQLiteException
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.coEvery
import io.mockk.coVerify
import io.mockk.every
import io.mockk.mockk
import io.mockk.slot
import io.mockk.verify
import java.io.File
import kotlin.test.assertIs
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.yield
import mozilla.components.browser.state.state.recover.RecoverableTab
import mozilla.components.browser.state.state.recover.TabState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.engine.Engine
import mozilla.components.feature.tab.collections.Tab
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.feature.tabgroups.storage.data.TabGroup
import mozilla.components.feature.tabgroups.storage.fakes.FakeTabGroupRepository
import mozilla.components.feature.tabgroups.storage.repository.TabGroupRepository
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.test.fakes.engine.FakeEngine
import mozilla.components.support.utils.FakeDateTimeProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.components.TabCollectionStorage
import org.mozilla.fenix.home.collections.migration.fake.FakeCollectionsMigrationRepository
import org.mozilla.fenix.tabstray.data.TabGroupTheme

@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class CollectionsToTabGroupsMigrationTest {

    private val tabGroupRepository = FakeTabGroupRepository()
    private val browserStore = BrowserStore()
    private val dateTimeProvider = FakeDateTimeProvider()
    private val crashReporter = mockk<CrashReporting>(relaxed = true)
    private val ioDispatcher = UnconfinedTestDispatcher()

    private fun mockCollectionStorage(collections: List<TabCollection>) =
        mockk<TabCollectionStorage> {
            coEvery { getCollectionsList() } returns collections
        }

    private fun mockFailingCollectionStorage() =
        mockk<TabCollectionStorage> {
            coEvery { getCollectionsList() } throws IllegalStateException("Unable to read the collections")
        }

    private fun collection(
        id: Long,
        title: String,
        tabIds: List<String>,
        restorableTabIds: List<String> = tabIds,
        failingTabIds: List<String> = emptyList(),
    ): TabCollection =
        FakeTabCollection(
            id = id,
            title = title,
            tabs =
                tabIds.map { tabId ->
                    FakeCollectionTab(
                        tabId = tabId,
                        restorable = tabId in restorableTabIds,
                        restoreException =
                            IllegalStateException("Unable to read the session snapshot of $tabId").takeIf {
                                tabId in failingTabIds
                            },
                    )
                },
        )

    private fun collectionsToTabGroupsMigration(
        repository: CollectionsMigrationRepository,
        storage: TabCollectionStorage,
        tabGroupRepository: TabGroupRepository = this.tabGroupRepository,
    ) =
        CollectionsToTabGroupsMigration(
            tabCollectionStorage = storage,
            restoreUseCase = TabsUseCases(browserStore).restore,
            tabGroupRepository = tabGroupRepository,
            collectionsMigrationRepository = repository,
            engine = FakeEngine(),
            filesDir = File("."),
            dateTimeProvider = dateTimeProvider,
            crashReporter = crashReporter,
            ioDispatcher = ioDispatcher,
        )

    private fun restoredTabIds(): Set<String> = browserStore.state.tabs.map { it.id }.toSet()

    @Test
    fun `GIVEN stored collections WHEN collections are migrated to tab groups THEN a tab group is created per collection`() =
        runTest {
            val storage =
                mockCollectionStorage(
                    listOf(
                        collection(id = 3L, title = "News", tabIds = listOf("c")),
                        collection(id = 2L, title = "Empty", tabIds = emptyList()),
                        collection(id = 1L, title = "Recipes", tabIds = listOf("a", "b")),
                    )
                )

            collectionsToTabGroupsMigration(FakeCollectionsMigrationRepository(), storage).migrateIfNeeded()

            val storedGroups = tabGroupRepository.tabGroupDataFlow.first()
            assertEquals(listOf("Recipes", "News"), storedGroups.tabGroups.map { it.title })
            assertEquals(
                listOf(TabGroupTheme.Yellow.name, TabGroupTheme.Orange.name),
                storedGroups.tabGroups.map { it.theme },
            )
        }

    @Test
    fun `GIVEN collections stored newest first WHEN collections are migrated to tab groups THEN they are migrated oldest first`() =
        runTest {
            val storage =
                mockCollectionStorage(
                    listOf(
                        collection(id = 3L, title = "Added third", tabIds = listOf("c")),
                        collection(id = 2L, title = "Added second", tabIds = listOf("b")),
                        collection(id = 1L, title = "Added first", tabIds = listOf("a")),
                    )
                )

            collectionsToTabGroupsMigration(FakeCollectionsMigrationRepository(), storage).migrateIfNeeded()

            val storedGroups = tabGroupRepository.tabGroupDataFlow.first()
            assertEquals(listOf("Added first", "Added second", "Added third"), storedGroups.tabGroups.map { it.title })
            assertEquals(
                listOf(TabGroupTheme.Yellow.name, TabGroupTheme.Orange.name, TabGroupTheme.Red.name),
                storedGroups.tabGroups.map { it.theme },
            )
        }

    @Test
    fun `GIVEN a stored collection WHEN it is migrated to a tab group THEN the tab group is named after the collection and closed`() =
        runTest {
            val storage =
                mockCollectionStorage(listOf(collection(id = 1L, title = "Recipes", tabIds = listOf("a", "b"))))

            collectionsToTabGroupsMigration(FakeCollectionsMigrationRepository(), storage).migrateIfNeeded()

            val tabGroup = tabGroupRepository.tabGroupDataFlow.first().tabGroups.single()
            assertEquals("Recipes", tabGroup.title)
            assertEquals(TabGroupTheme.default.name, tabGroup.theme)
            assertEquals(dateTimeProvider.currentTimeMillis(), tabGroup.lastModified)
            assertTrue(tabGroup.closed)
        }

    @Test
    fun `GIVEN existing tab groups WHEN collections are migrated THEN the tab group themes iterate from the last used theme`() =
        runTest {
            tabGroupRepository.addNewTabGroup(
                TabGroup(title = "Oldest", theme = TabGroupTheme.Blue.name, lastModified = 1L, tabIds = emptyList())
            )
            tabGroupRepository.addNewTabGroup(
                TabGroup(title = "Newest", theme = TabGroupTheme.Pink.name, lastModified = 2L, tabIds = emptyList())
            )
            val storage =
                mockCollectionStorage(
                    listOf(
                        collection(id = 2L, title = "News", tabIds = listOf("b")),
                        collection(id = 1L, title = "Recipes", tabIds = listOf("a")),
                    )
                )

            collectionsToTabGroupsMigration(FakeCollectionsMigrationRepository(), storage).migrateIfNeeded()

            val migratedGroups =
                tabGroupRepository.tabGroupDataFlow.first().tabGroups.filter { it.title in listOf("Recipes", "News") }
            assertEquals(
                listOf(TabGroupTheme.Purple.name, TabGroupTheme.Blue.name),
                migratedGroups.map { it.theme },
            )
        }

    @Test
    fun `GIVEN the tab group repository cannot be read WHEN a collection is migrated THEN the tab group is given the default theme`() =
        runTest {
            val createdTabGroup = slot<TabGroup>()
            val tabGroupRepository =
                mockk<TabGroupRepository>(relaxed = true) {
                    every { tabGroupDataFlow } returns flow { throw SQLiteException("Unable to read the tab groups") }
                    coEvery { createTabGroupWithTabs(capture(createdTabGroup)) } returns Unit
                }
            val storage = mockCollectionStorage(listOf(collection(id = 1L, title = "Recipes", tabIds = listOf("a"))))

            val migrated =
                collectionsToTabGroupsMigration(
                        repository = FakeCollectionsMigrationRepository(),
                        storage = storage,
                        tabGroupRepository = tabGroupRepository,
                    )
                    .migrateIfNeeded()

            assertTrue(migrated)
            assertEquals(TabGroupTheme.default.name, createdTabGroup.captured.theme)
            verify { crashReporter.submitCaughtException(any<SQLiteException>()) }
        }

    @Test
    fun `GIVEN reading the tab group repository is cancelled WHEN collections are migrated THEN the cancellation is not reported`() =
        runTest {
            val tabGroupRepository =
                mockk<TabGroupRepository>(relaxed = true) {
                    every { tabGroupDataFlow } returns flow { throw CancellationException("Migration cancelled") }
                }
            val storage = mockCollectionStorage(listOf(collection(id = 1L, title = "Recipes", tabIds = listOf("a"))))

            val result = runCatching {
                collectionsToTabGroupsMigration(
                        repository = FakeCollectionsMigrationRepository(),
                        storage = storage,
                        tabGroupRepository = tabGroupRepository,
                    )
                    .migrateIfNeeded()
            }

            assertIs<CancellationException>(result.exceptionOrNull())
            verify(exactly = 0) { crashReporter.submitCaughtException(any()) }
            coVerify(exactly = 0) { tabGroupRepository.createTabGroupWithTabs(any()) }
        }

    @Test
    fun `GIVEN a stored collection WHEN it is migrated to a tab group THEN its tabs are restored and assigned to the tab group`() =
        runTest {
            val storage =
                mockCollectionStorage(
                    listOf(
                        collection(id = 1L, title = "Recipes", tabIds = listOf("a", "b")),
                        collection(id = 2L, title = "News", tabIds = listOf("c")),
                    )
                )

            collectionsToTabGroupsMigration(FakeCollectionsMigrationRepository(), storage).migrateIfNeeded()

            val storedGroups = tabGroupRepository.tabGroupDataFlow.first()
            val recipesTabGroupId = storedGroups.tabGroups.first { it.title == "Recipes" }.id
            val newsTabGroupId = storedGroups.tabGroups.first { it.title == "News" }.id
            assertEquals(recipesTabGroupId, storedGroups.tabGroupAssignments["a"])
            assertEquals(recipesTabGroupId, storedGroups.tabGroupAssignments["b"])
            assertEquals(newsTabGroupId, storedGroups.tabGroupAssignments["c"])
            assertEquals(setOf("a", "b", "c"), restoredTabIds())
        }

    @Test
    fun `GIVEN the tab group cannot be created WHEN a collection is migrated THEN its tabs are not restored`() =
        runTest {
            val tabGroupRepository =
                FakeTabGroupRepository(
                    onCreateTabGroupWithTabs = { throw SQLiteException("Unable to create the tab group") }
                )
            val repository = FakeCollectionsMigrationRepository()
            val storage = mockCollectionStorage(listOf(collection(id = 1L, title = "Recipes", tabIds = listOf("a"))))

            val migrated =
                collectionsToTabGroupsMigration(
                        repository = repository,
                        storage = storage,
                        tabGroupRepository = tabGroupRepository,
                    )
                    .migrateIfNeeded()

            assertFalse(migrated)
            assertTrue(tabGroupRepository.tabGroupDataFlow.first().tabGroups.isEmpty())
            assertTrue(restoredTabIds().isEmpty())
            assertTrue(repository.getMigratedCollectionIds().isEmpty())
        }

    @Test
    fun `GIVEN the migration is cancelled while a tab group is created WHEN collections are migrated THEN only that collection finishes migrating`() =
        runTest(StandardTestDispatcher(ioDispatcher.scheduler)) {
            lateinit var migrationJob: Job
            val tabGroupRepository =
                FakeTabGroupRepository(
                    onCreateTabGroupWithTabs = {
                        // Cancel the migration while the tab group is written in a NonCancellable context.
                        migrationJob.cancel()
                        yield()
                    }
                )
            val repository = FakeCollectionsMigrationRepository()
            val storage =
                mockCollectionStorage(
                    listOf(
                        collection(id = 2L, title = "News", tabIds = listOf("b")),
                        collection(id = 1L, title = "Recipes", tabIds = listOf("a")),
                    )
                )

            migrationJob = launch {
                collectionsToTabGroupsMigration(
                        repository = repository,
                        storage = storage,
                        tabGroupRepository = tabGroupRepository,
                    )
                    .migrateIfNeeded()
            }
            migrationJob.join()

            assertEquals(listOf("Recipes"), tabGroupRepository.tabGroupDataFlow.first().tabGroups.map { it.title })
            assertEquals(setOf("a"), restoredTabIds())
            assertEquals(setOf("1"), repository.getMigratedCollectionIds())
            verify(exactly = 0) { crashReporter.submitCaughtException(any()) }
        }

    @Test
    fun `GIVEN the migration is cancelled while a collection's tabs are being restored WHEN collections are migrated THEN nothing is migrated and the cancellation is not reported`() =
        runTest {
            // A cancellation happens when the tabs are converted to recoverable tabs before the NonCancellable context.
            val cancelledTab =
                FakeCollectionTab(tabId = "a", restoreException = CancellationException("Migration cancelled"))
            val repository = FakeCollectionsMigrationRepository()
            val storage =
                mockCollectionStorage(
                    listOf(FakeTabCollection(id = 1L, title = "Recipes", tabs = listOf(cancelledTab)))
                )

            val result = runCatching { collectionsToTabGroupsMigration(repository, storage).migrateIfNeeded() }

            assertIs<CancellationException>(result.exceptionOrNull())
            assertTrue(tabGroupRepository.tabGroupDataFlow.first().tabGroups.isEmpty())
            assertTrue(restoredTabIds().isEmpty())
            assertTrue(repository.getMigratedCollectionIds().isEmpty())
            verify(exactly = 0) { crashReporter.submitCaughtException(any()) }
        }

    @Test
    fun `GIVEN stored collections WHEN they are migrated to tab groups THEN the migration is recorded`() = runTest {
        val repository = FakeCollectionsMigrationRepository()
        val storage =
            mockCollectionStorage(
                listOf(
                    collection(id = 1L, title = "Recipes", tabIds = listOf("a", "b")),
                    collection(id = 2L, title = "News", tabIds = listOf("c")),
                )
            )

        val migrated = collectionsToTabGroupsMigration(repository, storage).migrateIfNeeded()

        assertTrue(migrated)
        assertTrue(repository.hasMigratedCollections())
        assertTrue(repository.getMigratedCollectionIds().isEmpty())
        assertTrue(repository.shouldShowCollectionsMigrationCard())
    }

    @Test
    fun `GIVEN migrateCollectionsToTabGroupsEnabled is disabled WHEN collections are migrated to tab groups THEN migration is skipped`() =
        runTest {
            val repository = FakeCollectionsMigrationRepository(migrationEnabled = false)
            val storage = mockCollectionStorage(emptyList())

            collectionsToTabGroupsMigration(repository, storage).migrateIfNeeded()

            assertTrue(tabGroupRepository.tabGroupDataFlow.first().tabGroups.isEmpty())
            assertFalse(repository.hasMigratedCollections())
            assertFalse(repository.shouldShowCollectionsMigrationCard())
            coVerify(exactly = 0) { storage.getCollectionsList() }
        }

    @Test
    fun `GIVEN tabGroupsEnabled is disabled WHEN collections are migrated to tab groups THEN migration is skipped`() =
        runTest {
            val repository = FakeCollectionsMigrationRepository(tabGroupsEnabled = false)
            val storage = mockCollectionStorage(emptyList())

            collectionsToTabGroupsMigration(repository, storage).migrateIfNeeded()

            assertTrue(tabGroupRepository.tabGroupDataFlow.first().tabGroups.isEmpty())
            assertFalse(repository.hasMigratedCollections())
            assertFalse(repository.shouldShowCollectionsMigrationCard())
            coVerify(exactly = 0) { storage.getCollectionsList() }
        }

    @Test
    fun `GIVEN hasMigratedCollectionsToTabGroups is true WHEN collections are migrated to tab groups THEN migration is skipped`() =
        runTest {
            val repository = FakeCollectionsMigrationRepository(migrationCompleted = true)
            val storage = mockCollectionStorage(listOf(collection(id = 1L, title = "Recipes", tabIds = listOf("a"))))

            val migrated = collectionsToTabGroupsMigration(repository, storage).migrateIfNeeded()

            assertTrue(migrated)
            assertTrue(tabGroupRepository.tabGroupDataFlow.first().tabGroups.isEmpty())
            assertFalse(repository.shouldShowCollectionsMigrationCard())
            coVerify(exactly = 0) { storage.getCollectionsList() }
            verify(exactly = 0) { crashReporter.recordCrashBreadcrumb(any()) }
            verify(exactly = 0) { crashReporter.submitCaughtException(any()) }
        }

    @Test
    fun `GIVEN a collection has no tabs WHEN collections are migrated to tab groups THEN it is marked as migrated without creating a tab group`() =
        runTest {
            val repository = FakeCollectionsMigrationRepository()
            val storage = mockCollectionStorage(listOf(collection(id = 1L, title = "Empty", tabIds = emptyList())))

            collectionsToTabGroupsMigration(repository, storage).migrateIfNeeded()

            assertTrue(tabGroupRepository.tabGroupDataFlow.first().tabGroups.isEmpty())
            assertTrue(repository.getMigratedCollectionIds().isEmpty())
            assertTrue(repository.hasMigratedCollections())
            assertFalse(repository.shouldShowCollectionsMigrationCard())
        }

    @Test
    fun `GIVEN there are no collections WHEN collections are migrated to tab groups THEN the migration is complete without showing the card`() =
        runTest {
            val repository = FakeCollectionsMigrationRepository()

            val migrated =
                collectionsToTabGroupsMigration(repository, mockCollectionStorage(emptyList())).migrateIfNeeded()

            assertTrue(migrated)
            assertTrue(tabGroupRepository.tabGroupDataFlow.first().tabGroups.isEmpty())
            assertTrue(repository.getMigratedCollectionIds().isEmpty())
            assertTrue(repository.hasMigratedCollections())
            assertFalse(repository.shouldShowCollectionsMigrationCard())
        }

    @Test
    fun `GIVEN a collection was already migrated WHEN collections are migrated to tab groups in a subsequent run THEN proceed with only migrating collections that haven't been migrated`() =
        runTest {
            val repository = FakeCollectionsMigrationRepository(migratedIds = setOf("1"))
            val storage =
                mockCollectionStorage(
                    listOf(
                        collection(id = 1L, title = "Recipes", tabIds = listOf("a")),
                        collection(id = 2L, title = "News", tabIds = listOf("c")),
                    )
                )

            collectionsToTabGroupsMigration(repository, storage).migrateIfNeeded()

            val storedGroups = tabGroupRepository.tabGroupDataFlow.first()
            assertEquals(listOf("News"), storedGroups.tabGroups.map { it.title })
            assertEquals(storedGroups.tabGroups.single().id, storedGroups.tabGroupAssignments["c"])

            assertEquals(setOf("c"), restoredTabIds())
            assertTrue(repository.hasMigratedCollections())
            assertTrue(repository.getMigratedCollectionIds().isEmpty())
            assertTrue(repository.shouldShowCollectionsMigrationCard())
        }

    @Test
    fun `GIVEN a collection's tabs cannot be restored WHEN collections are migrated to tab groups THEN the tabs are migrated without their session state`() =
        runTest {
            val repository = FakeCollectionsMigrationRepository()
            val storage =
                mockCollectionStorage(
                    listOf(
                        collection(id = 2L, title = "News", tabIds = listOf("c")),
                        collection(id = 1L, title = "Recipes", tabIds = listOf("a"), restorableTabIds = emptyList()),
                    )
                )

            collectionsToTabGroupsMigration(repository, storage).migrateIfNeeded()

            // The tab that could not be restored is recreated from the collection's stored url and title.
            val recipesTab = browserStore.state.tabs.single { it.content.url == "https://example.com/a" }
            assertNull(recipesTab.engineState.engineSessionState)
            assertEquals("Title of a", recipesTab.content.title)
            assertEquals(2, browserStore.state.tabs.size)

            val storedGroups = tabGroupRepository.tabGroupDataFlow.first()
            assertEquals(listOf("Recipes", "News"), storedGroups.tabGroups.map { it.title })
            assertEquals(2, storedGroups.tabGroupAssignments.size)
            assertTrue(repository.getMigratedCollectionIds().isEmpty())
            assertTrue(repository.hasMigratedCollections())
            assertTrue(repository.shouldShowCollectionsMigrationCard())
        }

    @Test
    fun `GIVEN the collections cannot be read WHEN collections are migrated to tab groups THEN the migration is attempted again on a subsequent run`() =
        runTest {
            val repository = FakeCollectionsMigrationRepository()

            val migrated =
                collectionsToTabGroupsMigration(
                        repository,
                        mockFailingCollectionStorage(),
                    )
                    .migrateIfNeeded()

            assertFalse(migrated)
            assertTrue(tabGroupRepository.tabGroupDataFlow.first().tabGroups.isEmpty())
            assertTrue(repository.getMigratedCollectionIds().isEmpty())
            assertFalse(repository.hasMigratedCollections())
            assertFalse(repository.shouldShowCollectionsMigrationCard())
        }

    @Test
    fun `GIVEN a collection cannot be migrated WHEN collections are migrated to tab groups THEN it is not recorded as migrated and nothing is created for it`() =
        runTest {
            val repository = FakeCollectionsMigrationRepository()
            val storage =
                mockCollectionStorage(
                    listOf(
                        collection(id = 1L, title = "Recipes", tabIds = listOf("a"), failingTabIds = listOf("a")),
                        collection(id = 2L, title = "Empty", tabIds = emptyList()),
                    )
                )

            val migrated = collectionsToTabGroupsMigration(repository, storage).migrateIfNeeded()

            assertFalse(migrated)
            assertFalse(repository.hasMigratedCollections())
            assertEquals(setOf("2"), repository.getMigratedCollectionIds())
            assertFalse(repository.shouldShowCollectionsMigrationCard())

            val storedGroups = tabGroupRepository.tabGroupDataFlow.first()
            assertTrue(storedGroups.tabGroups.isEmpty())
            assertTrue(storedGroups.tabGroupAssignments.isEmpty())
            assertTrue(restoredTabIds().isEmpty())
        }

    @Test
    fun `GIVEN a collection failed to migrate WHEN another collection created a tab group THEN the card is not shown`() =
        runTest {
            val repository = FakeCollectionsMigrationRepository()
            val storage =
                mockCollectionStorage(
                    listOf(
                        collection(id = 1L, title = "Recipes", tabIds = listOf("a")),
                        collection(id = 2L, title = "News", tabIds = listOf("c"), failingTabIds = listOf("c")),
                    )
                )

            val migrated = collectionsToTabGroupsMigration(repository, storage).migrateIfNeeded()

            assertFalse(migrated)
            // The first collection produced a tab group, but the card stays hidden until every collection migrates.
            assertEquals(listOf("Recipes"), tabGroupRepository.tabGroupDataFlow.first().tabGroups.map { it.title })
            assertFalse(repository.hasMigratedCollections())
            assertEquals(setOf("1"), repository.getMigratedCollectionIds())
            assertFalse(repository.shouldShowCollectionsMigrationCard())
        }

    @Test
    fun `GIVEN stored collections WHEN collections are migrated to tab groups THEN the migration is recorded as breadcrumbs`() =
        runTest {
            val storage =
                mockCollectionStorage(
                    listOf(
                        collection(id = 1L, title = "Recipes", tabIds = listOf("a")),
                        collection(id = 2L, title = "News", tabIds = listOf("c")),
                    )
                )

            collectionsToTabGroupsMigration(FakeCollectionsMigrationRepository(), storage).migrateIfNeeded()

            val breadcrumbs = mutableListOf<Breadcrumb>()
            verify(exactly = 2) { crashReporter.recordCrashBreadcrumb(capture(breadcrumbs)) }

            assertEquals("Migrating collections to tab groups", breadcrumbs[0].message)
            assertEquals(mapOf("collections" to "2"), breadcrumbs[0].data)
            assertEquals(Breadcrumb.Level.INFO, breadcrumbs[0].level)

            assertEquals("Migrated collections to tab groups", breadcrumbs[1].message)
            assertEquals(mapOf("migratedAll" to "true"), breadcrumbs[1].data)
            assertEquals(Breadcrumb.Level.INFO, breadcrumbs[1].level)
        }

    @Test
    fun `GIVEN the collections cannot be read WHEN collections are migrated to tab groups THEN the failure is reported`() =
        runTest {
            collectionsToTabGroupsMigration(
                    FakeCollectionsMigrationRepository(),
                    mockFailingCollectionStorage(),
                )
                .migrateIfNeeded()

            val breadcrumb = slot<Breadcrumb>()
            verify(exactly = 1) { crashReporter.recordCrashBreadcrumb(capture(breadcrumb)) }
            assertEquals("Unable to fetch collections from storage", breadcrumb.captured.message)
            assertEquals(Breadcrumb.Level.ERROR, breadcrumb.captured.level)

            verify(exactly = 1) { crashReporter.submitCaughtException(any<IllegalStateException>()) }
        }

    @Test
    fun `GIVEN a collection cannot be migrated WHEN collections are migrated to tab groups THEN the failure is reported`() =
        runTest {
            val repository = FakeCollectionsMigrationRepository()
            val storage =
                mockCollectionStorage(
                    listOf(
                        collection(id = 1L, title = "Recipes", tabIds = listOf("a", "b"), failingTabIds = listOf("b"))
                    )
                )

            collectionsToTabGroupsMigration(repository, storage).migrateIfNeeded()

            val breadcrumbs = mutableListOf<Breadcrumb>()
            verify { crashReporter.recordCrashBreadcrumb(capture(breadcrumbs)) }

            val failure = breadcrumbs.single { it.level == Breadcrumb.Level.ERROR }
            assertEquals("Unable to migrate collection with 2 items to a tab group", failure.message)

            verify(exactly = 1) { crashReporter.submitCaughtException(any<IllegalStateException>()) }
            assertEquals(mapOf("migratedAll" to "false"), breadcrumbs.last().data)
            assertFalse(repository.hasMigratedCollections())
            assertFalse(repository.shouldShowCollectionsMigrationCard())
        }
}

private class FakeTabCollection(
    override val id: Long,
    override val title: String,
    override val tabs: List<Tab>,
) : TabCollection {

    override fun restore(context: Context, engine: Engine, restoreSessionId: Boolean): List<RecoverableTab> =
        restoreSubset(context = context, engine = engine, tabs = tabs, restoreSessionId = restoreSessionId)

    override fun restoreSubset(
        context: Context,
        engine: Engine,
        tabs: List<Tab>,
        restoreSessionId: Boolean,
    ): List<RecoverableTab> = tabs.mapNotNull { it.restore(context.filesDir, engine, restoreSessionId) }
}

private class FakeCollectionTab(
    private val tabId: String,
    private val restorable: Boolean = true,
    private val restoreException: Exception? = null,
) : Tab {

    override val id: Long = tabId.hashCode().toLong()
    override val title: String = "Title of $tabId"
    override val url: String = "https://example.com/$tabId"

    override fun restore(filesDir: File, engine: Engine, restoreSessionId: Boolean): RecoverableTab? =
        if (restoreException != null) {
            throw restoreException
        } else if (restorable) {
            RecoverableTab(engineSessionState = null, state = TabState(id = tabId, url = url))
        } else {
            null
        }
}
