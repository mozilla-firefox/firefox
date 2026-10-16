/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.utils

import android.app.Activity
import android.appwidget.AppWidgetManager
import android.content.ComponentName
import android.content.Context
import org.mozilla.gecko.search.SearchWidgetProvider

/**
 * Displays the "add search widget" prompt for capable devices.
 *
 * @param activity the parent [Activity].
 */
fun maybeShowAddSearchWidgetPrompt(activity: Activity) {
    val appWidgetManager = AppWidgetManager.getInstance(activity)

    // We don't use canShowAddSearchWidgetPrompt here directly as lint does not pick on the version check.
    if (appWidgetManager.isRequestPinAppWidgetSupported) {
        val searchWidgetProvider = ComponentName(activity, SearchWidgetProvider::class.java)
        appWidgetManager.requestPinAppWidget(searchWidgetProvider, null, null)
    }
}

/**
 * Displays the "add search widget" prompt if the device supports it.
 *
 * This function checks if the current Android version is Oreo (API 26) or higher and if the `AppWidgetManager` supports
 * pinning app widgets. If both conditions are met, it requests to pin the search widget.
 *
 * @param context [Context] used for the Android framework interactions.
 */
fun showAddSearchWidgetPromptIfSupported(context: Context) {
    val packageName = context.packageName
    val appWidgetManager = AppWidgetManager.getInstance(context)

    if (canShowAddSearchWidgetPrompt(appWidgetManager)) {
        val searchWidgetProvider =
            ComponentName(
                packageName,
                SearchWidgetProvider::class.java.name,
            )
        appWidgetManager.requestPinAppWidget(searchWidgetProvider, null, null)
    }
}

/** Checks whether the device is capable of displaying the "add search widget" prompt. */
fun canShowAddSearchWidgetPrompt(appWidgetManager: AppWidgetManager?) =
    appWidgetManager?.isRequestPinAppWidgetSupported == true
