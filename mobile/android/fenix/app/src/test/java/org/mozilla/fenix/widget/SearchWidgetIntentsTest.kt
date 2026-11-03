/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.widget

import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.pm.ResolveInfo
import android.graphics.Bitmap
import android.speech.RecognizerIntent
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.IntentReceiverActivity
import org.mozilla.fenix.home.intent.StartSearchIntentProcessor
import org.mozilla.fenix.widget.VoiceSearchActivity.Companion.SPEECH_PROCESSING
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class SearchWidgetIntentsTest {

    @Test
    fun `WHEN a normal text search intent is built THEN it opens to search without private mode`() {
        val intent = SearchWidgetIntents.textSearch(testContext, WIDGET_TYPE)

        assertEquals(IntentReceiverActivity::class.java.name, intent.component!!.className)
        assertEquals(
            StartSearchIntentProcessor.SEARCH_WIDGET,
            intent.getStringExtra(HomeActivity.OPEN_TO_SEARCH),
        )
        assertEquals(WIDGET_TYPE, intent.getStringExtra(SearchWidgetIntents.EXTRA_WIDGET_TYPE))
        assertFalse(intent.hasExtra(HomeActivity.PRIVATE_BROWSING_MODE))
    }

    @Test
    fun `WHEN a private text search intent is built THEN it opens to search in private mode`() {
        val intent = SearchWidgetIntents.textSearch(testContext, WIDGET_TYPE, isPrivateBrowsingMode = true)

        assertEquals(
            StartSearchIntentProcessor.SEARCH_WIDGET,
            intent.getStringExtra(HomeActivity.OPEN_TO_SEARCH),
        )
        assertTrue(intent.getBooleanExtra(HomeActivity.PRIVATE_BROWSING_MODE, false))
    }

    @Test
    fun `GIVEN voice search is disabled WHEN a voice search intent is built THEN it is null`() {
        stubSpeechRecognizer()

        assertNull(SearchWidgetIntents.maybeVoiceSearch(testContext, WIDGET_TYPE, isVoiceSearchEnabled = false))
    }

    @Test
    fun `GIVEN no speech recognizer is installed WHEN a voice search intent is built THEN it is null`() {
        assertNull(SearchWidgetIntents.maybeVoiceSearch(testContext, WIDGET_TYPE, isVoiceSearchEnabled = true))
    }

    @Test
    fun `GIVEN voice search is enabled and a speech recognizer is installed WHEN a voice search intent is built THEN it starts a voice search`() {
        stubSpeechRecognizer()

        val intent = SearchWidgetIntents.maybeVoiceSearch(testContext, WIDGET_TYPE, isVoiceSearchEnabled = true)!!

        assertEquals(VoiceSearchActivity::class.java.name, intent.component!!.className)
        assertTrue(intent.getBooleanExtra(SPEECH_PROCESSING, false))
        assertEquals(WIDGET_TYPE, intent.getStringExtra(SearchWidgetIntents.EXTRA_WIDGET_TYPE))
        assertEquals(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK, intent.flags)
    }

    @Test
    fun `GIVEN lens is disabled WHEN a lens search intent is built THEN it is null`() {
        assertNull(
            SearchWidgetIntents.maybeLensSearch(
                testContext,
                WIDGET_TYPE,
                isLensEnabled = false,
                selectedSearchEngine = searchEngine("google-b-1-m"),
            )
        )
    }

    @Test
    fun `GIVEN a non-Google default engine WHEN a lens search intent is built THEN it is null`() {
        assertNull(
            SearchWidgetIntents.maybeLensSearch(
                testContext,
                WIDGET_TYPE,
                isLensEnabled = true,
                selectedSearchEngine = searchEngine("bing"),
            )
        )
    }

    @Test
    fun `GIVEN no search engine WHEN a lens search intent is built THEN it is null`() {
        assertNull(
            SearchWidgetIntents.maybeLensSearch(
                testContext,
                WIDGET_TYPE,
                isLensEnabled = true,
                selectedSearchEngine = null,
            )
        )
    }

    @Test
    fun `GIVEN lens is enabled and Google is the default engine WHEN a lens search intent is built THEN it starts a lens search`() {
        val intent =
            SearchWidgetIntents.maybeLensSearch(
                testContext,
                WIDGET_TYPE,
                isLensEnabled = true,
                selectedSearchEngine = searchEngine("google-b-1-m"),
            )!!

        assertEquals(LensSearchActivity::class.java.name, intent.component!!.className)
        assertEquals(WIDGET_TYPE, intent.getStringExtra(SearchWidgetIntents.EXTRA_WIDGET_TYPE))
        assertEquals(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK, intent.flags)
    }

    private fun stubSpeechRecognizer() {
        val info =
            ResolveInfo().apply {
                activityInfo =
                    ActivityInfo().apply {
                        packageName = "fake.voice.recognizer"
                        name = "FakeRecognizerActivity"
                    }
            }
        @Suppress("Deprecation")
        shadowOf(testContext.packageManager)
            .addResolveInfoForIntent(Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH), info)
    }

    private fun searchEngine(id: String) =
        SearchEngine(
            id = id,
            name = "Engine",
            icon = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888),
            type = SearchEngine.Type.BUNDLED,
        )

    private companion object {
        const val WIDGET_TYPE = "bar_compact"
    }
}
