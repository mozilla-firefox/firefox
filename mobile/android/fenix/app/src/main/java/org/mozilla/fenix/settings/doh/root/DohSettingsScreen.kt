/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.doh.root

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.AlertDialog
import androidx.compose.material.Icon
import androidx.compose.material.IconButton
import androidx.compose.material.Scaffold
import androidx.compose.material.Text
import androidx.compose.material.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.Divider
import mozilla.components.compose.base.Dropdown
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.textfield.TextField
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.LinkText
import org.mozilla.fenix.compose.LinkTextState
import org.mozilla.fenix.compose.button.RadioButton
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.settings.doh.CustomProviderErrorState
import org.mozilla.fenix.settings.doh.DohSettingsState
import org.mozilla.fenix.settings.doh.ProtectionLevel
import org.mozilla.fenix.settings.doh.Provider
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Composable function that displays the root screen of DoH settings.
 *
 * @param state The current [DohSettingsState].
 * @param onNavigateUp Invoked when the user clicks the navigate up (back) button.
 * @param onLearnMoreClicked Invoked when the user wants to visit an external doc about DoH.
 * @param onExceptionsClicked Invoked when the user wants to manage exceptions.
 * @param onDohOptionSelected Invoked when the user selects a protection level.
 * @param onCustomClicked Invoked when the user chooses to configure a custom provider (dialog).
 * @param onCustomCancelClicked Invoked when the user exits the custom provider dialog.
 * @param onCustomAddClicked Invoked when the user adds a custom provider in the dialog.
 * @param onDefaultInfoClicked Invoked when the user accesses info about Default DoH level.
 * @param onIncreasedInfoClicked Invoked when the user accesses info about Increased DoH level.
 * @param onMaxInfoClicked Invoked when the user accesses info about Max DoH level.
 */
@Composable
internal fun DohSettingsScreen(
    state: DohSettingsState,
    onNavigateUp: () -> Unit = {},
    onLearnMoreClicked: (String) -> Unit = {},
    onExceptionsClicked: () -> Unit = {},
    onDohOptionSelected: (ProtectionLevel, Provider?) -> Unit = { _: ProtectionLevel, _: Provider? -> },
    onCustomClicked: () -> Unit = {},
    onCustomCancelClicked: () -> Unit = {},
    onCustomAddClicked: (Provider.Custom, String) -> Unit = { _: Provider, _: String -> },
    onDefaultInfoClicked: () -> Unit = {},
    onIncreasedInfoClicked: () -> Unit = {},
    onMaxInfoClicked: () -> Unit = {},
) {
    Scaffold(
        topBar = {
            Toolbar(onToolbarBackClick = onNavigateUp)
        },
        backgroundColor = FirefoxTheme.colors.layer1,
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(paddingValues),
        ) {
            DohSummary(
                onLearnMoreClicked = onLearnMoreClicked,
            )

            DohSelection(
                state = state,
                onDohOptionSelected = onDohOptionSelected,
                onCustomClicked = onCustomClicked,
                onCustomCancelClicked = onCustomCancelClicked,
                onCustomAddClicked = onCustomAddClicked,
                onDefaultInfoClicked = onDefaultInfoClicked,
                onIncreasedInfoClicked = onIncreasedInfoClicked,
                onMaxInfoClicked = onMaxInfoClicked,
            )

            Divider(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(8.dp),
            )

            ExceptionsRow(onExceptionsClicked = onExceptionsClicked)
        }
    }
}

@Composable
private fun Toolbar(onToolbarBackClick: () -> Unit) {
    TopAppBar(
        backgroundColor = FirefoxTheme.colors.layer1,
        title = {
            Text(
                color = FirefoxTheme.colors.textPrimary,
                style = FirefoxTheme.typography.headline6,
                text = stringResource(R.string.preference_doh_title),
            )
        },
        navigationIcon = {
            IconButton(onClick = onToolbarBackClick) {
                Icon(
                    painter = painterResource(R.drawable.mozac_ic_back_24),
                    contentDescription = stringResource(R.string.preference_doh_up_description),
                    tint = FirefoxTheme.colors.iconPrimary,
                )
            }
        },
    )
}

@Composable
private fun DohSummary(
    onLearnMoreClicked: (String) -> Unit,
) {
    val summary = stringResource(
        R.string.preference_doh_summary,
        stringResource(id = R.string.preference_doh_learn_more),
    )

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 6.dp, horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Column(
            modifier = Modifier.weight(1f),
        ) {
            Text(
                color = FirefoxTheme.colors.textPrimary,
                style = FirefoxTheme.typography.subtitle1,
                text = stringResource(R.string.preference_doh_title),
            )

            Spacer(modifier = Modifier.height(8.dp))

            LinkText(
                text = summary,
                linkTextStates = listOf(
                    LinkTextState(
                        text = stringResource(R.string.preference_doh_learn_more),
                        url = SupportUtils.getGenericSumoURLForTopic(SupportUtils.SumoTopic.DNS_OVER_HTTPS),
                        onClick = {
                            onLearnMoreClicked(it)
                        },
                    ),
                ),
                linkTextDecoration = TextDecoration.Underline,
                style = FirefoxTheme.typography.body2.copy(
                    textAlign = TextAlign.Left,
                    color = FirefoxTheme.colors.textSecondary,
                ),
            )
        }
    }
}

/**
 * Protection level composable - used for all levels of protection
 */
@Composable
private fun DohProtectionLevel(
    modifier: Modifier = Modifier,
    selected: Boolean,
    label: String,
    summary: String,
    showInfoIcon: Boolean,
    provider: @Composable (() -> Unit)? = null,
    onInfoClick: () -> Unit = {},
    onClick: () -> Unit,
) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = modifier
            .padding(
                start = 72.dp,
                top = 6.dp,
                end = 16.dp,
                bottom = 6.dp,
            ),
    ) {
        RadioButton(
            selected = selected,
            onClick = onClick,
            modifier = Modifier.align(Alignment.Top),
        )

        Spacer(modifier = Modifier.width(8.dp))

        Column(
            modifier = Modifier
                .weight(1f)
                .align(Alignment.Top),
            horizontalAlignment = Alignment.Start,
        ) {
            ProviderSummary(label, summary)

            provider?.invoke()
        }

        if (showInfoIcon) {
            Icon(
                painter = painterResource(R.drawable.mozac_ic_information_24),
                contentDescription = stringResource(R.string.preference_doh_info_description),
                tint = FirefoxTheme.colors.iconPrimary,
                modifier = Modifier
                    .padding(16.dp)
                    .align(Alignment.Top)
                    .clickable {
                        onInfoClick()
                    },
            )
        }
    }
}

@Composable
@Suppress("LongMethod")
private fun DohSelection(
    state: DohSettingsState,
    onDohOptionSelected: (ProtectionLevel, Provider?) -> Unit = { _: ProtectionLevel, _: Provider? -> },
    onCustomClicked: () -> Unit,
    onCustomCancelClicked: () -> Unit,
    onCustomAddClicked: (Provider.Custom, String) -> Unit,
    onDefaultInfoClicked: () -> Unit,
    onIncreasedInfoClicked: () -> Unit,
    onMaxInfoClicked: () -> Unit,
) {
    state.allProtectionLevels.forEach { protectionLevel ->
        when (protectionLevel) {
            is ProtectionLevel.Default -> DohProtectionLevel(
                modifier = Modifier.fillMaxWidth(),
                selected = protectionLevel == state.selectedProtectionLevel,
                label = stringResource(R.string.preference_doh_default_protection),
                summary = stringResource(
                    R.string.preference_doh_default_protection_summary,
                    stringResource(id = R.string.app_name),
                ),
                showInfoIcon = true,
                onInfoClick = onDefaultInfoClicked,
                onClick = {
                    onDohOptionSelected(protectionLevel, null)
                },
            )

            is ProtectionLevel.Increased -> DohProtectionLevel(
                modifier = Modifier.fillMaxWidth(),
                selected = protectionLevel == state.selectedProtectionLevel,
                label = stringResource(R.string.preference_doh_increased_protection),
                summary = stringResource(R.string.preference_doh_increased_protection_summary),
                showInfoIcon = true,
                provider = if (protectionLevel == state.selectedProtectionLevel) {
                    {
                        state.selectedProvider?.let {
                            ProviderDropdown(
                                selectedProviderOption = it,
                                onProviderSelected = { provider ->
                                    onDohOptionSelected(
                                        protectionLevel,
                                        provider,
                                    )
                                },
                                providers = state.providers,
                                onCustomClicked = onCustomClicked,
                            )
                        }
                    }
                } else {
                    null
                },
                onInfoClick = onIncreasedInfoClicked,
                onClick = {
                    onDohOptionSelected(
                        protectionLevel,
                        state.selectedProvider ?: state.providers.first(),
                    )
                },
            )

            is ProtectionLevel.Max -> DohProtectionLevel(
                modifier = Modifier.fillMaxWidth(),
                selected = protectionLevel == state.selectedProtectionLevel,
                label = stringResource(R.string.preference_doh_max_protection),
                summary = stringResource(
                    R.string.preference_doh_max_protection_summary,
                    stringResource(id = R.string.app_name),
                ),
                showInfoIcon = true,
                provider = if (protectionLevel == state.selectedProtectionLevel) {
                    {
                        state.selectedProvider?.let {
                            ProviderDropdown(
                                selectedProviderOption = it,
                                onProviderSelected = { provider ->
                                    onDohOptionSelected(
                                        protectionLevel,
                                        provider,
                                    )
                                },
                                providers = state.providers,
                                onCustomClicked = onCustomClicked,
                            )
                        }
                    }
                } else {
                    null
                },
                onInfoClick = onMaxInfoClicked,
                onClick = {
                    onDohOptionSelected(
                        protectionLevel,
                        state.selectedProvider ?: state.providers.first(),
                    )
                },
            )

            is ProtectionLevel.Off -> DohProtectionLevel(
                modifier = Modifier.fillMaxWidth(),
                selected = protectionLevel == state.selectedProtectionLevel,
                label = stringResource(R.string.preference_doh_off),
                summary = stringResource(R.string.preference_doh_off_summary),
                showInfoIcon = false,
                onClick = {
                    onDohOptionSelected(protectionLevel, null)
                },
            )
        }
    }

    if (state.selectedProvider is Provider.Custom && state.isCustomProviderDialogOn) {
        AlertDialogAddCustomProvider(
            customProviderErrorState = state.customProviderErrorState,
            onCustomCancelClicked = { onCustomCancelClicked() },
            onCustomAddClicked = { url ->
                onCustomAddClicked(state.selectedProvider, url)
            },
        )
    }
}

@Composable
private fun ProviderSummary(
    label: String,
    summary: String,
) {
    Text(
        color = FirefoxTheme.colors.textPrimary,
        style = FirefoxTheme.typography.subtitle1,
        text = label,
    )

    Spacer(modifier = Modifier.height(8.dp))

    Text(
        color = FirefoxTheme.colors.textSecondary,
        style = FirefoxTheme.typography.body2,
        text = summary,
    )
}

@Composable
private fun ProviderDropdown(
    selectedProviderOption: Provider,
    onProviderSelected: (Provider) -> Unit = {},
    providers: List<Provider>,
    onCustomClicked: () -> Unit,
) {
    val customText = stringResource(R.string.preference_doh_provider_custom)
    val defaultText = stringResource(R.string.preference_doh_provider_default)

    val placeholder = if (selectedProviderOption is Provider.BuiltIn) {
        selectedProviderOption.name + if (selectedProviderOption.default) " $defaultText" else ""
    } else {
        customText
    }

    val dropdownItems = buildProviderMenuItems(
        providers = providers,
        selectedProvider = selectedProviderOption,
        customText = customText,
        defaultText = defaultText,
        onProviderSelected = onProviderSelected,
        onCustomClicked = onCustomClicked,
    )

    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 16.dp),
    ) {
        Dropdown(
            label = stringResource(R.string.preference_doh_choose_provider),
            placeholder = placeholder,
            dropdownItems = dropdownItems,
        )
    }

    if (selectedProviderOption is Provider.Custom) {
        TextWithUnderline(
            text = selectedProviderOption.url,
            showCustomProviderDialog = { onCustomClicked() },
        )
    }
}

/**
 * Returns a list of [MenuItem.CheckableItem] based on the providers.
 */
private fun buildProviderMenuItems(
    providers: List<Provider>,
    selectedProvider: Provider,
    customText: String,
    defaultText: String,
    onProviderSelected: (Provider) -> Unit,
    onCustomClicked: () -> Unit,
): List<MenuItem.CheckableItem> {
    return providers.map { provider ->
        // Determine the label to display
        val text = when (provider) {
            is Provider.BuiltIn -> {
                provider.name + if (provider.default) " $defaultText" else ""
            }

            is Provider.Custom -> customText
        }

        MenuItem.CheckableItem(
            text = Text.String(text),
            isChecked = (provider == selectedProvider),
            onClick = {
                onProviderSelected(provider)
                if (provider is Provider.Custom) {
                    onCustomClicked()
                }
            },
        )
    }
}

@Composable
private fun AlertDialogAddCustomProvider(
    customProviderErrorState: CustomProviderErrorState,
    onCustomCancelClicked: () -> Unit,
    onCustomAddClicked: (String) -> Unit,
) {
    var customProviderInput by remember { mutableStateOf("") }
    val onCustomProviderInputChange: (String) -> Unit = { it -> customProviderInput = it }
    val nonHttpsString = stringResource(R.string.preference_doh_provider_custom_dialog_error_https)
    val invalidString = stringResource(R.string.preference_doh_provider_custom_dialog_error_invalid)

    AlertDialog(
        title = {
            Text(
                text = stringResource(R.string.preference_doh_provider_custom_dialog_title),
                style = FirefoxTheme.typography.headline7,
                color = FirefoxTheme.colors.textPrimary,
            )
        },
        text = {
            TextField(
                value = customProviderInput,
                onValueChange = {
                    onCustomProviderInputChange(it)
                },
                placeholder = "",
                errorText = when (customProviderErrorState) {
                    CustomProviderErrorState.NonHttps -> nonHttpsString
                    CustomProviderErrorState.Invalid -> invalidString
                    else -> ""
                },
                label = stringResource(R.string.preference_doh_provider_custom_dialog_textfield),
                isError = customProviderErrorState != CustomProviderErrorState.Valid,
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
            )
        },
        onDismissRequest = onCustomCancelClicked,
        confirmButton = {
            TextButton(
                text = stringResource(R.string.preference_doh_provider_custom_dialog_add),
                onClick = { onCustomAddClicked(customProviderInput) },
                upperCaseText = false,
            )
        },
        dismissButton = {
            TextButton(
                text = stringResource(R.string.preference_doh_provider_custom_dialog_cancel),
                onClick = onCustomCancelClicked,
                upperCaseText = false,
            )
        },
        backgroundColor = FirefoxTheme.colors.layer2,
    )
}

@Composable
private fun TextWithUnderline(
    showCustomProviderDialog: () -> Unit = {},
    text: String,
    modifier: Modifier = Modifier,
    textColor: Color = FirefoxTheme.colors.textPrimary,
    underlineColor: Color = FirefoxTheme.colors.formDefault,
) {
    Column(
        modifier = modifier,
    ) {
        Text(
            modifier = Modifier
                .fillMaxWidth()
                .clickable {
                    showCustomProviderDialog()
                },
            color = textColor,
            style = FirefoxTheme.typography.body2,
            text = text,
        )

        Spacer(modifier = Modifier.height(4.dp))

        Divider(
            modifier = Modifier
                .fillMaxWidth()
                .height(1.dp),
            color = underlineColor,
        )
    }
}

@Composable
private fun ExceptionsRow(onExceptionsClicked: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onExceptionsClicked() },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            painter = painterResource(R.drawable.ic_internet),
            contentDescription = stringResource(R.string.preference_doh_exceptions_description),
            tint = FirefoxTheme.colors.iconPrimary,
            modifier = Modifier.padding(16.dp),
        )
        Text(
            text = stringResource(R.string.preference_doh_exceptions),
            color = FirefoxTheme.colors.textPrimary,
            style = FirefoxTheme.typography.subtitle1,
        )
    }
}

@Composable
@FlexibleWindowLightDarkPreview
private fun DohScreenDefaultProviderPreview() {
    FirefoxTheme {
        val provider = Provider.BuiltIn(
            url = "https://mozilla.cloudflare-dns.com/dns-query",
            name = "Cloudflare",
            default = true,
        )
        DohSettingsScreen(
            state = DohSettingsState(
                allProtectionLevels = listOf(
                    ProtectionLevel.Default,
                    ProtectionLevel.Increased,
                    ProtectionLevel.Max,
                    ProtectionLevel.Off,
                ),
                selectedProtectionLevel = ProtectionLevel.Increased,
                providers = listOf(
                    provider,
                ),
                selectedProvider = provider,
                exceptionsList = emptyList(),
                isUserExceptionValid = true,
            ),
        )
    }
}

@Composable
@FlexibleWindowLightDarkPreview
private fun DohScreenCustomProviderPreview() {
    FirefoxTheme {
        val provider = Provider.Custom(url = "")
        DohSettingsScreen(
            state = DohSettingsState(
                allProtectionLevels = listOf(
                    ProtectionLevel.Default,
                    ProtectionLevel.Increased,
                    ProtectionLevel.Max,
                    ProtectionLevel.Off,
                ),
                selectedProtectionLevel = ProtectionLevel.Increased,
                providers = listOf(
                    provider,
                ),
                selectedProvider = provider,
                exceptionsList = emptyList(),
                isUserExceptionValid = true,
            ),
        )
    }
}
