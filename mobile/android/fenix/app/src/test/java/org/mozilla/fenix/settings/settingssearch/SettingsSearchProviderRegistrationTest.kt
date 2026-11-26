/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.settingssearch

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.robolectric.testContext
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.components.settingsSearchProviders

/**
 * Drift-detection tests enforcing that every settings screen ends up in the search index, either through the
 * XML-parsing path in [DefaultFenixSettingsIndexer] or through a [SettingsSearchProvider] registered in
 * [settingsSearchProviders]. The only permitted exceptions are the screens listed in [intentionallyNotIndexed] and
 * [knownIndexingGaps].
 *
 * The screen list is derived from [PreferenceFileInformation]'s sealed hierarchy rather than hardcoded, so adding a new
 * screen and forgetting to register it anywhere fails these tests instead of silently making that screen unsearchable.
 * Both exception lists are themselves asserted against the index, so an entry that stops applying fails a test rather
 * than lingering.
 */
@RunWith(AndroidJUnit4::class)
class SettingsSearchProviderRegistrationTest {

    /** Every declared screen, including any these tests cannot turn into an instance. */
    private val allSubclasses = PreferenceFileInformation::class.sealedSubclasses

    /** The subset of [allSubclasses] these tests can actually assert against. */
    private val allScreens = allSubclasses.mapNotNull { it.objectInstance }

    /** Compose-based screens, which have no backing XML file and must be provider-backed. */
    private val composeScreens = allScreens.filter { it.xmlResourceId == null }

    /** Screens with a backing XML file, which the indexer parses directly. */
    private val xmlScreens = allScreens.filter { it.xmlResourceId != null }

    /**
     * Screens deliberately kept out of the global settings search index. Every entry needs a reason, and is expected to
     * stay absent from [DefaultFenixSettingsIndexer.defaultPreferenceFileInformationList].
     */
    private val intentionallyNotIndexed =
        setOf<PreferenceFileInformation>(
            // Debug-only screen, indexed on its own by SecretSettingsSearchFragment.
            PreferenceFileInformation.SecretSettingsPreferences
        )

    /**
     * Screens that are registered for indexing but currently contribute nothing to the built index. These are bugs, not
     * opt-outs: entries should be removed as they are fixed, and nothing should be added here without a bug reference.
     */
    private val knownIndexingGaps =
        setOf<PreferenceFileInformation>(
            // Bug 2050628: every preference in default_search_engine_preferences.xml uses either
            // RadioSearchEngineListPreference or the short `<Preference>` tag, neither of which
            // DefaultFenixSettingsIndexer parses.
            PreferenceFileInformation.DefaultSearchEnginePreferences
        )

    private val screensExpectedInIndex = allScreens - intentionallyNotIndexed - knownIndexingGaps

    private fun registeredProviders() = settingsSearchProviders()

    /** Builds the index the way production does, returning the screens that contributed items. */
    private suspend fun screensReachedByIndexer(): Set<PreferenceFileInformation> {
        val indexer =
            DefaultFenixSettingsIndexer(
                context = testContext,
                additionalProviders = registeredProviders(),
            )
        indexer.indexAllSettings()

        return indexer.indexedSettings().map { it.preferenceFileInformation }.toSet()
    }

    @Test
    fun `GIVEN the PreferenceFileInformation hierarchy WHEN enumerated by reflection THEN every screen is discoverable`() {
        // Without these the remaining tests would pass vacuously: screens reflection cannot
        // enumerate are dropped from every coverage assertion below.
        assertEquals(
            expected = emptyList(),
            actual = allSubclasses.filter { it.objectInstance == null }.map { it.simpleName },
            message =
                "These PreferenceFileInformation subclasses are not singleton objects, so these " +
                    "tests cannot enumerate them and their search coverage goes unchecked. Make them " +
                    "objects, or teach this test how to instantiate them.",
        )
        assertTrue(
            actual = composeScreens.isNotEmpty(),
            message =
                "No Compose screens (null xmlResourceId) were discovered, so provider coverage " +
                    "would not be checked by these tests",
        )
    }

    @Test
    fun `GIVEN the XML indexer list WHEN inspecting entries THEN every entry points at a real XML resource`() {
        val entriesWithoutXml =
            DefaultFenixSettingsIndexer.defaultPreferenceFileInformationList(includeAutofillPreferences = true).filter {
                it.xmlResourceId == null
            }

        assertEquals(
            expected = emptyList(),
            actual = entriesWithoutXml,
            message =
                "Compose screens (null xmlResourceId) must be indexed via a " +
                    "SettingsSearchProvider, not added to defaultPreferenceFileInformationList",
        )
    }

    @Test
    fun `GIVEN XML-backed screens WHEN checking the indexer list THEN each one is registered for XML indexing`() {
        (xmlScreens - intentionallyNotIndexed).forEach { screen ->
            assertTrue(
                actual =
                    screen in
                        DefaultFenixSettingsIndexer.defaultPreferenceFileInformationList(
                            includeAutofillPreferences = true
                        ),
                message =
                    "$screen has a backing XML file but is not in " +
                        "defaultPreferenceFileInformationList, so none of its preferences are searchable. " +
                        "Add it to that list, or to intentionallyNotIndexed with a reason.",
            )
        }
    }

    @Test
    fun `GIVEN screens opted out of indexing WHEN checking the indexer list THEN the opt-out list is not stale`() {
        intentionallyNotIndexed.forEach { screen ->
            assertTrue(
                actual =
                    screen !in
                        DefaultFenixSettingsIndexer.defaultPreferenceFileInformationList(
                            includeAutofillPreferences = true
                        ),
                message =
                    "$screen is registered for XML indexing but still listed in " +
                        "intentionallyNotIndexed. Remove it from intentionallyNotIndexed.",
            )
        }
    }

    @Test
    fun `GIVEN Compose-based screens WHEN gathering provider items THEN every screen is covered by a provider`() {
        val coveredScreens =
            registeredProviders()
                .flatMap { it.getSearchItems(testContext) }
                .map { it.preferenceFileInformation }
                .toSet()

        composeScreens.forEach { screen ->
            assertTrue(
                actual = screen in coveredScreens,
                message =
                    "$screen is a Compose screen with no registered SettingsSearchProvider, so it " +
                        "would silently disappear from settings search. Register one in settingsSearchProviders().",
            )
        }
    }

    @Test
    fun `GIVEN the production indexer configuration WHEN the index is built THEN every screen is reachable`() =
        runTest {
            val reachedScreens = screensReachedByIndexer()

            screensExpectedInIndex.forEach { screen ->
                assertTrue(
                    actual = screen in reachedScreens,
                    message =
                        "$screen contributed no items to the built settings search index. Either its " +
                            "XML file yields no indexable preferences, or it needs a SettingsSearchProvider " +
                            "registered in settingsSearchProviders().",
                )
            }
        }

    @Test
    fun `GIVEN screens listed as known indexing gaps WHEN the index is built THEN the gap list is not stale`() =
        runTest {
            val reachedScreens = screensReachedByIndexer()

            knownIndexingGaps.forEach { screen ->
                assertTrue(
                    actual = screen !in reachedScreens,
                    message =
                        "$screen now contributes items to the settings search index. " +
                            "Remove it from knownIndexingGaps.",
                )
            }
        }
}
