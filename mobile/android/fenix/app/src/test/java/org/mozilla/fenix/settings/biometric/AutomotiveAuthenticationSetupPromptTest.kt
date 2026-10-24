/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.biometric

import android.content.DialogInterface
import android.provider.Settings
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.fragment.app.FragmentActivity
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
import org.robolectric.shadows.ShadowLooper

@RunWith(RobolectricTestRunner::class)
class AutomotiveAuthenticationSetupPromptTest {

    private lateinit var activity: FragmentActivity
    private var setUpCount = 0
    private var cancelCount = 0

    @Before
    fun setup() {
        val controller = Robolectric.buildActivity(FragmentActivity::class.java)
        controller.get().setTheme(R.style.NormalTheme)
        activity = controller.setup().get()
        setUpCount = 0
        cancelCount = 0
    }

    private fun showPrompt(): AlertDialog =
        DefaultAutomotiveAuthenticationSetupPrompt.show(
            activity = activity,
            onSetUp = { setUpCount++ },
            onCancel = { cancelCount++ },
        )

    @Test
    fun `WHEN the prompt is shown THEN it displays the automotive copy`() {
        val dialog = showPrompt()

        assertTrue(dialog.isShowing)
        assertEquals(
            activity.getString(R.string.automotive_authentication_setup_dialog_message),
            dialog.findViewById<TextView>(android.R.id.message)?.text.toString(),
        )
        assertEquals(
            activity.getString(R.string.automotive_authentication_setup_dialog_set_up_now),
            dialog.getButton(DialogInterface.BUTTON_POSITIVE).text.toString(),
        )
        assertEquals(
            activity.getString(R.string.automotive_authentication_setup_dialog_cancel),
            dialog.getButton(DialogInterface.BUTTON_NEGATIVE).text.toString(),
        )
    }

    @Test
    fun `WHEN set up now is clicked THEN the security settings are opened and onSetUp is invoked`() {
        val dialog = showPrompt()

        dialog.getButton(DialogInterface.BUTTON_POSITIVE).performClick()
        ShadowLooper.idleMainLooper()

        assertEquals(Settings.ACTION_SECURITY_SETTINGS, shadowOf(activity).nextStartedActivity?.action)
        assertEquals(1, setUpCount)
        assertEquals(0, cancelCount)
        assertFalse(dialog.isShowing)
    }

    @Test
    fun `WHEN cancel is clicked THEN onCancel is invoked and the security settings are not opened`() {
        val dialog = showPrompt()

        dialog.getButton(DialogInterface.BUTTON_NEGATIVE).performClick()
        ShadowLooper.idleMainLooper()

        assertNull(shadowOf(activity).nextStartedActivity)
        assertEquals(0, setUpCount)
        assertEquals(1, cancelCount)
        assertFalse(dialog.isShowing)
    }

    @Test
    fun `WHEN the prompt is dismissed THEN onCancel is invoked`() {
        val dialog = showPrompt()

        dialog.cancel()
        ShadowLooper.idleMainLooper()

        assertEquals(0, setUpCount)
        assertEquals(1, cancelCount)
    }

    @Test
    fun `WHEN the security settings cannot be opened THEN onCancel is invoked`() {
        shadowOf(activity.application).checkActivities(true)
        val dialog = showPrompt()

        dialog.getButton(DialogInterface.BUTTON_POSITIVE).performClick()
        ShadowLooper.idleMainLooper()

        assertEquals(0, setUpCount)
        assertEquals(1, cancelCount)
    }

    @Test
    fun `WHEN the prompt was already shown THEN it can be shown again`() {
        showPrompt().getButton(DialogInterface.BUTTON_NEGATIVE).performClick()
        ShadowLooper.idleMainLooper()

        assertTrue(showPrompt().isShowing)
    }
}
