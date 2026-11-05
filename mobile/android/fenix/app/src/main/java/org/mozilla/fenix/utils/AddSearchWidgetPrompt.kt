/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.utils

import android.app.Activity
import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.ComponentName
import org.mozilla.fenix.widget.glance.SearchBarWithLensWidgetReceiver

/**
 * Displays the "add search widget" prompt for capable devices.
 *
 * @param activity the parent [Activity].
 * @param provider The search widget provider the prompt offers to add.
 */
fun maybeShowAddSearchWidgetPrompt(
    activity: Activity,
    provider: Class<out AppWidgetProvider> = SearchBarWithLensWidgetReceiver::class.java,
) {
    val appWidgetManager = AppWidgetManager.getInstance(activity)

    // We don't use canShowAddSearchWidgetPrompt here directly as lint does not pick on the version check.
    if (appWidgetManager.isRequestPinAppWidgetSupported) {
        appWidgetManager.requestPinAppWidget(ComponentName(activity, provider), null, null)
    }
}

/** Checks whether the device is capable of displaying the "add search widget" prompt. */
fun canShowAddSearchWidgetPrompt(appWidgetManager: AppWidgetManager?) =
    appWidgetManager?.isRequestPinAppWidgetSupported == true
