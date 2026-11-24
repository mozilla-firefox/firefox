/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import mozilla.components.compose.base.theme.AcornCorners
import mozilla.components.compose.base.theme.AcornTheme

/** Card component to hold ListenPlayer contents */
@Composable
fun BoxScope.ListenPlayerCard(content: @Composable ColumnScope.() -> Unit) {
    Card(
        shape = RoundedCornerShape(AcornCorners.extraLarge),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(AcornTheme.layout.elevation.level2),
        border = BorderStroke(AcornTheme.layout.border.default, MaterialTheme.colorScheme.outlineVariant),
        modifier =
            Modifier.widthIn(max = PlayerMaxWidth).align(Alignment.Center).testTag(LISTEN_SHEET_PLAYER_CARD_TEST_TAG),
        content = content,
    )
}
