/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import androidx.datastore.core.DataStore
import java.io.IOException
import kotlin.test.assertEquals
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Test
import org.mozilla.fenix.datastore.CustomizedMenuRecord

@OptIn(ExperimentalCoroutinesApi::class)
class MenuCustomizationRepositoryTest {
    private val defaultMenuInfo = menuInfo(shown = listOf("a", "b", "c"), hidden = listOf("d"))

    @Test
    fun `GIVEN the menu was never customized WHEN reading the customization THEN return the default menu structure`() =
        runTest {
            val repository = repository(FakeDataStore(CustomizedMenuRecord.getDefaultInstance()))
            runCurrent()

            assertEquals(defaultMenuInfo, repository.customizedMenuInfo.value)
        }

    @Test
    fun `GIVEN the menu was customized WHEN reading the customization THEN return it with only the sections currently in the app`() =
        runTest {
            val dataStore = FakeDataStore(record(shown = listOf("c", "removed", "a"), hidden = emptyList()))
            val repository = repository(dataStore)
            runCurrent()

            assertEquals(
                menuInfo(shown = listOf("c", "b", "a"), hidden = listOf("d")),
                repository.customizedMenuInfo.value,
            )
        }

    @Test
    fun `GIVEN the customization was saved in an unknown format WHEN reading it THEN return the default menu structure`() =
        runTest {
            val dataStore = FakeDataStore(record(shown = listOf("c", "a"), hidden = listOf("b"), schemaVersion = 2))
            val repository = repository(dataStore)
            runCurrent()

            assertEquals(defaultMenuInfo, repository.customizedMenuInfo.value)
        }

    @Test
    fun `GIVEN the customization cannot be read WHEN reading it THEN return the default menu structure`() = runTest {
        val repository = repository(UnreadableDataStore)
        runCurrent()

        assertEquals(defaultMenuInfo, repository.customizedMenuInfo.value)
    }

    @Test
    fun `WHEN warming up THEN the customization is read before anything observes it`() = runTest {
        val repository = repository(FakeDataStore(record(shown = listOf("c", "b", "a"), hidden = listOf("d"))))

        repository.warmUp()
        runCurrent()

        assertEquals(menuInfo(shown = listOf("c", "b", "a"), hidden = listOf("d")), repository.customizedMenuInfo.value)
    }

    @Test
    fun `WHEN saving a customization THEN persist it with all the sections currently in the app`() = runTest {
        val dataStore = FakeDataStore(CustomizedMenuRecord.getDefaultInstance())
        val repository = repository(dataStore)

        repository.save(visibleSectionIds = listOf("b", "a"), hiddenSectionIds = listOf("c"))
        runCurrent()

        assertEquals(record(shown = listOf("b", "a"), hidden = listOf("c", "d")), dataStore.record)
        assertEquals(menuInfo(shown = listOf("b", "a"), hidden = listOf("c", "d")), repository.customizedMenuInfo.value)
    }

    @Test
    fun `WHEN saving a customization hiding a section that cannot be removed THEN persist that section as shown`() =
        runTest {
            val dataStore = FakeDataStore(CustomizedMenuRecord.getDefaultInstance())
            val repository = repository(dataStore, requiredSectionIds = setOf("c"))

            repository.save(visibleSectionIds = listOf("a", "b"), hiddenSectionIds = listOf("c"))
            runCurrent()

            assertEquals(record(shown = listOf("a", "b", "c"), hidden = listOf("d")), dataStore.record)
        }

    // The customization is read on a scope that runTest cancels at the end of each test.
    private fun TestScope.repository(
        dataStore: DataStore<CustomizedMenuRecord>,
        requiredSectionIds: Set<String> = emptySet(),
    ) =
        MenuCustomizationRepository(
            dataStore = dataStore,
            defaultMenuInfo = { defaultMenuInfo },
            requiredSectionIds = requiredSectionIds,
            scope = backgroundScope,
        )

    private fun menuInfo(shown: List<String>, hidden: List<String>) =
        CustomizedMenuInfo(visibleSectionIds = shown, hiddenSectionIds = hidden)

    private fun record(shown: List<String>, hidden: List<String>, schemaVersion: Int = 1) =
        CustomizedMenuRecord.newBuilder()
            .setSchemaVersion(schemaVersion)
            .addAllSections(shown.map { section(it) })
            .addAllHiddenSections(hidden.map { section(it) })
            .build()

    private fun section(id: String) = CustomizedMenuRecord.Section.newBuilder().setId(id).build()

    private class FakeDataStore(initial: CustomizedMenuRecord) : DataStore<CustomizedMenuRecord> {
        private val records = MutableStateFlow(initial)

        val record: CustomizedMenuRecord
            get() = records.value

        override val data: Flow<CustomizedMenuRecord> = records

        override suspend fun updateData(
            transform: suspend (t: CustomizedMenuRecord) -> CustomizedMenuRecord
        ): CustomizedMenuRecord = transform(records.value).also { records.value = it }
    }

    private object UnreadableDataStore : DataStore<CustomizedMenuRecord> {
        override val data: Flow<CustomizedMenuRecord> = flow { throw IOException("Cannot read") }

        override suspend fun updateData(
            transform: suspend (t: CustomizedMenuRecord) -> CustomizedMenuRecord
        ): CustomizedMenuRecord = throw IOException("Cannot write")
    }
}
