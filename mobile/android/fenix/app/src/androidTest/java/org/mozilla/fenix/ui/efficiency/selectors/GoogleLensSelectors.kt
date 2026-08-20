/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

// Selectors for the Google Lens opt-out sheet and camera screen, which live in LensCameraActivity's Compose tree.
// The search-bar Lens button itself stays in SearchBarSelectors: it belongs to the edit-mode search bar, this page
// begins once that button has been tapped.
object GoogleLensSelectors {
    // First-run opt-out bottom sheet raised by the first tap of the Lens button, BEFORE any camera access. This is the
    // deterministic launch boundary a UI test can assert without a real camera (the capture step beyond this needs
    // one).
    val OPT_OUT_SHEET_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.lens_opt_out_title),
            description = "Google Lens first-run opt-out bottom sheet title",
            groups = listOf("requiredForPage"),
        )

    // "Try it now" on the opt-out sheet — accepts the first-run prompt, which requests CAMERA and opens the Lens camera
    // screen (where the gallery button lives).
    val OPT_OUT_TRY_IT_NOW =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.lens_opt_out_try_it_now_button),
            description = "Google Lens opt-out 'Try it now' button",
            groups = listOf(),
        )

    // Gallery button on the Lens camera screen ("Choose from gallery to send to Google Lens"). Tapping it launches the
    // Android photo picker (ActivityResultContracts.PickVisualMedia -> MediaStore.ACTION_PICK_IMAGES), which a test
    // stubs with Espresso-Intents. Present in both LENS and QR modes, independent of camera availability.
    val GALLERY_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.content_description_gallery),
            description = "Google Lens gallery button",
            groups = listOf(),
        )

    val all =
        listOf(
            OPT_OUT_SHEET_TITLE,
            OPT_OUT_TRY_IT_NOW,
            GALLERY_BUTTON,
        )
}
