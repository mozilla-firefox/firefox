/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.listentopage

import android.os.Looper
import android.view.View
import androidx.activity.ComponentActivity
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.core.view.isVisible
import io.mockk.mockk
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.ListenStore
import mozilla.components.feature.listentopage.listenReducer
import mozilla.components.feature.listentopage.settings.ListenEnabledState
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(RobolectricTestRunner::class)
class ListenSheetIntegrationTest {

    private val activity = Robolectric.buildActivity(ComponentActivity::class.java).setup().get()

    private val container = CoordinatorLayout(activity)

    private val testScope = TestScope(UnconfinedTestDispatcher())

    private fun integration(isEnabled: Flow<ListenEnabledState> = MutableStateFlow(ListenEnabledState.Enabled)) =
        ListenSheetIntegration(
            container = container,
            browserStore = BrowserStore(),
            listenStore = ListenStore(ListenState(), ::listenReducer),
            engineView = mockk(relaxed = true),
            isAddressBarAtBottom = true,
            isEnabled = isEnabled,
            scope = testScope,
            onListenClicked = {},
            onCustomizeReaderViewClicked = {},
        )

    private val sheet: View
        get() = requireNotNull(container.findViewById(R.id.listenSheet))

    @After
    fun tearDown() {
        testScope.cancel()
    }

    @Before
    fun setUp() {
        activity.setContentView(container)
    }

    @Test
    fun `WHEN the feature is stopped THEN the sheet is hidden rather than taken out of the container`() {
        // a snackbar anchors itself to the sheet, and CoordinatorLayout throws when it measures
        // an anchor id that names no view the layout holds.
        val integration = integration()

        integration.start()
        shadowOf(Looper.getMainLooper()).idle()
        assertTrue(sheet.isVisible)

        integration.stop()

        assertEquals(1, container.childCount)
        assertFalse(sheet.isVisible)
    }

    @Test
    fun `GIVEN the feature was stopped WHEN it is started again THEN the same sheet is shown again`() {
        val integration = integration()

        integration.start()
        shadowOf(Looper.getMainLooper()).idle()
        integration.stop()
        integration.start()
        shadowOf(Looper.getMainLooper()).idle()

        assertEquals(1, container.childCount)
        assertTrue(sheet.isVisible)
    }

    @Test
    fun `GIVEN enabled is not yet known WHEN the feature is started THEN no sheet is added`() {
        integration(isEnabled = MutableStateFlow(ListenEnabledState.Loading)).start()
        shadowOf(Looper.getMainLooper()).idle()

        assertNull(container.findViewById(R.id.listenSheet))
    }

    @Test
    fun `GIVEN the feature is disabled WHEN the feature is started THEN no sheet is added`() {
        integration(isEnabled = MutableStateFlow(ListenEnabledState.Disabled)).start()
        shadowOf(Looper.getMainLooper()).idle()

        assertNull(container.findViewById(R.id.listenSheet))
    }

    @Test
    fun `GIVEN the feature is started WHEN enabled changes THEN the sheet follows it`() {
        val isEnabled = MutableStateFlow(ListenEnabledState.Loading)
        integration(isEnabled = isEnabled).start()

        isEnabled.value = ListenEnabledState.Enabled
        shadowOf(Looper.getMainLooper()).idle()
        assertTrue(sheet.isVisible)

        isEnabled.value = ListenEnabledState.Disabled
        assertFalse(sheet.isVisible)

        isEnabled.value = ListenEnabledState.Enabled
        assertTrue(sheet.isVisible)
    }

    @Test
    fun `GIVEN the feature is stopped WHEN enabled changes THEN the sheet stays hidden`() {
        val isEnabled = MutableStateFlow(ListenEnabledState.Enabled)
        val integration = integration(isEnabled = isEnabled)
        integration.start()
        shadowOf(Looper.getMainLooper()).idle()
        integration.stop()

        isEnabled.value = ListenEnabledState.Disabled
        isEnabled.value = ListenEnabledState.Enabled

        assertFalse(sheet.isVisible)
    }
}
