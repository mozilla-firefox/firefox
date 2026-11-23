/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.labs.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.layout.wrapContentWidth
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.flow.map
import mozilla.components.compose.base.LinkText
import mozilla.components.compose.base.LinkTextState
import mozilla.components.compose.base.PromoCard
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.modifier.thenConditional
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.compose.base.theme.ThemedValue
import mozilla.components.compose.base.theme.ThemedValueProvider
import mozilla.components.compose.base.theme.layout.AcornWindowSize
import mozilla.components.compose.base.utils.BackInvokedHandler
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.list.SwitchListItem
import org.mozilla.fenix.settings.labs.LabsItem
import org.mozilla.fenix.settings.labs.store.DialogState
import org.mozilla.fenix.settings.labs.store.LabsAction
import org.mozilla.fenix.settings.labs.store.LabsState
import org.mozilla.fenix.settings.labs.store.LabsStore
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Firefox Labs screen that displays a list of experimental items that can be opted into.
 *
 * @param store The [LabsStore] used to observe the screen state and dispatch actions.
 * @param onNavigationIconClick Callback invoked when the navigation icon is clicked.
 * @param onShareFeedbackClick Callback invoked when an item's "Share feedback" link is clicked, with the [LabsItem]
 *   whose link was tapped as the argument.
 */
@Composable
fun FirefoxLabsScreen(
    store: LabsStore,
    onNavigationIconClick: () -> Unit,
    onShareFeedbackClick: (LabsItem) -> Unit,
) {
    val labsItems by remember {
        store.stateFlow.map { state -> state.labsItems }
    }
        .collectAsState(initial = store.state.labsItems)

    BackInvokedHandler {
        onNavigationIconClick()
    }

    Scaffold(
        topBar = {
            FirefoxLabsTopAppBar(onNavigationIconClick = onNavigationIconClick)
        }
    ) { paddingValues ->
        if (labsItems.isEmpty()) {
            EmptyState(modifier = Modifier.padding(paddingValues))
        } else {
            FirefoxLabsScreenContent(
                labsItems = labsItems,
                paddingValues = paddingValues,
                onToggleLabsItem = { item ->
                    if (item.requiresRestart) {
                        store.dispatch(LabsAction.ShowToggleLabsItemDialog(item))
                    } else {
                        store.dispatch(LabsAction.ToggleLabsItem(item))
                    }
                },
                onRestoreDefaultsButtonClick = {
                    if (labsItems.any { it.enrolled && it.requiresRestart }) {
                        store.dispatch(LabsAction.ShowRestoreDefaultsDialog)
                    } else {
                        store.dispatch(LabsAction.RestoreDefaults)
                    }
                },
                onShareFeedbackClick = onShareFeedbackClick,
            )
        }
    }

    FirefoxLabsDialog(store = store)
}

@Composable
private fun FirefoxLabsScreenContent(
    labsItems: List<LabsItem>,
    paddingValues: PaddingValues,
    onToggleLabsItem: (LabsItem) -> Unit,
    onRestoreDefaultsButtonClick: () -> Unit,
    onShareFeedbackClick: (LabsItem) -> Unit,
) {
    LazyColumn(modifier = Modifier.padding(paddingValues).fillMaxSize()) {
        item {
            FirefoxLabsBanner()
        }

        items(labsItems) { labsItem ->
            LabsItemRow(
                item = labsItem,
                onToggle = onToggleLabsItem,
                onShareFeedbackClick = onShareFeedbackClick,
            )
        }

        item {
            val isWideScreen = AcornWindowSize.getWindowSize().isNotSmall()
            FilledButton(
                text = stringResource(R.string.firefox_labs_restore_default_button_text),
                modifier =
                    Modifier.fillMaxWidth()
                        .thenConditional(
                            modifier = Modifier.wrapContentWidth(Alignment.CenterHorizontally),
                            predicate = { isWideScreen },
                        )
                        .padding(horizontal = 16.dp, vertical = 24.dp),
                enabled = labsItems.any { it.enrolled },
                onClick = onRestoreDefaultsButtonClick,
            )
        }
    }
}

@Composable
private fun LabsItemRow(
    item: LabsItem,
    onToggle: (LabsItem) -> Unit,
    onShareFeedbackClick: (LabsItem) -> Unit,
) {
    val itemTitle = item.title
    SwitchListItem(
        label = itemTitle,
        checked = item.enrolled,
        description = item.description,
        maxDescriptionLines = Int.MAX_VALUE,
        enabled = item.available,
        showSwitchAfter = true,
        belowListItemContent = {
            if (!item.available) {
                Text(
                    text = stringResource(R.string.firefox_labs_feature_conflict),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = FirefoxTheme.typography.body2,
                )
            }

            item.feedbackUrl?.let { url ->
                LabsShareFeedbackLink(
                    item = item,
                    itemTitle = itemTitle,
                    url = url,
                    onShareFeedbackClick = onShareFeedbackClick,
                )
            }
        },
        onClick = { onToggle(item) },
    )
}

@Composable
private fun LabsShareFeedbackLink(
    item: LabsItem,
    itemTitle: String,
    url: String,
    onShareFeedbackClick: (LabsItem) -> Unit,
) {
    val shareFeedbackText = stringResource(R.string.firefox_labs_share_feedback)
    val shareFeedbackContentDescription =
        stringResource(
            R.string.firefox_labs_share_feedback_content_description,
            itemTitle,
        )
    LinkText(
        text = shareFeedbackText,
        linkTextStates =
            listOf(
                LinkTextState(
                    text = shareFeedbackText,
                    url = url,
                    onClick = { _ -> onShareFeedbackClick(item) },
                )
            ),
        linkTextDecoration = TextDecoration.Underline,
        contentDescription = shareFeedbackContentDescription,
    )
}

@Composable
private fun FirefoxLabsBanner() {
    PromoCard(
        modifier =
            Modifier.padding(
                    horizontal = FirefoxTheme.layout.space.dynamic200,
                    vertical = FirefoxTheme.layout.space.static100,
                )
                .height(IntrinsicSize.Min),
        title = { Text(text = stringResource(R.string.firefox_labs_banner_title_2)) },
        message = {
            Text(
                text =
                    String.format(
                        stringResource(R.string.firefox_labs_experimental_description),
                        stringResource(R.string.app_name),
                    )
            )
        },
        illustration = {
            Image(
                modifier = Modifier.fillMaxHeight().padding(top = FirefoxTheme.layout.space.static150),
                painter = painterResource(R.drawable.kit_expressive_full),
                contentDescription = null,
                contentScale = ContentScale.FillHeight,
            )
        },
    )
}

@Composable
private fun FirefoxLabsTopAppBar(onNavigationIconClick: () -> Unit) {
    TopAppBar(
        title = {
            Text(
                text = stringResource(R.string.firefox_labs_title),
                style = FirefoxTheme.typography.headline5,
            )
        },
        navigationIcon = {
            IconButton(
                onClick = onNavigationIconClick,
                contentDescription = null,
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                    contentDescription = null,
                )
            }
        },
        windowInsets =
            WindowInsets(
                top = 0.dp,
                bottom = 0.dp,
            ),
    )
}

@Composable
private fun EmptyState(modifier: Modifier = Modifier) {
    val isWideScreen = AcornWindowSize.getWindowSize().isNotSmall()
    Column(
        modifier =
            modifier
                .fillMaxSize()
                .wrapContentSize()
                .thenConditional(
                    modifier = Modifier.width(IntrinsicSize.Min),
                    predicate = { !isWideScreen },
                ),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Image(
            modifier = Modifier.width(180.dp).height(103.dp),
            painter = painterResource(R.drawable.kit_sleeping_under_laptop),
            contentDescription = null,
        )

        Spacer(modifier = Modifier.height(20.dp))

        Text(
            text = stringResource(id = R.string.firefox_labs_no_labs_available_description),
            color = MaterialTheme.colorScheme.onSurface,
            style = FirefoxTheme.typography.headline6,
            textAlign = TextAlign.Center,
        )
    }
}

@Composable
private fun FirefoxLabsDialog(store: LabsStore) {
    val dialogState by remember {
        store.stateFlow.map { state -> state.dialogState }
    }
        .collectAsState(initial = store.state.dialogState)

    when (val currentDialog = dialogState) {
        is DialogState.ToggleLabsItem -> {
            ToggleLabsItemDialog(
                itemEnrolled = currentDialog.item.enrolled,
                onConfirm = {
                    store.dispatch(LabsAction.ToggleLabsItem(item = currentDialog.item))
                },
                onDismiss = {
                    store.dispatch(LabsAction.CloseDialog)
                },
            )
        }

        is DialogState.RestoreDefaults -> {
            RestoreDefaultsDialog(
                onConfirm = {
                    store.dispatch(LabsAction.RestoreDefaults)
                },
                onDismiss = {
                    store.dispatch(LabsAction.CloseDialog)
                },
            )
        }

        DialogState.Closed -> {}
    }
}

@Composable
private fun ToggleLabsItemDialog(
    itemEnrolled: Boolean,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = {
            TextButton(
                text = stringResource(R.string.firefox_labs_dialog_restart_button),
                onClick = onConfirm,
            )
        },
        dismissButton = {
            TextButton(
                text = stringResource(R.string.firefox_labs_dialog_cancel_button),
                onClick = onDismiss,
            )
        },
        title = {
            Text(
                text = stringResource(R.string.firefox_labs_feature_dialog_title),
                style = FirefoxTheme.typography.headline5,
            )
        },
        text = {
            Text(
                text =
                    if (itemEnrolled) {
                        String.format(
                            stringResource(R.string.firefox_labs_feature_disable_dialog_message),
                            stringResource(R.string.app_name),
                        )
                    } else {
                        String.format(
                            stringResource(R.string.firefox_labs_feature_enable_dialog_message),
                            stringResource(R.string.app_name),
                        )
                    },
                style = FirefoxTheme.typography.body2,
            )
        },
    )
}

@Composable
private fun RestoreDefaultsDialog(
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = {
            TextButton(
                text = stringResource(R.string.firefox_labs_dialog_restart_button),
                onClick = onConfirm,
            )
        },
        dismissButton = {
            TextButton(
                text = stringResource(R.string.firefox_labs_dialog_cancel_button),
                onClick = onDismiss,
            )
        },
        title = {
            Text(
                text = stringResource(R.string.firefox_labs_restore_defaults_dialog_title),
                style = FirefoxTheme.typography.headline5,
            )
        },
        text = {
            Text(
                text =
                    String.format(
                        stringResource(R.string.firefox_labs_restore_defaults_dialog_message),
                        stringResource(R.string.app_name),
                    ),
                style = FirefoxTheme.typography.body2,
            )
        },
    )
}

private class FirefoxLabsScreenPreviewProvider :
    ThemedValueProvider<List<LabsItem>>(
        sequenceOf(
            listOf(
                LabsItem(
                    slug = "preview-lab-one",
                    title = "Preview lab one",
                    description = "Sample Labs item one for previews.",
                    enrolled = true,
                    requiresRestart = true,
                ),
                LabsItem(
                    slug = "preview-lab-two",
                    title = "Preview lab two",
                    description = "Sample Labs item two for previews.",
                    enrolled = false,
                    feedbackUrl = "https://connect.mozilla.org/",
                    requiresRestart = true,
                ),
                LabsItem(
                    slug = "preview-lab-three",
                    title = "Preview lab three",
                    description = "Sample Labs item three for previews.",
                    enrolled = false,
                    requiresRestart = true,
                    available = false,
                ),
            ),
            emptyList(),
        )
    )

@Composable
@FlexibleWindowLightDarkPreview
private fun FirefoxLabsScreenPreview(
    @PreviewParameter(FirefoxLabsScreenPreviewProvider::class) state: ThemedValue<List<LabsItem>>
) {
    FirefoxTheme(state.theme) {
        FirefoxLabsScreen(
            store =
                LabsStore(
                    initialState =
                        LabsState(
                            labsItems = state.value,
                            dialogState = DialogState.Closed,
                        )
                ),
            onNavigationIconClick = {},
            onShareFeedbackClick = {},
        )
    }
}

@Preview
@Composable
private fun LabsItemRowUnavailablePreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        LabsItemRow(
            item =
                LabsItem(
                    slug = "preview-lab-unavailable",
                    title = "Preview lab",
                    description = "Sample deactivated Labs item for previews.",
                    enrolled = false,
                    requiresRestart = true,
                    feedbackUrl = "https://connect.mozilla.org/",
                    available = false,
                ),
            onToggle = {},
            onShareFeedbackClick = {},
        )
    }
}

@Preview
@Composable
private fun ToggleLabsItemDialogPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        ToggleLabsItemDialog(
            itemEnrolled = true,
            onConfirm = {},
            onDismiss = {},
        )
    }
}

@Preview
@Composable
private fun EmptyStatePreview() {
    FirefoxTheme {
        EmptyState()
    }
}

@Preview
@Composable
private fun RestoreDefaultsDialogPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        RestoreDefaultsDialog(
            onConfirm = {},
            onDismiss = {},
        )
    }
}
