/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.collections.migration

import android.database.sqlite.SQLiteException
import java.io.File
import java.io.IOException
import java.util.UUID
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.state.recover.RecoverableTab
import mozilla.components.browser.state.state.recover.TabState
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.engine.Engine
import mozilla.components.feature.tab.collections.Tab
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.feature.tabgroups.storage.data.TabGroup
import mozilla.components.feature.tabgroups.storage.repository.TabGroupRepository
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.DateTimeProvider
import mozilla.components.support.utils.DefaultDateTimeProvider
import org.mozilla.fenix.components.TabCollectionStorage
import org.mozilla.fenix.tabstray.data.TabGroupTheme

private const val BREADCRUMB_CATEGORY = "CollectionsToTabGroupsMigration"

/**
 * Class that handles migrating stored [TabCollection]s into [TabGroup]s. Every tab in a [TabCollection] is restored as
 * a tab and then grouped under a closed [TabGroup] named after the collection.
 *
 * @param tabCollectionStorage The [TabCollectionStorage] containing the existing collections.
 * @param restoreUseCase Use case for restoring a collection's tabs.
 * @param tabGroupRepository [TabGroupRepository] used to observe and modify tab group data.
 * @param collectionsMigrationRepository [CollectionsMigrationRepository] used to read and record the migration
 *   progress.
 * @param engine The [Engine] implementation for restoring the engine state.
 * @param filesDir [File] directory holding the collections' on-disk session snapshots.
 * @param dateTimeProvider The [DateTimeProvider] used to get the current time.
 * @param crashReporter [CrashReporting] instance used for recording breadcrumbs and caught exceptions.
 * @param ioDispatcher The [CoroutineDispatcher] used for reading the collections' session snapshots from disk and
 *   persisting the migration progress.
 */
class CollectionsToTabGroupsMigration(
    private val tabCollectionStorage: TabCollectionStorage,
    private val restoreUseCase: TabsUseCases.RestoreUseCase,
    private val tabGroupRepository: TabGroupRepository,
    private val collectionsMigrationRepository: CollectionsMigrationRepository,
    private val engine: Engine,
    private val filesDir: File,
    private val dateTimeProvider: DateTimeProvider = DefaultDateTimeProvider(),
    private val crashReporter: CrashReporting? = null,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) {

    private val logger = Logger("CollectionsToTabGroupsMigration")

    /**
     * Migrates the stored [TabCollection]s to [TabGroup]s. Every tab in a [TabCollection] is restored as a tab and then
     * grouped under a closed [TabGroup] named after the collection.
     *
     * The migration can be run again if a collection fails to migrate at any point. The collections that were already
     * migrated are recorded so they are skipped by the next attempt.
     *
     * @return true when all [TabCollection]s have been migrated or if the migration should be skipped and false
     *   otherwise.
     */
    suspend fun migrateIfNeeded(): Boolean {
        if (!collectionsMigrationRepository.shouldMigrate()) {
            return true
        }

        val collections = getCollections() ?: return false
        val migratedCollectionIds = collectionsMigrationRepository.getMigratedCollectionIds()
        val pendingCollections = collections.filterNot { migratedCollectionIds.contains(it.id.toString()) }

        recordBreadcrumb(
            message = "Migrating collections to tab groups",
            data = mapOf("collections" to pendingCollections.size.toString()),
        )

        val results = pendingCollections.map { collection -> migrateCollection(collection = collection) }
        val migratedAllCollections = results.all {
            it == MigrationResult.TAB_GROUP_CREATED || it == MigrationResult.NO_TABS_TO_MIGRATE
        }

        recordBreadcrumb(
            message = "Migrated collections to tab groups",
            data = mapOf("migratedAll" to migratedAllCollections.toString()),
        )

        if (migratedAllCollections) {
            collectionsMigrationRepository.updateHasMigratedCollections(migrated = true)
            collectionsMigrationRepository.clearMigratedCollectionIds()

            // Only advertise the migration once every collection has been migrated, and it actually produced a tab
            // group, so that a user without any collections is not shown the migration card.
            if (results.any { it == MigrationResult.TAB_GROUP_CREATED }) {
                collectionsMigrationRepository.updateCollectionsMigrationCardVisibility(visible = true)
            }
        }

        return migratedAllCollections
    }

    /** Returns the stored [TabCollection]s or null if the collection storage could not be read. */
    @Suppress("TooGenericExceptionCaught")
    private suspend fun getCollections(): List<TabCollection>? =
        try {
            tabCollectionStorage.getCollectionsList()
        } catch (e: Exception) {
            reportFailure(message = "Unable to fetch collections from storage", throwable = e)
            null
        }

    /**
     * Migrates a single [TabCollection] to a [TabGroup] and returns the [MigrationResult].
     *
     * The broad exception catch is deliberate to handle unexpected exceptions. Restoring a tab reads and deserializes
     * an on-disk session snapshot whose reader only catches [IOException]. Creating the tab group is a Room write that
     * can throw [SQLiteException]. Exceptions are caught and reported, and returns [MigrationResult.FAILED] for the
     * next retry.
     *
     * @param collection The [TabCollection] to migrate.
     * @return The [MigrationResult] for the given [collection].
     */
    @Suppress("TooGenericExceptionCaught")
    private suspend fun migrateCollection(collection: TabCollection): MigrationResult =
        try {
            val result = migrateCollectionToTabGroup(collection = collection)

            if (result != MigrationResult.FAILED) {
                // Only record the collection as migrated once it succeeded, so a failed collection is retried.
                collectionsMigrationRepository.addMigratedCollectionId(collectionId = collection.id.toString())
            }

            result
        } catch (e: Exception) {
            reportFailure(
                message = "Unable to migrate collection with ${collection.tabs.size} items to a tab group",
                throwable = e,
            )
            MigrationResult.FAILED
        }

    /** Migrates a [TabCollection] to a closed [TabGroup] and returns the [MigrationResult]. */
    private suspend fun migrateCollectionToTabGroup(collection: TabCollection): MigrationResult =
        withContext(ioDispatcher) {
            val recoverableTabs =
                collection.tabs.map { tab ->
                    // Fall back to the url and title stored in the tab collection when the tab's saved
                    // session state could not be restored.
                    tab.restore(filesDir, engine) ?: tab.toRecoverableTab()
                }

            if (recoverableTabs.isEmpty()) {
                MigrationResult.NO_TABS_TO_MIGRATE
            } else {
                restoreUseCase.invoke(tabs = recoverableTabs)
                tabGroupRepository.createTabGroupWithTabs(
                    tabGroup =
                        TabGroup(
                            title = collection.title,
                            theme = TabGroupTheme.default.name,
                            closed = true,
                            lastModified = dateTimeProvider.currentTimeMillis(),
                            tabIds = recoverableTabs.map { it.state.id },
                        )
                )
                MigrationResult.TAB_GROUP_CREATED
            }
        }

    /** The outcome of migrating a single [TabCollection]. */
    private enum class MigrationResult {
        /** A [TabGroup] was created for the collection. */
        TAB_GROUP_CREATED,

        /** The collection had no tabs, so no [TabGroup] was created for it. */
        NO_TABS_TO_MIGRATE,

        /** The collection could not be migrated and is attempted again on a subsequent run. */
        FAILED,
    }

    /**
     * Returns a [RecoverableTab] built from this [Tab]'s stored url and title without any engine state. This means the
     * restored [Tab] will not have its session history such as back/forward history.
     */
    private fun Tab.toRecoverableTab(): RecoverableTab =
        RecoverableTab(
            engineSessionState = null,
            state =
                TabState(
                    id = UUID.randomUUID().toString(),
                    url = url,
                    title = title,
                ),
        )

    private fun reportFailure(message: String, throwable: Throwable) {
        logger.error(message, throwable)
        recordBreadcrumb(message = message, level = Breadcrumb.Level.ERROR)
        crashReporter?.submitCaughtException(throwable)
    }

    private fun recordBreadcrumb(
        message: String,
        data: Map<String, String> = emptyMap(),
        level: Breadcrumb.Level = Breadcrumb.Level.INFO,
    ) {
        crashReporter?.recordCrashBreadcrumb(
            Breadcrumb(
                message = message,
                data = data,
                category = BREADCRUMB_CATEGORY,
                level = level,
            )
        )
    }
}
