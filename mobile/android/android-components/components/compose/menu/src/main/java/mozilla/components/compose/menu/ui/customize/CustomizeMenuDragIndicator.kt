/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.ui.icons.R as iconsR

@Composable
internal fun CustomizeMenuDragIndicator(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier.size(AcornTheme.layout.space.static600),
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            painter = painterResource(iconsR.drawable.mozac_ic_drag_indicator_24),
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@PreviewLightDark
@Composable
private fun CustomizeMenuDragIndicatorPreview() {
    AcornTheme {
        Surface {
            CustomizeMenuDragIndicator()
        }
    }
}
