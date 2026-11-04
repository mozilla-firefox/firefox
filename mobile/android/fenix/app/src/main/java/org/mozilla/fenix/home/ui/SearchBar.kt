/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

private val searchBarShape: Shape
    @Composable get() = MaterialTheme.shapes.extraLarge
private val IconBoxSize = 48.dp

/**
 * Search bar.
 *
 * @param modifier [Modifier] for the content.
 * @param onClick Invoked when the user clicks on the search bar text.
 */
@Composable
internal fun SearchBar(
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
) {
    Row(
        modifier =
            modifier
                .width(FirefoxTheme.layout.size.containerMaxWidth)
                .clip(shape = searchBarShape)
                .background(color = MaterialTheme.colorScheme.surfaceBright)
                .clickable { onClick() }
                .padding(all = FirefoxTheme.layout.space.static50),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier = Modifier.size(size = IconBoxSize),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                painter = painterResource(id = iconsR.drawable.mozac_ic_search_24),
                tint = MaterialTheme.colorScheme.onSurface,
                contentDescription = null,
            )
        }

        Text(
            text = stringResource(R.string.search_hint),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(horizontal = FirefoxTheme.layout.space.static50).weight(1f),
            overflow = TextOverflow.Ellipsis,
            maxLines = 1,
            style = FirefoxTheme.typography.body1,
        )
    }
}

@Composable
@PreviewLightDark
private fun SearchBarPreview() {
    FirefoxTheme {
        SearchBar(
            modifier =
                Modifier.background(color = MaterialTheme.colorScheme.surface)
                    .padding(all = FirefoxTheme.layout.space.static200),
            onClick = {},
        )
    }
}
