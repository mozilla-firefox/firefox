/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.store

import kotlin.time.Instant
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.CurrentAuthProvider
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.UsageQuota
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.utils.Settings

/** Interface providing access to IP Protection related [android.content.SharedPreferences]. */
interface IPProtectionRepository {
    var hasAlreadyUsedVpn: Boolean
    var lastDataLimitNoticeCycleResetTime: Instant?

    /** The last recorded usage cycle reset time */
    var lastUsageCycleResetTime: Instant?

    var usageQuotaResetPromptPending: Boolean
}

/** The default implementation of [IPProtectionRepository] proxying calls to global Fenix [Settings]. */
class DefaultIPProtectionRepository(private val settings: Settings) : IPProtectionRepository {
    override var hasAlreadyUsedVpn: Boolean
        get() = settings.hasAlreadyUsedVpn
        set(value) {
            settings.hasAlreadyUsedVpn = value
        }

    override var lastDataLimitNoticeCycleResetTime: Instant?
        get() = settings.ipProtectionLastDataLimitNoticeCycleResetTime?.let(Instant::parseOrNull)
        set(value) {
            settings.ipProtectionLastDataLimitNoticeCycleResetTime = value?.toString()
        }

    override var lastUsageCycleResetTime: Instant?
        get() = settings.ipProtectionLastUsageCycleResetTime?.let(Instant::parseOrNull)
        set(value) {
            settings.ipProtectionLastUsageCycleResetTime = value?.toString()
        }

    override var usageQuotaResetPromptPending: Boolean
        get() = settings.ipProtectionUsageQuotaResetPromptPending
        set(value) {
            settings.ipProtectionUsageQuotaResetPromptPending = value
        }
}

/** [Middleware] responsible for intercepting actions and persisting VPN interactions. */
@OptIn(ExperimentalAndroidComponentsApi::class)
internal class IPProtectionPreferencesMiddleware(private val preferences: IPProtectionRepository) :
    Middleware<IPProtectionState, IPProtectionAction> {

    override fun invoke(
        store: Store<IPProtectionState, IPProtectionAction>,
        next: (IPProtectionAction) -> Unit,
        action: IPProtectionAction,
    ) {
        val previousState = store.state
        next(action)

        when (action) {
            IPProtectionAction.ActivationAction.Toggle -> {
                if (!preferences.hasAlreadyUsedVpn) {
                    preferences.hasAlreadyUsedVpn = true
                }
            }

            is IPProtectionAction.UsageAction.UsageChanged -> updateUsageCycle(store)

            is IPProtectionAction.ServiceAction.CurrentAuthProviderChanged -> {
                val authProviderChanged =
                    previousState.currentAuthProvider != CurrentAuthProvider.Unknown &&
                        previousState.currentAuthProvider != action.authProvider
                if (authProviderChanged) {
                    store.updatePendingResetPrompt(pending = false)
                    preferences.lastUsageCycleResetTime = null
                }
            }

            is IPProtectionAction.ProxyAction.ProxyStateChanged -> {
                if (store.state.proxyStatus == Authorized.Active) {
                    store.updatePendingResetPrompt(pending = false)
                }
            }

            else -> Unit
        }
    }

    private fun updateUsageCycle(store: Store<IPProtectionState, IPProtectionAction>) {
        if (store.state.usageQuota is UsageQuota.Unmetered) {
            store.updatePendingResetPrompt(pending = false)
            preferences.lastUsageCycleResetTime = null
            return
        }

        val resetTime = (store.state.usageQuota as? UsageQuota.Metered)?.resetTime ?: return
        val lastResetTime = preferences.lastUsageCycleResetTime
        val newCycleStarted = lastResetTime != null && resetTime > lastResetTime
        val promptPending =
            store.state.proxyStatus != Authorized.Active &&
                (newCycleStarted || preferences.usageQuotaResetPromptPending)
        store.updatePendingResetPrompt(pending = promptPending)

        if (resetTime != lastResetTime) {
            preferences.lastUsageCycleResetTime = resetTime
        }
    }

    private fun Store<IPProtectionState, IPProtectionAction>.updatePendingResetPrompt(pending: Boolean) {
        if (preferences.usageQuotaResetPromptPending != pending) {
            preferences.usageQuotaResetPromptPending = pending
        }
        if (state.usageQuotaResetPromptPending != pending) {
            dispatch(IPProtectionAction.UsageAction.UsageResetPromptPending(pending))
        }
    }
}
