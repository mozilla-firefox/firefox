/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.background
import androidx.compose.foundation.basicMarquee
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R

/**
 * The top of a customize menu screen list, telling the user what to drag and allowing to preview the menu.
 *
 * @param text What to tell the user to drag.
 * @param onPreviewClick Callback invoked when the "Preview" button is clicked.
 * @param modifier The [Modifier] to be applied to the bar.
 */
@Composable
internal fun CustomizeMenuDragBar(
    text: String,
    onPreviewClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier =
            modifier
                .fillMaxWidth()
                .defaultMinSize(minHeight = AcornTheme.layout.space.static600)
                .background(MaterialTheme.colorScheme.surfaceContainer)
                .padding(
                    start = AcornTheme.layout.space.static200,
                    end = AcornTheme.layout.space.static100,
                ),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = text,
            modifier = Modifier.weight(1f).basicMarquee().semantics { heading() },
            style = AcornTheme.typography.headline8,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 1,
        )
        TextButton(
            text = stringResource(R.string.mozac_menu_customize_preview),
            onClick = onPreviewClick,
        )
    }
}

/**
 * The title shown above the hidden groups or items of a customize menu screen list.
 *
 * @param text The title.
 * @param modifier The [Modifier] to be applied to the title.
 */
@Composable
internal fun CustomizeMenuHiddenTitle(text: String, modifier: Modifier = Modifier) {
    Text(
        text = text,
        modifier =
            modifier
                .semantics { heading() }
                .fillMaxWidth()
                .defaultMinSize(minHeight = AcornTheme.layout.space.static600)
                .padding(
                    start = AcornTheme.layout.space.static200,
                    end = AcornTheme.layout.space.static100,
                    top = AcornTheme.layout.space.static100,
                    bottom = AcornTheme.layout.space.static50,
                )
                .wrapContentHeight(Alignment.CenterVertically),
        style = AcornTheme.typography.headline8,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

/**
 * The empty area to drag groups or items into to hide them.
 *
 * @param text What to tell the user to drag into it.
 * @param isDropTarget Whether a dragged group or item is dropped into it, which highlights it.
 * @param modifier The [Modifier] to be applied to the area.
 */
@Composable
internal fun CustomizeMenuDropArea(text: String, isDropTarget: Boolean, modifier: Modifier = Modifier) {
    Box(
        modifier =
            modifier
                .fillMaxWidth()
                .padding(
                    start = AcornTheme.layout.space.static200,
                    top = AcornTheme.layout.space.static150,
                    end = AcornTheme.layout.space.static200,
                )
                .then(
                    if (isDropTarget) {
                        Modifier.background(
                            color = MaterialTheme.colorScheme.tertiaryContainer,
                            shape = MaterialTheme.shapes.extraLarge,
                        )
                    } else {
                        Modifier
                    }
                )
                .hiddenBorder(isHighlighted = isDropTarget)
                .defaultMinSize(minHeight = 72.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = text,
            style = AcornTheme.typography.body2,
            color =
                if (isDropTarget) {
                    MaterialTheme.colorScheme.tertiary
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
        )
    }
}
