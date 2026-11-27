/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import androidx.datastore.core.DataStore
import io.mockk.every
import io.mockk.mockk
import java.io.IOException
import kotlin.coroutines.CoroutineContext
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.emitAll
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.plus
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.UnconfinedTestDispatcher
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
    fun `GIVEN a top level only item was saved inside another item WHEN reading the items of the sections THEN return it at the top level`() =
        runTest {
            val saved = sectionInfo("tools", shown = listOf(item("more", "z", "x", "w"), item("y")))
            val dataStore =
                FakeDataStore(record(shown = emptyList(), hidden = emptyList(), customizedSections = listOf(saved)))
            val repository = repository(dataStore, topLevelOnlyItemIds = setOf("x"))
            runCurrent()

            assertEquals(
                mapOf("tools" to sectionInfo("tools", shown = listOf(item("more", "z", "w"), item("x"), item("y")))),
                repository.customizedSectionsInfo.value,
            )
        }

    @Test
    fun `WHEN saving a top level only item inside another item THEN persist it at the top level`() = runTest {
        val dataStore = FakeDataStore(CustomizedMenuRecord.getDefaultInstance())
        val repository = repository(dataStore, topLevelOnlyItemIds = setOf("x"))

        repository.save(sectionInfo("tools", shown = listOf(item("y"), item("more", "z", "x", "w"))))
        runCurrent()

        val expected = sectionInfo("tools", shown = listOf(item("y"), item("more", "z", "w"), item("x")))
        assertEquals(mapOf("tools" to expected), repository.customizedSectionsInfo.value)
        assertEquals(
            record(shown = emptyList(), hidden = emptyList(), customizedSections = listOf(expected)),
            dataStore.record,
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

    @Test
    fun `GIVEN the customization was read WHEN saving THEN report it right away and persist it once written`() =
        runTest {
            val dataStore = GatedDataStore(CustomizedMenuRecord.getDefaultInstance())
            val repository = repository(dataStore)
            dataStore.releaseRead()
            runCurrent()

            repository.save(menuInfo(shown = listOf("b", "a"), hidden = listOf("c")))
            repository.save(toolsInfo())
            runCurrent()

            assertEquals(
                menuInfo(shown = listOf("b", "a"), hidden = listOf("c", "d")),
                repository.customizedMenuInfo.value,
            )
            assertEquals(mapOf("tools" to toolsInfo()), repository.customizedSectionsInfo.value)
            assertEquals(CustomizedMenuRecord.getDefaultInstance(), dataStore.record)

            dataStore.releaseWrites()
            runCurrent()

            assertEquals(
                record(shown = listOf("b", "a"), hidden = listOf("c", "d"), customizedSections = listOf(toolsInfo())),
                dataStore.record,
            )
        }

    @Test
    fun `GIVEN the customization was read WHEN resetting THEN report the defaults right away`() = runTest {
        val dataStore =
            GatedDataStore(
                record(
                    shown = listOf("c", "b", "a"),
                    hidden = listOf("d"),
                    customizedSections = listOf(toolsInfo()),
                )
            )
        val repository = repository(dataStore)
        dataStore.releaseRead()
        runCurrent()

        repository.reset()

        assertEquals(defaultMenuInfo, repository.customizedMenuInfo.value)
        assertEquals(emptyMap(), repository.customizedSectionsInfo.value)
    }

    @Test
    fun `GIVEN the customization is being read WHEN saving and resetting THEN report them in order once it was read`() =
        runTest {
            val dataStore = GatedDataStore(record(shown = listOf("c", "b", "a"), hidden = listOf("d")))
            val repository = repository(dataStore)
            runCurrent()

            repository.save(menuInfo(shown = listOf("a", "b"), hidden = listOf("c")))
            repository.reset()
            repository.save(toolsInfo())

            assertNull(repository.customizedMenuInfo.value)
            assertNull(repository.customizedSectionsInfo.value)

            dataStore.releaseRead()
            dataStore.releaseWrites()
            runCurrent()

            assertEquals(defaultMenuInfo, repository.customizedMenuInfo.value)
            assertEquals(mapOf("tools" to toolsInfo()), repository.customizedSectionsInfo.value)
            assertEquals(
                CustomizedMenuRecord.newBuilder()
                    .setSchemaVersion(1)
                    .addAllCustomizedSections(listOf(toolsInfo().toRecord()))
                    .build(),
                dataStore.record,
            )
        }

    @Test
    fun `GIVEN a save was made while reading WHEN saving right after it was read THEN keep the earlier save`() =
        runTest {
            val dataStore = GatedDataStore(CustomizedMenuRecord.getDefaultInstance())
            val repository = repository(dataStore)
            runCurrent()
            repository.save(menuInfo(shown = listOf("b", "a"), hidden = listOf("c")))

            dataStore.releaseRead()
            runCurrent()
            repository.save(toolsInfo())

            assertEquals(
                menuInfo(shown = listOf("b", "a"), hidden = listOf("c", "d")),
                repository.customizedMenuInfo.value,
            )
            assertEquals(mapOf("tools" to toolsInfo()), repository.customizedSectionsInfo.value)
        }

    @Test
    fun `GIVEN the customization cannot be read for an unexpected reason WHEN reading it THEN return the defaults`() =
        runTest {
            val repository = repository(UnexpectedlyUnreadableDataStore)
            runCurrent()

            assertEquals(defaultMenuInfo, repository.customizedMenuInfo.value)
            assertEquals(emptyMap(), repository.customizedSectionsInfo.value)
        }

    @Test
    fun `GIVEN the customization cannot be read WHEN saving the items of a section THEN keep the saved menu sections on disk`() =
        runTest {
            val dataStore = FakeDataStore(record(shown = listOf("c", "b", "a"), hidden = listOf("d")))
            dataStore.failReads = true
            val repository = repository(dataStore)
            runCurrent()

            repository.save(toolsInfo())
            runCurrent()

            assertEquals(
                record(shown = listOf("c", "b", "a"), hidden = listOf("d"), customizedSections = listOf(toolsInfo())),
                dataStore.record,
            )
        }

    @Test
    fun `GIVEN the customization cannot be read WHEN saving the menu sections THEN keep the saved items of the sections on disk`() =
        runTest {
            val dataStore =
                FakeDataStore(
                    record(shown = emptyList(), hidden = emptyList(), customizedSections = listOf(toolsInfo()))
                )
            dataStore.failReads = true
            val repository = repository(dataStore)
            runCurrent()

            repository.save(menuInfo(shown = listOf("b", "a"), hidden = listOf("c")))
            runCurrent()

            assertEquals(
                record(shown = listOf("b", "a"), hidden = listOf("c", "d"), customizedSections = listOf(toolsInfo())),
                dataStore.record,
            )
        }

    @Test
    fun `GIVEN writing fails WHEN saving THEN keep the change in memory and persist the next save`() = runTest {
        val dataStore = FakeDataStore(CustomizedMenuRecord.getDefaultInstance())
        val repository = repository(dataStore)
        runCurrent()

        dataStore.failWrites = true
        repository.save(menuInfo(shown = listOf("b", "a"), hidden = listOf("c")))
        runCurrent()

        assertEquals(
            menuInfo(shown = listOf("b", "a"), hidden = listOf("c", "d")),
            repository.customizedMenuInfo.value,
        )
        assertEquals(CustomizedMenuRecord.getDefaultInstance(), dataStore.record)

        dataStore.failWrites = false
        repository.save(toolsInfo())
        runCurrent()

        assertEquals(
            CustomizedMenuRecord.newBuilder()
                .setSchemaVersion(1)
                .addAllCustomizedSections(listOf(toolsInfo().toRecord()))
                .build(),
            dataStore.record,
        )
        assertEquals(
            menuInfo(shown = listOf("b", "a"), hidden = listOf("c", "d")),
            repository.customizedMenuInfo.value,
        )
    }

    @Test
    fun `GIVEN something saves the items when the menu sections change WHEN saving the sections THEN both end on the newest customization`() =
        runTest {
            val repository = repository(FakeDataStore(CustomizedMenuRecord.getDefaultInstance()))
            runCurrent()
            repository.customizedMenuInfo
                .onEach { if (it?.visibleSectionIds == listOf("b", "a", "c")) repository.save(toolsInfo()) }
                .launchIn(backgroundScope + UnconfinedTestDispatcher(testScheduler))

            repository.save(menuInfo(shown = listOf("b", "a", "c"), hidden = listOf("d")))

            assertEquals(mapOf("tools" to toolsInfo()), repository.customizedSectionsInfo.value)
            assertEquals(
                menuInfo(shown = listOf("b", "a", "c"), hidden = listOf("d")),
                repository.customizedMenuInfo.value,
            )
        }

    @Test
    fun `GIVEN something resets the customization when the menu sections change WHEN saving the sections THEN both end on the defaults`() =
        runTest {
            val dataStore = FakeDataStore(CustomizedMenuRecord.getDefaultInstance())
            val repository = repository(dataStore)
            runCurrent()
            repository.customizedMenuInfo
                .onEach { if (it?.visibleSectionIds == listOf("b", "a", "c")) repository.reset() }
                .launchIn(backgroundScope + UnconfinedTestDispatcher(testScheduler))

            repository.save(menuInfo(shown = listOf("b", "a", "c"), hidden = listOf("d")))
            runCurrent()

            assertEquals(CustomizedMenuRecord.getDefaultInstance(), dataStore.record)
            assertEquals(defaultMenuInfo, repository.customizedMenuInfo.value)
            assertEquals(emptyMap(), repository.customizedSectionsInfo.value)
        }

    @Test
    fun `GIVEN a scope that runs coroutines in reverse order WHEN saving then resetting THEN the data store ends on the defaults`() =
        runTest {
            val dispatcher = ReversingDispatcher()
            val dataStore = FakeDataStore(CustomizedMenuRecord.getDefaultInstance())
            val repository = repository(dataStore, scope = CoroutineScope(dispatcher))
            dispatcher.runAll()

            repository.save(menuInfo(shown = listOf("b", "a"), hidden = listOf("c")))
            repository.reset()
            dispatcher.runAll()

            assertEquals(CustomizedMenuRecord.getDefaultInstance(), dataStore.record)
        }

    @Test
    fun `GIVEN no repository WHEN checking whether the customization is read THEN return true`() {
        val repository: MenuCustomizationRepository? = null

        assertTrue(
            repository.isMenuHighlightCustomizationRead(
                isMenuCustomizationScreenEnabled = false,
                isToolsCustomizationEnabled = false,
            )
        )
        assertTrue(
            repository.isMenuHighlightCustomizationRead(
                isMenuCustomizationScreenEnabled = true,
                isToolsCustomizationEnabled = false,
            )
        )
        assertTrue(
            repository.isMenuHighlightCustomizationRead(
                isMenuCustomizationScreenEnabled = false,
                isToolsCustomizationEnabled = true,
            )
        )
        assertTrue(
            repository.isMenuHighlightCustomizationRead(
                isMenuCustomizationScreenEnabled = true,
                isToolsCustomizationEnabled = true,
            )
        )
    }

    @Test
    fun `GIVEN the menu customization screen is off WHEN the menu customization is not read yet THEN return true`() {
        val repository = mockRepository(customizedMenuInfo = MutableStateFlow(null))

        assertTrue(
            repository.isMenuHighlightCustomizationRead(
                isMenuCustomizationScreenEnabled = false,
                isToolsCustomizationEnabled = false,
            )
        )
    }

    @Test
    fun `GIVEN the menu customization screen is on WHEN the menu customization is not read yet THEN return false`() {
        val repository = mockRepository(customizedMenuInfo = MutableStateFlow(null))

        assertFalse(
            repository.isMenuHighlightCustomizationRead(
                isMenuCustomizationScreenEnabled = true,
                isToolsCustomizationEnabled = false,
            )
        )
        assertFalse(
            repository.isMenuHighlightCustomizationRead(
                isMenuCustomizationScreenEnabled = true,
                isToolsCustomizationEnabled = true,
            )
        )
    }

    @Test
    fun `GIVEN the menu customization is read and tools customization is off WHEN the sections customization is not read THEN return true`() {
        val repository =
            mockRepository(
                customizedMenuInfo = MutableStateFlow(defaultMenuInfo),
                customizedSectionsInfo = MutableStateFlow(null),
            )

        assertTrue(
            repository.isMenuHighlightCustomizationRead(
                isMenuCustomizationScreenEnabled = true,
                isToolsCustomizationEnabled = false,
            )
        )
    }

    @Test
    fun `GIVEN the menu customization is read and tools customization is on WHEN the sections customization is not read THEN return false`() {
        val repository =
            mockRepository(
                customizedMenuInfo = MutableStateFlow(defaultMenuInfo),
                customizedSectionsInfo = MutableStateFlow(null),
            )

        assertFalse(
            repository.isMenuHighlightCustomizationRead(
                isMenuCustomizationScreenEnabled = false,
                isToolsCustomizationEnabled = true,
            )
        )
        assertFalse(
            repository.isMenuHighlightCustomizationRead(
                isMenuCustomizationScreenEnabled = true,
                isToolsCustomizationEnabled = true,
            )
        )
    }

    @Test
    fun `GIVEN both customizations are read and both settings are on WHEN checking whether it is read THEN return true`() {
        val repository =
            mockRepository(
                customizedMenuInfo = MutableStateFlow(defaultMenuInfo),
                customizedSectionsInfo = MutableStateFlow(emptyMap()),
            )

        assertTrue(
            repository.isMenuHighlightCustomizationRead(
                isMenuCustomizationScreenEnabled = true,
                isToolsCustomizationEnabled = true,
            )
        )
    }

    @Test
    fun `GIVEN no repository WHEN observing the menu customization THEN emit null right away`() = runTest {
        val repository: MenuCustomizationRepository? = null
        val emissions = mutableListOf<CustomizedMenuInfo?>()
        repository
            .customizedMenuInfoFlow(enabled = true)
            .onEach(emissions::add)
            .launchIn(backgroundScope + UnconfinedTestDispatcher(testScheduler))

        assertEquals(listOf<CustomizedMenuInfo?>(null), emissions)
    }

    @Test
    fun `GIVEN the menu customization screen is off WHEN observing the menu customization THEN emit null until it is read`() =
        runTest {
            val menuInfoFlow = MutableStateFlow<CustomizedMenuInfo?>(null)
            val repository = mockRepository(customizedMenuInfo = menuInfoFlow)
            val emissions = mutableListOf<CustomizedMenuInfo?>()
            repository
                .customizedMenuInfoFlow(enabled = false)
                .onEach(emissions::add)
                .launchIn(backgroundScope + UnconfinedTestDispatcher(testScheduler))

            assertEquals(listOf<CustomizedMenuInfo?>(null), emissions)

            menuInfoFlow.value = defaultMenuInfo

            assertEquals(listOf(null, defaultMenuInfo), emissions)
        }

    @Test
    fun `GIVEN the menu customization screen is on WHEN observing the menu customization THEN emit nothing until it is read`() =
        runTest {
            val menuInfoFlow = MutableStateFlow<CustomizedMenuInfo?>(null)
            val repository = mockRepository(customizedMenuInfo = menuInfoFlow)
            val emissions = mutableListOf<CustomizedMenuInfo?>()
            repository
                .customizedMenuInfoFlow(enabled = true)
                .onEach(emissions::add)
                .launchIn(backgroundScope + UnconfinedTestDispatcher(testScheduler))

            assertEquals(emptyList(), emissions)

            menuInfoFlow.value = defaultMenuInfo

            assertEquals(listOf<CustomizedMenuInfo?>(defaultMenuInfo), emissions)
        }

    @Test
    fun `GIVEN tools customization is off WHEN observing the sections customization THEN emit an empty map right away`() =
        runTest {
            val repository = mockRepository(customizedSectionsInfo = MutableStateFlow(null))
            val emissions = mutableListOf<Map<String, CustomizedSectionInfo>>()
            repository
                .customizedSectionsInfoFlow(enabled = false)
                .onEach(emissions::add)
                .launchIn(backgroundScope + UnconfinedTestDispatcher(testScheduler))

            assertEquals(listOf(emptyMap()), emissions)
        }

    @Test
    fun `GIVEN tools customization is on WHEN observing the sections customization THEN emit nothing until it is read`() =
        runTest {
            val sectionsInfoFlow = MutableStateFlow<Map<String, CustomizedSectionInfo>?>(null)
            val repository = mockRepository(customizedSectionsInfo = sectionsInfoFlow)
            val emissions = mutableListOf<Map<String, CustomizedSectionInfo>>()
            repository
                .customizedSectionsInfoFlow(enabled = true)
                .onEach(emissions::add)
                .launchIn(backgroundScope + UnconfinedTestDispatcher(testScheduler))

            assertEquals(emptyList(), emissions)

            val sections = mapOf("tools" to toolsInfo())
            sectionsInfoFlow.value = sections

            assertEquals(listOf(sections), emissions)
        }

    private fun mockRepository(
        customizedMenuInfo: MutableStateFlow<CustomizedMenuInfo?> = MutableStateFlow(null),
        customizedSectionsInfo: MutableStateFlow<Map<String, CustomizedSectionInfo>?> = MutableStateFlow(null),
    ): MenuCustomizationRepository = mockk {
        every { this@mockk.customizedMenuInfo } returns customizedMenuInfo
        every { this@mockk.customizedSectionsInfo } returns customizedSectionsInfo
    }

    // The customization is read on a scope that runTest cancels at the end of each test.
    private fun TestScope.repository(
        dataStore: DataStore<CustomizedMenuRecord>,
        requiredSectionIds: Set<String> = emptySet(),
        topLevelOnlyItemIds: Set<String> = emptySet(),
        scope: CoroutineScope = backgroundScope,
    ) =
        MenuCustomizationRepository(
            dataStore = dataStore,
            defaultMenuInfo = { defaultMenuInfo },
            defaultSectionsInfo = mapOf("tools" to defaultToolsInfo),
            requiredSectionIds = requiredSectionIds,
            topLevelOnlyItemIds = topLevelOnlyItemIds,
            scope = scope,
        )

    private fun toolsInfo() = sectionInfo("tools", shown = listOf(item("y"), item("x"), item("more", "z", "w")))

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
        var failReads = false
        var failWrites = false

        val record: CustomizedMenuRecord
            get() = records.value

        override val data: Flow<CustomizedMenuRecord> = flow {
            if (failReads) throw IOException("Cannot read")
            emitAll(records)
        }

        override suspend fun updateData(
            transform: suspend (t: CustomizedMenuRecord) -> CustomizedMenuRecord
        ): CustomizedMenuRecord {
            if (failWrites) throw IOException("Cannot write")
            return transform(records.value).also { records.value = it }
        }
    }

    /** A data store whose read and writes wait until they are released. */
    private class GatedDataStore(initial: CustomizedMenuRecord) : DataStore<CustomizedMenuRecord> {
        private val records = MutableStateFlow(initial)
        private val readGate = CompletableDeferred<Unit>()
        private val writeGate = CompletableDeferred<Unit>()

        val record: CustomizedMenuRecord
            get() = records.value

        override val data: Flow<CustomizedMenuRecord> = flow {
            readGate.await()
            emitAll(records)
        }

        fun releaseRead() = readGate.complete(Unit)

        fun releaseWrites() = writeGate.complete(Unit)

        override suspend fun updateData(
            transform: suspend (t: CustomizedMenuRecord) -> CustomizedMenuRecord
        ): CustomizedMenuRecord {
            writeGate.await()
            return transform(records.value).also { records.value = it }
        }
    }

    private object UnexpectedlyUnreadableDataStore : DataStore<CustomizedMenuRecord> {
        override val data: Flow<CustomizedMenuRecord> = flow { throw IllegalStateException("Cannot read") }

        override suspend fun updateData(
            transform: suspend (t: CustomizedMenuRecord) -> CustomizedMenuRecord
        ): CustomizedMenuRecord = throw IOException("Cannot write")
    }

    private object UnreadableDataStore : DataStore<CustomizedMenuRecord> {
        override val data: Flow<CustomizedMenuRecord> = flow { throw IOException("Cannot read") }

        override suspend fun updateData(
            transform: suspend (t: CustomizedMenuRecord) -> CustomizedMenuRecord
        ): CustomizedMenuRecord = throw IOException("Cannot write")
    }

    private class ReversingDispatcher : CoroutineDispatcher() {
        private val tasks = ArrayDeque<Runnable>()

        override fun dispatch(context: CoroutineContext, block: Runnable) {
            tasks.addFirst(block)
        }

        fun runAll() {
            while (tasks.isNotEmpty()) {
                tasks.removeFirst().run()
            }
        }
    }
}
