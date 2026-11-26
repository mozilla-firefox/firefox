/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import androidx.datastore.core.DataStore
import java.io.IOException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.datastore.CustomizedMenuRecord
import org.mozilla.fenix.datastore.CustomizedMenuRecord.CustomizedSection

/**
 * Persists the user's customization of the browser menu, always adapted to the sections and items currently in the app.
 *
 * @param dataStore Where the customization is stored.
 * @param defaultMenuInfo The structure of the browser menu for users who didn't customize it.
 * @param defaultSectionsInfo The items of each section whose items can be customized, keyed by section id, for users
 *   who didn't customize them.
 * @param requiredSectionIds Ids of the sections of the browser menu which cannot be removed.
 * @param topLevelOnlyItemIds Ids of the menu items which can only be directly in their section, never inside another
 *   item.
 * @param scope [CoroutineScope] outliving any screen, used for reading and writing the customization.
 */
class MenuCustomizationRepository(
    private val dataStore: DataStore<CustomizedMenuRecord>,
    private val defaultMenuInfo: () -> CustomizedMenuInfo,
    private val defaultSectionsInfo: Map<String, CustomizedSectionInfo>,
    private val requiredSectionIds: Set<String>,
    private val topLevelOnlyItemIds: Set<String>,
    private val scope: CoroutineScope,
) {
    private val lock = Any()

    // Both are guarded by lock. The record is null until the stored customization was read, until then the changes
    // are kept as pending and applied on top of it.
    private var record: CustomizedMenuRecord? = null
    private val pending = mutableListOf<(CustomizedMenuRecord) -> CustomizedMenuRecord>()

    // Each write waits for the one before it, so that they reach the data store in the order of the saves whatever
    // the dispatcher of scope.
    private var lastWrite: Job? = null

    private val mutableMenuInfo = MutableStateFlow<CustomizedMenuInfo?>(null)
    private val mutableSectionsInfo = MutableStateFlow<Map<String, CustomizedSectionInfo>?>(null)

    /** The current structure of the menu, as customized by the user or the default one, or `null` until it was read. */
    val customizedMenuInfo: StateFlow<CustomizedMenuInfo?> = mutableMenuInfo

    /**
     * The items of each section as customized by the user, keyed by section id, or `null` until they were read.
     * Sections whose items were not customized are not included.
     */
    val customizedSectionsInfo: StateFlow<Map<String, CustomizedSectionInfo>?> = mutableSectionsInfo

    init {
        scope.launch {
            val stored =
                dataStore.data
                    .catch { exception ->
                        logger.error("Could not read the browser menu customization", exception)
                        emit(CustomizedMenuRecord.getDefaultInstance())
                    }
                    .first()
            synchronized(lock) {
                record = pending.fold(stored) { current, transform -> transform(current) }
                pending.clear()
                publish()
            }
        }
    }

    /** Persist a new customization of the menu sections, keeping the one of their items. */
    fun save(menuInfo: CustomizedMenuInfo) {
        val reconciled = menuInfo.reconciled()
        persist { it.toCurrentBuilder().withMenuInfo(reconciled).build() }
    }

    /** Persist a new customization of the items of a section, keeping the one of the menu sections. */
    fun save(sectionInfo: CustomizedSectionInfo) {
        val default = defaultSectionsInfo[sectionInfo.sectionId] ?: return
        val reconciled = sectionInfo.reconciledWith(default, topLevelOnlyItemIds)
        persist { it.toCurrentBuilder().withSectionInfo(reconciled).build() }
    }

    /** Reset the customization of both the menu sections and their items back to default. */
    fun reset() {
        persist { CustomizedMenuRecord.getDefaultInstance() }
    }

    /**
     * Start reading the stored customization ahead of the menu being opened. It is read as soon as this repository is
     * created, so this only ensures that it is.
     */
    fun warmUp() = Unit

    /** Report the change right away, then write it. */
    private fun persist(transform: (CustomizedMenuRecord) -> CustomizedMenuRecord) {
        synchronized(lock) {
            val previousWrite = lastWrite
            lastWrite = scope.launch {
                previousWrite?.join()
                update(transform)
            }
            val current = record
            if (current == null) {
                pending.add(transform)
            } else {
                record = transform(current)
                publish()
            }
        }
    }

    // Reads the record for each flow, so that a change made by a collector of the first flow ends up in both.
    // Collectors may run, and call back into the repository, inside lock, so they must not wait for work that needs
    // it on another thread.
    private fun publish() {
        mutableMenuInfo.value = record?.toCustomizedMenuInfo()
        mutableSectionsInfo.value = record?.toCustomizedSectionsInfo()
    }

    /** Reconcile the user customized menu structure with the sections currently available in the app. */
    private fun CustomizedMenuInfo.reconciled() = reconciledWith(defaultMenuInfo(), requiredSectionIds)

    private val CustomizedMenuRecord.isCustomized
        get() = schemaVersion == SCHEMA_VERSION

    private fun CustomizedMenuRecord.toCustomizedMenuInfo(): CustomizedMenuInfo =
        if (isCustomized) {
            CustomizedMenuInfo(
                    visibleSectionIds = sectionsList.map { it.id },
                    hiddenSectionIds = hiddenSectionsList.map { it.id },
                    isNavigationSectionMoved = isNavigationSectionMoved,
                )
                .reconciled()
        } else {
            defaultMenuInfo()
        }

    private fun CustomizedMenuRecord.toCustomizedSectionsInfo(): Map<String, CustomizedSectionInfo> =
        if (isCustomized) {
            customizedSectionsList
                .mapNotNull { section ->
                    defaultSectionsInfo[section.sectionId]?.let {
                        section.toCustomizedSectionInfo().reconciledWith(it, topLevelOnlyItemIds)
                    }
                }
                .associateBy { it.sectionId }
        } else {
            emptyMap()
        }

    private fun CustomizedSection.toCustomizedSectionInfo() =
        CustomizedSectionInfo(
            sectionId = sectionId,
            items = itemsList.map { it.toCustomizedItemInfo() },
            hiddenItems = hiddenItemsList.map { it.toCustomizedItemInfo() },
        )

    private fun CustomizedSection.Item.toCustomizedItemInfo() =
        CustomizedItemInfo(id = id, subItemIds = subItemIdsList.toList())

    /** A builder of this record, or of an empty one of the current version if this is of another version. */
    private fun CustomizedMenuRecord.toCurrentBuilder(): CustomizedMenuRecord.Builder =
        if (isCustomized) {
            toBuilder()
        } else {
            CustomizedMenuRecord.newBuilder().setSchemaVersion(SCHEMA_VERSION)
        }

    private fun CustomizedMenuRecord.Builder.withMenuInfo(menuInfo: CustomizedMenuInfo) =
        clearSections()
            .addAllSections(menuInfo.visibleSectionIds.map { it.toSectionRecord() })
            .clearHiddenSections()
            .addAllHiddenSections(menuInfo.hiddenSectionIds.map { it.toSectionRecord() })
            .setIsNavigationSectionMoved(menuInfo.isNavigationSectionMoved)

    private fun CustomizedMenuRecord.Builder.withSectionInfo(sectionInfo: CustomizedSectionInfo) = apply {
        val otherSections = customizedSectionsList.filterNot { it.sectionId == sectionInfo.sectionId }
        clearCustomizedSections()
        addAllCustomizedSections(otherSections + sectionInfo.toRecord())
    }

    private fun String.toSectionRecord(): CustomizedMenuRecord.Section =
        CustomizedMenuRecord.Section.newBuilder().setId(this).build()

    private fun CustomizedSectionInfo.toRecord(): CustomizedSection =
        CustomizedSection.newBuilder()
            .setSectionId(sectionId)
            .addAllItems(items.map { it.toRecord() })
            .addAllHiddenItems(hiddenItems.map { it.toRecord() })
            .build()

    private fun CustomizedItemInfo.toRecord(): CustomizedSection.Item =
        CustomizedSection.Item.newBuilder().setId(id).addAllSubItemIds(subItemIds).build()

    private suspend fun update(transform: (CustomizedMenuRecord) -> CustomizedMenuRecord) {
        try {
            dataStore.updateData { transform(it) }
        } catch (exception: IOException) {
            logger.error("Could not persist the browser menu customization", exception)
        }
    }

    private companion object {
        const val SCHEMA_VERSION = 1
        val logger = Logger("MenuCustomizationRepository")
    }
}

/**
 * The items of each menu section as customized by the user, keyed by section id, or no customization if [enabled] is
 * `false`, this repository is `null` or the customization was not read yet.
 */
fun MenuCustomizationRepository?.customizedSectionsInfoIfEnabled(enabled: Boolean): Map<String, CustomizedSectionInfo> =
    if (enabled) this?.customizedSectionsInfo?.value.orEmpty() else emptyMap()

/**
 * Observe the items of each menu section as customized by the user, keyed by section id, with no customization if
 * [enabled] is `false`, this repository is `null` or the customization was not read yet.
 */
fun MenuCustomizationRepository?.customizedSectionsInfoFlow(
    enabled: Boolean
): Flow<Map<String, CustomizedSectionInfo>> =
    if (enabled && this != null) {
        customizedSectionsInfo.map { it.orEmpty() }
    } else {
        flowOf(emptyMap())
    }
