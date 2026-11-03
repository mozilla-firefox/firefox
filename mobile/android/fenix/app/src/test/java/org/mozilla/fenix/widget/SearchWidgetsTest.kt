/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.widget

import android.appwidget.AppWidgetManager
import android.content.ComponentName
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.SearchWidget
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.widget.glance.CompactSearchBarWidgetReceiver
import org.mozilla.fenix.widget.glance.SearchBarWidgetReceiver
import org.mozilla.gecko.search.SearchWidgetProvider
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class SearchWidgetsTest {
    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private lateinit var settings: Settings
    private val appWidgetManager = AppWidgetManager.getInstance(testContext)
    private var nextWidgetId = 1

    @Before
    fun setup() {
        settings = Settings(testContext)
    }

    @Test
    fun `WHEN a search widget is added THEN its widget type is recorded and the installed state is persisted`() {
        addWidget(CompactSearchBarWidgetReceiver::class.java)

        SearchWidgets.syncInstalledState(testContext, settings)

        assertEquals(listOf("bar_compact"), addedWidgetTypes())
        assertTrue(settings.searchWidgetInstalled)
    }

    @Test
    fun `GIVEN a widget already recorded WHEN another widget is added THEN only the new widget is recorded`() {
        addWidget(SearchBarWidgetReceiver::class.java)
        SearchWidgets.syncInstalledState(testContext, settings)

        addWidget(SearchWidgetProvider::class.java)
        SearchWidgets.syncInstalledState(testContext, settings)
        SearchWidgets.syncInstalledState(testContext, settings)

        assertEquals(listOf("search_bar", "legacy"), addedWidgetTypes())
    }

    @Test
    fun `GIVEN widgets installed before their IDs were persisted WHEN syncing THEN no widget is recorded as added`() {
        settings.searchWidgetInstalled = true
        addWidget(SearchBarWidgetReceiver::class.java)

        SearchWidgets.syncInstalledState(testContext, settings)

        assertNull(SearchWidget.widgetAdded.testGetValue())
        assertEquals(setOf("1"), settings.searchWidgetIds)
    }

    @Test
    fun `WHEN no search widget is installed THEN nothing is recorded`() {
        SearchWidgets.syncInstalledState(testContext, settings)

        assertNull(SearchWidget.widgetAdded.testGetValue())
        assertFalse(settings.searchWidgetInstalled)
    }

    private fun addWidget(provider: Class<*>) {
        shadowOf(appWidgetManager).bindAppWidgetId(nextWidgetId++, ComponentName(testContext, provider))
    }

    private fun addedWidgetTypes() = SearchWidget.widgetAdded.testGetValue()!!.map { it.extra!!["widget_type"] }
}
