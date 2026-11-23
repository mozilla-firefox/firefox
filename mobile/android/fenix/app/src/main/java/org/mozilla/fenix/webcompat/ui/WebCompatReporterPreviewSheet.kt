/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.webcompat.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.traversalIndex
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.BottomSheetHandle
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.compose.ExpandableMenuItemAnimation
import org.mozilla.fenix.components.menu.compose.MenuGroup
import org.mozilla.fenix.compose.rememberModalSheetState
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.webcompat.store.PreviewReporterItem

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun WebCompatReporterPreviewSheet(
    previewReporterItems: List<PreviewReporterItem>,
    onDismissRequest: () -> Unit,
) {
    ModalBottomSheet(
        onDismissRequest = onDismissRequest,
        sheetState = rememberModalSheetState(skipPartiallyExpanded = true),
        dragHandle = {
            BottomSheetHandle(
                onRequestDismiss = onDismissRequest,
                contentDescription = stringResource(R.string.a11y_action_label_collapse),
                modifier =
                    Modifier.width(FirefoxTheme.layout.size.static400)
                        .padding(vertical = FirefoxTheme.layout.space.static200)
                        .semantics { traversalIndex = -1f },
            )
        },
        containerColor = MaterialTheme.colorScheme.surface,
        modifier = Modifier.padding(top = 72.dp),
        contentWindowInsets = { WindowInsets.safeDrawing.only(WindowInsetsSides.Bottom) },
    ) {
        PreviewSheetContent(previewReporterItems = previewReporterItems)
    }
}

@Composable
private fun PreviewSheetContent(previewReporterItems: List<PreviewReporterItem>) {
    val expandedItems = remember { mutableStateMapOf<String, Boolean>() }

    Column(
        modifier =
            Modifier.fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(
                    start = FirefoxTheme.layout.space.dynamic200,
                    end = FirefoxTheme.layout.space.dynamic200,
                    bottom = FirefoxTheme.layout.space.dynamic200,
                ),
        horizontalAlignment = Alignment.Start,
    ) {
        Text(
            text = stringResource(id = R.string.webcompat_reporter_preview_bottom_sheet_header),
            style = FirefoxTheme.typography.headline7,
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

        MenuGroup {
            for (previewReporterItem in previewReporterItems) {
                val isExpanded = expandedItems[previewReporterItem.title] == true
                PreviewReporterItemRow(
                    previewReporterItem = previewReporterItem,
                    isExpanded = isExpanded,
                    onExpandToggle = { expandedItems[previewReporterItem.title] = !isExpanded },
                )
            }
        }
    }
}

@Composable
private fun PreviewReporterItemRow(
    previewReporterItem: PreviewReporterItem,
    isExpanded: Boolean,
    onExpandToggle: () -> Unit,
) {
    val rotation by animateFloatAsState(targetValue = if (isExpanded) 180f else 0f)

    Column(modifier = Modifier.fillMaxWidth().clip(MaterialTheme.shapes.extraSmall)) {
        Row(
            modifier =
                Modifier.fillMaxWidth()
                    .background(
                        color =
                            if (isExpanded) {
                                MaterialTheme.colorScheme.surfaceContainerHighest
                            } else {
                                MaterialTheme.colorScheme.surfaceContainerHigh
                            }
                    )
                    .clickable(
                        onClickLabel =
                            stringResource(
                                if (isExpanded) {
                                    R.string.a11y_action_label_collapse
                                } else {
                                    R.string.a11y_action_label_expand
                                }
                            ),
                        onClick = onExpandToggle,
                    )
                    .padding(FirefoxTheme.layout.space.static200),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = previewReporterItem.title,
                modifier = Modifier.weight(1f),
                style = FirefoxTheme.typography.body1,
                color = MaterialTheme.colorScheme.onSurface,
            )

            Row(
                modifier =
                    Modifier.background(
                            color = MaterialTheme.colorScheme.surfaceContainerHighest,
                            shape = MaterialTheme.shapes.large,
                        )
                        .padding(all = 2.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Icon(
                    painter = painterResource(id = iconsR.drawable.mozac_ic_chevron_down_20),
                    contentDescription =
                        "${previewReporterItem.title}, " +
                            stringResource(
                                if (isExpanded) {
                                    R.string.a11y_action_label_collapse
                                } else {
                                    R.string.a11y_action_label_expand
                                }
                            ),
                    modifier = Modifier.rotate(rotation),
                    tint = MaterialTheme.colorScheme.onSurface,
                )
            }
        }

        ExpandableMenuItemAnimation(isExpanded = isExpanded) {
            PreviewReporterItemExpandedContent(previewReporterItem.data)
        }
    }
}

@Composable
private fun PreviewReporterItemExpandedContent(data: Map<String, String>) {
    Column(modifier = Modifier.fillMaxWidth().background(color = MaterialTheme.colorScheme.surfaceContainerHigh)) {
        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static100))
        for ((key, value) in data) {
            Text(
                text = "$key: $value",
                modifier = Modifier.fillMaxWidth().padding(horizontal = FirefoxTheme.layout.space.static200),
                style = FirefoxTheme.typography.body2,
                color = MaterialTheme.colorScheme.secondary,
            )
        }
        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static100))
    }
}

@Composable
private fun WebCompatReporterPreviewSheetContent() {
    val previewReporterItems = remember {
        listOf(
            PreviewReporterItem(
                title = "Browser Info",
                data =
                    mapOf(
                        "App" to "Fenix",
                        "Version" to "123.0",
                        "OS" to "Android 14",
                    ),
            ),
            PreviewReporterItem(
                title = "Basic",
                data =
                    mapOf(
                        "Vendor" to "Google",
                        "Renderer" to "ANGLE",
                    ),
            ),
            PreviewReporterItem(
                title = "Graphics",
                data =
                    mapOf(
                        "Vendor" to "Google",
                        "Renderer" to "ANGLE",
                    ),
            ),
        )
    }
    PreviewSheetContent(previewReporterItems = previewReporterItems)
}

@Preview
@Composable
private fun WebCompatReporterSheetPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        WebCompatReporterPreviewSheetContent()
    }
}
