/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ai

import android.content.Context
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.InfoCard
import mozilla.components.compose.base.InfoType
import mozilla.components.compose.base.LinkText
import mozilla.components.compose.base.LinkTextState
import mozilla.components.compose.base.PromoCard
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.button.TextButton
import mozilla.components.concept.ai.controls.AIControllableFeature
import mozilla.components.concept.ai.controls.AIFeatureMetadata
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.list.IconListItem
import org.mozilla.fenix.compose.list.SwitchListItem
import org.mozilla.fenix.compose.settings.SettingsSectionHeader
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.settings.settingssearch.PreferenceFileInformation
import org.mozilla.fenix.settings.settingssearch.SettingsSearchItem
import org.mozilla.fenix.settings.settingssearch.SettingsSearchProvider
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme
import mozilla.components.ui.icons.R as iconsR

private const val HEADER_ITEM_COUNT = 2

/**
 * UI state for [AIControlsScreen].
 */
internal data class AIControlsScreenState(
    val featureEnabledState: Map<AIFeatureMetadata.FeatureId, Boolean> = mapOf(),
    val registeredFeatures: List<AIControllableFeature> = emptyList(),
    val showDialog: Boolean = false,
    val isBlocked: Boolean = false,
    val itemToScrollTo: String? = null,
)

/**
 * Callback bundle for [AIControlsScreen].
 */
internal data class AIControlsCallbacks(
    val onDialogDismiss: () -> Unit = {},
    val onDialogConfirm: () -> Unit = {},
    val onToggle: (Boolean) -> Unit = {},
    val onFeatureToggle: (AIControllableFeature, Boolean) -> Unit = { _, _ -> },
    val onFeatureNavLinkClick: (AIFeatureMetadataDestination, String) -> Unit = { _, _ -> },
    val onBannerLearnMoreClick: () -> Unit = {},
)

@Composable
internal fun AIControlsScreen(
    state: AIControlsScreenState,
    callbacks: AIControlsCallbacks,
) {
    Surface {
        if (state.showDialog) {
            BlockAIDialog(
                registeredFeatures = state.registeredFeatures,
                onDismiss = { callbacks.onDialogDismiss() },
                onConfirm = { callbacks.onDialogConfirm() },
            )
        }

        val lazyListState = rememberLazyListState()

        ScrollToItemEffect(state.itemToScrollTo, state.registeredFeatures, lazyListState)

        AIControlsList(
            state = state,
            lazyListState = lazyListState,
            callbacks = callbacks,
        )
    }
}

@Composable
private fun ScrollToItemEffect(
    itemToScrollTo: String?,
    registeredFeatures: List<AIControllableFeature>,
    lazyListState: LazyListState,
) {
    // Guards against re-scrolling when the header re-composes (e.g. `isBlocked` toggles
    // add/remove the blocked-info banner) after the initial navigation-driven scroll.
    var hasScrolled by rememberSaveable { mutableStateOf(false) }
    LaunchedEffect(itemToScrollTo, registeredFeatures) {
        if (hasScrolled || itemToScrollTo.isNullOrBlank()) return@LaunchedEffect
        val featureIndex = registeredFeatures.indexOfFirst { it.id.value == itemToScrollTo }
        if (featureIndex != -1) {
            lazyListState.animateScrollToItem(featureIndex + HEADER_ITEM_COUNT)
            hasScrolled = true
        }
    }
}

@Composable
private fun AIControlsList(
    state: AIControlsScreenState,
    lazyListState: LazyListState,
    callbacks: AIControlsCallbacks,
) {
    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        state = lazyListState,
    ) {
        item {
            AIControlsHeader(
                isBlocked = state.isBlocked,
                onToggle = callbacks.onToggle,
                onBannerLearnMoreClick = callbacks.onBannerLearnMoreClick,
            )
        }

        item {
            AIFeaturesHeader()
        }

        items(
            items = state.registeredFeatures,
            key = { it.id.value },
        ) { feature ->
            FeatureRow(
                featureEnabledState = state.featureEnabledState,
                feature = feature,
                onFeatureToggle = callbacks.onFeatureToggle,
                onFeatureNavLinkClick = callbacks.onFeatureNavLinkClick,
            )
        }
    }
}

@Composable
private fun AIControlsHeader(
    isBlocked: Boolean,
    onToggle: (Boolean) -> Unit,
    onBannerLearnMoreClick: () -> Unit,
) {
    Column {
        AIChoiceBanner(onLearnMoreClick = onBannerLearnMoreClick)
        BlockAIEnhancementsToggle(isBlocked = isBlocked, onToggle = onToggle)
        NavLink(
            text = stringResource(R.string.ai_controls_see_whats_included),
            onClick = onBannerLearnMoreClick,
        )
        if (isBlocked) {
            BlockedInfoCard(
                modifier = Modifier.padding(start = 16.dp, top = 8.dp, end = 16.dp, bottom = 16.dp),
            )
        }
    }
}

@Composable
private fun BlockAIEnhancementsToggle(
    isBlocked: Boolean,
    onToggle: (Boolean) -> Unit,
) {
    SwitchListItem(
        label = stringResource(R.string.ai_controls_block_ai_title),
        checked = isBlocked,
        description = stringResource(
            R.string.ai_controls_block_ai_description,
            stringResource(R.string.app_name),
        ),
        maxDescriptionLines = Int.MAX_VALUE,
        showSwitchAfter = true,
        onClick = { onToggle(isBlocked) },
    )
}

@Composable
private fun AIFeaturesHeader() {
    Column {
        HorizontalDivider()
        SettingsSectionHeader(
            text = stringResource(R.string.ai_controls_ai_powered_features),
            modifier = Modifier.padding(
                horizontal = FirefoxTheme.layout.space.dynamic200,
                vertical = 8.dp,
            ),
        )
    }
}

@Composable
private fun FeatureRow(
    featureEnabledState: Map<AIFeatureMetadata.FeatureId, Boolean>,
    feature: AIControllableFeature,
    onFeatureToggle: (AIControllableFeature, Boolean) -> Unit,
    onFeatureNavLinkClick: (AIFeatureMetadataDestination, String) -> Unit,
) {
    val isEnabled = featureEnabledState[feature.id] ?: false

    Column {
        SwitchListItem(
            label = stringResource(feature.description.titleRes),
            checked = isEnabled,
            enabled = true,
            description = stringResource(feature.description.descriptionRes),
            maxDescriptionLines = Int.MAX_VALUE,
            showSwitchAfter = true,
            onClick = { onFeatureToggle(feature, !isEnabled) },
        )

        feature.destination?.let {
            NavLink(
                text = stringResource(it.label),
                onClick = { onFeatureNavLinkClick(it, feature.id.value) },
            )
        }
    }
}

@Composable
private fun AIChoiceBanner(onLearnMoreClick: () -> Unit) {
    val learnMoreText = stringResource(R.string.ai_controls_learn_more)
    val description = stringResource(R.string.ai_controls_banner_supporting_text_2, learnMoreText)

    PromoCard(
        description = null,
        modifier = Modifier.padding(
            start = 16.dp,
            end = 16.dp,
            top = 8.dp,
            bottom = 16.dp,
        ),
        title = stringResource(R.string.ai_controls_banner_headline, stringResource(R.string.app_name)),
        footer = description to LinkTextState(
            text = learnMoreText,
            url = "",
            onClick = { onLearnMoreClick() },
        ),
        illustration = {
            Image(
                painter = painterResource(iconsR.drawable.mozac_ic_fox_ai_on_state),
                contentDescription = null,
                modifier = Modifier
                    .width(62.dp)
                    .height(63.dp),
            )
        },
    )
}

@Composable
private fun BlockedInfoCard(
    modifier: Modifier = Modifier,
) {
    InfoCard(
        description = stringResource(R.string.ai_controls_blocked_info_banner),
        type = InfoType.Warning,
        modifier = modifier,
    )
}

@Composable
private fun BlockAIDialog(
    registeredFeatures: List<AIControllableFeature>,
    onDismiss: () -> Unit,
    onConfirm: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Text(
                text = stringResource(R.string.ai_controls_block_dialog_title),
                style = FirefoxTheme.typography.headline5,
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth(),
            )
        },
        text = {
            Column {
                val appName = stringResource(R.string.app_name)
                val bodyText = stringResource(R.string.ai_controls_block_dialog_body, appName, appName)
                val whatBlocked = stringResource(R.string.ai_controls_block_dialog_what_will_be_blocked)

                Text(
                    text = bodyText,
                    style = FirefoxTheme.typography.body2,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )

                Spacer(modifier = Modifier.height(16.dp))

                Text(
                    text = whatBlocked,
                    style = FirefoxTheme.typography.body2.copy(fontWeight = FontWeight.W700),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )

                Spacer(modifier = Modifier.height(16.dp))

                for (feature in registeredFeatures) {
                    IconListItem(
                        label = stringResource(feature.description.titleRes),
                        beforeIconPainter = painterResource(feature.description.iconRes),
                    )
                }
            }
        },
        dismissButton = {
            TextButton(
                text = stringResource(R.string.ai_controls_block_dialog_cancel),
                onClick = onDismiss,
            )
        },
        confirmButton = {
            TextButton(
                text = stringResource(R.string.ai_controls_block_dialog_block),
                onClick = onConfirm,
                colors = ButtonDefaults.textButtonColors(
                    contentColor = MaterialTheme.colorScheme.error,
                ),
            )
        },
    )
}

@Composable
private fun NavLink(
    text: String,
    onClick: () -> Unit,
) {
    Column(
        modifier = Modifier
            .clickable(onClick = onClick)
            .fillMaxWidth()
            .padding(
                start = FirefoxTheme.layout.space.dynamic200,
                end = FirefoxTheme.layout.space.dynamic200,
                top = 4.dp,
            )
            .height(48.dp),
    ) {
        LinkText(
            text = text,
            linkTextStates = listOf(
                LinkTextState(
                    text = text,
                    url = "",
                    onClick = { onClick() },
                ),
            ),
            linkTextDecoration = TextDecoration.Underline,
        )
    }
}

@FlexibleWindowPreview
@Composable
private fun AIControlsScreenPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        AIControlsScreen(
            state = AIControlsScreenState(),
            callbacks = AIControlsCallbacks(),
        )
    }
}

@Preview
@Composable
private fun BlockAIDialogPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        BlockAIDialog(
            registeredFeatures = emptyList(),
            onDismiss = {},
            onConfirm = {},
        )
    }
}

@Preview
@Composable
private fun BlockedInfoCardPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        BlockedInfoCard()
    }
}

/**
 * Provides [SettingsSearchItem]s for the AI Controls settings screen for use in settings search.
 */
object AIControlsSearchProvider : SettingsSearchProvider {
    private val preferenceFileInformation = PreferenceFileInformation.AIControlsPreferences

    /**
     * Preference key used to identify the top-level "Block AI enhancements" toggle when navigating
     * from a settings search result.
     */
    const val BLOCK_AI_ENHANCEMENTS_KEY = "BLOCK_AI_ENHANCEMENTS"

    override fun getSearchItems(context: Context): List<SettingsSearchItem> {
        val categoryHeader = context.getString(preferenceFileInformation.categoryHeaderResourceId)
        val appName = context.getString(R.string.app_name)

        return buildList {
            add(
                SettingsSearchItem(
                    title = context.getString(R.string.ai_controls_block_ai_title),
                    summary = context.getString(R.string.ai_controls_block_ai_description, appName),
                    preferenceKey = BLOCK_AI_ENHANCEMENTS_KEY,
                    categoryHeader = categoryHeader,
                    preferenceFileInformation = preferenceFileInformation,
                ),
            )

            for (feature in context.components.aiFeatureRegistry.getFeatures()) {
                add(
                    SettingsSearchItem(
                        title = context.getString(feature.description.titleRes),
                        summary = context.getString(feature.description.descriptionRes),
                        preferenceKey = feature.id.value,
                        categoryHeader = categoryHeader,
                        preferenceFileInformation = preferenceFileInformation,
                    ),
                )
            }
        }
    }
}
