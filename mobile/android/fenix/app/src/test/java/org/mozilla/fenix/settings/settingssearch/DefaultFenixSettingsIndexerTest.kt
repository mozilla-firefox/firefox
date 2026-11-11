/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.settingssearch

import android.content.Context
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.robolectric.annotation.Config

@RunWith(AndroidJUnit4::class)
@Config(manifest = Config.NONE)
class DefaultFenixSettingsIndexerTest {

    private lateinit var indexer: DefaultFenixSettingsIndexer
    private lateinit var context: Context

    @Before
    fun setUp() {
        context = testContext
        indexer = DefaultFenixSettingsIndexer(context)
    }

    @Test
    fun `GIVEN a query with special characters with getSettingsWithQuery THEN return empty list`() = runTest {
        indexer.indexAllSettings()
        val query = "%"

        val results = indexer.getSettingsWithQuery(query)

        assertTrue(results.isEmpty())
    }

    @Test
    fun `GIVEN indexAllSettings is called WHEN it is a suspend function THEN it completes successfully`() = runTest {
        indexer.indexAllSettings()

        val results = indexer.getSettingsWithQuery("search")

        assertFalse(results.isEmpty())
    }

    @Test
    fun `GIVEN a blank or empty query WHEN filtering matching settings THEN return empty list`() = runTest {
        indexer.indexAllSettings()

        val emptyResults = indexer.getSettingsWithQuery("")
        val whitespaceResults = indexer.getSettingsWithQuery("   ")

        assertTrue(emptyResults.isEmpty())
        assertTrue(whitespaceResults.isEmpty())
    }

    @Test
    fun `GIVEN a query with invalid characters WHEN filtering matching settings THEN return empty list`() = runTest {
        indexer.indexAllSettings()

        val tabsResults = indexer.getSettingsWithQuery("\t\n")

        assertTrue(tabsResults.isEmpty())
    }

    @Test
    fun `GIVEN a query containing spaces WHEN filtering matching settings THEN results match trimmed query`() =
        runTest {
            indexer.indexAllSettings()

            val trimmedResults = indexer.getSettingsWithQuery("theme")
            val spacedResults = indexer.getSettingsWithQuery(" theme ")

            assertEquals(trimmedResults, spacedResults)
        }

    @Test
    fun `GIVEN a query containing different cases WHEN filtering matching settings THEN filtering is case-insensitive`() =
        runTest {
            indexer.indexAllSettings()

            val lowerResults = indexer.getSettingsWithQuery("theme")
            val upperResults = indexer.getSettingsWithQuery("THEME")
            val mixedResults = indexer.getSettingsWithQuery("ThEmE")

            assertEquals(lowerResults, upperResults)
            assertEquals(lowerResults, mixedResults)
        }

    @Test
    fun `GIVEN a preference key is excluded WHEN indexing THEN that item is absent from results`() = runTest {
        val privacyReportKey = context.getString(R.string.pref_key_privacy_report)
        val privacyReportTitle = context.getString(R.string.customize_toggle_privacy_report)
        val excludingIndexer =
            DefaultFenixSettingsIndexer(
                context = context,
                excludedPreferenceKeys = { setOf(privacyReportKey) },
            )

        excludingIndexer.indexAllSettings()

        val results = excludingIndexer.getSettingsWithQuery(privacyReportTitle)
        assertFalse(results.any { it.preferenceKey == privacyReportKey })
    }
}
