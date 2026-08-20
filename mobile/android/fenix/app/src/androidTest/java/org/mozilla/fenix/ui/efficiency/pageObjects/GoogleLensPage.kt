/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import android.content.ContentValues
import android.graphics.Bitmap
import android.graphics.Color
import android.net.Uri
import android.provider.MediaStore
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.selectors.GoogleLensSelectors

class GoogleLensPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) : BasePage(composeRule) {
    override val pageName = "GoogleLensPage"

    // No navigation edge is registered on purpose, and this page is intentionally left out of PageContext. The opt-out
    // sheet and Lens camera live in LensCameraActivity's Compose tree, a transient surface reached only via the
    // search-bar Lens button under forced settings (Nimbus + user pref) and a fresh first-run state, which the generic
    // navigation graph cannot express. Registered in PageContext it would get an auto-generated reachability case that
    // always fails. Tests drive the entry sequence explicitly and act on this page's selectors, mirroring
    // ReaderViewPage.

    override fun mozGetSelectorsByGroup(group: String): List<Selector> {
        return GoogleLensSelectors.all.filter { it.groups.contains(group) }
    }

    /** Accept the opt-out sheet ("Try it now"): requests CAMERA and opens the Lens camera screen. */
    fun acceptOptOutSheet(): GoogleLensPage {
        mozClick(GoogleLensSelectors.OPT_OUT_TRY_IT_NOW)
        return this
    }

    /** Dismiss the opt-out sheet and close LensCameraActivity (back to the HomeActivity search bar). */
    fun dismissOptOutSheet(): GoogleLensPage {
        mozPressBackUntilGone(GoogleLensSelectors.OPT_OUT_SHEET_TITLE)
        return this
    }

    /**
     * Insert a small image into MediaStore and return its content URI (the app can read its own inserts). Stateless:
     * the caller owns the returned URI and is responsible for deleting it, since a fresh page-object instance is
     * created per access and cannot hold it for teardown.
     */
    fun seedGalleryImage(): Uri {
        val resolver = appContext.contentResolver
        val values =
            ContentValues().apply {
                put(MediaStore.Images.Media.DISPLAY_NAME, "lens_gallery_test.png")
                put(MediaStore.Images.Media.MIME_TYPE, "image/png")
            }
        val uri =
            resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values)
                ?: error("Failed to insert test image into MediaStore")
        val bitmap = Bitmap.createBitmap(64, 64, Bitmap.Config.ARGB_8888).apply { eraseColor(Color.BLUE) }
        resolver.openOutputStream(uri)!!.use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
        return uri
    }
}
