/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.widget

import android.content.ComponentName
import android.content.Intent
import android.os.Bundle
import android.os.StrictMode
import android.speech.RecognizerIntent
import androidx.activity.result.ActivityResult
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.VisibleForTesting
import androidx.appcompat.app.AppCompatActivity
import mozilla.components.support.locale.LocaleManager
import mozilla.components.support.utils.ext.getParcelableCompat
import org.mozilla.fenix.GleanMetrics.SearchWidget
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.IntentReceiverActivity
import org.mozilla.fenix.ext.components

/** Launches voice recognition then uses it to start a new web search. */
class VoiceSearchActivity : AppCompatActivity() {

    /** Holds the intent that initially started this activity so that it can persist through the speech activity. */
    private var previousIntent: Intent? = null
    private lateinit var startForResult: ActivityResultLauncher<Intent>

    @VisibleForTesting
    internal fun handleActivityResult(result: ActivityResult) {
        if (result.resultCode == RESULT_OK) {
            val spokenText = result.data?.getStringArrayListExtra(RecognizerIntent.EXTRA_RESULTS)?.firstOrNull()
            val context = this

            previousIntent?.apply {
                component = ComponentName(context, IntentReceiverActivity::class.java)
                putExtra(SPEECH_PROCESSING, spokenText)
                putExtra(HomeActivity.OPEN_TO_BROWSER_AND_LOAD, true)
                startActivity(this)
            }
        }

        finish()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putParcelable(PREVIOUS_INTENT, previousIntent)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        startForResult =
            registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
                handleActivityResult(it)
            }

        if (Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).resolveActivity(packageManager) == null) {
            finish()
            return
        }

        // Retrieve the previous intent from the saved state
        previousIntent = savedInstanceState?.getParcelableCompat(PREVIOUS_INTENT, Intent::class.java)
        if (previousIntent.isForSpeechProcessing()) {
            // Don't reopen the speech recognizer
            return
        }

        // The intent property is nullable, but the rest of the code below assumes it is not.
        val intent = intent?.let { Intent(intent) } ?: Intent()

        if (intent.isForSpeechProcessing()) {
            previousIntent = intent
            displaySpeechRecognizer()
        } else {
            finish()
        }
    }

    /** Displays a speech recognizer popup that listens for input from the user. */
    private fun displaySpeechRecognizer() {
        val intentSpeech =
            Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
                putExtra(
                    RecognizerIntent.EXTRA_LANGUAGE_MODEL,
                    RecognizerIntent.LANGUAGE_MODEL_FREE_FORM,
                )
                putExtra(
                    RecognizerIntent.EXTRA_LANGUAGE,
                    components.strictMode.allowViolation(StrictMode::allowThreadDiskReads) {
                        LocaleManager.getCurrentLocale(this@VoiceSearchActivity)
                    },
                )
            }
        SearchWidget.voiceButton.record(
            SearchWidget.VoiceButtonExtra(
                widgetType = previousIntent?.getStringExtra(SearchWidgetIntents.EXTRA_WIDGET_TYPE)
            )
        )

        startForResult.launch(intentSpeech)
    }

    /**
     * Returns true if the [SPEECH_PROCESSING] extra is present and set to true. Returns false if the intent is null.
     */
    private fun Intent?.isForSpeechProcessing(): Boolean = this?.getBooleanExtra(SPEECH_PROCESSING, false) == true

    companion object {
        internal const val PREVIOUS_INTENT = "org.mozilla.fenix.previous_intent"

        /**
         * In [VoiceSearchActivity] activity, used to store if the speech processing should start. In
         * [IntentReceiverActivity] activity, used to store the search terms.
         */
        const val SPEECH_PROCESSING = "speech_processing"
    }
}
