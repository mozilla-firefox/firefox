/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.identitycredential.previews

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable

@Composable
internal fun DialogPreviewMaterialTheme(content: @Composable () -> Unit) {
    val colorScheme =
        if (!isSystemInDarkTheme()) {
            lightColorScheme()
        } else {
            darkColorScheme()
        }
    MaterialTheme(colorScheme = colorScheme) {
        content()
    }
}
