/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ext

import android.app.Activity
import android.content.Context
import android.view.ContextThemeWrapper
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.test.core.app.ApplicationProvider
import io.mockk.every
import io.mockk.mockk
import io.mockk.mockkObject
import io.mockk.unmockkObject
import io.mockk.verify
import mozilla.components.support.locale.LocaleManager
import mozilla.components.support.locale.LocaleManager.getSystemDefault
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.experiments.nimbus.NimbusEventStore
import org.mozilla.fenix.FenixApplication
import org.mozilla.fenix.R
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.helpers.FenixRobolectricTestRunner
import org.mozilla.fenix.utils.Settings
import java.lang.String.format
import java.util.Locale

@RunWith(FenixRobolectricTestRunner::class)
class ContextTest {

    private lateinit var mockContext: Context
    private val selectedLocale = Locale.Builder().setLanguage("ro").setRegion("RO").build()
    private val appName = "Firefox Preview"

    private val mockId: Int = 11

    @Before
    fun setup() {
        mockkObject(LocaleManager)

        mockContext = mockk(relaxed = true)
        mockContext.resources.configuration.setLocale(selectedLocale)

        every { LocaleManager.getCurrentLocale(mockContext) } returns selectedLocale
    }

    @After
    fun teardown() {
        unmockkObject(LocaleManager)
    }

    @Test
    fun `getStringWithArgSafe returns selected locale for correct formatted string`() {
        val correctlyFormattedString = "Incearca noul %1s"
        every { mockContext.getString(mockId) } returns correctlyFormattedString

        val result = mockContext.getStringWithArgSafe(mockId, appName)

        assertEquals("Incearca noul Firefox Preview", result)
    }

    @Test
    fun `getStringWithArgSafe returns English locale for incorrect formatted string`() {
        val englishString = "Try the new %1s"
        val incorrectlyFormattedString = "Incearca noul %1&amp;s"
        every { getSystemDefault() } returns Locale.forLanguageTag("en")
        every { mockContext.getString(mockId) } returns incorrectlyFormattedString
        every { format(mockContext.getString(mockId), appName) } returns format(englishString, appName)

        val result = mockContext.getStringWithArgSafe(mockId, appName)

        assertEquals("Try the new Firefox Preview", result)
    }

    @Test
    fun `GIVEN context WHEN seeking application of context THEN send back application context`() {
        val expectedAppValue = ApplicationProvider.getApplicationContext<FenixApplication>()
        assertEquals(expectedAppValue, testContext.application)
    }

    @Test
    fun `GIVEN context WHEN requiring components THEN send back application components`() {
        val expectedComponentsValue = ApplicationProvider.getApplicationContext<FenixApplication>().components
        assertEquals(expectedComponentsValue, testContext.components)
    }

    @Test
    fun `GIVEN context WHEN getting metrics controller THEN send back metrics`() {
        every { testContext.components.analytics } returns mockk(relaxed = true)
        val expectedMetricsValue = ApplicationProvider.getApplicationContext<FenixApplication>().components.analytics.metrics
        assertEquals(expectedMetricsValue, testContext.metrics)
    }

    @Test
    fun `GIVEN activity context WHEN make it an activity THEN return activity`() {
        val mockActivity = mockk<Activity> {
            every { baseContext } returns null
        }
        val mockContext: Context = mockActivity
        assertEquals(mockActivity, mockContext.asActivity())
    }

    @Test
    fun `GIVEN theme wrapper context WHEN make it an activity THEN return base`() {
        val mockActivity = mockk<Activity>()
        val mockThemeWrapper = mockk<ContextThemeWrapper> {
            every { baseContext } returns mockActivity
        }
        val mockContext: Context = mockThemeWrapper
        assertEquals(mockActivity, mockContext.asActivity())
    }

    @Test
    fun `GIVEN theme wrapper context without activity base context WHEN make it an activity THEN return null`() {
        val mockThemeWrapper = mockk<ContextThemeWrapper> {
            every { baseContext } returns mockk<FenixApplication>()
        }
        val mockContext: Context = mockThemeWrapper
        assertNull(mockContext.asActivity())
    }

    @Test
    fun `GIVEN activity context WHEN get root view THEN return content view`() {
        val rootView = mockk<ViewGroup>()
        val mockActivity = mockk<Activity> {
            every { baseContext } returns null
            every { window } returns mockk {
                every { decorView } returns mockk {
                    every { findViewById<View>(android.R.id.content) } returns rootView
                }
            }
        }
        assertEquals(rootView, mockActivity.getRootView())
    }

    @Test
    fun `GIVEN activity context without window WHEN get root view THEN return content view`() {
        val mockActivity = mockk<Activity> {
            every { baseContext } returns null
            every { window } returns null
        }
        assertNull(mockActivity.getRootView())
    }

    @Test
    fun `GIVEN activity context without valid content view WHEN get root view THEN return content view`() {
        val mockActivity = mockk<Activity> {
            every { baseContext } returns null
            every { window } returns mockk {
                every { decorView } returns mockk {
                    every { findViewById<View>(android.R.id.content) } returns mockk<TextView>()
                }
            }
        }
        assertNull(mockActivity.getRootView())
    }

    @Test
    fun `GIVEN context WHEN given a preference key THEN send back the right string`() {
        val comparisonStr = testContext.getString(R.string.private_browsing_common_myths)
        val actualStr = testContext.getPreferenceKey(R.string.private_browsing_common_myths)
        assertEquals(comparisonStr, actualStr)
    }

    @Test
    fun `recordEventInNimbus records the given event to the Nimbus event store`() {
        val eventStore = mockk<NimbusEventStore>(relaxed = true)
        every { mockContext.components.nimbus.events } returns eventStore

        val eventId = "test event"
        mockContext.recordEventInNimbus(eventId)

        verify { eventStore.recordEvent(eventId) }
    }

    @Test
    fun `GIVEN context WHEN toolbar position is bottom THEN isToolbarAtBottom returns true`() {
        val settings: Settings = mockk()
        every { testContext.settings() } returns settings
        every { settings.toolbarPosition } returns ToolbarPosition.BOTTOM

        assertTrue(testContext.isToolbarAtBottom())
    }

    @Test
    fun `GIVEN context WHEN toolbar position is top THEN isToolbarAtBottom returns false`() {
        val settings: Settings = mockk()
        every { testContext.settings() } returns settings
        every { settings.toolbarPosition } returns ToolbarPosition.TOP

        assertFalse(testContext.isToolbarAtBottom())
    }
}
