/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.widget

import android.app.Activity.RESULT_CANCELED
import android.app.Activity.RESULT_OK
import android.content.ComponentName
import android.content.Intent
import android.content.IntentFilter
import android.os.Bundle
import android.speech.RecognizerIntent.ACTION_RECOGNIZE_SPEECH
import android.speech.RecognizerIntent.EXTRA_LANGUAGE_MODEL
import android.speech.RecognizerIntent.EXTRA_RESULTS
import android.speech.RecognizerIntent.LANGUAGE_MODEL_FREE_FORM
import androidx.activity.result.ActivityResult
import androidx.test.core.app.ApplicationProvider
import io.mockk.every
import io.mockk.mockk
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.FenixApplication
import org.mozilla.fenix.GleanMetrics.SearchWidget
import org.mozilla.fenix.HomeActivity.Companion.OPEN_TO_BROWSER_AND_LOAD
import org.mozilla.fenix.IntentReceiverActivity
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.helpers.perf.TestStrictModeManager
import org.mozilla.fenix.widget.VoiceSearchActivity.Companion.PREVIOUS_INTENT
import org.mozilla.fenix.widget.VoiceSearchActivity.Companion.SPEECH_PROCESSING
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.android.controller.ActivityController
import org.robolectric.shadows.ShadowActivity

@RunWith(RobolectricTestRunner::class)
class VoiceSearchActivityTest {
    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private lateinit var controller: ActivityController<VoiceSearchActivity>
    private lateinit var activity: VoiceSearchActivity
    private lateinit var shadow: ShadowActivity

    @Before
    fun setup() {
        val intent = Intent()
        intent.putExtra(SPEECH_PROCESSING, true)
        intent.putExtra(SearchWidgetIntents.EXTRA_WIDGET_TYPE, "legacy")

        controller = Robolectric.buildActivity(VoiceSearchActivity::class.java, intent)
        activity = controller.get()
        shadow = shadowOf(activity)
    }

    private fun allowVoiceIntentToResolveActivity() {
        val context = ApplicationProvider.getApplicationContext<FenixApplication>()
        val shadowPackageManager = shadowOf(context.packageManager)
        val component = ComponentName("com.test", "Test")
        shadowPackageManager.addActivityIfNotPresent(component)
        shadowPackageManager.addIntentFilterForActivity(
            component,
            IntentFilter(ACTION_RECOGNIZE_SPEECH).apply { addCategory(Intent.CATEGORY_DEFAULT) },
        )
    }

    @Test
    fun `process intent with speech processing set to true`() {
        every { testContext.components.analytics } returns mockk(relaxed = true)
        every { testContext.components.strictMode } returns TestStrictModeManager()
        allowVoiceIntentToResolveActivity()
        controller.create()

        val intentForResult = shadow.peekNextStartedActivityForResult()
        assertEquals(ACTION_RECOGNIZE_SPEECH, intentForResult.intent.action)
        assertEquals(
            LANGUAGE_MODEL_FREE_FORM,
            intentForResult.intent.getStringExtra(EXTRA_LANGUAGE_MODEL),
        )
        assertEquals(mapOf("widget_type" to "legacy"), SearchWidget.voiceButton.testGetValue()!!.single().extra)
    }

    @Test
    fun `process intent with speech processing set to false`() {
        allowVoiceIntentToResolveActivity()
        val intent = Intent()
        intent.putExtra(SPEECH_PROCESSING, false)

        val controller = Robolectric.buildActivity(VoiceSearchActivity::class.java, intent)
        val activity = controller.get()

        controller.create()

        assertTrue(activity.isFinishing)
    }

    @Test
    fun `process null intent`() {
        allowVoiceIntentToResolveActivity()
        val controller = Robolectric.buildActivity(VoiceSearchActivity::class.java, null)
        val activity = controller.get()

        controller.create()

        assertTrue(activity.isFinishing)
    }

    @Test
    fun `save previous intent to instance state`() {
        allowVoiceIntentToResolveActivity()
        val previousIntent =
            Intent().apply {
                putExtra(SPEECH_PROCESSING, true)
            }
        val savedInstanceState =
            Bundle().apply {
                putParcelable(PREVIOUS_INTENT, previousIntent)
            }
        val outState = Bundle()

        controller.create(savedInstanceState)
        controller.saveInstanceState(outState)

        @Suppress("DEPRECATION") assertEquals(previousIntent, outState.getParcelable<Intent>(PREVIOUS_INTENT))
    }

    @Test
    fun `process intent with speech processing in previous intent set to true`() {
        allowVoiceIntentToResolveActivity()
        val savedInstanceState = Bundle()
        val previousIntent =
            Intent().apply {
                putExtra(SPEECH_PROCESSING, true)
            }
        savedInstanceState.putParcelable(PREVIOUS_INTENT, previousIntent)

        controller.create(savedInstanceState)

        assertFalse(activity.isFinishing)
        assertNull(shadow.peekNextStartedActivityForResult())
    }

    @Test
    fun `handle no activity able to resolve voice intent`() {
        controller.create()
        assertTrue(activity.isFinishing)
    }

    @Test
    fun `handle speech result`() {
        every { testContext.components.analytics } returns mockk(relaxed = true)
        every { testContext.components.strictMode } returns TestStrictModeManager()
        allowVoiceIntentToResolveActivity()
        controller.create()

        val resultIntent =
            Intent().apply {
                putStringArrayListExtra(EXTRA_RESULTS, arrayListOf("hello world"))
            }
        val result = ActivityResult(RESULT_OK, resultIntent)
        activity.handleActivityResult(result)

        val browserIntent = shadow.peekNextStartedActivity()

        assertTrue(activity.isFinishing)
        assertEquals(
            ComponentName(activity, IntentReceiverActivity::class.java),
            browserIntent.component,
        )
        assertEquals("hello world", browserIntent.getStringExtra(SPEECH_PROCESSING))
        assertTrue(browserIntent.getBooleanExtra(OPEN_TO_BROWSER_AND_LOAD, false))
    }

    @Test
    fun `handle invalid result code`() {
        every { testContext.components.analytics } returns mockk(relaxed = true)
        every { testContext.components.strictMode } returns TestStrictModeManager()
        allowVoiceIntentToResolveActivity()
        controller.create()

        val resultIntent = Intent()
        val result = ActivityResult(RESULT_CANCELED, resultIntent)
        activity.handleActivityResult(result)

        assertTrue(activity.isFinishing)
    }
}
