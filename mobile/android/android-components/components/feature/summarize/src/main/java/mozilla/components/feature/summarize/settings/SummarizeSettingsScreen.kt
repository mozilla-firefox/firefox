/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LocalMinimumInteractiveComponentSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.feature.summarize.R
import mozilla.components.ui.icons.R as iconsR

/**
 * Reusable composable that renders the summarize settings UI.
 *
 * @param store The [SummarizeSettingsStore] providing state and accepting actions.
 */
@Composable
fun SummarizeSettingsContent(
    store: SummarizeSettingsStore,
    modifier: Modifier = Modifier,
) {
    val state by store.stateFlow.collectAsStateWithLifecycle()

    SummarizeSettingsContent(
        modifier = modifier,
        state = state,
        onSummarizePagesToggled = { store.dispatch(SummarizePagesPreferenceToggled) },
        onShakeToSummarizeToggled = { store.dispatch(ShakeToSummarizePreferenceToggled) },
        onLearnMoreClicked = { store.dispatch(LearnMoreClicked) },
    )
}

/**
 * Reusable composable that renders the summarize settings UI.
 *
 * @param state The current [SummarizeSettingsState].
 * @param onSummarizePagesToggled Called when the user toggles the summarize pages setting.
 * @param onShakeToSummarizeToggled Called when the user toggles the shake to summarize setting.
 * @param onLearnMoreClicked Called when the user clicks the learn more link.
 */
@Composable
fun SummarizeSettingsContent(
    state: SummarizeSettingsState,
    onSummarizePagesToggled: () -> Unit,
    onShakeToSummarizeToggled: () -> Unit,
    onLearnMoreClicked: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier.fillMaxWidth(),
    ) {
        SwitchRow(
            label = stringResource(id = R.string.mozac_summarize_settings_summarize_pages),
            description = stringResource(
                id = R.string.mozac_summarize_settings_summarize_pages_cloud,
            ),
            checked = state.summarizePagesEnabled,
            onToggle = onSummarizePagesToggled,
        )

        Text(
            text = stringResource(id = R.string.mozac_summarize_settings_learn_more),
            style = MaterialTheme.typography.bodyMedium.copy(
                color = MaterialTheme.colorScheme.tertiary,
                textDecoration = TextDecoration.Underline,
            ),
            modifier = Modifier
                .clickable(onClick = onLearnMoreClicked),
        )

        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static300))

        Text(
            text = stringResource(id = R.string.mozac_summarize_settings_gestures),
            style = MaterialTheme.typography.titleSmall.copy(
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            ),
            modifier = Modifier.padding(vertical = AcornTheme.layout.space.static100),
        )

        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static100))

        SwitchRow(
            label = stringResource(id = R.string.mozac_summarize_settings_shake_to_summarize),
            description = stringResource(
                id = R.string.mozac_summarize_settings_shake_to_summarize_description,
            ),
            checked = state.shakeToSummarizeEnabled,
            enabled = state.summarizePagesEnabled,
            onToggle = onShakeToSummarizeToggled,
        )
    }
}

@Composable
private fun SwitchRow(
    label: String,
    description: String,
    checked: Boolean,
    enabled: Boolean = true,
    onToggle: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(enabled = enabled, onClick = onToggle)
            .padding(vertical = AcornTheme.layout.space.static150),
        verticalAlignment = Alignment.Top,
    ) {
        Column(
            modifier = Modifier.weight(1f),
        ) {
            Text(
                text = label,
                style = MaterialTheme.typography.bodyLarge.copy(
                    color = if (enabled) {
                        MaterialTheme.colorScheme.onSurface
                    } else {
                        MaterialTheme.colorScheme.onSurface.copy(alpha = DISABLED_ALPHA)
                    },
                ),
            )
            Text(
                text = description,
                style = MaterialTheme.typography.bodyMedium.copy(
                    color = if (enabled) {
                        MaterialTheme.colorScheme.onSurfaceVariant
                    } else {
                        MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = DISABLED_ALPHA)
                    },
                ),
            )
        }

        CompositionLocalProvider(
            LocalMinimumInteractiveComponentSize provides 0.dp,
        ) {
            Switch(
                checked = checked,
                onCheckedChange = { onToggle() },
                enabled = enabled,
                modifier = Modifier.padding(start = AcornTheme.layout.space.static200),
            )
        }
    }
}

@Composable
internal fun SettingsAppBar(
    onBackClicked: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(64.dp)
            .background(MaterialTheme.colorScheme.surface),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        IconButton(onClick = onBackClicked) {
            Icon(
                painter = painterResource(id = iconsR.drawable.mozac_ic_back_24),
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurface,
            )
        }

        Text(
            text = stringResource(id = R.string.mozac_summarize_settings_title),
            style = MaterialTheme.typography.headlineSmall,
            color = MaterialTheme.colorScheme.onSurface,
        )
    }
}

private const val DISABLED_ALPHA = 0.38f
