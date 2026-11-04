/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.layout.wrapContentWidth
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import mozilla.components.compose.base.LinkText
import mozilla.components.compose.base.LinkTextState
import mozilla.components.compose.base.Switch
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import org.mozilla.fenix.R
import org.mozilla.fenix.onboarding.store.PrivacyPreferencesAction
import org.mozilla.fenix.onboarding.store.PrivacyPreferencesStore
import org.mozilla.fenix.onboarding.view.hapticOnCheckedChange
import org.mozilla.fenix.onboarding.view.hapticOnClick
import org.mozilla.fenix.onboarding.view.hapticOnLinkClick
import org.mozilla.fenix.theme.FirefoxTheme

/** Dialog to manage privacy preferences during onboarding. */
@Composable
fun ManagePrivacyPreferencesDialog(
    store: PrivacyPreferencesStore,
    onDismissRequest: () -> Unit,
    onCrashReportingLinkClick: () -> Unit,
    onUsageDataLinkClick: () -> Unit,
) {
    val state by store.stateFlow.collectAsState()

    Dialog(
        onDismissRequest = { onDismissRequest() },
        properties = DialogProperties(dismissOnClickOutside = false),
    ) {
        Card(
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerHigh),
            shape = MaterialTheme.shapes.small,
        ) {
            Column(Modifier.padding(16.dp).verticalScroll(rememberScrollState())) {
                Title()

                Spacer(modifier = Modifier.height(16.dp))

                UsageDataPreference(store, state.usageDataEnabled, onUsageDataLinkClick)

                Spacer(modifier = Modifier.height(24.dp))

                CrashReportingPreference(
                    store,
                    state.crashReportingEnabled,
                    onCrashReportingLinkClick,
                )

                Spacer(modifier = Modifier.height(16.dp))

                PositiveButton(onDismissRequest)
            }
        }
    }
}

@Composable
private fun Title() {
    Text(
        text = stringResource(R.string.onboarding_preferences_dialog_title),
        style = FirefoxTheme.typography.headline7,
    )
}

@Composable
private fun CrashReportingPreference(
    store: PrivacyPreferencesStore,
    crashReportingEnabled: Boolean,
    onLinkClick: () -> Unit,
) {
    SwitchWithLabel(
        label = stringResource(R.string.onboarding_preferences_dialog_crash_reporting_title),
        checked = crashReportingEnabled,
        onCheckedChange = {
            store.dispatch(PrivacyPreferencesAction.CrashReportingPreferenceUpdatedTo(it))
        },
        modifier = Modifier.wrapContentWidth(),
        labelStyle = FirefoxTheme.typography.body2,
    )

    Spacer(modifier = Modifier.height(8.dp))

    Text(
        text = stringResource(R.string.onboarding_preferences_dialog_crash_reporting_description),
        style = FirefoxTheme.typography.caption,
    )

    Spacer(modifier = Modifier.height(16.dp))

    LinkText(
        text = stringResource(R.string.onboarding_preferences_dialog_crash_reporting_learn_more_2),
        linkTextStates =
            listOf(
                LinkTextState(
                    text = stringResource(id = R.string.onboarding_preferences_dialog_crash_reporting_learn_more_2),
                    url = "",
                    onClick = hapticOnLinkClick { onLinkClick() },
                )
            ),
        style = FirefoxTheme.typography.caption,
    )
}

@Composable
private fun UsageDataPreference(
    store: PrivacyPreferencesStore,
    usageDataEnabled: Boolean,
    onLinkClick: () -> Unit,
) {
    SwitchWithLabel(
        label = stringResource(R.string.onboarding_preferences_dialog_usage_data_title),
        checked = usageDataEnabled,
        onCheckedChange = {
            store.dispatch(PrivacyPreferencesAction.UsageDataPreferenceUpdatedTo(it))
        },
        modifier = Modifier.wrapContentWidth(),
        labelStyle = FirefoxTheme.typography.body2,
    )

    Spacer(modifier = Modifier.height(8.dp))

    Text(
        text = stringResource(R.string.onboarding_preferences_dialog_usage_data_description_2),
        style = FirefoxTheme.typography.caption,
    )

    Spacer(modifier = Modifier.height(16.dp))

    LinkText(
        text = stringResource(R.string.onboarding_preferences_dialog_usage_data_learn_more_2),
        linkTextStates =
            listOf(
                LinkTextState(
                    text = stringResource(id = R.string.onboarding_preferences_dialog_usage_data_learn_more_2),
                    url = "",
                    onClick = hapticOnLinkClick { onLinkClick() },
                )
            ),
        style = FirefoxTheme.typography.caption,
    )
}

@Composable
private fun PositiveButton(onDismissRequest: () -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth().wrapContentHeight(),
        horizontalArrangement = Arrangement.End,
    ) {
        TextButton(
            text = stringResource(R.string.onboarding_preferences_dialog_positive_button),
            onClick = hapticOnClick { onDismissRequest() },
        )
    }
}

@Composable
private fun SwitchWithLabel(
    label: String,
    checked: Boolean,
    modifier: Modifier = Modifier,
    description: String? = null,
    enabled: Boolean = true,
    labelStyle: TextStyle = FirefoxTheme.typography.body1,
    onCheckedChange: ((Boolean) -> Unit),
) {
    val onCheckedChangeWithHaptic = hapticOnCheckedChange(onCheckedChange)

    Row(
        modifier =
            Modifier.toggleable(
                    value = checked,
                    enabled = enabled,
                    role = Role.Switch,
                    onValueChange = onCheckedChangeWithHaptic,
                )
                .then(modifier),
        horizontalArrangement = Arrangement.spacedBy(16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = label,
                modifier = Modifier.defaultMinSize(minHeight = 24.dp).wrapContentHeight(),
                color =
                    if (enabled) {
                        MaterialTheme.colorScheme.onSurface
                    } else {
                        MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                    },
                style = labelStyle,
            )

            description?.let {
                Text(
                    text = description,
                    modifier = Modifier.defaultMinSize(minHeight = 20.dp).wrapContentHeight(),
                    color =
                        if (enabled) {
                            MaterialTheme.colorScheme.onSurfaceVariant
                        } else {
                            MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                        },
                    style = FirefoxTheme.typography.body2,
                )
            }
        }

        Switch(
            modifier = Modifier.clearAndSetSemantics {},
            checked = checked,
            onCheckedChange = onCheckedChangeWithHaptic,
            enabled = enabled,
        )
    }
}

@FlexibleWindowPreview
@Composable
private fun ManagePrivacyPreferencesDialogPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        ManagePrivacyPreferencesDialog(PrivacyPreferencesStore(), {}, {}, {})
    }
}
