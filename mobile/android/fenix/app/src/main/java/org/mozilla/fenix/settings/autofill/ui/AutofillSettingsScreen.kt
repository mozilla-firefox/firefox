/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.autofill.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import androidx.navigation.compose.rememberNavController
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.theme.ThemedValue
import mozilla.components.compose.base.theme.ThemedValueProvider
import mozilla.components.concept.storage.Address
import mozilla.components.concept.storage.CreditCard
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.list.IconListItem
import org.mozilla.fenix.compose.list.SwitchListItem
import org.mozilla.fenix.compose.list.TextListItem
import org.mozilla.fenix.compose.settings.SettingsSectionHeader
import org.mozilla.fenix.debugsettings.addresses.FakeCreditCardsAddressesStorage.Companion.generateCreditCard
import org.mozilla.fenix.debugsettings.addresses.FakeCreditCardsAddressesStorage.Companion.toAddress
import org.mozilla.fenix.debugsettings.addresses.FakeCreditCardsAddressesStorage.Companion.toCreditCard
import org.mozilla.fenix.debugsettings.addresses.generateFakeAddressForLangTag
import org.mozilla.fenix.theme.FirefoxTheme

@Composable
internal fun AutofillSettingsScreen(
    buildStore: (NavHostController) -> AutofillSettingsStore,
    accountManager: FxaAccountManager?,
    isAddressSyncEnabled: Boolean,
) {
    val navController = rememberNavController()
    val store = buildStore(navController)

    DisposableEffect(Unit) {
        val authenticated = accountManager?.authenticatedAccount() != null
        val needsReauthentication = accountManager?.accountNeedsReauth() == true

        when {
            needsReauthentication -> {
                store.dispatch(AccountAuthenticationAction.Failed)
            }
            authenticated -> {
                store.dispatch(AccountAuthenticationAction.Authenticated)
            }
            !authenticated -> {
                store.dispatch(AccountAuthenticationAction.NotAuthenticated)
            }
        }

        onDispose {
            store.dispatch(ViewDisposed)
        }
    }

    Scaffold(
        topBar = {
            AutofillSettingsTopBar(store)
        },
        contentWindowInsets = WindowInsets(0.dp),
    ) { paddingValues ->
        Column(modifier = Modifier.padding(paddingValues).fillMaxWidth().verticalScroll(rememberScrollState())) {
            Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static100))

            AutofillSettingsAddressSection(store, isAddressSyncEnabled)

            Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

            HorizontalDivider()

            Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static300))

            AutofillSettingsCreditCardSection(store)
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun AutofillSettingsTopBar(store: AutofillSettingsStore) {
    TopAppBar(
        windowInsets =
            WindowInsets(
                top = 0.dp,
                bottom = 0.dp,
            ),
        title = {
            Text(
                text = stringResource(R.string.preferences_autofill),
                style = FirefoxTheme.typography.headline5,
            )
        },
        navigationIcon = {
            IconButton(
                onClick = { store.dispatch(AutofillSettingsBackClicked) },
                contentDescription =
                    stringResource(R.string.autofill_settings_navigate_back_button_content_description),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                    contentDescription = null,
                )
            }
        },
    )
}

@Composable
private fun AutofillSettingsAddressSection(
    store: AutofillSettingsStore,
    isAddressSyncEnabled: Boolean,
) {
    val state by store.stateFlow.collectAsState()

    SettingsSectionHeader(
        text = stringResource(id = R.string.preferences_addresses),
        modifier = Modifier.padding(horizontal = FirefoxTheme.layout.space.dynamic200),
    )

    Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

    SwitchListItem(
        label = stringResource(id = R.string.preferences_addresses_save_and_autofill_addresses_2),
        checked = state.saveFillAddresses,
        description = stringResource(id = R.string.preferences_addresses_save_and_autofill_addresses_summary_2),
        maxDescriptionLines = Int.MAX_VALUE,
        showSwitchAfter = true,
        onClick = { store.dispatch(ChangeAddressSaveFillPreference(!state.saveFillAddresses)) },
    )

    Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

    if (isAddressSyncEnabled) {
        if (state.accountAuthState == AccountAuthState.Authenticated) {
            SwitchListItem(
                label = stringResource(id = R.string.preferences_addresses_sync_addresses),
                checked = state.syncAddresses,
                showSwitchAfter = true,
            ) {
                store.dispatch(UpdateAddressesSyncStatus(!state.syncAddresses))
            }
        } else {
            TextListItem(
                label = stringResource(id = R.string.preferences_addresses_sync_addresses_across_devices),
                onClick = { store.dispatch(SyncAddressesAcrossDevicesClicked) },
            )
        }
    }

    Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

    if (state.addresses.isEmpty()) {
        IconListItem(
            label = stringResource(R.string.preferences_addresses_add_address),
            beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_plus_24),
            onClick = { store.dispatch(AddAddressClicked) },
        )
    } else {
        TextListItem(
            label = stringResource(id = R.string.preferences_addresses_manage_addresses),
            onClick = { store.dispatch(AddAddressClicked) },
        )
    }
}

@Composable
private fun AutofillSettingsCreditCardSection(store: AutofillSettingsStore) {
    val state by store.stateFlow.collectAsState()

    SettingsSectionHeader(
        text = stringResource(id = R.string.preferences_credit_cards_2),
        modifier = Modifier.padding(horizontal = FirefoxTheme.layout.space.dynamic200),
    )

    Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

    SwitchListItem(
        label = stringResource(id = R.string.preferences_credit_cards_save_and_autofill_cards_2),
        checked = state.saveFillCards,
        description =
            stringResource(
                id = R.string.preferences_credit_cards_save_and_autofill_cards_summary_2,
                stringResource(id = R.string.app_name),
            ),
        maxDescriptionLines = Int.MAX_VALUE,
        showSwitchAfter = true,
    ) {
        store.dispatch(ChangeCardSaveFillPreference(!state.saveFillCards))
    }

    Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static100))

    if (state.accountAuthState == AccountAuthState.Authenticated) {
        SwitchListItem(
            label = stringResource(id = R.string.preferences_credit_cards_sync_cards),
            checked = state.syncCreditCards,
            showSwitchAfter = true,
        ) {
            store.dispatch(UpdateCreditCardsSyncStatus(!state.syncCreditCards))
        }
    } else {
        TextListItem(
            label = stringResource(id = R.string.preferences_credit_cards_sync_cards_across_devices),
            onClick = { store.dispatch(SyncCardsAcrossDevicesClicked) },
        )
    }

    Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static100))

    if (state.creditCards.isEmpty()) {
        IconListItem(
            label = stringResource(R.string.credit_cards_add_card),
            beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_plus_24),
            onClick = { store.dispatch(AddCardClicked) },
        )
    } else {
        TextListItem(
            label = stringResource(id = R.string.preferences_credit_cards_manage_saved_cards_2),
            onClick = { store.dispatch(ManageCreditCardsClicked) },
        )
    }
}

private data class AutofillSettingsScreenPreviewState(
    val addresses: List<Address> = emptyList(),
    val creditCards: List<CreditCard> = emptyList(),
    val accountAuthState: AccountAuthState = AccountAuthState.LoggedOut,
)

private class AutofillSettingsScreenPreviewProvider :
    ThemedValueProvider<AutofillSettingsScreenPreviewState>(
        sequenceOf(
            AutofillSettingsScreenPreviewState(),
            AutofillSettingsScreenPreviewState(
                addresses = listOf("en-CA".generateFakeAddressForLangTag().toAddress()),
                creditCards = listOf(generateCreditCard().toCreditCard()),
            ),
            AutofillSettingsScreenPreviewState(accountAuthState = AccountAuthState.Authenticated),
        )
    )

@Composable
@FlexibleWindowPreview
private fun AutofillSettingsScreenPreview(
    @PreviewParameter(AutofillSettingsScreenPreviewProvider::class)
    state: ThemedValue<AutofillSettingsScreenPreviewState>
) {
    val store = { _: NavHostController ->
        AutofillSettingsStore(
            initialState =
                AutofillSettingsState.default.copy(
                    addresses = state.value.addresses,
                    creditCards = state.value.creditCards,
                    accountAuthState = state.value.accountAuthState,
                )
        )
    }
    FirefoxTheme(state.theme) {
        AutofillSettingsScreen(
            buildStore = store,
            accountManager = null,
            isAddressSyncEnabled = true,
        )
    }
}
