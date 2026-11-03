/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.widget

import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.ComponentName
import android.content.Context
import org.mozilla.fenix.GleanMetrics.Metrics
import org.mozilla.fenix.GleanMetrics.SearchWidget
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.widget.glance.CompactSearchBarWidgetReceiver
import org.mozilla.fenix.widget.glance.SearchBarWidgetReceiver
import org.mozilla.fenix.widget.glance.SearchBarWidgetStyle
import org.mozilla.fenix.widget.glance.SearchBarWithLensWidgetReceiver
import org.mozilla.gecko.search.SearchWidgetProvider

/** The home screen search widgets, across the legacy and the Glance widget frameworks. */
object SearchWidgets {
    /** The widget type of the legacy [SearchWidgetProvider] recorded in the search widget telemetry. */
    const val LEGACY_WIDGET_TYPE = "legacy"

    /**
     * Every search widget provider, mapped to its widget type recorded in telemetry. Each one gets its own enabled and
     * disabled callbacks from the system.
     */
    val providers: Map<Class<out AppWidgetProvider>, String> =
        mapOf(
            SearchWidgetProvider::class.java to LEGACY_WIDGET_TYPE,
            SearchBarWidgetReceiver::class.java to SearchBarWidgetStyle.FULL_WIDTH.telemetryId,
            SearchBarWithLensWidgetReceiver::class.java to SearchBarWidgetStyle.FULL_WIDTH_WITH_LENS.telemetryId,
            CompactSearchBarWidgetReceiver::class.java to SearchBarWidgetStyle.COMPACT.telemetryId,
        )

    /** Persists and records whether any search widget is on the home screen, and records newly added widgets. */
    @Synchronized
    fun syncInstalledState(context: Context, settings: Settings = context.components.settings) {
        val appWidgetManager = AppWidgetManager.getInstance(context)
        val widgetTypesById =
            providers
                .flatMap { (provider, widgetType) ->
                    appWidgetManager.getAppWidgetIds(ComponentName(context, provider)).map {
                        it.toString() to widgetType
                    }
                }
                .toMap()

        // Widgets already on the home screen before the IDs were first persisted are not recorded as added.
        if (settings.searchWidgetIds.isNotEmpty() || !settings.searchWidgetInstalled) {
            widgetTypesById
                .filterKeys { it !in settings.searchWidgetIds }
                .values
                .forEach { SearchWidget.widgetAdded.record(SearchWidget.WidgetAddedExtra(widgetType = it)) }
        }
        settings.searchWidgetIds = widgetTypesById.keys

        val installed = widgetTypesById.isNotEmpty()
        if (settings.searchWidgetInstalled != installed) {
            settings.searchWidgetInstalled = installed
            Metrics.searchWidgetInstalled.set(installed)
        }
    }
}
