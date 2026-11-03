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
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.ListenStore
import mozilla.components.feature.listentopage.listenReducer
import mozilla.components.feature.listentopage.settings.ListenEnabledState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class ListenSheetIntegrationTest {

    private val activity = Robolectric.buildActivity(ComponentActivity::class.java).setup().get()

    private val container = CoordinatorLayout(activity)

    private fun integration(isEnabled: StateFlow<ListenEnabledState> = MutableStateFlow(ListenEnabledState.Enabled)) =
        ListenSheetIntegration(
            container = container,
            browserStore = BrowserStore(),
            listenStore = ListenStore(ListenState(), ::listenReducer),
            engineView = mockk(relaxed = true),
            isAddressBarAtBottom = true,
            isEnabled = isEnabled,
            onListenClicked = {},
            onCustomizeReaderViewClicked = {},
        )

    private val sheet: View
        get() = requireNotNull(container.findViewById(R.id.listenSheet))

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
    fun `GIVEN listening is disabled WHEN the feature is started THEN the sheet is still shown`() {
        integration(isEnabled = MutableStateFlow(ListenEnabledState.Disabled)).start()
        shadowOf(Looper.getMainLooper()).idle()

        assertTrue(sheet.isVisible)
    }

    @Test
    fun `GIVEN listening is not yet known WHEN the feature is started THEN the sheet is still shown`() {
        integration(isEnabled = MutableStateFlow(ListenEnabledState.Loading)).start()
        shadowOf(Looper.getMainLooper()).idle()

        assertTrue(sheet.isVisible)
    }
}
