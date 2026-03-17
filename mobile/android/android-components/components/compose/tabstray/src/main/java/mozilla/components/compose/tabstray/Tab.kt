/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.tabstray

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.ui.icons.R as iconsR

/**
 * Renders a single [TabSessionState] as a list item.
 *
 * @param tab The tab to render.
 * @param selected Whether this tab is selected or not.
 * @param onClick Gets invoked when the tab gets clicked.
 * @param onClose Gets invoked when tab gets closed.
 */
@Composable
fun Tab(
    tab: TabSessionState,
    selected: Boolean = false,
    onClick: (String) -> Unit = {},
    onClose: (String) -> Unit = {},
) {
    Box(
        modifier = Modifier
            .background(if (selected) Color(0xFFFF45A1FF.toInt()) else Color.Unspecified)
            .size(width = Dp.Unspecified, height = 72.dp)
            .fillMaxWidth()
            .clickable { onClick.invoke(tab.id) }
            .padding(8.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceAround,
        ) {
            // BrowserThumbnail(tab)
            Column(
                modifier = Modifier
                    .weight(1f)
                    .align(Alignment.CenterVertically)
                    .padding(8.dp),
            ) {
                Text(
                    text = tab.content.title,
                    fontWeight = FontWeight.Bold,
                    overflow = TextOverflow.Ellipsis,
                    maxLines = 1,
                    color = Color.White,
                )
                Text(
                    text = tab.content.url,
                    style = MaterialTheme.typography.bodyMedium,
                    overflow = TextOverflow.Ellipsis,
                    maxLines = 1,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            IconButton(
                modifier = Modifier
                    .align(Alignment.CenterVertically)
                    .requiredSize(24.dp),
                onClick = { onClose.invoke(tab.id) },
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_cross_24),
                    contentDescription = "close",
                    tint = Color.White,
                )
            }
        }
    }
}
