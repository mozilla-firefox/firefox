/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package org.mozilla.fenix.settings

import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalLocale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.paneTitle
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import kotlin.time.Instant
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.compose.base.LinkText
import mozilla.components.compose.base.LinkTextState
import mozilla.components.compose.base.PromoCard
import mozilla.components.compose.base.Switch
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.modifier.debouncedToggleable
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.state.AccountState
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.Country
import mozilla.components.feature.ipprotection.store.state.DataVolume
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.IPProtectionStatus
import mozilla.components.feature.ipprotection.store.state.Location
import mozilla.components.feature.ipprotection.store.state.Recommended
import mozilla.components.feature.ipprotection.store.state.Uninitialized
import mozilla.components.feature.ipprotection.store.state.UsageQuota
import mozilla.components.feature.ipprotection.store.state.gigabytes
import mozilla.components.feature.ipprotection.store.state.ipProtectionStatus
import mozilla.components.feature.ipprotection.store.state.isActivationInFlight
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.list.TextListItem
import org.mozilla.fenix.compose.settings.SettingsSectionHeader
import org.mozilla.fenix.theme.FirefoxTheme

private val PROMO_ILLUSTRATION_SIZE = 60.dp

/**
 * The main VPN / IP Protection settings screen.
 *
 * @param state Current [IPProtectionState] to render.
 * @param snackbarHostState The [SnackbarHostState] used to display snackbars.
 * @param promoDate Locale-formatted end date used by the promo copy when the user is not on a metered plan. `null`
 *   means the promo cannot be rendered (e.g. Nimbus shipped a malformed date) and the header falls back to the plain
 *   description.
 * @param onVpnToggle Called when the VPN switch is toggled.
 * @param onLearnMoreClick Called when any "Learn more" link is tapped.
 * @param onGetStartedClick Called when the "Get started" button is tapped.
 * @param showDebugAction Whether to show the debug menu action in the toolbar.
 * @param onDebugActionClick Called when the debug menu action is tapped.
 * @param onNavigateBack Called when the back navigation icon is tapped.
 * @param onLocationClicked Called when the VPN location row is tapped.
 * @param isLocationSelectionEnabled Whether the location row is interactive. When `false`, the row is displayed without
 *   a click affordance.
 */
@Suppress("LongParameterList")
@Composable
fun IPProtectionScreen(
    state: IPProtectionState,
    snackbarHostState: SnackbarHostState,
    promoDate: String?,
    onVpnToggle: (Boolean) -> Unit,
    onLearnMoreClick: () -> Unit,
    onGetStartedClick: () -> Unit,
    showDebugAction: Boolean = false,
    onDebugActionClick: () -> Unit = {},
    onNavigateBack: () -> Unit,
    onLocationClicked: () -> Unit,
    isLocationSelectionEnabled: Boolean = false,
) {
    val screenTitle = stringResource(R.string.ip_protection_settings_title)
    val status = state.ipProtectionStatus

    Scaffold(
        modifier = Modifier.semantics { paneTitle = screenTitle },
        topBar = {
            IPProtectionTopAppBar(
                showDebugAction = showDebugAction,
                onNavigateBack = onNavigateBack,
                onDebugActionClick = onDebugActionClick,
            )
        },
        snackbarHost = {
            SnackbarHost(hostState = snackbarHostState)
        },
    ) { paddingValues ->
        Surface(
            modifier = Modifier.fillMaxSize().padding(paddingValues),
            color = MaterialTheme.colorScheme.surface,
        ) {
            Column(modifier = Modifier.verticalScroll(rememberScrollState())) {
                VpnHeader(
                    isActive = status == IPProtectionStatus.Enabled,
                    promoDate = promoDate.takeIf { state.usageQuota !is UsageQuota.Metered },
                    onLearnMoreClick = onLearnMoreClick,
                )

                VpnToggleRow(
                    checked = status == IPProtectionStatus.Enabled,
                    enabled = status == IPProtectionStatus.Disabled || status == IPProtectionStatus.Enabled,
                    onToggle = onVpnToggle,
                )

                HorizontalDivider()

                if (status.isReadyToUse()) {
                    val usageQuota = state.usageQuota
                    if (usageQuota is UsageQuota.Metered) {
                        DataLimitSection(
                            quota = usageQuota,
                            isDataLimitReached = state.proxyStatus is Authorized.DataLimitReached,
                            onLearnMoreClick = onLearnMoreClick,
                        )

                        HorizontalDivider()
                    }

                    VpnLocationSection(
                        selectedLocation = state.locationState.selectedLocation,
                        onLocationClicked = onLocationClicked,
                        enabled = isLocationSelectionEnabled,
                        isActivating = state.isActivationInFlight,
                    )
                } else {
                    GetStartedSection(
                        status = status,
                        onGetStartedClick = onGetStartedClick,
                    )
                }
            }
        }
    }
}

private fun IPProtectionStatus.isReadyToUse() =
    when (this) {
        IPProtectionStatus.Disabled,
        IPProtectionStatus.Activating,
        IPProtectionStatus.Enabled,
        IPProtectionStatus.DataLimitReached,
        IPProtectionStatus.ConnectionError -> true

        IPProtectionStatus.Unavailable,
        IPProtectionStatus.Uninitialized,
        IPProtectionStatus.AuthRequired,
        IPProtectionStatus.AwaitingService -> false
    }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun IPProtectionTopAppBar(
    showDebugAction: Boolean,
    onNavigateBack: () -> Unit,
    onDebugActionClick: () -> Unit,
) {
    TopAppBar(
        title = {
            Text(
                text = stringResource(R.string.ip_protection_settings_title),
                style = FirefoxTheme.typography.headline5,
                modifier = Modifier.semantics { heading() },
            )
        },
        navigationIcon = {
            IconButton(
                onClick = onNavigateBack,
                contentDescription = stringResource(R.string.ip_protection_navigate_back_button_content_description),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                    contentDescription = null,
                )
            }
        },
        actions = {
            if (showDebugAction) {
                IconButton(
                    onClick = onDebugActionClick,
                    contentDescription = stringResource(R.string.content_description_menu),
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_debug_drawer_24),
                        contentDescription = stringResource(R.string.debug_drawer_title),
                    )
                }
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
private fun DataLimitSection(
    isDataLimitReached: Boolean,
    quota: UsageQuota.Metered,
    onLearnMoreClick: () -> Unit,
) {
    Column(
        modifier =
            Modifier.fillMaxWidth()
                .padding(
                    horizontal = FirefoxTheme.layout.space.dynamic200,
                    vertical = FirefoxTheme.layout.space.static150,
                )
    ) {
        Text(
            text = stringResource(R.string.ip_protection_data_limit_label),
            style = FirefoxTheme.typography.subtitle1,
            color = MaterialTheme.colorScheme.onSurface,
        )

        if (!isDataLimitReached) {
            Text(
                text =
                    stringResource(
                        R.string.ip_protection_data_limit_value,
                        quota.remaining.gigabytes,
                        quota.max.gigabytes,
                    ),
                style = FirefoxTheme.typography.body2,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        } else {
            Text(
                text =
                    stringResource(R.string.ip_protection_data_limit_reached_description, quota.max.gigabytes.toInt()),
                style = FirefoxTheme.typography.body2,
                color = MaterialTheme.colorScheme.error,
            )
        }
    }

    LinearProgressIndicator(
        progress = { (quota.remaining / quota.max).coerceIn(0f, 1f) },
        modifier = Modifier.fillMaxWidth().padding(horizontal = FirefoxTheme.layout.space.dynamic200).clip(CircleShape),
        color = MaterialTheme.colorScheme.primary,
        trackColor = MaterialTheme.colorScheme.surfaceVariant,
        drawStopIndicator = {},
    )

    Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static100))

    val linkColor = MaterialTheme.colorScheme.tertiary
    Text(
        text =
            buildAnnotatedString {
                append(stringResource(R.string.ip_protection_data_reset_info, quota.max.gigabytes))
                append(" ")
                withStyle(SpanStyle(color = linkColor, textDecoration = TextDecoration.Underline)) {
                    append(stringResource(R.string.ip_protection_learn_more))
                }
            },
        style = FirefoxTheme.typography.body2,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier =
            Modifier.fillMaxWidth()
                .clickable { onLearnMoreClick() }
                .padding(
                    horizontal = FirefoxTheme.layout.space.dynamic200,
                    vertical = FirefoxTheme.layout.space.static150,
                ),
    )
}

@Composable
private fun ColumnScope.GetStartedSection(
    status: IPProtectionStatus,
    onGetStartedClick: () -> Unit,
) {
    Spacer(modifier = Modifier.weight(1f))

    val text =
        when (status) {
            IPProtectionStatus.AwaitingService,
            IPProtectionStatus.Uninitialized -> stringResource(R.string.ip_protection_connecting)
            IPProtectionStatus.Unavailable,
            IPProtectionStatus.AuthRequired,
            IPProtectionStatus.Disabled,
            IPProtectionStatus.Activating,
            IPProtectionStatus.Enabled,
            IPProtectionStatus.DataLimitReached,
            IPProtectionStatus.ConnectionError -> stringResource(R.string.ip_protection_get_started)
        }

    FilledButton(
        text = text,
        enabled = status == IPProtectionStatus.AuthRequired,
        modifier = Modifier.padding(horizontal = FirefoxTheme.layout.space.static200).fillMaxWidth(),
        onClick = onGetStartedClick,
    )

    Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static400))
}

@Composable
private fun VpnLocationSection(
    selectedLocation: Location,
    onLocationClicked: () -> Unit,
    enabled: Boolean,
    isActivating: Boolean,
) {
    // The row keeps its enabled appearance while activating, it just stops being tappable.
    val isClickable = enabled && !isActivating

    SettingsSectionHeader(
        text = stringResource(R.string.ip_protection_location_section),
        modifier =
            Modifier.padding(
                horizontal = FirefoxTheme.layout.space.dynamic200,
                vertical = FirefoxTheme.layout.space.static100,
            ),
    )

    when (selectedLocation) {
        is Recommended -> {
            TextListItem(
                label = stringResource(R.string.ip_protection_location_recommended_label),
                description =
                    stringResource(
                        R.string.ip_protection_location_fastest_description,
                        stringResource(R.string.firefox),
                    ),
                maxDescriptionLines = Int.MAX_VALUE,
                onClick = onLocationClicked.takeIf { isClickable },
            )
        }
        is Country -> {
            TextListItem(
                label = selectedLocation.displayName(LocalLocale.current.platformLocale),
                onClick = onLocationClicked.takeIf { isClickable },
            )
        }
    }
}

@Composable
private fun VpnToggleRow(
    checked: Boolean,
    enabled: Boolean,
    onToggle: (Boolean) -> Unit,
) {
    Row(
        modifier =
            Modifier.fillMaxWidth()
                .defaultMinSize(minHeight = 56.dp)
                .debouncedToggleable(
                    value = checked,
                    enabled = enabled,
                    role = Role.Switch,
                    onValueChange = onToggle,
                )
                .padding(
                    horizontal = FirefoxTheme.layout.space.dynamic200,
                    vertical = FirefoxTheme.layout.space.static150,
                ),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static200),
    ) {
        Icon(
            painter = painterResource(mozilla.components.ui.icons.R.drawable.mozac_ic_globe_24),
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurface,
        )
        Text(
            text = stringResource(R.string.ip_protection_toggle_label),
            modifier = Modifier.weight(1f),
            style = FirefoxTheme.typography.subtitle1,
            color = MaterialTheme.colorScheme.onSurface,
        )
        Switch(
            checked = checked,
            onCheckedChange = null,
            enabled = enabled,
        )
    }
}

@Composable
private fun VpnHeader(
    isActive: Boolean,
    promoDate: String?,
    onLearnMoreClick: () -> Unit,
) {
    // The promo card only belongs on the screen while a promo is running - bug 2070125.
    if (promoDate == null) {
        VpnDescription(onLearnMoreClick = onLearnMoreClick)
    } else {
        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static100))

        VpnPromoCard(
            isActive = isActive,
            promoDate = promoDate,
            onLearnMoreClick = onLearnMoreClick,
            modifier = Modifier.padding(horizontal = FirefoxTheme.layout.space.dynamic200),
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))
    }
}

@Composable
private fun VpnDescription(onLearnMoreClick: () -> Unit) {
    val learnMoreText = stringResource(R.string.ip_protection_learn_more)

    LinkText(
        text = stringResource(R.string.ip_protection_promo_body_2, learnMoreText),
        linkTextStates =
            listOf(
                LinkTextState(
                    text = learnMoreText,
                    url = "",
                    onClick = { onLearnMoreClick() },
                )
            ),
        modifier =
            Modifier.fillMaxWidth()
                .padding(horizontal = FirefoxTheme.layout.space.dynamic200)
                .padding(
                    top = FirefoxTheme.layout.space.static100,
                    bottom = FirefoxTheme.layout.space.static200,
                ),
        style = FirefoxTheme.typography.body1.copy(color = MaterialTheme.colorScheme.onSurface),
        linkTextDecoration = TextDecoration.Underline,
    )
}

@Composable
private fun VpnPromoCard(
    isActive: Boolean,
    promoDate: String,
    onLearnMoreClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val learnMoreText = stringResource(R.string.ip_protection_learn_more)

    PromoCard(
        description = null,
        modifier = modifier.fillMaxWidth(),
        title = stringResource(R.string.ip_protection_promo_headline, stringResource(R.string.firefox)),
        footer =
            stringResource(R.string.ip_protection_onboarding_body_promo, promoDate, learnMoreText) to
                LinkTextState(
                    text = learnMoreText,
                    url = "",
                    onClick = { onLearnMoreClick() },
                ),
        illustration = {
            Image(
                painter =
                    painterResource(
                        if (isActive) {
                            R.drawable.ic_kit_shield_on_state
                        } else {
                            R.drawable.ic_kit_shield_off_state
                        }
                    ),
                contentDescription = null,
                modifier = Modifier.size(PROMO_ILLUSTRATION_SIZE),
            )
        },
        verticalAlignment = Alignment.CenterVertically,
    )
}

private val FiftyGb = DataVolume.roundedGigabytes(50)
private val FortyGb = DataVolume.roundedGigabytes(40)

@OptIn(ExperimentalAndroidComponentsApi::class)
@FlexibleWindowPreview
@Composable
private fun IPProtectionScreenActivePreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme = theme) {
        IPProtectionScreen(
            state =
                IPProtectionState(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    proxyStatus = Authorized.Active,
                    serviceStatus = ServiceState.Ready,
                    usageQuota =
                        UsageQuota.Metered(
                            max = FiftyGb,
                            remaining = FortyGb,
                        ),
                ),
            snackbarHostState = SnackbarHostState(),
            promoDate = null,
            onVpnToggle = {},
            onLearnMoreClick = {},
            onGetStartedClick = {},
            showDebugAction = false,
            onDebugActionClick = {},
            onNavigateBack = {},
            onLocationClicked = {},
        )
    }
}

@OptIn(ExperimentalAndroidComponentsApi::class)
@FlexibleWindowPreview
@Composable
private fun IPProtectionScreenActivatingPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme = theme) {
        IPProtectionScreen(
            state =
                IPProtectionState(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    proxyStatus = Authorized.Activating,
                    serviceStatus = ServiceState.Ready,
                    usageQuota =
                        UsageQuota.Metered(
                            resetTime = Instant.parse("2026-06-01T00:00:00Z"),
                            max = FiftyGb,
                            remaining = FortyGb,
                        ),
                ),
            snackbarHostState = SnackbarHostState(),
            promoDate = null,
            onVpnToggle = {},
            onLearnMoreClick = {},
            onGetStartedClick = {},
            showDebugAction = false,
            onDebugActionClick = {},
            onNavigateBack = {},
            onLocationClicked = {},
            isLocationSelectionEnabled = true,
        )
    }
}

@OptIn(ExperimentalAndroidComponentsApi::class)
@FlexibleWindowPreview
@Composable
private fun IPProtectionScreenNotEnrolledPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme = theme) {
        IPProtectionScreen(
            state =
                IPProtectionState(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    serviceStatus = ServiceState.Unauthenticated,
                    accountState = AccountState(AccountStatus.Authenticated),
                    usageQuota = null,
                ),
            snackbarHostState = SnackbarHostState(),
            promoDate = "January 31",
            onVpnToggle = {},
            onLearnMoreClick = {},
            onGetStartedClick = {},
            showDebugAction = false,
            onDebugActionClick = {},
            onNavigateBack = {},
            onLocationClicked = {},
        )
    }
}

@OptIn(ExperimentalAndroidComponentsApi::class)
@FlexibleWindowPreview
@Composable
private fun IPProtectionScreenPausedPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme = theme) {
        IPProtectionScreen(
            state =
                IPProtectionState(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    proxyStatus = Authorized.DataLimitReached,
                    serviceStatus = ServiceState.Ready,
                    usageQuota =
                        UsageQuota.Metered(
                            resetTime = Instant.parse("2026-06-01T00:00:00Z"),
                            max = FiftyGb,
                            remaining = DataVolume.Zero,
                        ),
                ),
            snackbarHostState = SnackbarHostState(),
            promoDate = null,
            onVpnToggle = {},
            onLearnMoreClick = {},
            onGetStartedClick = {},
            showDebugAction = false,
            onDebugActionClick = {},
            onNavigateBack = {},
            onLocationClicked = {},
        )
    }
}

@OptIn(ExperimentalAndroidComponentsApi::class)
@FlexibleWindowPreview
@Composable
private fun IPProtectionScreenConnectingPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme = theme) {
        IPProtectionScreen(
            state =
                IPProtectionState(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    proxyStatus = Uninitialized,
                    usageQuota =
                        UsageQuota.Metered(
                            resetTime = Instant.parse("2026-06-01T00:00:00Z"),
                            max = FiftyGb,
                            remaining = FortyGb,
                        ),
                ),
            snackbarHostState = SnackbarHostState(),
            promoDate = null,
            onVpnToggle = {},
            onLearnMoreClick = {},
            onGetStartedClick = {},
            showDebugAction = false,
            onDebugActionClick = {},
            onNavigateBack = {},
            onLocationClicked = {},
        )
    }
}
