/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import androidx.datastore.core.DataStore
import java.io.IOException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.datastore.CustomizedMenuRecord

/**
 * Persists the user's customization of the browser menu, always adapted to the sections currently in the app.
 *
 * @param dataStore Where the customization is stored.
 * @param defaultMenuInfo The structure of the browser menu for users who didn't customize it.
 * @param requiredSectionIds Ids of the sections of the browser menu which cannot be removed.
 * @param scope [CoroutineScope] outliving any screen, used for reading and writing the customization.
 */
class MenuCustomizationRepository(
    private val dataStore: DataStore<CustomizedMenuRecord>,
    private val defaultMenuInfo: () -> CustomizedMenuInfo,
    private val requiredSectionIds: Set<String>,
    private val scope: CoroutineScope,
) {
    /** The current structure of the menu, as customized by the user or the default one, or `null` until it was read. */
    val customizedMenuInfo: StateFlow<CustomizedMenuInfo?> =
        dataStore.data
            .map { it.toCustomizedMenuInfo() }
            .catch { exception ->
                logger.error("Could not read the browser menu customization", exception)
                emit(defaultMenuInfo())
            }
            .stateIn(scope, SharingStarted.Eagerly, null)

    /**
     * Persist a new customization.
     *
     * @param visibleSectionIds Ids of the sections to show, in the order they should be shown in.
     * @param hiddenSectionIds Ids of the sections the user removed from the menu.
     */
    fun save(visibleSectionIds: List<String>, hiddenSectionIds: List<String>) {
        scope.launch { update { CustomizedMenuInfo(visibleSectionIds, hiddenSectionIds).reconciled().toRecord() } }
    }

    /**
     * Start reading the stored customization ahead of the menu being opened. It is read as soon as this repository is
     * created, so this only ensures that it is.
     */
    fun warmUp() = Unit

    /** Reconcile the user customized menu structure with the sections currently available in the app. */
    private fun CustomizedMenuInfo.reconciled() = reconciledWith(defaultMenuInfo(), requiredSectionIds)

    private val CustomizedMenuRecord.isCustomized
        get() = schemaVersion == SCHEMA_VERSION

    private fun CustomizedMenuRecord.toCustomizedMenuInfo(): CustomizedMenuInfo =
        if (isCustomized) {
            CustomizedMenuInfo(
                    visibleSectionIds = sectionsList.map { it.id },
                    hiddenSectionIds = hiddenSectionsList.map { it.id },
                )
                .reconciled()
        } else {
            defaultMenuInfo()
        }

    private fun CustomizedMenuInfo.toRecord(): CustomizedMenuRecord =
        CustomizedMenuRecord.newBuilder()
            .setSchemaVersion(SCHEMA_VERSION)
            .addAllSections(visibleSectionIds.map { it.toSectionRecord() })
            .addAllHiddenSections(hiddenSectionIds.map { it.toSectionRecord() })
            .build()

    private fun String.toSectionRecord(): CustomizedMenuRecord.Section =
        CustomizedMenuRecord.Section.newBuilder().setId(this).build()

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
