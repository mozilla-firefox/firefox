/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection.debug

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import kotlin.time.Instant
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.R
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.AccountState
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.DataVolume
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.ProxyRequest
import mozilla.components.feature.ipprotection.store.state.UsageQuota
import mozilla.components.feature.ipprotection.store.state.gigabytes
import mozilla.components.feature.ipprotection.store.state.used
import mozilla.components.lib.state.ext.observeAsComposableState

/**
 * A debug view to monitor [IPProtectionState] and renders useful data in it to aid in developer and QA testing.
 *
 * @param store The [IPProtectionStore] to observe and render.
 * @param modifier The [Modifier] applied to the root container.
 */
@Composable
fun IPProtectionStateDebug(
    store: IPProtectionStore,
    modifier: Modifier = Modifier,
) {
    val state by store.observeAsComposableState { it }
    IPProtectionStateDebugContent(
        state = state,
        modifier = modifier,
    )
}

/**
 * Debug Composable that renders useful data in [IPProtectionState] to aid in developer and QA testing.
 *
 * @param state The [IPProtectionState] to render.
 * @param modifier The [Modifier] applied to the root container.
 */
@Composable
fun IPProtectionStateDebugContent(
    state: IPProtectionState,
    modifier: Modifier = Modifier,
) {
    Surface(
        modifier = modifier.fillMaxSize(),
        color = MaterialTheme.colorScheme.background,
    ) {
        Column(
            modifier =
                Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(AcornTheme.layout.space.static200),
            verticalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static150),
        ) {
            Text(
                text = stringResource(R.string.mozac_feature_ipprotection_title),
                style = MaterialTheme.typography.headlineSmall,
                color = MaterialTheme.colorScheme.onBackground,
            )

            DebugSection(title = stringResource(R.string.mozac_feature_ipprotection_eligibility)) {
                DebugRow(
                    stringResource(R.string.mozac_feature_ipprotection_eligibilitystatus),
                    state.eligibilityStatus.toString(),
                )
            }

            DebugSection(title = stringResource(R.string.mozac_feature_ipprotection_name)) {
                DebugRow(
                    stringResource(R.string.mozac_feature_ipprotection_proxystatus),
                    state.proxyStatus.toString(),
                )
                DebugRow(
                    stringResource(R.string.mozac_feature_ipprotection_servicestatus),
                    state.serviceStatus.name,
                )
                DebugRow(
                    stringResource(R.string.mozac_feature_ipprotection_lasterror),
                    state.lastError ?: "null",
                )
            }

            DataUsageSection(state = state)

            AccountSection(state = state)

            DebugSection(title = stringResource(R.string.mozac_feature_ipprotection_vpn_ui)) {
                val proxyRequest = state.proxyRequest
                DebugRow(
                    stringResource(R.string.mozac_feature_ipprotection_activation_state),
                    proxyRequest?.javaClass?.simpleName ?: "null",
                )
                DebugRow(
                    stringResource(R.string.mozac_feature_ipprotection_selected_location_code),
                    when (proxyRequest) {
                        is ProxyRequest.Activate -> proxyRequest.countryCode ?: "recommended"
                        is ProxyRequest.SwitchLocation -> proxyRequest.countryCode ?: "recommended"
                        ProxyRequest.Deactivate -> "null"
                        null -> "null"
                    },
                )
            }
        }
    }
}

@Composable
private fun DataUsageSection(state: IPProtectionState) {
    val quota = state.usageQuota
    val unlimited = stringResource(R.string.mozac_feature_ipprotection_unlimited)
    DebugSection(title = stringResource(R.string.mozac_feature_ipprotection_data_usage)) {
        DebugRow(
            stringResource(R.string.mozac_feature_ipprotection_remainingdatabytes),
            when (quota) {
                is UsageQuota.Metered -> quota.remaining.bytes.toString()
                is UsageQuota.Unmetered -> unlimited
                null -> "null"
            },
        )
        DebugRow(
            stringResource(R.string.mozac_feature_ipprotection_maxdatabytes),
            when (quota) {
                is UsageQuota.Metered -> quota.max.bytes.toString()
                is UsageQuota.Unmetered -> unlimited
                null -> "null"
            },
        )
        DebugRow(
            stringResource(R.string.mozac_feature_ipprotection_useddatagb),
            when (quota) {
                is UsageQuota.Metered -> "%.2fGB".format(quota.used.gigabytes)
                is UsageQuota.Unmetered -> unlimited
                null -> "null"
            },
        )
        DebugRow(
            stringResource(R.string.mozac_feature_ipprotection_resetdate),
            quota?.resetTime?.toString() ?: "null",
        )
    }
}

@Composable
private fun AccountSection(state: IPProtectionState) {
    DebugSection(title = stringResource(R.string.mozac_feature_ipprotection_account)) {
        DebugRow(
            stringResource(R.string.mozac_feature_ipprotection_account_status),
            state.accountState.status.name,
        )
    }
}

@Composable
private fun DebugSection(
    title: String,
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(
        modifier =
            Modifier.fillMaxWidth()
                .clip(MaterialTheme.shapes.medium)
                .background(MaterialTheme.colorScheme.surfaceVariant)
                .padding(AcornTheme.layout.space.static150),
        verticalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static100),
    ) {
        Text(
            text = title,
            style = MaterialTheme.typography.titleMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        content()
    }
}

@Composable
private fun DebugRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static100),
    ) {
        Text(
            text = label,
            modifier = Modifier.weight(1f),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurface,
            fontFamily = FontFamily.Monospace,
        )
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun IPProtectionStateDebugPreview() {
    AcornTheme {
        IPProtectionStateDebugContent(
            state =
                IPProtectionState(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    proxyStatus = Authorized.Active,
                    serviceStatus = ServiceState.Ready,
                    usageQuota =
                        UsageQuota.Metered(
                            resetTime = Instant.parse("2026-06-01T00:00:00Z"),
                            remaining = DataVolume(bytes = 2_000_000_000L),
                            max = DataVolume(bytes = 5_000_000_000L),
                        ),
                    accountState = AccountState(status = AccountStatus.EnrolledAndEntitled),
                    lastError = "invalid_response",
                    proxyRequest = ProxyRequest.Activate(null),
                )
        )
    }
}
