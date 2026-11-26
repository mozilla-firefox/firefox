/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")

package org.mozilla.fenix.tabstray.ui.inactivetabs

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.painter.BitmapPainter
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.cfr.CFR
import mozilla.components.compose.base.cfr.CFRTooltip
import mozilla.components.compose.base.cfr.IndicatorDirection
import mozilla.components.compose.base.cfr.rememberCFRPositionProvider
import mozilla.components.compose.base.cfr.rememberCFRState
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.list.ExpandableListHeader
import org.mozilla.fenix.ext.toShortUrl
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.tabstray.ui.tabitems.BasicTabListItem
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Top-level list for displaying an expandable section of Inactive Tabs.
 *
 * @param inactiveTabs List of [TabsTrayItem.Tab] to display.
 * @param expanded Whether to show the inactive tabs section expanded or collapsed.
 * @param showAutoCloseDialog Whether to show the auto close inactive tabs dialog.
 * @param showCFR Whether to show the CFR.
 * @param onHeaderClick Called when the user clicks on the inactive tabs section header.
 * @param onDeleteAllButtonClick Called when the user clicks on the delete all inactive tabs button.
 * @param onAutoCloseDismissClick Called when the user clicks on the auto close dialog's dismiss button.
 * @param onEnableAutoCloseClick Called when the user clicks on the auto close dialog's enable button.
 * @param onTabClick Called when the user clicks on a specific inactive tab.
 * @param onTabCloseClick Called when the user clicks on a specific inactive tab's close button.
 * @param onCFRShown Invoked when the CFR is displayed.
 * @param onCFRClick Invoked when the CFR is clicked.
 * @param onCFRDismiss Invoked when the CFR is dismissed.
 */
@Composable
@Suppress("LongParameterList")
fun InactiveTabsList(
    inactiveTabs: List<TabsTrayItem.Tab>,
    expanded: Boolean,
    showAutoCloseDialog: Boolean,
    showCFR: Boolean,
    onHeaderClick: (Boolean) -> Unit,
    onDeleteAllButtonClick: () -> Unit,
    onAutoCloseDismissClick: () -> Unit,
    onEnableAutoCloseClick: () -> Unit,
    onTabClick: (TabsTrayItem.Tab) -> Unit,
    onTabCloseClick: (TabsTrayItem.Tab) -> Unit,
    onCFRShown: () -> Unit,
    onCFRClick: () -> Unit,
    onCFRDismiss: () -> Unit,
) {
    Card(
        colors =
            CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceContainerLowest,
                contentColor = MaterialTheme.colorScheme.secondary,
            ),
        shape = MaterialTheme.shapes.medium,
    ) {
        InactiveTabsHeader(
            expanded = expanded,
            showCFR = showCFR,
            onCFRShown = onCFRShown,
            onCFRClick = onCFRClick,
            onCFRDismiss = onCFRDismiss,
            onClick = { onHeaderClick(!expanded) },
        )

        if (expanded) {
            if (showAutoCloseDialog) {
                InactiveTabsAutoClosePrompt(
                    onDismissClick = onAutoCloseDismissClick,
                    onEnableAutoCloseClick = onEnableAutoCloseClick,
                )
            }

            InactiveTabsSubHeader(
                inactiveTabsSize = inactiveTabs.size,
                onDeleteAllButtonClick = onDeleteAllButtonClick,
            )

            inactiveTabs.forEachIndexed { index, tab ->
                val tabUrl = tab.url.toShortUrl()
                val faviconPainter =
                    tab.icon?.run {
                        prepareToDraw()
                        BitmapPainter(asImageBitmap())
                    }

                BasicTabListItem(
                    title = tab.title,
                    url = tabUrl,
                    faviconPainter = faviconPainter,
                    onClick = { onTabClick(tab) },
                    showCloseButton = true,
                    onCloseButtonClick = { onTabCloseClick(tab) },
                )

                if (index != inactiveTabs.size - 1) {
                    HorizontalDivider()
                }
            }

            Spacer(modifier = Modifier.height(8.dp))
        }
    }
}

/**
 * Collapsible header for the Inactive Tabs section with a CFR.
 *
 * @param expanded Whether the section is expanded.
 * @param showCFR Whether to show the CFR.
 * @param onClick Called when the user clicks on the header.
 * @param onCFRShown Invoked when the CFR is displayed.
 * @param onCFRClick Invoked when the CFR is clicked.
 * @param onCFRDismiss Invoked when the CFR is dismissed.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun InactiveTabsHeader(
    expanded: Boolean,
    showCFR: Boolean,
    onClick: () -> Unit,
    onCFRShown: () -> Unit,
    onCFRClick: () -> Unit,
    onCFRDismiss: () -> Unit,
) {
    val cfrState = rememberCFRState()
    val dismissCFR = {
        cfrState.dismiss()
        onCFRDismiss()
    }

    ExpandableListHeader(
        headerText = stringResource(R.string.inactive_tabs_title),
        headerTextStyle = FirefoxTheme.typography.body1,
        headerTextColor = MaterialTheme.colorScheme.secondary,
        iconTint = MaterialTheme.colorScheme.secondary,
        contentPadding = PaddingValues(all = 16.dp),
        expanded = expanded,
        expandActionContentDescription = stringResource(R.string.inactive_tabs_expand_content_description),
        collapseActionContentDescription = stringResource(R.string.inactive_tabs_collapse_content_description),
        onClick = onClick,
        headerTextWrapper = { headerText ->
            CFRTooltip(
                state = cfrState,
                positionProvider =
                    rememberCFRPositionProvider(
                        indicatorDirection = IndicatorDirection.UP,
                        spacingBetweenCfrAndAnchor = 12.dp,
                    ),
                cfr = {
                    CFR(
                        onDismiss = dismissCFR,
                        text = {
                            Column {
                                Text(text = stringResource(R.string.tab_tray_inactive_onboarding_message))

                                Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

                                Text(
                                    text = stringResource(R.string.tab_tray_inactive_onboarding_button_text),
                                    modifier =
                                        Modifier.clickable(role = Role.Button) {
                                            cfrState.dismiss()
                                            onCFRClick()
                                        },
                                    style =
                                        FirefoxTheme.typography.body2.copy(textDecoration = TextDecoration.Underline),
                                )
                            }
                        },
                    )
                },
                onDismissRequest = dismissCFR,
                enableUserInput = false,
                anchor = headerText,
            )
        },
    )

    LaunchedEffect(showCFR) {
        if (showCFR) {
            onCFRShown()
            cfrState.show()
        }
    }
}

@Composable
private fun InactiveTabsSubHeader(
    inactiveTabsSize: Int,
    onDeleteAllButtonClick: () -> Unit,
) {
    Row(
        modifier =
            Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.surfaceContainerHigh).padding(start = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text =
                stringResource(
                    R.string.inactive_tabs_num_items,
                    inactiveTabsSize,
                ),
            modifier = Modifier.weight(1f),
            style = FirefoxTheme.typography.body1,
        )

        IconButton(
            onClick = onDeleteAllButtonClick,
            contentDescription = stringResource(R.string.inactive_tabs_delete_all),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_delete_24),
                contentDescription = null,
            )
        }
    }
}

/**
 * Inactive Tabs auto close dialog.
 *
 * @param onDismissClick Called when the user clicks on the auto close dialog's dismiss button.
 * @param onEnableAutoCloseClick Called when the user clicks on the auto close dialog's enable button.
 */
@Composable
private fun InactiveTabsAutoClosePrompt(
    onDismissClick: () -> Unit,
    onEnableAutoCloseClick: () -> Unit,
) {
    Card(
        modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
        shape = MaterialTheme.shapes.medium,
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerLowest),
        border =
            BorderStroke(
                width = 1.dp,
                color = MaterialTheme.colorScheme.outlineVariant,
            ),
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 12.dp),
            horizontalAlignment = Alignment.End,
        ) {
            Spacer(modifier = Modifier.height(12.dp))

            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    text = stringResource(R.string.tab_tray_inactive_auto_close_title),
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.weight(1f),
                    style = FirefoxTheme.typography.headline8,
                )

                IconButton(
                    onClick = onDismissClick,
                    contentDescription =
                        stringResource(R.string.tab_tray_inactive_auto_close_button_content_description),
                    modifier = Modifier.size(20.dp),
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_cross_20),
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.onSurface,
                    )
                }
            }

            Text(
                text =
                    stringResource(
                        R.string.tab_tray_inactive_auto_close_body_2,
                        stringResource(R.string.app_name),
                    ),
                color = MaterialTheme.colorScheme.secondary,
                modifier = Modifier.fillMaxWidth(),
                fontSize = 14.sp,
            )

            TextButton(
                text = stringResource(R.string.tab_tray_inactive_turn_on_auto_close_button_2),
                onClick = onEnableAutoCloseClick,
            )
        }
    }
}

@Composable
@PreviewLightDark
private fun InactiveTabsAutoClosePromptPreview() {
    FirefoxTheme {
        Box(Modifier.background(MaterialTheme.colorScheme.surface)) {
            InactiveTabsAutoClosePrompt(
                onDismissClick = {},
                onEnableAutoCloseClick = {},
            )
        }
    }
}

@Composable
@PreviewLightDark
private fun InactiveTabsListPreview(
    @PreviewParameter(InactiveTabsListPreviewParameterProvider::class) showCFR: Boolean
) {
    var expanded by remember { mutableStateOf(true) }
    var showAutoClosePrompt by remember { mutableStateOf(true) }

    FirefoxTheme {
        Box(Modifier.background(MaterialTheme.colorScheme.surface)) {
            InactiveTabsList(
                inactiveTabs =
                    listOf(
                        createTab(url = "www.mozilla.com"),
                        createTab(url = "www.example.com"),
                    ),
                expanded = expanded,
                showAutoCloseDialog = showAutoClosePrompt,
                showCFR = showCFR,
                onHeaderClick = { expanded = !expanded },
                onDeleteAllButtonClick = {},
                onAutoCloseDismissClick = { showAutoClosePrompt = !showAutoClosePrompt },
                onEnableAutoCloseClick = { showAutoClosePrompt = !showAutoClosePrompt },
                onTabClick = {},
                onTabCloseClick = {},
                onCFRShown = {},
                onCFRClick = {},
                onCFRDismiss = {},
            )
        }
    }
}

private class InactiveTabsListPreviewParameterProvider : PreviewParameterProvider<Boolean> {
    override val values: Sequence<Boolean> = sequenceOf(true, false)
}
