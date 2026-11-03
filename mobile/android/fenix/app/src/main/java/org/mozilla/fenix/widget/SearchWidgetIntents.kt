/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.widget

import android.content.Context
import android.content.Intent
import android.speech.RecognizerIntent
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.IntentReceiverActivity
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.isGoogleSearchEngine
import org.mozilla.fenix.home.intent.StartSearchIntentProcessor
import org.mozilla.fenix.widget.VoiceSearchActivity.Companion.SPEECH_PROCESSING

/** Builds the intents launched by the device home screen Firefox search widgets. */
internal object SearchWidgetIntents {
    /** Intent extra holding the type of the search widget that launched the intent, recorded in telemetry. */
    const val EXTRA_WIDGET_TYPE = "org.mozilla.fenix.widget.WIDGET_TYPE"

    /**
     * Intent that opens the browser and starts a new text search, in a private tab if [isPrivateBrowsingMode] is true.
     */
    fun textSearch(context: Context, widgetType: String, isPrivateBrowsingMode: Boolean = false): Intent =
        Intent(context, IntentReceiverActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
            putExtra(HomeActivity.OPEN_TO_SEARCH, StartSearchIntentProcessor.SEARCH_WIDGET)
            putExtra(EXTRA_WIDGET_TYPE, widgetType)
            if (isPrivateBrowsingMode) {
                putExtra(HomeActivity.PRIVATE_BROWSING_MODE, true)
            }
        }

    /**
     * Intent that starts a new voice search, or null if voice search is disabled or unavailable.
     *
     * @param context Context used to build the intent and find a speech recognizer.
     * @param widgetType The type of the search widget that launches the intent, recorded in telemetry.
     * @param isVoiceSearchEnabled Whether the user enabled voice search.
     */
    fun maybeVoiceSearch(
        context: Context,
        widgetType: String,
        isVoiceSearchEnabled: Boolean = context.components.settings.shouldShowVoiceSearch,
    ): Intent? {
        if (!isVoiceSearchEnabled) {
            return null
        }

        Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).resolveActivity(context.packageManager) ?: return null

        return Intent(context, VoiceSearchActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
            putExtra(SPEECH_PROCESSING, true)
            putExtra(EXTRA_WIDGET_TYPE, widgetType)
        }
    }

    /**
     * Intent that starts a Google Lens image search, or null when the Google Lens integration is disabled or the user's
     * selected search engine is not Google.
     *
     * @param context Context used to build the intent.
     * @param widgetType The type of the search widget that launches the intent, recorded in telemetry.
     * @param isLensEnabled Whether the Google Lens integration and the user's Google Lens setting are both enabled.
     * @param selectedSearchEngine The user's selected or default search engine in normal browsing.
     */
    fun maybeLensSearch(
        context: Context,
        widgetType: String,
        isLensEnabled: Boolean =
            context.components.settings.run { googleLensIntegrationEnabled && googleLensIntegrationUserEnabled },
        selectedSearchEngine: SearchEngine? =
            context.components.core.store.state.search.selectedOrDefaultSearchEngine(private = false),
    ): Intent? {
        if (!isLensEnabled || !selectedSearchEngine.isGoogleSearchEngine()) {
            return null
        }

        return Intent(context, LensSearchActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
            putExtra(EXTRA_WIDGET_TYPE, widgetType)
        }
    }
}
