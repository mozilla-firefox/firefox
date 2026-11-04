/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.intent

import android.content.Intent
import androidx.navigation.NavController
import androidx.test.core.app.ApplicationProvider
import io.mockk.Called
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SearchState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.search.ext.createSearchEngine
import mozilla.components.support.test.robolectric.testContext
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.BrowserDirection
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.perf.TestStrictModeManager
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.widget.VoiceSearchActivity.Companion.SPEECH_PROCESSING
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class SpeechProcessingIntentProcessorTest {

    private val activity: HomeActivity = mockk(relaxed = true)
    private val navController: NavController = mockk(relaxed = true)
    private val out: Intent = mockk(relaxed = true)
    private val settings: Settings = mockk()

    private val searchEngine =
        createSearchEngine(
            name = "Test",
            url = "https://www.example.org/?q={searchTerms}",
            icon = mockk(),
        )

    private lateinit var store: BrowserStore

    @Before
    fun setup() {
        val searchEngine = searchEngine

        store =
            BrowserStore(
                BrowserState(
                    search =
                        SearchState(
                            customSearchEngines = listOf(searchEngine),
                            userSelectedSearchEngineId = searchEngine.id,
                            complete = true,
                        )
                )
            )

        every { activity.applicationContext } returns ApplicationProvider.getApplicationContext()
    }

    @Test
    fun `do not process blank intents`() {
        val processor = SpeechProcessingIntentProcessor(activity, store)
        processor.process(Intent(), navController, out, settings)

        verify { activity wasNot Called }
        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    @Test
    fun `do not process when open extra is false`() {
        val intent =
            Intent().apply {
                putExtra(HomeActivity.OPEN_TO_BROWSER_AND_LOAD, false)
            }
        val processor = SpeechProcessingIntentProcessor(activity, store)
        processor.process(intent, navController, out, settings)

        verify { activity wasNot Called }
        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    @Test
    fun `reads the speech processing extra`() {
        every { testContext.components.strictMode } returns TestStrictModeManager()
        every { testContext.components.nimbus.events } returns mockk(relaxed = true)

        val intent =
            Intent().apply {
                putExtra(HomeActivity.OPEN_TO_BROWSER_AND_LOAD, true)
                putExtra(SPEECH_PROCESSING, "hello world")
            }

        val processor = SpeechProcessingIntentProcessor(activity, store)
        processor.process(intent, mockk(), mockk(relaxed = true), settings)

        verify {
            @Suppress("DEPRECATION")
            activity.openToBrowserAndLoad(
                searchTermOrURL = "hello world",
                newTab = true,
                from = BrowserDirection.FromGlobal,
                forceSearch = true,
                engine = searchEngine,
                accessPoint = "widget",
            )
        }
    }
}
