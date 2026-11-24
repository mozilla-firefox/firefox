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
import org.mozilla.fenix.datastore.CustomizedMenuRecord.CustomizedSection

@OptIn(ExperimentalCoroutinesApi::class)
class MenuCustomizationRepositoryTest {
    private val defaultMenuInfo = menuInfo(shown = listOf("a", "b", "c"), hidden = listOf("d"))
    private val defaultToolsInfo = sectionInfo("tools", shown = listOf(item("x"), item("y"), item("more", "z", "w")))

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
    fun `GIVEN the user moved the navigation section WHEN reading the customization THEN return it as moved`() =
        runTest {
            val dataStore =
                FakeDataStore(
                    record(shown = listOf("c", "b", "a"), hidden = listOf("d"), isNavigationSectionMoved = true)
                )
            val repository = repository(dataStore)
            runCurrent()

            assertEquals(
                menuInfo(shown = listOf("c", "b", "a"), hidden = listOf("d"), isNavigationSectionMoved = true),
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

        repository.save(menuInfo(shown = listOf("b", "a"), hidden = listOf("c")))
        runCurrent()

        assertEquals(record(shown = listOf("b", "a"), hidden = listOf("c", "d")), dataStore.record)
        assertEquals(menuInfo(shown = listOf("b", "a"), hidden = listOf("c", "d")), repository.customizedMenuInfo.value)
    }

    @Test
    fun `WHEN saving a customization with the navigation section moved THEN persist that it was moved`() = runTest {
        val dataStore = FakeDataStore(CustomizedMenuRecord.getDefaultInstance())
        val repository = repository(dataStore)

        repository.save(menuInfo(shown = listOf("b", "a"), hidden = listOf("c"), isNavigationSectionMoved = true))
        runCurrent()

        assertEquals(
            record(shown = listOf("b", "a"), hidden = listOf("c", "d"), isNavigationSectionMoved = true),
            dataStore.record,
        )
        assertEquals(
            menuInfo(shown = listOf("b", "a"), hidden = listOf("c", "d"), isNavigationSectionMoved = true),
            repository.customizedMenuInfo.value,
        )
    }

    @Test
    fun `WHEN saving a customization hiding a section that cannot be removed THEN persist that section as shown`() =
        runTest {
            val dataStore = FakeDataStore(CustomizedMenuRecord.getDefaultInstance())
            val repository = repository(dataStore, requiredSectionIds = setOf("c"))

            repository.save(menuInfo(shown = listOf("a", "b"), hidden = listOf("c")))
            runCurrent()

            assertEquals(record(shown = listOf("a", "b", "c"), hidden = listOf("d")), dataStore.record)
        }

    @Test
    fun `GIVEN the menu was never customized WHEN reading the items of the sections THEN return none as customized`() =
        runTest {
            val repository = repository(FakeDataStore(CustomizedMenuRecord.getDefaultInstance()))
            runCurrent()

            assertEquals(emptyMap(), repository.customizedSectionsInfo.value)
        }

    @Test
    fun `GIVEN the items of a section were customized WHEN reading them THEN return them with only the items currently in the app`() =
        runTest {
            val saved = sectionInfo("tools", shown = listOf(item("more", "w", "removed"), item("y")))
            val dataStore =
                FakeDataStore(record(shown = emptyList(), hidden = emptyList(), customizedSections = listOf(saved)))
            val repository = repository(dataStore)
            runCurrent()

            assertEquals(
                mapOf("tools" to sectionInfo("tools", shown = listOf(item("x"), item("more", "z", "w"), item("y")))),
                repository.customizedSectionsInfo.value,
            )
        }

    @Test
    fun `GIVEN items were saved for a section whose items cannot be customized WHEN reading them THEN ignore them`() =
        runTest {
            val saved = sectionInfo("library", shown = listOf(item("x")))
            val dataStore =
                FakeDataStore(record(shown = emptyList(), hidden = emptyList(), customizedSections = listOf(saved)))
            val repository = repository(dataStore)
            runCurrent()

            assertEquals(emptyMap(), repository.customizedSectionsInfo.value)
        }

    @Test
    fun `GIVEN the items of a section were saved in an unknown format WHEN reading them THEN return none as customized`() =
        runTest {
            val saved = sectionInfo("tools", shown = listOf(item("y"), item("x"), item("more", "z", "w")))
            val dataStore =
                FakeDataStore(
                    record(
                        shown = emptyList(),
                        hidden = emptyList(),
                        schemaVersion = 2,
                        customizedSections = listOf(saved),
                    )
                )
            val repository = repository(dataStore)
            runCurrent()

            assertEquals(emptyMap(), repository.customizedSectionsInfo.value)
        }

    @Test
    fun `GIVEN the customization cannot be read WHEN reading the items of the sections THEN return none as customized`() =
        runTest {
            val repository = repository(UnreadableDataStore)
            runCurrent()

            assertEquals(emptyMap(), repository.customizedSectionsInfo.value)
        }

    @Test
    fun `WHEN saving the items of a section THEN persist them with all the items currently in the app and keep the saved sections`() =
        runTest {
            val dataStore = FakeDataStore(record(shown = listOf("b", "a"), hidden = listOf("c", "d")))
            val repository = repository(dataStore)

            repository.save(
                sectionInfo("tools", shown = listOf(item("y"), item("more", "w")), hidden = listOf(item("x")))
            )
            runCurrent()

            val expected =
                sectionInfo("tools", shown = listOf(item("y"), item("more", "z", "w")), hidden = listOf(item("x")))
            assertEquals(
                record(shown = listOf("b", "a"), hidden = listOf("c", "d"), customizedSections = listOf(expected)),
                dataStore.record,
            )
            assertEquals(mapOf("tools" to expected), repository.customizedSectionsInfo.value)
            assertEquals(
                menuInfo(shown = listOf("b", "a"), hidden = listOf("c", "d")),
                repository.customizedMenuInfo.value,
            )
        }

    @Test
    fun `GIVEN the menu was never customized WHEN saving the items of a section THEN keep showing the default menu sections`() =
        runTest {
            val dataStore = FakeDataStore(CustomizedMenuRecord.getDefaultInstance())
            val repository = repository(dataStore)

            repository.save(sectionInfo("tools", shown = listOf(item("y"), item("x"), item("more", "z", "w"))))
            runCurrent()

            assertEquals(defaultMenuInfo, repository.customizedMenuInfo.value)
        }

    @Test
    fun `WHEN saving the items of a section whose items cannot be customized THEN don't persist them`() = runTest {
        val dataStore = FakeDataStore(CustomizedMenuRecord.getDefaultInstance())
        val repository = repository(dataStore)

        repository.save(sectionInfo("library", shown = listOf(item("x"))))
        runCurrent()

        assertEquals(CustomizedMenuRecord.getDefaultInstance(), dataStore.record)
    }

    @Test
    fun `WHEN saving the menu sections THEN keep the saved items of the sections`() = runTest {
        val saved = sectionInfo("tools", shown = listOf(item("y"), item("x"), item("more", "z", "w")))
        val dataStore =
            FakeDataStore(
                record(shown = listOf("a", "b", "c"), hidden = listOf("d"), customizedSections = listOf(saved))
            )
        val repository = repository(dataStore)

        repository.save(menuInfo(shown = listOf("c", "b", "a"), hidden = listOf("d")))
        runCurrent()

        assertEquals(
            record(shown = listOf("c", "b", "a"), hidden = listOf("d"), customizedSections = listOf(saved)),
            dataStore.record,
        )
        assertEquals(mapOf("tools" to saved), repository.customizedSectionsInfo.value)
    }

    @Test
    fun `WHEN resetting the customization THEN restore both the default menu sections and their default items`() =
        runTest {
            val saved = sectionInfo("tools", shown = listOf(item("y"), item("x"), item("more", "z", "w")))
            val dataStore =
                FakeDataStore(
                    record(shown = listOf("c", "b", "a"), hidden = listOf("d"), customizedSections = listOf(saved))
                )
            val repository = repository(dataStore)

            repository.reset()
            runCurrent()

            assertEquals(defaultMenuInfo, repository.customizedMenuInfo.value)
            assertEquals(emptyMap(), repository.customizedSectionsInfo.value)
        }

    // The customization is read on a scope that runTest cancels at the end of each test.
    private fun TestScope.repository(
        dataStore: DataStore<CustomizedMenuRecord>,
        requiredSectionIds: Set<String> = emptySet(),
    ) =
        MenuCustomizationRepository(
            dataStore = dataStore,
            defaultMenuInfo = { defaultMenuInfo },
            defaultSectionsInfo = mapOf("tools" to defaultToolsInfo),
            requiredSectionIds = requiredSectionIds,
            scope = backgroundScope,
        )

    private fun menuInfo(shown: List<String>, hidden: List<String>, isNavigationSectionMoved: Boolean = false) =
        CustomizedMenuInfo(
            visibleSectionIds = shown,
            hiddenSectionIds = hidden,
            isNavigationSectionMoved = isNavigationSectionMoved,
        )

    private fun record(
        shown: List<String>,
        hidden: List<String>,
        schemaVersion: Int = 1,
        isNavigationSectionMoved: Boolean = false,
        customizedSections: List<CustomizedSectionInfo> = emptyList(),
    ) =
        CustomizedMenuRecord.newBuilder()
            .setSchemaVersion(schemaVersion)
            .addAllSections(shown.map { section(it) })
            .addAllHiddenSections(hidden.map { section(it) })
            .setIsNavigationSectionMoved(isNavigationSectionMoved)
            .addAllCustomizedSections(customizedSections.map { it.toRecord() })
            .build()

    private fun section(id: String) = CustomizedMenuRecord.Section.newBuilder().setId(id).build()

    private fun CustomizedSectionInfo.toRecord() =
        CustomizedSection.newBuilder()
            .setSectionId(sectionId)
            .addAllItems(items.map { it.toRecord() })
            .addAllHiddenItems(hiddenItems.map { it.toRecord() })
            .build()

    private fun CustomizedItemInfo.toRecord() =
        CustomizedSection.Item.newBuilder().setId(id).addAllSubItemIds(subItemIds).build()

    private fun sectionInfo(
        sectionId: String,
        shown: List<CustomizedItemInfo>,
        hidden: List<CustomizedItemInfo> = emptyList(),
    ) = CustomizedSectionInfo(sectionId = sectionId, items = shown, hiddenItems = hidden)

    private fun item(id: String, vararg subItemIds: String) =
        CustomizedItemInfo(id = id, subItemIds = subItemIds.toList())

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
