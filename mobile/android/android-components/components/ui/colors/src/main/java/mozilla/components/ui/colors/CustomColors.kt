/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.ui.colors

import androidx.compose.ui.graphics.Color

/**
 * Colors from the [Photon Design System](https://design.firefox.com/photon/visuals/color.html).
 *
 * _"Firefox colors are bold, vibrant and attractive. They enhance the experience by providing visual
 * clues and by bringing attention to primary actions."_
 */


object CustomColorsDark {
    val Accent = Color(0xFFF5671E)// same for both themes
    // dark theme
    val IconSecondary = Color(0xFF828B93)
    val IconNormal = Color(0xFFEDEEF0)
    val IconInverse = Color(0xFF171B1E)
    val TextInverse = Color(0xFF171B1E)
    val TextSecondary = Color(0xFF828B93)
    val TextNormal = Color(0xFFEDEEF0)
    val ControlsTabSecondarySelected = Color(0xFFE2E5E8)
    val ControlsTabNormal = Color(0xFF1F2428)
    val ControlsButtonSecondaryNormal = Color(0xFF272D32)
    val BackgroundPlate = Color(0xFF171B1E)
    val ControlsInputFocusFill = Color(0xFF272D32)
    val ControlsButtonPrimaryNormal = Color(0xFFDCE1E6)
    val BackgroundNormal = Color(0xFF0F1214)

}

object CustomColorsLight {
    // dark theme
    val IconSecondary = Color(0xFF4D5964)
    val IconNormal = Color(0xFF272D32)
    val IconInverse = Color(0xFFEDEEF0)
    val TextNormal = Color(0xFF272D32)
    val TextInverse = Color(0xFFEDEEF0)
    val TextSecondary = Color(0xFF4D5964)
    val ControlsTabSecondarySelected = Color(0xFF1F2428)
    val ControlsTabNormal = Color(0xFFDCE1E6)
    val ControlsButtonSecondaryNormal = Color(0xFFDCE1E6)
    val BackgroundPlate = Color(0xFFEDEEF0)
    val ControlsInputFocusFill = Color(0xFFE2E5E8)
    val ControlsButtonPrimaryNormal = Color(0xFF272D32)
    val BackgroundNormal = Color(0xFFF7F8FA)

}
