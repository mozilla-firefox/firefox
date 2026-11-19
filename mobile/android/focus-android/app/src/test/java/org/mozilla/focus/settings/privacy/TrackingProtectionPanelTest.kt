/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.settings.privacy

import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.Button
import androidx.appcompat.app.AppCompatActivity
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.focus.R
import org.mozilla.focus.TestFocusApplication
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(application = TestFocusApplication::class, qualifiers = "en")
class TrackingProtectionPanelTest {

    private var connectionInfoRequests = 0

    @Test
    fun `GIVEN the certificate details link is shown THEN it replaces the connection security row`() {
        val panel = createPanel(showCertificateDetailsLink = true)

        assertTrue(panel.isShown(R.id.secure_connection_link))
        assertFalse(panel.isShown(R.id.security_info))
    }

    @Test
    fun `GIVEN the certificate details link is not shown THEN the connection security row is shown instead`() {
        val panel = createPanel(showCertificateDetailsLink = false)

        assertFalse(panel.isShown(R.id.secure_connection_link))
        assertTrue(panel.isShown(R.id.security_info))
    }

    @Test
    fun `WHEN the website presents a QWAC THEN the drag handle has more space under it`() {
        val panel = createPanel(showCertificateDetailsLink = true)
        assertTrue(panel.isShown(R.id.drag_handle))
        assertEquals(panel.dimension(R.dimen.bottom_sheet_drag_handle_bottom_margin), panel.dragHandleBottomMargin())

        panel.updateQwacStatus(isQwac = true)

        assertEquals(
            panel.dimension(R.dimen.bottom_sheet_drag_handle_bottom_margin_qwac),
            panel.dragHandleBottomMargin(),
        )
    }

    @Test
    fun `WHEN the certificate details link is clicked THEN the certificate details are opened`() {
        val panel = createPanel(showCertificateDetailsLink = true)

        panel.findViewById<View>(R.id.secure_connection_link)!!.performClick()

        assertEquals(1, connectionInfoRequests)
    }

    @Test
    fun `THEN the certificate details link is announced as a button that views the certificate details`() {
        val panel = createPanel(showCertificateDetailsLink = true)
        val link = panel.findViewById<View>(R.id.secure_connection_link)!!

        val info = link.createAccessibilityNodeInfo()

        assertEquals(Button::class.java.name, info.className)
        assertEquals(
            panel.context.getString(R.string.connection_details_view_certificate_details),
            info.actionList.first { it.id == AccessibilityNodeInfo.ACTION_CLICK }.label,
        )
    }

    private fun createPanel(showCertificateDetailsLink: Boolean): TrackingProtectionPanel {
        val activity =
            Robolectric.buildActivity(AppCompatActivity::class.java)
                .also { it.get().setTheme(R.style.AppTheme) }
                .setup()
                .get()

        return TrackingProtectionPanel(
                context = activity,
                lifecycleOwner = activity,
                siteInfo =
                    SiteSecurityInfo(
                        tabUrl = "https://www.mozilla.org",
                        blockedTrackersCount = 0,
                        isTrackingProtectionOn = true,
                        isConnectionSecure = true,
                        showCertificateDetailsLink = showCertificateDetailsLink,
                    ),
                interactor =
                    object : TrackingProtectionPanelInteractor {
                        override fun toggleTrackingProtection(enabled: Boolean) = Unit

                        override fun updateTrackingProtectionPolicy(tracker: String?, enabled: Boolean) = Unit

                        override fun showConnectionInfo() {
                            connectionInfoRequests++
                        }
                    },
            )
            .also { it.show() }
    }

    private fun TrackingProtectionPanel.isShown(id: Int) = findViewById<View>(id)!!.visibility == View.VISIBLE

    private fun TrackingProtectionPanel.dimension(id: Int) = context.resources.getDimensionPixelSize(id)

    private fun TrackingProtectionPanel.dragHandleBottomMargin() =
        (findViewById<View>(R.id.drag_handle_bar)!!.layoutParams as ViewGroup.MarginLayoutParams).bottomMargin
}
