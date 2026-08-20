/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import android.Manifest
import android.app.Activity.RESULT_OK
import android.app.Instrumentation
import android.content.Intent
import android.net.Uri
import android.provider.MediaStore
import androidx.test.espresso.intent.Intents.intended
import androidx.test.espresso.intent.Intents.intending
import androidx.test.espresso.intent.matcher.IntentMatchers.hasAction
import androidx.test.rule.GrantPermissionRule
import org.junit.After
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.pageObjects.GoogleLensPage
import org.mozilla.fenix.ui.efficiency.selectors.GoogleLensSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SearchBarSelectors

/**
 * Coverage for the Google Lens button in the focused (edit-mode) search bar.
 *
 * Scope: this automates only the front half of the manual Google Lens tests. It verifies the Lens button is shown, that
 * tapping it reaches the first-run opt-out bottom sheet, and that the gallery ("upload an image") entry point launches
 * the photo picker and accepts a selection. Taking a picture, and observing the actual Lens result for a captured OR
 * uploaded image, require the real camera and/or a live upload to Google's servers, so they are NOT automatable on CI
 * and stay manual steps.
 *
 * The Lens button is gated by BrowserToolbarSearchMiddleware.updateSearchEndPageActions(): googleLensIntegrationEnabled
 * && googleLensIntegrationUserEnabled && !isPrivate && selectedEngine.isGoogleSearchEngine()
 * googleLensIntegrationEnabled defaults from the `google-lens-integration` Nimbus flag (off on release, on for
 * nightly/developer), so we force both settings on in @Before to make the button deterministic across channels.
 */
class GoogleLensSearchTest : BaseTest() {

    // The Lens camera screen (which hosts the gallery button) is only reached if CAMERA is granted — otherwise
    // LensCameraActivity toasts "Permission denied" and finishes. Pre-grant so no system dialog interrupts the flow.
    @get:Rule val cameraPermissionRule: GrantPermissionRule = GrantPermissionRule.grant(Manifest.permission.CAMERA)

    // GoogleLensPage is not part of PageContext: the opt-out sheet + Lens camera are a transient surface in
    // LensCameraActivity reachable only via the search-bar Lens button under forced settings, which the navigation
    // graph cannot express. Instantiate it locally, mirroring ReaderViewPage.
    private val googleLens
        get() = GoogleLensPage(composeRule)

    private var seededImageUri: Uri? = null

    @Before
    fun forceLensIntegrationEnabled() {
        val settings = appContext.components.settings
        settings.googleLensIntegrationEnabled = true
        settings.googleLensIntegrationUserEnabled = true
        // Ensure the first-run opt-out sheet is shown on the first Lens tap (a prior run that tapped
        // "Try it now" would otherwise leave this true and skip straight to the camera).
        settings.hasAcceptedGoogleLensFirstRun = false
    }

    @After
    fun deleteSeededImage() {
        seededImageUri?.let { appContext.contentResolver.delete(it, null, null) }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4135958
    // Kept as a single test for now (opt-out sheet, then gallery upload) to stay 1:1 with the one manual case, rather
    // than
    // split per the "one test, one assertion" rule.
    @Test
    fun verifyGoogleLensFunctionalityTest() {
        // Lens button is shown for Google + normal mode, and tapping it raises the first-run opt-out sheet.
        on.home.navigateToPage()
        on.searchBar.navigateToPage()
        on.searchBar.mozVerify(SearchBarSelectors.GOOGLE_LENS_BUTTON)
        on.searchBar.mozClick(SearchBarSelectors.GOOGLE_LENS_BUTTON)
        googleLens.mozVerify(GoogleLensSelectors.OPT_OUT_SHEET_TITLE)

        // Follow-up: return to the homepage, re-open Lens, and this time upload an image from the gallery.
        googleLens.dismissOptOutSheet()
        on.home.navigateToPage(forceNavigation = true)

        on.searchBar.navigateToPage()
        on.searchBar.mozClick(SearchBarSelectors.GOOGLE_LENS_BUTTON)
        googleLens.acceptOptOutSheet()
        googleLens.mozVerify(GoogleLensSelectors.GALLERY_BUTTON)

        // Stub the Android photo picker so no real system picker opens: return a seeded image as the selection.
        val galleryImageUri = googleLens.seedGalleryImage().also { seededImageUri = it }
        intending(hasAction(MediaStore.ACTION_PICK_IMAGES))
            .respondWith(Instrumentation.ActivityResult(RESULT_OK, Intent().apply { data = galleryImageUri }))
        googleLens.mozClick(GoogleLensSelectors.GALLERY_BUTTON)

        // Deterministic boundary: the picker was launched and the selection was accepted (Lens camera closed). The
        // uploaded-image Lens result opens in a tab only after a live upload to Google, so it stays a manual step.
        intended(hasAction(MediaStore.ACTION_PICK_IMAGES))
        googleLens.mozWaitUntilAbsent(GoogleLensSelectors.GALLERY_BUTTON)
    }
}
