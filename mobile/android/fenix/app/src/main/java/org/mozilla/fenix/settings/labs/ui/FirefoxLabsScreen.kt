/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.labs.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
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
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.flow.map
import mozilla.components.compose.base.PromoCard
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.utils.BackInvokedHandler
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.list.SwitchListItem
import org.mozilla.fenix.settings.labs.FeatureKey
import org.mozilla.fenix.settings.labs.LabsFeature
import org.mozilla.fenix.settings.labs.store.DialogState
import org.mozilla.fenix.settings.labs.store.LabsAction
import org.mozilla.fenix.settings.labs.store.LabsState
import org.mozilla.fenix.settings.labs.store.LabsStore
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme
import org.mozilla.fenix.theme.ThemedValue
import org.mozilla.fenix.theme.ThemedValueProvider
import mozilla.components.ui.icons.R as iconsR

/**
 * Firefox Labs screen that displays a list of experimental features that can be opted into.
 *
 * @param store The [LabsStore] used to observe the screen state and dispatch actions.
 * @param onNavigationIconClick Callback invoked when the navigation icon is clicked.
 */
@Composable
fun FirefoxLabsScreen(
    store: LabsStore,
    onNavigationIconClick: () -> Unit,
) {
    val labsFeatures by remember { store.stateFlow.map { state -> state.labsFeatures } }
        .collectAsState(initial = store.state.labsFeatures)

    BackInvokedHandler {
        onNavigationIconClick()
    }

    Scaffold(
        topBar = {
            FirefoxLabsTopAppBar(
                onNavigationIconClick = onNavigationIconClick,
            )
        },
    ) { paddingValues ->
        if (labsFeatures.isEmpty()) {
            EmptyState(modifier = Modifier.padding(paddingValues))
        } else {
            FirefoxLabsScreenContent(
                labsFeatures = labsFeatures,
                paddingValues = paddingValues,
                onToggleFeature = { feature -> store.dispatch(LabsAction.ShowToggleFeatureDialog(feature)) },
                onRestoreDefaultsButtonClick = { store.dispatch(LabsAction.ShowRestoreDefaultsDialog) },
            )
        }
    }

    FirefoxLabsDialog(store = store)
}

@Composable
private fun FirefoxLabsScreenContent(
    labsFeatures: List<LabsFeature>,
    paddingValues: PaddingValues,
    onToggleFeature: (LabsFeature) -> Unit,
    onRestoreDefaultsButtonClick: () -> Unit,
) {
    LazyColumn(
        modifier = Modifier
            .padding(paddingValues)
            .fillMaxSize(),
    ) {
        item {
            FirefoxLabsBanner()
        }

        items(labsFeatures) { feature ->
            SwitchListItem(
                label = stringResource(id = feature.name),
                checked = feature.enabled,
                description = stringResource(id = feature.description),
                maxDescriptionLines = Int.MAX_VALUE,
                showSwitchAfter = true,
                onClick = { onToggleFeature(feature) },
            )
        }

        item {
            FilledButton(
                text = stringResource(R.string.firefox_labs_restore_default_button_text),
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 24.dp),
                onClick = onRestoreDefaultsButtonClick,
            )
        }
    }
}

@Composable
private fun FirefoxLabsBanner() {
    PromoCard(
        modifier = Modifier.padding(
            start = 16.dp,
            end = 16.dp,
            top = 8.dp,
            bottom = 16.dp,
        ),
        title = { Text(text = stringResource(R.string.firefox_labs_banner_title)) },
        message = {
            Text(
                text = String.format(
                    stringResource(R.string.firefox_labs_experimental_description),
                    stringResource(R.string.app_name),
                ),
            )
        },
        illustration = {
            Image(
                painter = painterResource(iconsR.drawable.mozac_ic_fox_ai_on_state),
                contentDescription = null,
            )
        },
    )
}

@OptIn(ExperimentalMaterial3Api::class)
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
        windowInsets = WindowInsets(
            top = 0.dp,
            bottom = 0.dp,
        ),
    )
}

@Composable
private fun EmptyState(modifier: Modifier = Modifier) {
    Column(
        modifier = modifier.fillMaxSize(),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Image(
            painter = painterResource(R.drawable.ic_onboarding_marketing_redesign),
            contentDescription = null,
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

        Text(
            text = stringResource(id = R.string.firefox_labs_no_labs_available_description),
            color = MaterialTheme.colorScheme.onSurface,
            style = FirefoxTheme.typography.headline6,
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static600))
    }
}

@Composable
private fun FirefoxLabsDialog(store: LabsStore) {
    val dialogState by remember { store.stateFlow.map { state -> state.dialogState } }
        .collectAsState(initial = store.state.dialogState)

    when (val currentDialog = dialogState) {
        is DialogState.ToggleFeature -> {
            ToggleFeatureDialog(
                featureEnabled = currentDialog.feature.enabled,
                onConfirm = {
                    store.dispatch(LabsAction.ToggleFeature(feature = currentDialog.feature))
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
private fun ToggleFeatureDialog(
    featureEnabled: Boolean,
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
                text = if (featureEnabled) {
                    stringResource(R.string.firefox_labs_disable_feature_dialog_title)
                } else {
                    stringResource(R.string.firefox_labs_enable_feature_dialog_title)
                },
                style = FirefoxTheme.typography.headline5,
            )
        },
        text = {
            Text(
                text = String.format(
                    stringResource(R.string.firefox_labs_enable_feature_dialog_message),
                    stringResource(R.string.app_name),
                ),
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
                text = String.format(
                    stringResource(R.string.firefox_labs_restore_defaults_dialog_message),
                    stringResource(R.string.app_name),
                ),
                style = FirefoxTheme.typography.body2,
            )
        },
    )
}

private class FirefoxLabsScreenPreviewProvider : ThemedValueProvider<List<LabsFeature>>(
    sequenceOf(
        listOf(
            LabsFeature(
                key = FeatureKey.HOMEPAGE_AS_A_NEW_TAB,
                name = R.string.firefox_labs_homepage_as_a_new_tab,
                description = R.string.firefox_labs_homepage_as_a_new_tab_description,
                enabled = true,
            ),
        ),
        emptyList(),
    ),
)

@Composable
@FlexibleWindowLightDarkPreview
private fun FirefoxLabsScreenPreview(
    @PreviewParameter(FirefoxLabsScreenPreviewProvider::class) state: ThemedValue<List<LabsFeature>>,
) {
    FirefoxTheme(state.theme) {
        FirefoxLabsScreen(
            store = LabsStore(
                initialState = LabsState(
                    labsFeatures = state.value,
                    dialogState = DialogState.Closed,
                ),
            ),
            onNavigationIconClick = {},
        )
    }
}

@Preview
@Composable
private fun ToggleFeatureDialogPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        ToggleFeatureDialog(
            featureEnabled = true,
            onConfirm = {},
            onDismiss = {},
        )
    }
}

@Preview
@Composable
private fun RestoreDefaultsDialogPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        RestoreDefaultsDialog(
            onConfirm = {},
            onDismiss = {},
        )
    }
}
